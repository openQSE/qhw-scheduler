# qhw-scheduler Standardization Plan

## Purpose

This document captures the longer-term standardization direction for
`qhw-scheduler`. It is intentionally separate from `detailed-design.md`.
The detailed design should drive the first implementation. This document records how
that implementation can later become a language-neutral scheduler interface
specification.

The standard should follow the same broad pattern used by MPI. It should define
abstract operations, parameter direction, parameter meaning, error cases, and
required behavior. Language bindings should then map those abstract operations
onto C, Python, Rust, C++, or other implementation surfaces.

The standard should not start as the first artifact. A working reference
implementation should come first. That implementation will expose mistakes in
the task model, lifecycle model, plugin model, and scheduler callback model.
The standard should be written once those details have been exercised by QFw
and at least one simulator or test harness.

## Standardization Goal

The goal is interoperability between scheduler implementations. A site should
be able to write a scheduler in C, Rust, Python, or another language and still
present the same scheduler interface to a runtime. A runtime should not need to
know which language or internal data structures the scheduler uses.

The specification should define what every conforming implementation must do.
It should not require one internal implementation strategy.

For example, the specification should not say that a scheduler is represented
by an opaque C pointer. That is a C binding detail. The specification should
say that a scheduler instance owns task state, device state, policy state, and
local QPU state, policy state, and lifecycle history. The C binding may expose
that instance as an opaque handle.
A Python binding may expose it as a class instance.

## Implementation-First Approach

The first milestone should be a C reference implementation with Python
bindings. That implementation should remain pragmatic. It should validate the
core behavior before any interface is proposed as stable.

Recommended order:

- Build the C core and FIFO policy.
- Add priority and round-robin policies.
- Add the plugin ABI.
- Add Python bindings.
- Integrate with QFw QRC through an adapter.
- Add a simulator or standalone harness that uses the same interface.
- Use the working implementation to write the first draft standard.

This avoids standardizing the wrong abstraction too early. The standard should
come from behavior that has been implemented, tested, and integrated.

## Specification Layers

The future standard should be split into a language-neutral core and language
bindings.

Recommended layout:

```text
qhw-scheduler-spec/
  qhw-scheduler-core.md
  qhw-scheduler-data-model.md
  qhw-scheduler-lifecycle.md
  qhw-scheduler-policy.md
  qhw-scheduler-extensions.md
  qhw-scheduler-conformance.md

  bindings/
    c-binding.md
    python-binding.md
```

The core documents should be normative. They should define required behavior.
The binding documents should define how the same concepts map to a particular
language.

## Normative Core Concepts

The core specification should define these concepts independently of language:

| Concept | Meaning |
| --- | --- |
| Scheduler instance | Owns task state, local QPU state, policy state, and lifecycle history. |
| Task descriptor | Describes one schedulable unit of work without exposing the payload internals. |
| QPU profile | Describes the one QPU execution target managed by the scheduler instance. |
| Assignment | Represents the selected task returned by a scheduler. |
| Metadata entry | Carries typed extension data through numeric keys. |
| Policy | Selects runnable work based on task, QPU, lifecycle, and metadata state. |
| Lifecycle event | Updates scheduler state when work starts, completes, fails, times out, or is cancelled. |
| Callback | Lets the runtime provide domain-specific logic such as cost estimation or task splitting. |

These concepts should not mention C structs, Python classes, heap layouts, or
plugin shared objects. Those belong in implementation profiles.

## Identity Model

The standard should use numeric identity values.

Required rules:

- IDs are unsigned 64-bit integers.
- ID value zero is invalid.
- Task IDs are unique within a scheduler instance.
- QPU IDs identify the local QPU for diagnostics and correlation. They are not
  used for selecting among QPUs.
- Job, reservation, and owner IDs are caller-defined correlation IDs.
- A scheduler implementation validates duplicate task IDs.
- A scheduler implementation does not need to allocate IDs.

String IDs should not be part of the normative identity model. External UUIDs,
provider job IDs, user-visible device names, and log labels can be carried in
metadata or maintained outside the scheduler.

