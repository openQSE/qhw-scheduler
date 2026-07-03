# qhw-scheduler Public Interface Standard

## Purpose

This document defines the public `qhw-scheduler` interface in a form that can
be used as a language-neutral standard. The first binding described here is the
C binding implemented by the reference library.

The interface describes one scheduler instance managing one QPU execution
target. A runtime submits task descriptors, asks the scheduler for the next
task, and reports lifecycle events as the task moves through execution. Policy
plugins decide task ordering. The core library owns task state, lifecycle
state, QPU runtime counters, and the loaded policy registry.

## Interface Style

Each operation is written in a standard form followed by its C binding. The
standard operation names are conceptual. The C binding uses the exported
`qhw_sched_*` and `qhw_sched_qpu_*` symbols.

Parameter directions follow the MPI convention:

| Direction | Meaning |
| --- | --- |
| `IN` | The implementation reads the parameter. |
| `OUT` | The implementation writes the parameter. |
| `INOUT` | The implementation reads and writes the parameter. |

## Core Objects

| Object | Meaning |
| --- | --- |
| Scheduler | Owns task state, policy state, plugin registry, callbacks, and a reference to one QPU execution target. |
| QPU execution target | Describes the local QPU or QPU partition managed by one scheduler instance. |
| Task descriptor | Describes one schedulable unit of quantum work. The payload is opaque to the scheduler. |
| Assignment | Identifies the selected task returned by the scheduler. |
| Metadata entry | Carries typed numeric-key extension data used by policies and runtimes. |
| Policy | Implements ready-task ordering for one scheduler instance. |
| Callback table | Lets the runtime provide domain-specific operations, currently task splitting. |

## Identity Model

All public task and QPU identifiers are unsigned 64-bit integers. ID value zero
is invalid for task identifiers and QPU identifiers. Task IDs are unique within
one scheduler instance. Owner, job, and reservation IDs are caller-defined
correlation values.

The scheduler does not allocate task IDs. The caller supplies them.

## Result Codes

| Standard code | C code | Meaning |
| --- | --- | --- |
| `OK` | `QHW_SCHED_OK` | Operation completed successfully. |
| `INVALID_ARGUMENT` | `QHW_SCHED_ERR_INVALID_ARG` | An input argument is null, malformed, or out of range. |
| `NO_MEMORY` | `QHW_SCHED_ERR_NO_MEMORY` | Required memory could not be allocated. |
| `NOT_FOUND` | `QHW_SCHED_ERR_NOT_FOUND` | A referenced task, policy, plugin, or runnable task was not found. |
| `EXISTS` | `QHW_SCHED_ERR_EXISTS` | A supplied ID or plugin name already exists. |
| `PLUGIN_ERROR` | `QHW_SCHED_ERR_PLUGIN` | A plugin failed to load or failed validation. |
| `UNSUPPORTED` | `QHW_SCHED_ERR_UNSUPPORTED` | The requested option or operation is not supported. |
| `STATE_ERROR` | `QHW_SCHED_ERR_STATE` | The requested transition is not valid for the current state. |

## Public Data Types

The C binding defines these public value types in
`qhw_scheduler_types.h`.

| C type | Role |
| --- | --- |
| `qhw_sched_attr_t` | Scheduler creation attributes, including threading mode and optional allocator. |
| `qhw_sched_allocator_t` | Custom allocator hooks used by a scheduler instance. |
| `qhw_sched_kv_t` | Typed numeric-key metadata or option entry. |
| `qhw_sched_qpu_profile_t` | Static profile for one QPU execution target. |
| `qhw_sched_qpu_runtime_t` | Runtime counters for one QPU execution target. |
| `qhw_sched_task_desc_t` | Task descriptor submitted to the scheduler. |
| `qhw_sched_assignment_t` | Selected task returned by `SCHED_SELECT_NEXT`. |
| `qhw_sched_split_config_t` | Slice configuration passed to split callbacks. |
| `qhw_sched_callbacks_t` | Runtime callback table. |
| `qhw_sched_policy_info_t` | Policy information returned by policy listing. |

