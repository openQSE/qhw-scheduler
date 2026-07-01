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

## Common Task Ordering Rules

Each task has a stable `task_id`, owner metadata, optional payload reference,
priority, deadline, estimated runtime, and extensible metadata. The scheduler
does not parse the payload. Policies order tasks using fields in the task
envelope and any metadata they explicitly understand.

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