## Threading Model

The standard should define the threading contract separately from any binding.
The model should follow the useful part of the libfabric pattern. A caller
declares how it will access scheduler objects, and the implementation may use
that declaration to select locking behavior.

The first standard should define these modes:

| Mode | Meaning |
| --- | --- |
| `THREAD_SAFE` | Multiple threads may call scheduler operations on the same scheduler instance. The implementation protects internal state. |
| `THREAD_USER` | The user serializes access to one scheduler instance. The implementation may use no-op locks. |

The default for the first reference implementation should be `THREAD_SAFE`.
This makes the safe behavior automatic. A caller that needs lower overhead can
explicitly request `THREAD_USER` when it can prove that access to the
scheduler instance is serialized.

Bindings should expose the configured mode. The C binding can represent this as
a scheduler attribute and a query function. Other bindings can use constructor
arguments and properties.

## Data Model

The standard should define abstract data objects. Bindings can represent these
objects as structs, classes, dictionaries, records, or serialized messages.

### Task Descriptor

A task descriptor should contain at least:

| Field | Direction | Meaning |
| --- | --- | --- |
| `task_id` | IN | Non-zero task ID unique in the scheduler instance. |
| `job_id` | IN | Optional caller-defined job correlation ID. |
| `reservation_id` | IN | Optional caller-defined reservation correlation ID. |
| `owner_id` | IN | Optional caller-defined user, account, or tenant ID. |
| `priority` | IN | Signed priority value interpreted by the active policy. |
| `created_time` | IN | Task creation time or monotonic insertion time. |
| `deadline_time` | IN | Optional deadline used by deadline-aware policies. |
| `estimated_runtime` | IN | Optional coarse runtime estimate. |
| `payload_type` | IN | Numeric payload type understood by the runtime adapter. |
| `payload_reference` | IN | Opaque pointer, handle, bytes, or binding-specific payload reference. |
| `metadata` | IN | Typed numeric-key metadata entries. |

The scheduler must not parse payloads. Scheduling policy should use common
fields and metadata. Runtime adapters are responsible for turning a selected
task into provider, simulator, QFw, QRMI, or QDMI submission calls.

### QPU Profile

A QPU profile should contain at least:

| Field | Direction | Meaning |
| --- | --- | --- |
| `qpu_id` | IN | Non-zero QPU correlation ID. |
| `state` | IN/OUT | Current scheduling state. |
| `provider_queue_capacity` | IN | Maximum desired depth of the provider-facing queue. |
| `active_task_count` | IN/OUT | Number of currently running tasks. This is zero or one for the first implementation. |
| `metadata` | IN | Typed numeric-key metadata entries. |

QPU metadata may include qubit count, shot limits, gate timing, topology ID,
calibration ID, provider name, or a human-readable QPU name. The core
specification should define the metadata mechanism. A policy may document which
metadata keys it uses.

### Future Partitioning Model

The first standard should define one scheduler instance as managing one QPU
execution target. The execution target may be a full physical QPU. In the
future, it may also be a QPU partition.

Two partitioning models should remain possible:

| Model | Scheduler Role |
| --- | --- |
| Externally visible partitions | Each partition is treated as a separate execution target with its own scheduler instance. Placement across partitions happens above this scheduler. |
| Internal dynamic partitioning | One scheduler manages sub-QPU placement and concurrent occupancy inside a physical QPU. This requires a richer QPU model and should be future work. |

These models can coexist. A resource manager or mesh layer may assign a job to
a visible partition, then a local scheduler may still order tasks within that
partition. If a future QPU supports safe concurrent execution inside one
partition, a later scheduler profile can add placement metadata and callbacks.

The base specification should use the term "QPU execution target" rather than
"physical QPU". That keeps the first interface narrow while leaving room for
partition-aware systems.

### Assignment

An assignment should contain at least:

| Field | Direction | Meaning |
| --- | --- | --- |
| `task_id` | OUT | Selected task. |
| `slice_index` | OUT | Slice index for split work. Zero for unsliced work. |
| `slice_count` | OUT | Total slice count. One for unsliced work. |
| `metadata` | OUT | Policy-specific assignment metadata. |