## Threading Model

The scheduler supports two threading modes.

| Mode | Meaning |
| --- | --- |
| `THREAD_SAFE` | Multiple threads may call operations on the same scheduler instance. The implementation protects internal state. |
| `THREAD_USER` | The caller serializes access to the scheduler instance. The implementation may use no-op locks. |

The C binding exposes these modes as `QHW_SCHED_THREAD_SAFE` and
`QHW_SCHED_THREAD_USER`.

## Built-In Metadata And Option Keys

The standard reserves numeric keys for common metadata and policy options.

| Key | Use |
| --- | --- |
| `QHW_SCHED_META_SHOTS` | Requested shot count. |
| `QHW_SCHED_META_DEPTH` | Circuit or task depth estimate. |
| `QHW_SCHED_META_NUM_QUBITS` | Number of qubits used by the task. |
| `QHW_SCHED_META_TWO_QUBIT_GATES` | Number of two-qubit gates. |
| `QHW_SCHED_META_ESTIMATED_RUNTIME_NS` | Estimated runtime in nanoseconds. |
| `QHW_SCHED_META_DEVICE_NAME` | Device-name metadata. |
| `QHW_SCHED_META_PROVIDER` | Provider identifier metadata. |
| `QHW_SCHED_META_PARENT_TASK_ID` | Parent task ID for sliced work. |
| `QHW_SCHED_META_SLICE_INDEX` | Slice index for sliced work. |
| `QHW_SCHED_META_SLICE_COUNT` | Total slice count for sliced work. |
| `QHW_SCHED_META_REQUESTED_SHOTS` | Original requested shot count. |
| `QHW_SCHED_META_CHILD_TASK_COUNT` | Number of child tasks produced by slicing. |
| `QHW_SCHED_META_MAX_SHOTS` | Maximum shot count associated with a task or QPU. |
| `QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD` | Policy option controlling shot slicing threshold. |
| `QHW_SCHED_OPT_SLICE_MAX_SHOTS` | Policy option limiting shots per child task. |
| `QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS` | Policy option controlling small final slices. |
| `QHW_SCHED_OPT_SLICE_MAX_CHILDREN` | Policy option limiting the number of generated child tasks. |
| `QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE` | Enable deadline-based priority boosting. |
| `QHW_SCHED_OPT_DEADLINE_NOW_NS` | Static current time for deadline tests or controlled runs. |
| `QHW_SCHED_OPT_DEADLINE_NORMAL_THRESHOLD` | Normal deadline urgency threshold. |
| `QHW_SCHED_OPT_DEADLINE_URGENT_THRESHOLD` | Urgent deadline threshold. |
| `QHW_SCHED_OPT_DEADLINE_CRITICAL_THRESHOLD` | Critical deadline threshold. |
| `QHW_SCHED_OPT_DEADLINE_NORMAL_BOOST` | Priority boost for normal urgency. |
| `QHW_SCHED_OPT_DEADLINE_URGENT_BOOST` | Priority boost for urgent tasks. |
| `QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST` | Priority boost for critical tasks. |
| `QHW_SCHED_OPT_ORDER_KEY` | Ordered-policy key. May be supplied more than once. |
| `QHW_SCHED_KEY_USER_BASE` | Start of caller-defined metadata key range. |

## Ordered Policy Keys

The ordered policy composes ordering keys in the order supplied by repeated
`QHW_SCHED_OPT_ORDER_KEY` options.

| Key | Meaning |
| --- | --- |
| `QHW_SCHED_ORDER_PRIORITY` | Higher effective priority is selected first. |
| `QHW_SCHED_ORDER_SJF` | Lower estimated cost is selected first. |
| `QHW_SCHED_ORDER_LJF` | Higher estimated cost is selected first. |
| `QHW_SCHED_ORDER_FIFO` | Older ready-task insertion sequence is selected first. |
| `QHW_SCHED_ORDER_ROUND_ROBIN` | Reservation, job, or singleton task groups rotate when earlier keys tie. |

## Task Lifecycle

