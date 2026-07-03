# Scheduler Policies

`qhw-scheduler` runs one policy plugin per scheduler instance. The core owns
task lifetime, lifecycle state, QPU runtime counters, and locking. A policy
plugin owns only its ready-task ordering data structure.

The core calls a policy when a task is submitted, when the device asks for the
next task, and when a task reaches a terminal state. Policy callbacks run while
the scheduler lock is held, so plugins should avoid blocking calls and should
keep their data structures cheap to update.

Selecting a task moves it from `QUEUED` to `ASSIGNED`. Policy replay only
rebuilds ready queues from `QUEUED` tasks, so resetting a policy cannot return
work that has already been handed to the caller.

Task slicing is handled by the core during submission. Policies expose slicing
configuration, such as `QHW_SCHED_OPT_SLICE_MAX_SHOTS`, and the caller-provided
split callback builds child task descriptors. The policy ready queue receives
only schedulable child tasks.

## Common Task Ordering Rules

Each task has a stable `task_id`, optional owner, job, and reservation IDs,
optional payload reference, priority, deadline, estimated runtime, and
extensible metadata. The scheduler does not parse the payload. Policies order
tasks using fields in the task envelope and any metadata they explicitly
understand.

When a policy needs a deterministic tie-breaker, it uses insertion order. The
insertion sequence is assigned by the policy when the task enters that policy's
ready queue. Replaying queued tasks into a new policy preserves the core task
enqueue order.

## FIFO

`fifo` preserves task insertion order. The first queued task is the first task
returned by `qhw_sched_select_next()`.

The FIFO plugin stores ready tasks in a doubly linked list:

| Operation | Cost | Notes |
| --- | --- | --- |
| Submit | O(1) | Append to the tail of the ready list. |
| Select next | O(1) | Pop from the head of the ready list. |
| Cancel queued task | O(n) | Scan the ready list and remove the task. |

FIFO is useful as a baseline policy and for devices where fairness is already
handled by an upper layer. It adds minimal scheduling overhead and keeps queue
behavior easy to reason about.

## Priority

`priority` selects the task with the highest numeric priority. Tasks with the
same priority are selected in insertion order. This makes the policy stable:
raising a task's priority changes its ordering, while equal-priority tasks keep
FIFO behavior.

The policy can optionally apply deadline-based priority boosting. Boosting is
configured through deadline option keys and is refreshed lazily when the QPU asks
for the next task. No background thread is required.

The priority plugin uses two data structures:

| Structure | Purpose |
| --- | --- |
| Binary heap | Keeps the best ready task at the root. |
| Task-id hash table | Finds queued tasks quickly for cancellation. |

The heap comparison key is:

```text
higher priority, then lower insertion sequence, then lower task_id
```

The `task_id` tie-breaker is only used when two tasks have the same priority
and sequence value. Normal insertion assigns unique sequence values.

| Operation | Cost | Notes |
| --- | --- | --- |
| Submit | O(log n) | Insert into the heap and task-id index. |
| Select next | O(log n) | Pop the heap root and remove the index entry. |
| Cancel queued task | O(log n) | Lookup by task ID, then remove by index. |
| Update queued priority | O(log n) | Lookup by ID, then reheapify by index. |

The plugin stores shallow task descriptors. Payload memory and copied metadata
remain owned by the core task table. This avoids duplicating large payloads and
keeps plugin memory bounded by ready-queue metadata.

Priority updates are accepted only for `QUEUED` tasks. The core task table
finds the task by ID, updates the copied descriptor, and invokes the policy
callback. The priority plugin finds the queued item through its task-id hash
table and reorders the binary heap at the stored heap index. Updates to
`ASSIGNED`, `RUNNING`, or terminal tasks return a state error.

## Ordered

`ordered` is the composable ordering policy. It uses one heap and a list of
ordering keys. The first key that differs decides the selected task. If all
configured keys compare equal, insertion order and then `task_id` provide a
deterministic tie-breaker.

The default order is:

```text
priority, fifo
```

Callers configure the order by passing repeated `QHW_SCHED_OPT_ORDER_KEY`
options to `qhw_sched_set_policy()`. Supported keys are:

| Key | Behavior |
| --- | --- |
| `QHW_SCHED_ORDER_PRIORITY` | Higher effective priority is selected first. Deadline boosting can modify effective priority. |
| `QHW_SCHED_ORDER_SJF` | Lower estimated cost is selected first. |
| `QHW_SCHED_ORDER_LJF` | Higher estimated cost is selected first. |
| `QHW_SCHED_ORDER_FIFO` | Older ready-task insertion sequence is selected first. |
| `QHW_SCHED_ORDER_ROUND_ROBIN` | Reservation, job, or singleton task groups rotate when earlier keys tie. |

Estimated cost is cached when the task enters the ready queue. The cost source
is `estimated_runtime_ns` when nonzero, then
`QHW_SCHED_META_ESTIMATED_RUNTIME_NS` metadata when present, then
`QHW_SCHED_META_SHOTS` when present, then unit cost. This keeps heap
comparisons cheap and avoids repeated metadata scans in the hot path.

Examples:

| Order keys | Meaning |
| --- | --- |
| `priority,fifo` | Highest priority first, FIFO among equal priorities. |
| `sjf,fifo` | Shortest estimated task first, FIFO among equal costs. |
| `ljf,fifo` | Longest estimated task first, FIFO among equal costs. |
| `sjf,priority,fifo` | Shortest estimated task first, then highest priority among equal costs. |
| `ljf,priority,fifo` | Longest estimated task first, then highest priority among equal costs. |
| `sjf,round_robin,fifo` | Shortest estimated task first, then rotate equal-cost groups. |
| `ljf,round_robin,fifo` | Longest estimated task first, then rotate equal-cost groups. |
| `priority,sjf,fifo` | Highest priority first, then shortest task among equal priorities. |
| `priority,ljf,fifo` | Highest priority first, then longest task among equal priorities. |

Deadline boosting is shared with the `priority` policy. When enabled, the
priority key compares boosted priority rather than the submitted base priority.
This supports deadline-aware variants without duplicating a separate deadline
policy.

| Operation | Cost | Notes |
| --- | --- | --- |
| Submit | O(log n) | Cache cost, compute effective priority, and insert into the heap and task-id index. |
| Select next | O(log n) | Refresh expired deadline boosts, then pop the heap root. |
| Cancel queued task | O(log n) | Lookup by task ID, then remove by stored heap index. |
| Update queued priority | O(log n) | Lookup by task ID, recompute effective priority, then reheapify by index. |

## Round Robin

`round_robin` rotates across task groups. The group key is selected from the
task envelope in this order:

```text
reservation_id if nonzero, else job_id if nonzero, else task_id
```

The key includes both the source namespace and the numeric value. A
`reservation_id` of `10`, a `job_id` of `10`, and an ungrouped `task_id` of
`10` are three separate groups.

This lets an admission or resource-management layer group work by reservation
when that information exists. Until reservation IDs are available, the policy
uses `job_id`, which maps naturally to hybrid jobs. Tasks without either field
become singleton groups, so unrelated untagged work stays in submission order.

Within a group, tasks remain FIFO. Across groups, the plugin keeps a ring of
active groups. Each `qhw_sched_select_next()` operation pops one task from the
front group. If that group still has ready work, the group moves to the tail
of the active ring.

The plugin uses these data structures:

| Structure | Purpose |
| --- | --- |
| Active group list | Rotates groups in round-robin order. |
| Per-group FIFO list | Preserves insertion order within one group. |
| Group hash tables | Find reservation, job, and singleton-task groups quickly while keeping their ID namespaces separate. |
| Task-id hash table | Finds queued tasks quickly for cancellation. |

| Operation | Cost | Notes |
| --- | --- | --- |
| Submit | O(1) average | Find or create the group, then append to that group's FIFO list. |
| Select next | O(1) average | Pop one task from the current group and rotate the group if it remains active. |
| Cancel queued task | O(1) average | Lookup by task ID, remove from the group FIFO list, and remove an empty group from the active ring. |
| Update queued priority | O(1) | Accepted as a no-op because this policy does not order by priority. |

Round robin is useful when several hybrid jobs or reservations are sharing one
QPU. It prevents one group from draining all of its queued tasks before other
groups make progress, while keeping each group's own task order stable.