The assignment does not need a separate ID. The selected task and slice fields
provide enough identity for the scheduler contract because the scheduler
instance owns one QPU.

## Operation Definition Style

Each standardized operation should be written in an MPI-like form. The
operation should name all parameters, describe parameter direction, define
valid inputs, and list expected result codes.

Template:

```text
SCHED_OPERATION_NAME(parameter_a, parameter_b, parameter_c)

IN parameter_a
    Description of input parameter.

OUT parameter_b
    Description of output parameter.

INOUT parameter_c
    Description of parameter read and modified by the operation.

Returns
    OK
    INVALID_ARGUMENT
    NOT_FOUND
    INVALID_STATE

Semantics
    Normative behavior required from all conforming implementations.
```

The operation name is the abstract interface. Bindings map it to functions,
methods, or messages.

## Candidate Standard Operations

The first standard should include the operations needed by QFw QRC and a
standalone scheduler test harness.

| Operation | Purpose |
| --- | --- |
| `SCHED_CREATE` | Create a scheduler instance with a selected policy and QPU profile. |
| `SCHED_DESTROY` | Destroy a scheduler instance and release owned state. |
| `SCHED_RESET` | Clear task, QPU, policy, and lifecycle state. |
| `SCHED_GET_THREADING` | Return the selected threading mode for a scheduler instance. |
| `SCHED_SET_POLICY` | Select or reconfigure the active policy. |
| `SCHED_UPDATE_QPU` | Update QPU metadata or dynamic state. |
| `SCHED_SET_QPU_STATE` | Mark the QPU available, busy, draining, or unavailable. |
| `SCHED_GET_QPU` | Return the scheduler view of the local QPU. |
| `SCHED_SUBMIT_TASK` | Add one task to the scheduler. |
| `SCHED_SUBMIT_TASKS` | Add a batch of tasks. |
| `SCHED_UPDATE_TASK` | Update task metadata or priority. |
| `SCHED_CANCEL_TASK` | Cancel a pending task or mark running work for cancellation. |
| `SCHED_GET_TASK` | Return the scheduler view of a task. |
| `SCHED_NEXT_TASK` | Select the next runnable task for the local QPU. |
| `SCHED_TASK_STARTED` | Notify that a selected task started execution. |
| `SCHED_TASK_COMPLETED` | Notify successful completion. |
| `SCHED_TASK_FAILED` | Notify failure. |
| `SCHED_TASK_TIMED_OUT` | Notify timeout. |
| `SCHED_TASK_CANCELLED` | Notify cancellation. |
| `SCHED_GET_STATS` | Return scheduler and policy statistics. |

More operations can be added later. The first standard should stay small enough
to implement in multiple languages.

## Example Operation Definitions

### SCHED_SUBMIT_TASK

```text
SCHED_SUBMIT_TASK(scheduler, task)

IN scheduler
    Scheduler instance.

IN task
    Task descriptor. The task ID must be non-zero and unique within the
    scheduler instance.

Returns
    OK
        The task was accepted by the scheduler.

    INVALID_ARGUMENT
        The task descriptor is malformed, or the task ID is zero.

    DUPLICATE_ID
        The task ID already exists in the scheduler instance.

    NO_MEMORY
        The implementation could not copy required task state.

    POLICY_ERROR
        The active policy rejected the task.

Semantics
    The implementation records the task in pending or ready state. The
    implementation must not inspect the task payload. The active policy may
    inspect task fields and metadata. The task becomes eligible for future
    selection according to policy and QPU state.
```

C binding:

```c
qhw_sched_rc_t qhw_sched_submit_task(
    qhw_sched_t *scheduler,
    const qhw_sched_task_desc_t *task);
```

Python binding:

```python
scheduler.submit_task(task)
```

### SCHED_UPDATE_QPU