Submitted tasks enter `QUEUED` state unless the core slices the task. A sliced
parent enters `WAITING` state while child tasks are queued. A successful
selection moves a task to `ASSIGNED`. The runtime reports `STARTED` after it
hands work to the device or provider. Terminal events are `COMPLETED`,
`FAILED`, and `CANCELLED`.

The C state values are:

| State | Meaning |
| --- | --- |
| `QHW_SCHED_TASK_QUEUED` | Task is ready for policy selection. |
| `QHW_SCHED_TASK_ASSIGNED` | Task has been selected and handed to the caller. |
| `QHW_SCHED_TASK_RUNNING` | Caller reported that execution started. |
| `QHW_SCHED_TASK_COMPLETED` | Task completed successfully. |
| `QHW_SCHED_TASK_FAILED` | Task failed. |
| `QHW_SCHED_TASK_CANCELLED` | Task was cancelled. |
| `QHW_SCHED_TASK_WAITING` | Parent task is waiting for sliced children. |

## Public Operations

### QPU_CREATE

```text
QPU_CREATE(profile, qpu)

IN profile
    QPU execution target profile. The QPU ID must be nonzero. Metadata is
    copied by the implementation.

OUT qpu
    Created QPU execution target handle.

Returns
    OK, INVALID_ARGUMENT, NO_MEMORY
```

Creates one QPU execution target object. Runtime counters are initialized to
zero.

C binding:

```c
qhw_sched_rc_t qhw_sched_qpu_create(
	const qhw_sched_qpu_profile_t *profile,
	qhw_sched_qpu_t **out_qpu);
```

### QPU_DESTROY

```text
QPU_DESTROY(qpu)

IN qpu
    QPU execution target handle.
```

Releases one QPU handle. The object is destroyed when its last reference is
released.

C binding:

```c
void qhw_sched_qpu_destroy(qhw_sched_qpu_t *qpu);
```

### QPU_GET_PROFILE

```text
QPU_GET_PROFILE(qpu, profile)

IN qpu
    QPU execution target handle.

OUT profile
    Current QPU profile view.

Returns
    OK, INVALID_ARGUMENT
```

Returns the QPU profile stored by the implementation. Metadata pointers in the
C binding refer to memory owned by the QPU object.

C binding:

```c
qhw_sched_rc_t qhw_sched_qpu_get_profile(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_profile_t *out_profile);
```

### QPU_GET_RUNTIME

```text
QPU_GET_RUNTIME(qpu, runtime)

IN qpu
    QPU execution target handle.

OUT runtime
    Runtime counters.

Returns
    OK, INVALID_ARGUMENT
```

Returns queued, completed, failed, cancelled, and running-task counters.

C binding:

```c
qhw_sched_rc_t qhw_sched_qpu_get_runtime(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_runtime_t *out_runtime);
```

### SCHED_CREATE

```text
SCHED_CREATE(policy_name, attributes, qpu, options, scheduler)

IN policy_name
    Initial policy name. The current C binding reserves this parameter and
    expects NULL.

IN attributes
    Optional scheduler attributes. NULL selects default attributes.

IN qpu
    QPU execution target managed by the scheduler.

IN options
    Initial policy options. The current C binding reserves this parameter and
    expects no options.

OUT scheduler
    Created scheduler handle.

Returns
    OK, INVALID_ARGUMENT, NO_MEMORY, UNSUPPORTED
```

Creates a scheduler instance. The caller loads plugins and selects the active
policy after creation.

C binding:

```c
qhw_sched_rc_t qhw_sched_create(
	const char *policy_name,
	const qhw_sched_attr_t *attr,
	qhw_sched_qpu_t *qpu,
	const qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_t **out_sched);
```

### SCHED_DESTROY

```text
SCHED_DESTROY(scheduler)

IN scheduler
    Scheduler handle.
```

Destroys a scheduler instance, releases loaded plugins, frees task state, and
releases the scheduler reference to the QPU execution target.

C binding:

```c
void qhw_sched_destroy(qhw_sched_t *sched);
```