```text
SCHED_UPDATE_QPU(scheduler, updates)

IN scheduler
    Scheduler instance.

IN updates
    Metadata or dynamic QPU fields to update.

Returns
    OK
        The QPU profile was updated.

    INVALID_ARGUMENT
        The update set is malformed.

    NO_MEMORY
        The implementation could not copy required QPU state.

Semantics
    The implementation updates the scheduler's local QPU profile. The active
    policy may use QPU fields and metadata during task selection. The
    implementation does not contact the physical QPU.
```

C binding:

```c
qhw_sched_rc_t qhw_sched_update_qpu(
    qhw_sched_t *scheduler,
    const qhw_sched_kv_t *updates,
    size_t update_count);
```

Python binding:

```python
scheduler.update_qpu(updates)
```

### SCHED_NEXT_TASK

```text
SCHED_NEXT_TASK(scheduler, assignment)

IN scheduler
    Scheduler instance.

OUT assignment
    Selected task assignment.

Returns
    OK
        An assignment was produced.

    INVALID_ARGUMENT
        The scheduler or assignment parameter is invalid.

    NO_RUNNABLE_TASK
        No pending task can currently run on the local QPU.

    POLICY_ERROR
        The active policy could not select work.

Semantics
    The implementation selects a runnable task for the local QPU. The policy
    may consider task state, QPU state, priority, metadata, callback
    results, and lifecycle history. A successful call does not imply that the
    provider has started work. The caller must notify the scheduler with
    SCHED_TASK_STARTED after submission begins.
```

C binding:

```c
qhw_sched_rc_t qhw_sched_next_task(
    qhw_sched_t *scheduler,
    qhw_sched_assignment_t *assignment);
```

Python binding:

```python
assignment = scheduler.next_task()
```

### SCHED_TASK_COMPLETED

```text
SCHED_TASK_COMPLETED(scheduler, task, result_summary)

IN scheduler
    Scheduler instance.

IN task
    Task ID.

IN result_summary
    Optional metadata describing the result or measured execution behavior.

Returns
    OK
        The lifecycle event was accepted.

    NOT_FOUND
        The task ID is unknown.

    INVALID_STATE
        The task is not in a state that can complete.

Semantics
    The implementation moves the task to a terminal completed state. It may
    update policy accounting, free QPU capacity, and make other tasks
    eligible for selection. The implementation does not retrieve provider
    results. Result retrieval belongs to the runtime adapter.
```

C binding:

```c
qhw_sched_rc_t qhw_sched_task_completed(
    qhw_sched_t *scheduler,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *result_summary,
    size_t result_summary_count);
```

Python binding:

```python
scheduler.task_completed(task_id, result_summary={})
```

## Error Model

The standard should define a common result code set. Bindings can map these to
integers, exceptions, enum values, or status objects.

Candidate result codes:

| Code | Meaning |
| --- | --- |
| `OK` | Operation completed successfully. |
| `INVALID_ARGUMENT` | An input parameter is malformed or out of range. |
| `NO_MEMORY` | The implementation could not allocate required state. |
| `NOT_FOUND` | A referenced task, policy, or plugin does not exist. |
| `DUPLICATE_ID` | A supplied ID already exists in the scheduler instance. |
| `INVALID_STATE` | The requested lifecycle transition is not legal. |
| `NO_RUNNABLE_TASK` | No task can currently be selected. |
| `POLICY_ERROR` | The active policy rejected or failed the operation. |
| `PLUGIN_ERROR` | A plugin failed to load or execute. |

The C binding can expose these as enum values. The Python binding can raise
typed exceptions while preserving the original result code.

## Lifecycle Rules

The standard should define legal task state transitions. This prevents
different implementations from treating lifecycle events differently.

Initial state:

- A submitted task enters `PENDING` or `READY`.

Allowed terminal states:

- `COMPLETED`
- `FAILED`
- `TIMED_OUT`
- `CANCELLED`

Typical transitions:

```text
PENDING -> READY
READY -> RUNNING
RUNNING -> COMPLETED
RUNNING -> FAILED
RUNNING -> TIMED_OUT
PENDING -> CANCELLED
READY -> CANCELLED
RUNNING -> CANCELLED
```

The standard should define what happens when a cancellation request targets a
running task. Some providers cannot interrupt running work. A conforming
implementation may mark the task as cancellation-requested, but it must report
the final terminal state once the runtime knows it.

## Metadata And Extension Model

The standard should use typed numeric metadata keys. Numeric keys avoid string
matching in scheduler policy paths and make C, Python, Rust, and C++
implementations easier to align.

The first standard should reserve a small built-in key range:

| Key | Meaning |
| --- | --- |
| `SHOTS` | Number of shots requested. |
| `DEPTH` | Circuit or task depth estimate. |
| `NUM_QUBITS` | Number of qubits used by the task. |
| `TWO_QUBIT_GATES` | Number of two-qubit gates. |
| `ESTIMATED_RUNTIME` | Estimated execution time. |
| `QPU_NAME` | Human-readable QPU name. |
| `PROVIDER` | Provider identifier for diagnostics. |

Site and provider extensions should use a reserved user range. The standard
can later define a registry if interoperability requires shared extension keys.

## Policy Contract

The policy contract should define what a scheduler policy may rely on.

Policies may inspect:

- Task descriptor fields.
- QPU profile fields.
- Metadata entries.
- Lifecycle history maintained by the scheduler.
- Callback results supplied by the runtime adapter.

Policies must not:

- Call hardware providers directly.
- Call QFw, QRMI, QDMI, or simulator APIs directly.
- Parse opaque task payloads unless the implementation explicitly declares a
  payload-specific extension.
- Mutate provider state.
- Assume that string labels are available.

This boundary keeps scheduling independent from submission and QPU access.

## Plugin Packaging

The language-neutral standard should define the policy contract. It should not
require one plugin packaging model. A C implementation may use shared-object
plugins. A Python implementation may use entry points or import paths. A Rust
implementation may use traits and dynamic libraries.

The C binding should define its own plugin profile:

- Policy plugins are shared objects.
- A plugin exports one descriptor symbol.
- The core validates ABI version, descriptor size, policy name, callbacks, and
  thread flags.
- Installed plugins live under the scheduler library prefix.
- Runtime lookup can use a compiled-in plugin path, an environment override, or
  explicit plugin paths.

This keeps the standard focused on behavior while still allowing the C
reference implementation to support black-box site policies.

## Binding Strategy

Each binding should map the same abstract operations to language-appropriate
forms.

### C Binding

The C binding should use:

- Opaque handles for long-lived scheduler and plugin state.
- Public value structs for task descriptors, QPU profiles, assignments,
  metadata, and callback tables.
- Enum result codes.
- Explicit ownership and free functions for implementation-allocated output.

Opaque handles are a C ABI technique. They should appear in the C binding, not
in the language-neutral core specification.

### Python Binding

The Python binding should use:

- `Scheduler` as a class that owns a scheduler instance.
- `Task`, `QPU`, and `Assignment` as value objects.
- Python dictionaries or typed helper objects for metadata.
- Python exceptions that preserve the standard result code.
- Optional callbacks for cost estimation, task comparison, and splitting.

The Python binding should not define a different scheduler model. It should be
a direct mapping of the abstract operations.

## Conformance

A conforming implementation should pass a shared behavior test suite. The tests
should not depend on a specific implementation language.

Initial conformance tests should cover:

- Duplicate task IDs.
- FIFO behavior.
- Priority behavior.
- Round-robin behavior.
- Task cancellation before execution.
- Lifecycle transition validation.
- No-runnable-task behavior.
- QPU availability changes.
- Metadata preservation.
- Statistics reporting.

The test suite should be usable against the C reference implementation, Python
wrappers, and independent implementations.

## Relationship To detailed-design.md

The detailed design remains the implementation plan. It can make concrete decisions
about CMake, shared libraries, plugin symbols, opaque handles, memory
ownership, and Python extension mechanics.

This standardization plan describes the interface model that should emerge
from that implementation. Once the first implementation is stable, the useful
parts of `detailed-design.md` can be split into:

- A language-neutral scheduler specification.
- A C binding document.
- A Python binding document.
- A plugin ABI document.

That split should happen after the interface has been exercised in real code.