### SCHED_GET_THREADING

```text
SCHED_GET_THREADING(scheduler)

IN scheduler
    Scheduler handle.

Returns
    Threading mode.
```

Returns the threading mode selected when the scheduler was created.

C binding:

```c
qhw_sched_threading_t qhw_sched_get_threading(qhw_sched_t *sched);
```

### SCHED_LAST_ERROR

```text
SCHED_LAST_ERROR(scheduler)

IN scheduler
    Scheduler handle.

Returns
    Human-readable diagnostic string.
```

Returns the last diagnostic message stored by the scheduler. This is mainly
used for plugin load failures.

C binding:

```c
const char *qhw_sched_last_error(qhw_sched_t *sched);
```

### SCHED_LOAD_PLUGIN

```text
SCHED_LOAD_PLUGIN(scheduler, path)

IN scheduler
    Scheduler handle.

IN path
    Shared-object path for the policy plugin.

Returns
    OK, INVALID_ARGUMENT, NO_MEMORY, EXISTS, PLUGIN_ERROR
```

Loads a policy plugin, validates its descriptor, and registers the policy name
with the scheduler instance.

C binding:

```c
qhw_sched_rc_t qhw_sched_load_plugin(
	qhw_sched_t *sched,
	const char *shared_object_path);
```

### SCHED_LIST_POLICIES

```text
SCHED_LIST_POLICIES(scheduler, policies, count)

IN scheduler
    Scheduler handle.

OUT policies
    Array of policy information records allocated by the scheduler.

OUT count
    Number of records returned.

Returns
    OK, INVALID_ARGUMENT, NO_MEMORY
```

Returns the policies loaded into one scheduler instance. The caller releases
the returned array with `SCHED_FREE_POLICY_INFO_ARRAY`.

C binding:

```c
qhw_sched_rc_t qhw_sched_list_policies(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t **out_policies,
	size_t *out_count);
```

### SCHED_FREE_POLICY_INFO_ARRAY

```text
SCHED_FREE_POLICY_INFO_ARRAY(scheduler, policies)

IN scheduler
    Scheduler handle that allocated the policy array.

IN policies
    Policy array returned by `SCHED_LIST_POLICIES`.
```

Frees a policy information array.

C binding:

```c
void qhw_sched_free_policy_info_array(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t *policies);
```

### SCHED_SET_POLICY

```text
SCHED_SET_POLICY(scheduler, policy_name, options)

IN scheduler
    Scheduler handle.

IN policy_name
    Name of a loaded policy.

IN options
    Policy option entries.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, NO_MEMORY, UNSUPPORTED, STATE_ERROR
```

Initializes the selected policy and replays queued tasks into it. Tasks already
assigned, running, or terminal are not replayed.

C binding:

```c
qhw_sched_rc_t qhw_sched_set_policy(
	qhw_sched_t *sched,
	const char *policy_name,
	const qhw_sched_kv_t *options,
	size_t option_count);
```

### SCHED_SET_CALLBACKS

```text
SCHED_SET_CALLBACKS(scheduler, callbacks)

IN scheduler
    Scheduler handle.

IN callbacks
    Callback table. NULL clears the callback table.

Returns
    OK, INVALID_ARGUMENT
```

Installs runtime callbacks. The current public callback is `split_task`, which
is used when policy and QPU slicing options require child tasks.

C binding:

```c
qhw_sched_rc_t qhw_sched_set_callbacks(
	qhw_sched_t *sched,
	const qhw_sched_callbacks_t *callbacks);
```

### SCHED_SUBMIT_TASK

```text
SCHED_SUBMIT_TASK(scheduler, task)

IN scheduler
    Scheduler handle.

IN task
    Task descriptor. The task ID must be nonzero and unique in the scheduler.

Returns
    OK, INVALID_ARGUMENT, NO_MEMORY, EXISTS, UNSUPPORTED, STATE_ERROR
```

Adds one task to the scheduler. The scheduler copies task metadata and stores
the payload reference without parsing the payload. If slicing is active, the
core invokes the split callback and enqueues the generated child tasks.

C binding:

```c
qhw_sched_rc_t qhw_sched_submit_task(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task);
```

### SCHED_SELECT_NEXT

```text
SCHED_SELECT_NEXT(scheduler, assignment)

IN scheduler
    Scheduler handle.

OUT assignment
    Selected task assignment.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Selects the next queued task according to the active policy. A successful
selection moves the task to `ASSIGNED`. The caller reports execution progress
with lifecycle operations.

C binding:

```c
qhw_sched_rc_t qhw_sched_select_next(
	qhw_sched_t *sched,
	qhw_sched_assignment_t *out_assignment);
```

### SCHED_TASK_UPDATE_PRIORITY

```text
SCHED_TASK_UPDATE_PRIORITY(scheduler, task_id, priority)

IN scheduler
    Scheduler handle.

IN task_id
    Task to update.

IN priority
    New priority value.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Updates the priority of a queued task and notifies the active policy. Assigned,
running, waiting, or terminal tasks are not updated.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_update_priority(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	int64_t priority);
```

### SCHED_TASK_STARTED

```text
SCHED_TASK_STARTED(scheduler, task_id)

IN scheduler
    Scheduler handle.

IN task_id
    Task that started execution.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Moves an assigned task to `RUNNING` and updates QPU runtime state. A scheduler
with no active policy may also start a queued task directly.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_started(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);
```

### SCHED_TASK_COMPLETED

```text
SCHED_TASK_COMPLETED(scheduler, task_id)

IN scheduler
    Scheduler handle.

IN task_id
    Running task that completed successfully.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Moves a running task to `COMPLETED`, updates QPU runtime counters, and updates
the parent state when the completed task is a slice.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_completed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);
```

### SCHED_TASK_FAILED

```text
SCHED_TASK_FAILED(scheduler, task_id)

IN scheduler
    Scheduler handle.

IN task_id
    Task that failed.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Moves a non-terminal task to `FAILED`, updates QPU runtime counters, and
updates the parent state when the failed task is a slice.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_failed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);
```

### SCHED_TASK_CANCELLED

```text
SCHED_TASK_CANCELLED(scheduler, task_id)

IN scheduler
    Scheduler handle.

IN task_id
    Task that was cancelled.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND, STATE_ERROR
```

Moves a non-terminal task to `CANCELLED`, removes queued work from the active
policy, updates QPU runtime counters, and updates parent state for sliced work.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_cancelled(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);
```

### SCHED_TASK_GET_STATE

```text
SCHED_TASK_GET_STATE(scheduler, task_id, state)

IN scheduler
    Scheduler handle.

IN task_id
    Task to query.

OUT state
    Current task lifecycle state.

Returns
    OK, INVALID_ARGUMENT, NOT_FOUND
```

Returns the scheduler lifecycle state for one task.

C binding:

```c
qhw_sched_rc_t qhw_sched_task_get_state(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t *out_state);
```

### SCHED_TASK_COUNT

```text
SCHED_TASK_COUNT(scheduler)

IN scheduler
    Scheduler handle.

Returns
    Number of task records currently owned by the scheduler.
```

Returns the current number of task records in the scheduler. This includes
queued, assigned, running, waiting, and terminal tasks that have not been
removed by scheduler destruction.

C binding:

```c
size_t qhw_sched_task_count(qhw_sched_t *sched);
```

## C Binding Ownership Rules

The C binding uses opaque handles for `qhw_sched_t` and `qhw_sched_qpu_t`.
Created handles are released with their matching destroy functions.

The scheduler copies task metadata during submission. Payload pointers remain
caller-managed. QPU profile metadata is copied during QPU creation. Policy
arrays returned by `qhw_sched_list_policies()` are freed with
`qhw_sched_free_policy_info_array()`.

## Public Header Scope

This document covers the runtime-facing API in `qhw_scheduler.h` and its public
types. The policy plugin ABI in `qhw_scheduler_plugin.h` is also public C API,
but it serves policy authors rather than application runtimes. It should be
specified in a separate plugin-authoring standard section.
