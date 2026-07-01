# qhw-scheduler Detailed Design

## Purpose

This document is a C-focused companion to `../sched_plan.md`. It narrows the
`qhw-scheduler` scope to a QPU-local scheduler. The scheduler is
analogous to a CPU kernel scheduler. It manages the queue for one QPU execution
target and decides which accepted quantum task should occupy that QPU next.
The core scheduler should be implemented in C for predictable overhead, compact
state, and a stable ABI. Python bindings should sit on top of that ABI.

The C implementation is the reference implementation. The interface is
language-neutral. The same concepts can be implemented in Rust, C++, Python, Go,
or another language when the observable behavior and data contract match.

The scheduler is independent of QFw, DEFw, QRMI, QDMI, qSchedSim, and hardware
provider APIs. Those systems integrate through adapters. The scheduler owns
local QPU queue state and policy. The caller owns QPU execution and lifecycle
events.

## Design Goals

- Provide a small C ABI that can be called from QFw, Python, simulators, and
  future runtime layers.
- Keep scheduling policy separate from task submission, provider interaction,
  and hardware authentication.
- Make task payloads opaque. The scheduler operates on task envelopes,
  metadata, and payload references. Circuits, QASM, QIR, Qiskit objects,
  provider JSON, and simulator objects stay inside adapter-owned payloads.
- Keep quantum-specific information in extensible metadata. A policy may use
  that metadata, while the core scheduler remains based on the common task
  envelope.
- Manage one QPU execution target per scheduler instance.
- Avoid multi-QPU placement. A higher-level runtime or mesh scheduler should
  decide which QPU receives work before this scheduler sees the task.
- Load scheduling policies as plugin shared objects.
- Expose enough hooks for FIFO first, then priority, round-robin,
  shortest-job-first, longest-job-first, hybrid ordering, and time-slicing
  policies in later phases.
- Keep Python bindings thin. Python should configure and observe the scheduler,
  while the hot queue and selection path remains in C.
- Keep implementation lines at 80 characters or less.
- Prefer simple and clear implementations over early optimization.

## Repository Layout

The proposed repository should be independent from QFw.

```text
qhw-scheduler/
  CMakeLists.txt
  pyproject.toml
  README.md
  LICENSE

  include/
    qhw_scheduler/
      qhw_scheduler.h
      qhw_scheduler_types.h
      qhw_scheduler_plugin.h
      qhw_scheduler_abi.h

  src/
    qhw_scheduler_internal.h
    qhw_scheduler.c
    qhw_task.c
    qhw_qpu.c
    qhw_stats.c
    qhw_error.c
    qhw_plugin.c
    qhw_allocator.c
    qhw_thread.c

    util/
      qhw_hash_table.c
      qhw_hash_table.h
      qhw_heap.c
      qhw_heap.h
      qhw_rb_tree.c
      qhw_rb_tree.h
      qhw_list.c
      qhw_list.h
      qhw_ring.c
      qhw_ring.h

    plugins/
      fifo.c

  bindings/
    python/
      qhw_scheduler_py.c

  python/
    qhw_scheduler/
      __init__.py
      scheduler.py
      task.py
      qpu.py
      errors.py

  tests/
    c/
      test_fifo.c
      test_plugin_load.c
      test_lifecycle.c
      test_thread_safety.c
      test_errors.c

    python/
      test_fifo.py
      test_bindings.py
      test_qfw_adapter_shape.py

  examples/
    c/
      fifo_example.c
      plugin_example.c

    python/
      fifo_example.py
      qfw_adapter_sketch.py

  docs/
    interface.md
    plugin_abi.md
    scheduler_policies.md
    qfw_integration.md
```

### Build System

CMake is a reasonable first choice because it can build the C library, plugin
shared objects, C tests, and the Python extension. Python wheels can be built
through `scikit-build-core` in `pyproject.toml`.

The build should allow the caller to choose static or shared core libraries.
The options should be explicit:

```text
QHW_SCHED_BUILD_SHARED=ON|OFF
QHW_SCHED_BUILD_STATIC=ON|OFF
QHW_SCHED_BUILD_PLUGINS=ON|OFF
QHW_SCHED_BUILD_PYTHON=ON|OFF
QHW_SCHED_INSTALL_PLUGINS=ON|OFF
```

The project may support building both `libqhw_scheduler.so` and
`libqhw_scheduler.a` in one build. If that adds build complexity, the first
implementation can require one of `QHW_SCHED_BUILD_SHARED` or
`QHW_SCHED_BUILD_STATIC` to be enabled.

The build should produce some or all of these artifacts:

- `libqhw_scheduler.so`.
- `libqhw_scheduler.a`.
- Policy plugin shared objects. Examples include `qhw_sched_fifo.so`,
  `qhw_sched_round_robin.so`, `qhw_sched_priority.so`,
  `qhw_sched_weaver.so`, and `qhw_sched_tbweaver.so`.
- A Python extension module that links against the C library.

The core library contains scheduler lifecycle, task accounting, threading, and
plugin loading. Every scheduling policy is built as a plugin shared object. The
standard distribution installs the common policies into the scheduler plugin
directory. Site-specific policies are installed as black-box plugin shared
objects in the same directory layout or discovered through the plugin search
path.

### Install Layout

The build should provide an install target. A default installation should use
this layout:

```text
<prefix>/
  include/
    qhw_scheduler/
      qhw_scheduler.h
      qhw_scheduler_types.h
      qhw_scheduler_plugin.h
      qhw_scheduler_abi.h

  lib/
    libqhw_scheduler.so
    libqhw_scheduler.a

    qhw_scheduler/
      plugins/
        qhw_sched_fifo.so
        qhw_sched_round_robin.so
        qhw_sched_priority.so
        qhw_sched_weaver.so
        qhw_sched_tbweaver.so

    cmake/
      qhw_scheduler/
        qhw_schedulerConfig.cmake
        qhw_schedulerTargets.cmake

  lib/pkgconfig/
    qhw_scheduler.pc
```

The Python package can be installed through the wheel path generated by
`pyproject.toml`. Python should use the installed C library and plugin search
path shared by C applications. Policy plugins are installed through the
scheduler plugin layout.

Runtime plugin discovery should support three inputs:

- A compiled-in default plugin directory based on the install prefix.
- Additional plugin directories listed in `QHW_SCHED_PLUGIN_PATH`.
- Explicit paths passed to `qhw_sched_load_plugin()`.

`QHW_SCHED_PLUGIN_PATH` should follow the usual colon-separated path convention
on POSIX systems. `qhw_sched_load_default_plugins()` should load plugins from
the installed default directory and then load plugins from those additional
directories. Explicit plugin loading should open the requested shared-object
path directly.

## Public C ABI

The public ABI should use opaque handles and versioned value structs. Callers
interact through handles, IDs, and value descriptors. Scheduler internals stay
behind private implementation headers.

### Handles

```c
typedef struct qhw_sched qhw_sched_t;
typedef struct qhw_sched_qpu qhw_sched_qpu_t;
typedef struct qhw_sched_plugin qhw_sched_plugin_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_t` | Public ABI handle returned by `qhw_sched_create()`. Callers pass it to scheduler APIs when submitting tasks, reporting lifecycle events, selecting work, and querying scheduler-owned QPU state. The handle hides queues, locks, plugin state, and accounting so the implementation can evolve while preserving the public ABI. |
| `qhw_sched_qpu_t` | Public ABI handle returned by `qhw_sched_qpu_create()`. It represents one QPU execution target with library-owned profile metadata and runtime state. Scheduler instances retain this handle when they bind to a QPU. |
| `qhw_sched_plugin_t` | Public ABI handle reserved for plugin-management APIs. It gives callers a stable way to refer to a loaded policy module if unload, inspection, or policy switching APIs are added later. The loaded shared-object state remains scheduler-local. |

`qhw_sched_t`, `qhw_sched_qpu_t`, and `qhw_sched_plugin_t` are public opaque
handles. Their concrete definitions live in `src/qhw_scheduler_internal.h`.
Public APIs identify task records by numeric IDs and exchange state through
value descriptors.

### ABI Versioning

Every versioned public descriptor should contain a `struct_size` field. New
fields can be appended while preserving older callers. `struct_size` records
the byte size of the ABI structure itself, usually `sizeof(the_struct_type)`.
Scheduled task size, payload byte length, and estimated runtime use separate
fields or metadata.

Plugin descriptors should carry an ABI version.

```c
#define QHW_SCHED_ABI_VERSION 1

typedef struct qhw_sched_version {
    size_t struct_size;
    uint32_t abi_version;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} qhw_sched_version_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_version_t` | Public ABI value returned by version-query APIs. Callers, language bindings, and plugin loaders use it to check whether the runtime library and plugin ABI are compatible. |

### Return Codes

All public calls should return a small integer code. Detailed error text should
be available through `qhw_sched_last_error()`.

```c
typedef enum qhw_sched_rc {
    QHW_SCHED_OK = 0,
    QHW_SCHED_ERR_INVALID_ARG = 1,
    QHW_SCHED_ERR_NO_MEMORY = 2,
    QHW_SCHED_ERR_NOT_FOUND = 3,
    QHW_SCHED_ERR_ALREADY_EXISTS = 4,
    QHW_SCHED_ERR_POLICY = 5,
    QHW_SCHED_ERR_PLUGIN = 6,
    QHW_SCHED_ERR_STATE = 7
} qhw_sched_rc_t;
```

### Scheduler IDs

The core ABI should use numeric IDs only. Callers must provide non-zero task
IDs that are unique within a scheduler context. The scheduler validates task ID
uniqueness. Callers allocate task, QPU, job, reservation, owner, and other
identity values. The QPU ID is a caller-defined correlation ID for the QPU bound
to the scheduler instance.

```c
typedef uint64_t qhw_sched_task_id_t;
typedef uint64_t qhw_sched_qpu_id_t;
typedef uint64_t qhw_sched_job_id_t;
typedef uint64_t qhw_sched_reservation_id_t;
typedef uint64_t qhw_sched_owner_id_t;

#define QHW_SCHED_INVALID_ID 0
```

External strings such as UUID text, provider job IDs, QFw circuit IDs, and
human-readable QPU names should live outside the scheduler or in metadata
used for diagnostics. The hot scheduler path should use numeric IDs and avoid
string comparison, string hashing, string ownership, and string lifetime rules.

Metadata and option keys should follow the same rule. The C ABI should use
numeric keys. Documentation and language bindings can map friendly names to
those numeric keys.

### Scheduler Attributes

Scheduler creation should take explicit attributes. Threading mode belongs to
the core scheduler configuration.

```c
typedef enum qhw_sched_threading {
    QHW_SCHED_THREAD_SAFE = 1,
    QHW_SCHED_THREAD_USER = 2
} qhw_sched_threading_t;

typedef struct qhw_sched_allocator {
    size_t struct_size;
    void *(*alloc)(size_t size, void *user_data);
    void *(*realloc)(void *ptr, size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);
    void *user_data;
} qhw_sched_allocator_t;

typedef struct qhw_sched_attr {
    size_t struct_size;
    qhw_sched_threading_t threading;
    const qhw_sched_allocator_t *allocator;
    uint64_t flags;
} qhw_sched_attr_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_allocator_t` | Public ABI allocator descriptor supplied through `qhw_sched_attr_t`. It lets embedding runtimes route scheduler allocations through their own allocator, memory tracker, arena, or debugging layer. The scheduler copies the function pointers and `user_data` into scheduler-local state during creation. |
| `qhw_sched_attr_t` | Public ABI value passed to `qhw_sched_create()`. It configures scheduler behavior that must be fixed before shared state exists, including threading mode, allocator selection, and creation flags. |

The threading model is selected when the scheduler is created.
`QHW_SCHED_THREAD_SAFE` means the scheduler protects internal state and can be
called concurrently. `QHW_SCHED_THREAD_USER` means the caller serializes access
to one scheduler instance, which lets the implementation use no-op locks.

Allocator configuration is also selected when the scheduler is created. A
`NULL` allocator means the scheduler uses the default `malloc`, `realloc`, and
`free` path. A supplied allocator must provide all three functions. The
scheduler copies the allocator callbacks and stores `user_data` as an opaque
value passed back to those callbacks.

## Core Data Structures

The core data structures should be introduced from the reusable building blocks
to the scheduler outputs. Metadata values come first because task descriptors,
QPU profile data, runtime snapshots, policy options, and statistics all use the
same key/value container.

### Metadata Values

Metadata must be compact and typed. The initial ABI should avoid embedding JSON
in the hot path.

```c
typedef uint64_t qhw_sched_key_t;

typedef enum qhw_sched_builtin_key {
    QHW_SCHED_KEY_INVALID = 0,
    QHW_SCHED_META_SHOTS = 1,
    QHW_SCHED_META_DEPTH = 2,
    QHW_SCHED_META_NUM_QUBITS = 3,
    QHW_SCHED_META_TWO_QUBIT_GATES = 4,
    QHW_SCHED_META_ESTIMATED_RUNTIME_NS = 5,
    QHW_SCHED_META_DEVICE_NAME = 6,
    QHW_SCHED_META_PROVIDER = 7,
    QHW_SCHED_META_PARENT_TASK_ID = 8,
    QHW_SCHED_META_SLICE_INDEX = 9,
    QHW_SCHED_META_SLICE_COUNT = 10,
    QHW_SCHED_META_REQUESTED_SHOTS = 11,
    QHW_SCHED_META_CHILD_TASK_COUNT = 12,
    QHW_SCHED_META_MAX_SHOTS = 13,

    QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD = 100,
    QHW_SCHED_OPT_SLICE_MAX_SHOTS = 101,
    QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS = 102,
    QHW_SCHED_OPT_SLICE_MAX_CHILDREN = 103,

    QHW_SCHED_KEY_USER_BASE = 0x100000000ULL
} qhw_sched_builtin_key_t;

typedef enum qhw_sched_value_type {
    QHW_SCHED_VALUE_I64 = 0,
    QHW_SCHED_VALUE_U64 = 1,
    QHW_SCHED_VALUE_F64 = 2,
    QHW_SCHED_VALUE_BOOL = 3,
    QHW_SCHED_VALUE_STRING = 4,
    QHW_SCHED_VALUE_BYTES = 5
} qhw_sched_value_type_t;

typedef struct qhw_sched_kv {
    qhw_sched_key_t key;
    qhw_sched_value_type_t type;
    union {
        int64_t i64;
        uint64_t u64;
        double f64;
        int boolean;
        const char *string;
        struct {
            const void *data;
            size_t size;
        } bytes;
    } value;
} qhw_sched_kv_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_kv_t` | Public ABI extension value used anywhere the fixed structs are too narrow. Callers use it for task metadata and policy options. Plugins use it for policy-specific inputs and statistics. Numeric keys keep the hot path compact while still allowing site-specific extensions. |

The scheduler should copy metadata it needs to retain. Callers may release
descriptor memory after submission returns. Task metadata, QPU metadata, and
policy options use the same compact key/value container. Site-specific and
provider-specific keys should start at `QHW_SCHED_KEY_USER_BASE` or use a
registry defined by the language-neutral specification.

### QPU Profile And Runtime State

One scheduler instance manages one QPU execution target. The caller first builds
a QPU object from a profile descriptor. The profile describes the execution
target and its capabilities. The returned `qhw_sched_qpu_t` owns copied profile
metadata and scheduler-visible runtime state.

Runtime state belongs to the scheduler. Queue length, running task ID,
provider-queue occupancy, and lifecycle counters are derived from task
submission, selection, and lifecycle events. Callers query this state through
runtime-state APIs on the QPU handle.

```c
typedef enum qhw_sched_qpu_state {
    QHW_SCHED_QPU_AVAILABLE = 0,
    QHW_SCHED_QPU_BUSY = 1,
    QHW_SCHED_QPU_DRAINING = 2,
    QHW_SCHED_QPU_UNAVAILABLE = 3
} qhw_sched_qpu_state_t;

typedef struct qhw_sched_qpu_profile {
    size_t struct_size;
    qhw_sched_qpu_id_t qpu_id;

    uint64_t provider_queue_capacity;

    const qhw_sched_kv_t *metadata;
    size_t metadata_count;
} qhw_sched_qpu_profile_t;

typedef struct qhw_sched_qpu_runtime {
    size_t struct_size;
    qhw_sched_qpu_id_t qpu_id;

    qhw_sched_qpu_state_t state;
    uint64_t pending_task_count;
    uint64_t provider_queue_depth;
    uint64_t active_task_count;
    qhw_sched_task_id_t running_task_id;

    uint64_t submitted_count;
    uint64_t started_count;
    uint64_t completed_count;
    uint64_t failed_count;
    uint64_t cancelled_count;

    const qhw_sched_kv_t *metadata;
    size_t metadata_count;
} qhw_sched_qpu_runtime_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_qpu_profile_t` | Public ABI profile descriptor supplied to `qhw_sched_qpu_create()`. It identifies the QPU and describes stable or slowly changing capabilities such as provider queue capacity, qubit count, topology reference, calibration ID, max shots, and provider name. |
| `qhw_sched_qpu_runtime_t` | Public ABI snapshot returned by query APIs. The scheduler builds it from its own queue, selected task, provider-queue accounting, lifecycle events, and external availability events. Callers use it for reporting and observability. |

The first implementation should model the QPU as a single execution slot. In
runtime snapshots, `active_task_count` should be zero or one.
`provider_queue_capacity` in the profile describes how many assignments the
adapter is willing to keep in the provider-facing queue, if the provider has
one. `provider_queue_depth` in the runtime snapshot reports how many assignments
the scheduler currently believes are queued below it. A site may set capacity
to one for strict local scheduling.

Profile metadata may include `num_qubits`, `max_shots`,
`supports_shot_slicing`, `average_gate_ns`, `readout_ns`, topology IDs,
calibration identifiers, provider names, or human-readable QPU names. Runtime
metadata may include external availability reasons, last calibration update
time, provider status, or adapter-observed queue state. The scheduler core
assigns meaning only to keys documented by the selected policy.

### Task Descriptor

The task descriptor is a scheduling envelope. It carries enough information for
generic queue policy through common fields, metadata, and opaque payload
references.

```c
typedef enum qhw_sched_task_state {
    QHW_SCHED_TASK_PENDING = 0,
    QHW_SCHED_TASK_READY = 1,
    QHW_SCHED_TASK_RUNNING = 2,
    QHW_SCHED_TASK_COMPLETED = 3,
    QHW_SCHED_TASK_FAILED = 4,
    QHW_SCHED_TASK_CANCELLED = 5
} qhw_sched_task_state_t;

typedef uint64_t qhw_sched_payload_type_t;

typedef enum qhw_sched_builtin_payload_type {
    QHW_SCHED_PAYLOAD_NONE = 0,
    QHW_SCHED_PAYLOAD_OPAQUE_PTR = 1,
    QHW_SCHED_PAYLOAD_BYTES = 2,
    QHW_SCHED_PAYLOAD_EXTERNAL_HANDLE = 3,

    QHW_SCHED_PAYLOAD_USER_BASE = 0x100000000ULL
} qhw_sched_builtin_payload_type_t;

typedef struct qhw_sched_task_desc {
    size_t struct_size;
    qhw_sched_task_id_t task_id;
    qhw_sched_job_id_t job_id;
    qhw_sched_reservation_id_t reservation_id;
    qhw_sched_owner_id_t owner_id;

    int64_t priority;
    uint64_t created_ns;
    uint64_t deadline_ns;
    uint64_t estimated_runtime_ns;

    qhw_sched_payload_type_t payload_type;
    const void *payload;
    size_t payload_size;
    uint64_t payload_handle;

    const qhw_sched_kv_t *metadata;
    size_t metadata_count;
} qhw_sched_task_desc_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_task_desc_t` | Public ABI task envelope supplied by the caller when work enters the scheduler. It carries task identity, ownership, priority, timing hints, an opaque payload reference, and metadata used by policy plugins. `struct_size` is the ABI structure size. Task size and slicing inputs are carried by fields such as `estimated_runtime_ns` and by metadata such as `QHW_SCHED_META_SHOTS`. The scheduler copies the parts it must retain into scheduler-local task records. |

The payload fields are optional. A QFw adapter may store a `Circuit` pointer in
`payload`. A simulator adapter may store a simulator task pointer. A language
binding may store an object handle in `payload_handle`. The C core should treat
these as opaque values.

Quantum-specific values such as `shots`, `depth`, `num_qubits`, and
`two_qubit_gate_count` should live in `metadata`. A quantum-aware policy can
read those fields by convention. A generic policy can ignore them.

The caller should put `QHW_SCHED_META_SHOTS` in the submitted task metadata for
sampling tasks. That key records the shot count requested by the logical task.
A shot-slicing policy compares this task-level value with QPU profile metadata
such as `QHW_SCHED_META_MAX_SHOTS` and policy options such as
`QHW_SCHED_OPT_SLICE_MAX_SHOTS`. During submission, the selected policy may
expand a logical task into scheduled child tasks. The parent task remains an
aggregate object that tracks child completion. The children are the runnable
units returned later by `qhw_sched_next_task()`.

For sliced work, the parent should retain `QHW_SCHED_META_REQUESTED_SHOTS`,
`QHW_SCHED_META_CHILD_TASK_COUNT`, and the original payload reference. Each
child should carry a unique `task_id`, `QHW_SCHED_META_PARENT_TASK_ID`,
`QHW_SCHED_META_SLICE_INDEX`, `QHW_SCHED_META_SLICE_COUNT`, and its own
`QHW_SCHED_META_SHOTS` value. Children inherit owner, job, reservation,
priority, deadline, and payload reference unless a policy explicitly overrides
one of those fields.

Child tasks should reference the parent's payload. The scheduler should not
duplicate circuit buffers, provider JSON, QASM text, QIR modules, or simulator
objects when expanding a task. The parent owns the shared payload reference for
the lifetime of all children. Children store the same payload pointer or handle
and carry only child-specific metadata.

Child tasks should be schedulable using the same policy inputs as the parent.
That means a scheduler comparing priority, owner, reservation, deadline, or
estimated runtime should see a child as part of the original logical task.
Only the fields that identify the child or describe the slice should differ by
default:

| Field group | Child task behavior |
| --- | --- |
| Identity | `task_id` is unique per child. `QHW_SCHED_META_PARENT_TASK_ID` links the child to the logical parent. |
| Ownership | `job_id`, `reservation_id`, and `owner_id` inherit from the parent. |
| Scheduling policy | `priority`, `deadline_ns`, and policy-relevant metadata inherit from the parent unless the slicing policy explicitly changes them. |
| Payload | `payload_type`, `payload`, `payload_size`, and `payload_handle` inherit from the parent by reference. Children point back to the parent's payload and store only child-specific metadata. |
| Slice description | `QHW_SCHED_META_SLICE_INDEX`, `QHW_SCHED_META_SLICE_COUNT`, and child `QHW_SCHED_META_SHOTS` are child-specific. |
| Timing estimate | `estimated_runtime_ns` may be recomputed for the child using the child shot count, or inherited when no better estimate is available. |

The effective slice size should come from both QPU capability and policy
configuration. A typical time-slice policy can use:

```text
slice_shots = min(qpu_max_shots, policy_max_shots)
```

`qpu_max_shots` should come from QPU profile metadata, such as a
`QHW_SCHED_META_MAX_SHOTS` key or a site-defined equivalent.
`policy_max_shots` should come from `QHW_SCHED_OPT_SLICE_MAX_SHOTS`.

### Assignment Descriptor

The selected scheduled task should be returned as an assignment. Since slicing
happens at submission time, `qhw_sched_next_task()` returns an already-runnable
task. For unsliced work, the selected task is the logical task itself. For
sliced work, the selected task is one child slice.

```c
typedef struct qhw_sched_assignment {
    size_t struct_size;
    qhw_sched_task_id_t task_id;

    uint64_t slice_index;
    uint64_t slice_count;
    const qhw_sched_kv_t *metadata;
    size_t metadata_count;
} qhw_sched_assignment_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_assignment_t` | Public ABI result returned by `qhw_sched_next_task()`. It identifies the scheduled task selected for provider submission. For sliced work, the task ID refers to a child task and the slice fields identify its position under the parent logical task. |

For unsliced tasks, `slice_index` should be zero and `slice_count` should be
one. For sliced tasks, these values should match the metadata stored on the
child task. The adapter should submit the selected child task with its own shot
count and then report lifecycle events for that child.

## Scheduler API

### Context Lifecycle

```c
qhw_sched_rc_t qhw_sched_qpu_create(
    const qhw_sched_qpu_profile_t *profile,
    qhw_sched_qpu_t **out_qpu);

void qhw_sched_qpu_destroy(qhw_sched_qpu_t *qpu);

qhw_sched_rc_t qhw_sched_create(
    const char *policy_name,
    const qhw_sched_attr_t *attr,
    qhw_sched_qpu_t *qpu,
    const qhw_sched_kv_t *options,
    size_t option_count,
    qhw_sched_t **out_sched);

qhw_sched_threading_t qhw_sched_get_threading(qhw_sched_t *sched);

void qhw_sched_destroy(qhw_sched_t *sched);

qhw_sched_rc_t qhw_sched_reset(qhw_sched_t *sched);

const char *qhw_sched_last_error(qhw_sched_t *sched);
```

`qhw_sched_qpu_create()` validates the supplied profile and copies metadata into
a library-owned QPU execution-target object. `qhw_sched_create()` binds a
scheduler instance to one `qhw_sched_qpu_t`. The scheduler retains the QPU
handle, and `qhw_sched_destroy()` releases that retained reference. Caller-owned
references are released with `qhw_sched_qpu_destroy()`.

Callers may create multiple QPU handles and then create one scheduler per QPU
handle. Each scheduler still manages one execution target. Selection among
multiple QPU handles belongs to a higher runtime or resource-management layer.

Capability metadata may be refreshed later when calibration, topology, or
provider limits change. The scheduler instance remains bound to the QPU handle
supplied at creation time.

The first implementation supports both thread-safe and caller-serialized modes.
A scheduler instance created with `QHW_SCHED_THREAD_SAFE` protects its internal
task table, QPU record, plugin registry, policy state, statistics, and error
state. A scheduler instance created with `QHW_SCHED_THREAD_USER` assumes the
caller serializes access to that scheduler instance and uses no-op locks
internally.

### Policy Selection

```c
typedef struct qhw_sched_policy_info {
    size_t struct_size;
    const char *name;
    const char *version;
    const char *description;
    uint64_t capabilities;
    uint64_t thread_flags;
    const char *plugin_path;
} qhw_sched_policy_info_t;

qhw_sched_rc_t qhw_sched_set_policy(
    qhw_sched_t *sched,
    const char *policy_name,
    const qhw_sched_kv_t *options,
    size_t option_count);

qhw_sched_rc_t qhw_sched_load_plugin(
    qhw_sched_t *sched,
    const char *shared_object_path);

qhw_sched_rc_t qhw_sched_load_plugin_dir(
    qhw_sched_t *sched,
    const char *directory_path);

qhw_sched_rc_t qhw_sched_load_default_plugins(
    qhw_sched_t *sched);

qhw_sched_rc_t qhw_sched_list_policies(
    qhw_sched_t *sched,
    qhw_sched_policy_info_t **out_policies,
    size_t *out_count);

void qhw_sched_free_policy_info_array(
    qhw_sched_policy_info_t *policies,
    size_t count);
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_policy_info_t` | Public ABI policy-discovery record returned by `qhw_sched_list_policies()`. It lets callers inspect loaded policy plugins before selecting one by name. The scheduler owns the source registry and returns a snapshot that the caller releases with `qhw_sched_free_policy_info_array()`. |

Policies are selected by name. Standard policy names are defined as public
string constants, and `qhw_sched_list_policies()` reports the loaded policies
available in a scheduler instance.

```c
#define QHW_SCHED_POLICY_FIFO "fifo"
#define QHW_SCHED_POLICY_PRIORITY "priority"
#define QHW_SCHED_POLICY_ROUND_ROBIN "round_robin"
#define QHW_SCHED_POLICY_SJF "sjf"
#define QHW_SCHED_POLICY_LJF "ljf"
#define QHW_SCHED_POLICY_TIME_SLICE "time_slice"
```

Policy discovery reports plugins already loaded into the scheduler. Callers
discover installed plugin policies by first calling
`qhw_sched_load_default_plugins()`, `qhw_sched_load_plugin_dir()`, or
`qhw_sched_load_plugin()`, then calling `qhw_sched_list_policies()`.
`qhw_sched_load_default_plugins()` loads the installed policy directory and the
additional directories listed in `QHW_SCHED_PLUGIN_PATH`.

Policy-specific configuration is passed through the `options` array in
`qhw_sched_create()` and `qhw_sched_set_policy()`. These options use
`qhw_sched_kv_t`, the same compact key/value container used for metadata and
statistics. Each policy must document which option keys it consumes and what
defaults it applies for omitted options.

Shot-slicing policy configuration should use explicit option keys:

| Option | Meaning |
| --- | --- |
| `QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD` | Minimum `QHW_SCHED_META_SHOTS` value that makes a task eligible for slicing. Tasks below this value run as one unit. |
| `QHW_SCHED_OPT_SLICE_MAX_SHOTS` | Maximum shot count assigned to one child task when a large task is split. |
| `QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS` | Minimum remainder worth keeping as a separate final child. Smaller remainders can be folded into the previous child. |
| `QHW_SCHED_OPT_SLICE_MAX_CHILDREN` | Upper bound on the number of child tasks created from one logical task. |

The task descriptor carries scheduler-visible task facts, such as
`QHW_SCHED_META_SHOTS`. Policy options control how the selected policy uses
those facts. This keeps task description, policy behavior, and site
configuration separate.

Shot slicing should run during `qhw_sched_submit_task()` or
`qhw_sched_submit_tasks()`. Submission either enqueues the task as a single
runnable unit or expands it into child tasks and enqueues those children. The
parent task remains in the scheduler as an aggregate tracking record.
Completion of the parent is derived from completion of all children.

### Task-Facing API

```c
qhw_sched_rc_t qhw_sched_submit_task(
    qhw_sched_t *sched,
    const qhw_sched_task_desc_t *task);

qhw_sched_rc_t qhw_sched_submit_tasks(
    qhw_sched_t *sched,
    const qhw_sched_task_desc_t *tasks,
    size_t task_count);

qhw_sched_rc_t qhw_sched_cancel_task(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const char *reason);

qhw_sched_rc_t qhw_sched_update_task(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *updates,
    size_t update_count);

qhw_sched_rc_t qhw_sched_get_task(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    qhw_sched_task_desc_t *out_task);

qhw_sched_rc_t qhw_sched_pending_count(
    qhw_sched_t *sched,
    uint64_t *out_count);
```

The core should own copied task records. The caller should receive references
or snapshots through query functions. Internal task records remain
scheduler-owned.

When submission expands a task, `qhw_sched_get_task()` should be able to return
the parent aggregate record and each child record by ID. Parent records track
child count, completed child count, failed child count, cancelled child count,
and aggregate terminal state. Child records track normal lifecycle state and
carry parent and slice metadata.

Expanded tasks should follow shallow payload retention across the implementation.
Task descriptors, metadata arrays, lifecycle state, and parent-child accounting
are copied into scheduler-owned records. Large opaque payloads stay shared
through parent-owned pointers or handles.

### QPU-Facing API

```c
qhw_sched_rc_t qhw_sched_update_qpu_profile(
    qhw_sched_qpu_t *qpu,
    const qhw_sched_kv_t *updates,
    size_t update_count);

qhw_sched_rc_t qhw_sched_set_qpu_state(
    qhw_sched_qpu_t *qpu,
    qhw_sched_qpu_state_t state,
    const char *reason);

qhw_sched_rc_t qhw_sched_get_qpu_profile(
    qhw_sched_qpu_t *qpu,
    qhw_sched_qpu_profile_t *out_profile);

qhw_sched_rc_t qhw_sched_get_qpu_runtime(
    qhw_sched_qpu_t *qpu,
    qhw_sched_qpu_runtime_t *out_runtime);
```

QPU profile updates carry external facts supplied by the adapter or operator,
such as changed topology, disabled qubits, updated calibration IDs, new provider
queue limits, or changed shot limits. Scheduler-owned load state comes from
task submission, selection, and lifecycle events.

QPU state changes are external availability events. Adapters translate runtime,
simulator, operator, or provider events into `qhw_sched_set_qpu_state()`. Queue
depth and counters are queried through `qhw_sched_get_qpu_runtime()`.

### Selection API

```c
qhw_sched_rc_t qhw_sched_next_task(
    qhw_sched_t *sched,
    qhw_sched_assignment_t *out_assignment);
```

`qhw_sched_next_task()` selects the next runnable task for the scheduler's QPU.
Multi-QPU placement happens before a task is submitted to this scheduler.

### Lifecycle API

```c
qhw_sched_rc_t qhw_sched_task_started(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *metadata,
    size_t metadata_count);

qhw_sched_rc_t qhw_sched_task_completed(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *result_summary,
    size_t result_summary_count);

qhw_sched_rc_t qhw_sched_task_failed(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *error_summary,
    size_t error_summary_count);

qhw_sched_rc_t qhw_sched_task_timed_out(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const qhw_sched_kv_t *error_summary,
    size_t error_summary_count);

qhw_sched_rc_t qhw_sched_task_cancelled(
    qhw_sched_t *sched,
    qhw_sched_task_id_t task_id,
    const char *reason);
```

These calls let the scheduler update accounting, fairness, retry state, and
QPU occupancy. They also support policies that need to observe completed
runtime in addition to estimated cost.

### Statistics API

```c
qhw_sched_rc_t qhw_sched_get_stats(
    qhw_sched_t *sched,
    qhw_sched_kv_t **out_stats,
    size_t *out_count);

void qhw_sched_free_kv_array(qhw_sched_kv_t *items, size_t count);
```

Stats should include queue length, running task count, completed task count,
failed task count, average wait time, and policy-specific metrics.

## Callback Surface

Some decisions are domain-specific and belong in adapter callbacks.
The scheduler should allow adapters to register callbacks. Plugins can call
these callbacks through the core.

Callbacks receive QPU profile and runtime snapshots, not the mutable
`qhw_sched_qpu_t` handle. This keeps callback decisions based on stable state
captured by the scheduler.

```c
typedef struct qhw_sched_callbacks {
    size_t struct_size;

    int (*compare)(
        const qhw_sched_task_desc_t *left,
        const qhw_sched_task_desc_t *right,
        const qhw_sched_qpu_profile_t *qpu,
        const qhw_sched_qpu_runtime_t *runtime,
        void *user_data);

    qhw_sched_rc_t (*estimate_cost)(
        const qhw_sched_task_desc_t *task,
        const qhw_sched_qpu_profile_t *qpu,
        const qhw_sched_qpu_runtime_t *runtime,
        qhw_sched_kv_t **out_cost,
        size_t *out_count,
        void *user_data);

    qhw_sched_rc_t (*split_task)(
        const qhw_sched_task_desc_t *task,
        const qhw_sched_qpu_profile_t *qpu,
        const qhw_sched_qpu_runtime_t *runtime,
        qhw_sched_task_desc_t **out_children,
        size_t *out_count,
        void *user_data);

    void *user_data;
} qhw_sched_callbacks_t;

qhw_sched_rc_t qhw_sched_set_callbacks(
    qhw_sched_t *sched,
    const qhw_sched_callbacks_t *callbacks);
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_callbacks_t` | Public ABI callback table registered by the caller. It lets the scheduler ask adapter-owned code for domain-specific policy inputs, such as how two tasks compare, how much a task costs, or how a large task should be split. The callback table keeps circuit-aware logic outside the generic core. |

Tasks submitted to this scheduler are QPU-ready tasks. Admission, compilation,
placement, and QPU compatibility checks happen before submission. The callback
surface is for ordering, cost estimation, and task expansion.

The `compare` callback lets a caller define size-aware ordering while the core
stays independent of quantum-circuit internals. The `split_task` callback lets
a time-slicing policy ask the caller to split work during submission. For shot
slicing, the caller inspects `QHW_SCHED_META_SHOTS`, applies the policy options,
and returns child task descriptors. Each child should carry its own shot count
and parent/slice metadata. The scheduler enqueues the children as runnable
tasks and keeps the parent as aggregate state.

This keeps the scheduler generic while avoiding a shadow task database in the
caller. The task envelope carries opaque extension data. The callback knows how
to interpret that data when policy requires it.

## Plugin ABI

Plugins should be policy modules. They interact with tasks, QPU snapshots, and
callbacks through the scheduler ABI. Provider and runtime integration stays in
the adapter layer.

### Plugin Descriptor

```c
typedef struct qhw_sched_plugin_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *name;
    const char *version;
    const char *description;
    uint64_t capabilities;
    uint64_t thread_flags;

    qhw_sched_rc_t (*init)(
        qhw_sched_t *sched,
        const qhw_sched_kv_t *options,
        size_t option_count,
        void **out_policy_state);

    void (*fini)(void *policy_state);

    qhw_sched_rc_t (*on_task_submit)(
        void *policy_state,
        const qhw_sched_task_desc_t *task);

    qhw_sched_rc_t (*on_task_update)(
        void *policy_state,
        qhw_sched_task_id_t task_id,
        const qhw_sched_kv_t *updates,
        size_t update_count);

    qhw_sched_rc_t (*select_next)(
        void *policy_state,
        qhw_sched_assignment_t *out_assignment);

    qhw_sched_rc_t (*on_task_started)(
        void *policy_state,
        qhw_sched_task_id_t task_id);

    qhw_sched_rc_t (*on_task_finished)(
        void *policy_state,
        qhw_sched_task_id_t task_id,
        qhw_sched_task_state_t terminal_state);

    qhw_sched_rc_t (*get_stats)(
        void *policy_state,
        qhw_sched_kv_t **out_stats,
        size_t *out_count);
} qhw_sched_plugin_desc_t;
```

| Structure | Explanation |
| --- | --- |
| `qhw_sched_plugin_desc_t` | Public plugin ABI descriptor exported by each scheduling-policy module. The scheduler core reads it when loading a plugin, validates the ABI version and required callbacks, then invokes those callbacks to maintain policy state and select the next task. |

Initial plugin thread flags:

```c
typedef enum qhw_sched_plugin_thread_flag {
    QHW_SCHED_PLUGIN_LOCK_SAFE = 1ULL << 0,
    QHW_SCHED_PLUGIN_CALLS_USER_CALLBACKS = 1ULL << 1,
    QHW_SCHED_PLUGIN_REENTRANT = 1ULL << 2
} qhw_sched_plugin_thread_flag_t;
```

The core should accept dynamic plugins only when they declare compatible thread
behavior. A plugin that calls user callbacks is lock-safe only when it follows
the scheduler's two-phase callback rules.

Dynamic plugins should export one symbol:

```c
const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void);
```

The core should validate `abi_version`, `size`, `name`, and required function
pointers before accepting a plugin.

### Plugin Boundary

Plugin shared objects should include only public scheduler headers. Private
tables, private queues, and private lock structures stay inside the core
library. This keeps the plugin ABI stable enough for black-box site policies.

A plugin should implement policy behavior through the descriptor callbacks. The
core owns task records, retained QPU handles, lifecycle state, and memory
ownership. The plugin owns only its policy state. It may keep indexes or queues
derived from task IDs, but it should treat task and QPU descriptors as snapshots
provided by the core.

The core should provide helper functions only when they are part of the public
plugin ABI. Internal helper behavior may change between implementation
releases.

### Plugin Loading

The core should support explicit and discoverable plugin loading:

```c
qhw_sched_rc_t qhw_sched_load_plugin(
    qhw_sched_t *sched,
    const char *shared_object_path);

qhw_sched_rc_t qhw_sched_load_plugin_dir(
    qhw_sched_t *sched,
    const char *directory_path);

qhw_sched_rc_t qhw_sched_load_default_plugins(
    qhw_sched_t *sched);
```

Loading a plugin should:

1. Open the shared object.
2. Resolve `qhw_sched_plugin_descriptor`.
3. Validate ABI version, descriptor size, name, callbacks, capabilities, and
   thread flags.
4. Register the plugin policy by name.
5. Keep the shared object handle alive until the scheduler is destroyed or the
   plugin is explicitly unloaded.

The first implementation can keep loaded plugins alive for the lifetime of the
scheduler instance. Unloading can be added later if a real use case appears.

### Plugin Selection

Policy names come from loaded plugin descriptors. For example, the standard
FIFO plugin exports the name `fifo`, and a site plugin exports its own policy
name through the same descriptor field. Duplicate policy names should be
rejected unless an explicit override flag is added.

Supported deployment modes:

- Standard policy plugins installed under the scheduler plugin directory.
- Site policy plugins installed under the scheduler plugin directory.
- External policy plugin directories listed in `QHW_SCHED_PLUGIN_PATH`.

### Standard Policy Plugins

The standard distribution should provide these policy plugins:

- `fifo`: preserve insertion order.
- `priority`: select highest priority, then oldest task.
- `round_robin`: rotate between owners, jobs, or reservations.
- `sjf`: select the task with the smallest estimated cost.
- `ljf`: select the task with the largest estimated cost.
- `weaver`: combine priority and workload-size policy.
- `tbweaver`: time-based weaver policy.
- `time_slice`: split large tasks into smaller slices.
- `deadline`: prefer tasks close to their deadline.
- `fair_share`: weight users, accounts, or reservations over time.

## Internal C Design

The core should provide reusable internal helpers, but plugins should own their
policy-specific queues.

Recommended internal components:

- Task registry keyed by numeric `task_id`.
- One QPU record held directly by the scheduler context.
- Reusable data structure helpers under `src/util`, including hash tables,
  heaps, red-black trees, intrusive lists, and ring buffers.
- Metadata copy/free helpers.
- Assignment allocator and free helpers.
- Plugin manager for dynamic policy plugins.
- Error buffer per scheduler context.
- Optional allocator hooks for embedded runtimes.

The first implementation should avoid global mutable state. A scheduler context
should carry all policy, task, local QPU, stats, and error state.

### Private Structure Definitions

The concrete scheduler state should live in `src/qhw_scheduler_internal.h`.
This header is compiled into the core library and is not installed as part of
the public ABI. It gives the implementation one place to define retained task
records, QPU state, plugin registry entries, locks, accounting, and error
storage.

```c
/* src/qhw_scheduler_internal.h */
struct qhw_task_record {
    qhw_sched_task_desc_t desc;
    qhw_sched_task_state_t state;
    qhw_sched_task_id_t parent_task_id;
    uint64_t slice_index;
    uint64_t slice_count;
    uint64_t child_task_count;
    uint64_t completed_child_count;
    uint64_t failed_child_count;
    uint64_t cancelled_child_count;
    uint64_t enqueue_seq;
    uint64_t last_update_ns;
    uint64_t start_ns;
    uint64_t finish_ns;
};

struct qhw_qpu_record {
    qhw_sched_qpu_profile_t profile;
    qhw_sched_qpu_runtime_t runtime;
    qhw_sched_task_id_t running_task_id;
    uint64_t last_update_ns;
    uint64_t refcount;
};

struct qhw_policy_ops {
    const qhw_sched_plugin_desc_t *desc;
    void *state;
};

struct qhw_sched_plugin {
    const qhw_sched_plugin_desc_t *desc;
    void *dl_handle;
};

struct qhw_sched {
    qhw_sched_threading_t threading;
    struct qhw_mutex lock;
    struct qhw_lock_ops lock_ops;
    struct qhw_policy_ops policy;

    struct qhw_task_table tasks;
    qhw_sched_qpu_t *qpu;
    struct qhw_plugin_registry plugins;

    qhw_sched_callbacks_t callbacks;
    struct qhw_sched_stats stats;
    struct qhw_sched_error last_error;
    struct qhw_allocator allocator;

    uint64_t enqueue_seq_next;
    uint32_t flags;
};
```

| Structure | Explanation |
| --- | --- |
| `struct qhw_task_record` | Scheduler-local task state. It is created from `qhw_sched_task_desc_t` when the caller submits work, then tracks queue order, lifecycle transitions, parent-child slicing relationships, aggregate child completion, and timing data until the task reaches a terminal state. |
| `struct qhw_qpu_record` | Scheduler-local concrete state behind `qhw_sched_qpu_t`. It keeps the caller-provided QPU profile, scheduler-owned runtime snapshot, currently running task, last update time, and reference count. Policies use this state to reason about local device occupancy while queue load remains scheduler-owned. |
| `struct qhw_policy_ops` | Scheduler-local policy binding. It connects the selected plugin descriptor with the plugin's private state. The core calls the active policy through this descriptor interface. |
| `struct qhw_sched_plugin` | Scheduler-local plugin registry entry. It records the plugin descriptor and dynamic loader handle for one loaded policy module. |
| `struct qhw_sched` | Scheduler-local concrete scheduler context behind `qhw_sched_t`. It owns the lock strategy, active policy, task table, retained QPU handle, plugin registry, callbacks, exported stats, error buffer, allocator hooks, and sequence counters. |

The concrete table, stats, error, and allocator structures should also be
private. The initial implementation can use these shapes:

```c
struct qhw_task_table {
    struct qhw_hash_table by_id;
    size_t count;
};

struct qhw_plugin_registry {
    struct qhw_sched_plugin *items;
    size_t count;
    size_t capacity;
};

struct qhw_sched_stats {
    uint64_t submitted_count;
    uint64_t started_count;
    uint64_t completed_count;
    uint64_t failed_count;
    uint64_t cancelled_count;
};

struct qhw_sched_error {
    qhw_sched_rc_t code;
    char message[256];
};

struct qhw_allocator {
    void *(*alloc)(size_t size, void *user_data);
    void *(*realloc)(void *ptr, size_t size, void *user_data);
    void (*free)(void *ptr, void *user_data);
    void *user_data;
};

struct qhw_mutex {
    pthread_mutex_t mutex;
};

struct qhw_lock_ops {
    void (*lock)(struct qhw_mutex *lock);
    int (*trylock)(struct qhw_mutex *lock);
    void (*unlock)(struct qhw_mutex *lock);
};
```

| Structure | Explanation |
| --- | --- |
| `struct qhw_task_table` | Scheduler-local registry for retained task records. It is keyed by `task_id` and is used for lifecycle updates, cancellation, parent-child accounting, and result correlation. Scheduling order lives in policy-owned ready queues. |
| `struct qhw_plugin_registry` | Scheduler-local container for loaded policy plugins. It lets the core resolve a policy name to the corresponding descriptor before creating or switching a scheduler policy. |
| `struct qhw_sched_stats` | Scheduler-local accounting state exported through the statistics API. It gives callers visibility into submitted, started, completed, failed, and cancelled work through exported counters. |
| `struct qhw_sched_error` | Scheduler-local error buffer for one scheduler context. Public APIs return compact error codes, and callers can query this buffer when they need a human-readable reason. |
| `struct qhw_allocator` | Scheduler-local copy of the allocator selected at scheduler creation. Core code uses it for retained task records, output arrays, plugin registry storage, policy state requested through helper APIs, and other scheduler-owned memory. |
| `struct qhw_mutex` | Scheduler-local lock wrapper. It stores the concrete lock object used when the scheduler runs in thread-safe mode. |
| `struct qhw_lock_ops` | Scheduler-local lock dispatch table. It points to real lock functions in `QHW_SCHED_THREAD_SAFE` mode and no-op functions in `QHW_SCHED_THREAD_USER` mode, so public APIs can use one locking path. |

The core task registry and policy ordering structures are separate.
`qhw_task_table` uses `src/util/qhw_hash_table` so task lookup by
`task_id` remains fast as the number of submitted tasks grows. The active
policy plugin owns its ready queue and chooses the data structure that matches
its scheduling rule. FIFO can use an intrusive list or ring queue. Priority,
deadline, shortest-job-first, and longest-job-first can use a binary heap or
red-black tree. Round-robin can combine a hash table of owner queues with an
active-owner ring. Weaver-style policies can keep policy-specific state built
from the same utility structures.

The plugin registry can use a dynamic array. Policy modules are loaded during
setup or policy changes, and resolving a policy name is not part of the
per-task scheduling hot path.

The allocator hook gives the scheduler one allocation path. The default path
can wrap `malloc`, `realloc`, and `free`. An embedding runtime can replace that
path with an arena allocator, a tracked allocator, or a fault-injection
allocator used by tests. The scheduler should copy allocator callbacks during
`qhw_sched_create()` and use the private `struct qhw_allocator` internally.
Policy plugins should request memory through scheduler helper functions, so
plugin state and core state are allocated and released through the same
allocator.

`qhw_sched_create()` should choose the lock functions based on the configured
threading mode. In `QHW_SCHED_THREAD_SAFE`, `lock_ops` should call
`pthread_mutex_lock()`, `pthread_mutex_trylock()`, and
`pthread_mutex_unlock()`. In `QHW_SCHED_THREAD_USER`, `lock_ops` should point
to no-op functions. Public APIs should call `lock_ops.lock()` and
`lock_ops.unlock()` unconditionally. The configured threading mode determines
whether those calls execute real locks or no-op functions.

Internal helpers should assume the scheduler lock is already held unless their
name explicitly says otherwise. User callbacks and plugin callbacks that can
call back into the scheduler should run after the core lock is released.

### Source File Responsibilities

The implementation should split responsibilities across source files:

| File | Responsibility |
| --- | --- |
| `qhw_scheduler.c` | Context lifecycle, public API dispatch, policy selection, error handling. |
| `qhw_task.c` | Task record copy, lookup, update, state transitions, and free logic. |
| `qhw_qpu.c` | QPU object lifecycle, profile copy/update, runtime-state transitions, reference counting, and free logic. |
| `qhw_plugin.c` | Dynamic plugin loading, plugin registry management, and ABI validation. |
| `qhw_stats.c` | Stats update and stats export. |
| `qhw_error.c` | Last-error storage and formatting. |
| `qhw_allocator.c` | Default allocator and optional allocator hooks. |
| `qhw_thread.c` | Mutex wrapper and thread portability helpers. |
| `util/qhw_hash_table.c` | Scheduler-local hash table used by the core task registry and by plugins that need keyed lookup. |
| `util/qhw_heap.c` | Heap implementation for policies that need fast minimum or maximum selection by score. |
| `util/qhw_rb_tree.c` | Red-black tree implementation for policies that need ordered traversal, arbitrary deletion, or efficient priority updates. |
| `util/qhw_list.c` | Intrusive list helpers for FIFO queues and per-owner queues. |
| `util/qhw_ring.c` | Ring-buffer helpers for simple queues and round-robin owner rotation. |

The private header should be included only by `src/*.c` files. Policy plugins
should include the public plugin header.

## Memory And Ownership

The ABI needs strict ownership rules.

- Input descriptors are caller-owned.
- The scheduler copies metadata and descriptor fields needed after the call
  returns.
- Opaque payload pointers are copied by value. The scheduler performs shallow
  retention of payload references.
- Child tasks created from a parent task share the parent payload reference.
  Expanded tasks copy only task descriptors, metadata, and scheduler-owned
  accounting fields.
- Output arrays allocated by the scheduler must be freed by scheduler-provided
  free functions.
- Plugins should allocate policy state and scheduler-owned output through
  scheduler helper functions.

```c
void *qhw_sched_alloc(qhw_sched_t *sched, size_t size);
void *qhw_sched_realloc(qhw_sched_t *sched, void *ptr, size_t size);
void qhw_sched_free(qhw_sched_t *sched, void *ptr);
```

These helpers use the allocator stored in the scheduler context. They give
policy plugins a stable allocation path without exposing private allocator
state. Public APIs that return scheduler-owned arrays should still provide
type-specific free functions, such as `qhw_sched_free_policy_info_array()`,
because callers should not need to know which allocator the scheduler selected.

## Threading Model

The first implementation should support a configurable threading model. The
caller states how it will access scheduler objects, and the implementation
selects the required locking behavior.

Supported modes:

| Mode | Meaning |
| --- | --- |
| `QHW_SCHED_THREAD_SAFE` | Multiple threads may call public APIs on the same scheduler instance. The scheduler protects internal state. |
| `QHW_SCHED_THREAD_USER` | The user serializes access to one scheduler instance. The scheduler may use no-op locks. |

`QHW_SCHED_THREAD_SAFE` should be the default configured mode for the first
implementation when the caller passes `NULL` attributes. This keeps the default
safe. Performance-sensitive callers can explicitly request
`QHW_SCHED_THREAD_USER` when they can prove that all access to a scheduler
instance is serialized.

Required behavior:

- Public APIs call the configured lock and unlock functions before reading or
  mutating internal task, QPU, policy, plugin, stats, or error state.
- In `QHW_SCHED_THREAD_SAFE`, those functions use a real mutex.
- In `QHW_SCHED_THREAD_USER`, those functions are no-ops.
- Internal helper functions document whether the scheduler lock must already be
  held.
- The scheduler exposes snapshots or scheduler-owned output allocations.
- Output descriptors are snapshots or scheduler-owned allocations with explicit
  free functions.
- Public APIs may be called concurrently only when the configured mode is
  `QHW_SCHED_THREAD_SAFE`.

Callbacks need special handling. The scheduler should release the scheduler
mutex before invoking user callbacks. A callback may need to call back into the
scheduler, query external runtime state, or invoke Python. Releasing the core
lock before this call path avoids deadlock.

The implementation should use a two-phase pattern when a callback is needed:

1. Acquire the scheduler mutex and copy the task, QPU, and policy state
   needed for the callback.
2. Release the scheduler mutex.
3. Invoke the callback using the copied state.
4. Reacquire the scheduler mutex.
5. Validate that the task and QPU state are still compatible.
6. Apply the result or retry the policy decision if state changed.

Plugin calls are different from user callbacks. Loaded plugins declare whether
they are lock-safe. The first plugin ABI should assume `select_next()` and
lifecycle hooks are called with the scheduler lock held, while adapter callbacks
made from those plugins must follow the two-phase pattern above.

The first implementation can use `pthread_mutex_t` through `qhw_thread.c` for
the real-lock path and no-op functions for the domain path. Other platform
mutexes can be added later behind the same private wrapper while preserving the
public ABI.

## Python Binding Design

The Python package should expose a small object model over the C ABI.

```python
from qhw_scheduler import Scheduler, Task, QPU, Threading

qpu = QPU(qpu_id=1, metadata={"num_qubits": 20})
sched = Scheduler(policy="fifo", threading=Threading.SAFE, qpu=qpu)
sched.submit(Task(task_id=100, job_id=10, metadata={"shots": 1000}))
assignment = sched.next_task()
```

Expected Python classes:

- `Scheduler`: owns the C scheduler context.
- `Task`: value object that builds `qhw_sched_task_desc_t`.
- `QPU`: owns a `qhw_sched_qpu_t` handle created from `qhw_sched_qpu_profile_t`.
- `QPUProfile`: value object that builds `qhw_sched_qpu_profile_t`.
- `QPURuntime`: read-only view of `qhw_sched_qpu_runtime_t`.
- `Assignment`: Python view of `qhw_sched_assignment_t`.
- `SchedulerError`: raised when the C ABI returns an error code.

The Python extension should release the GIL around policy selection only if the
configured scheduler has no Python callbacks. If Python callbacks are installed,
the binding must reacquire the GIL before invoking them.

## QFw Adapter Plan

QFw should use the public scheduler API for all integration points. Plugin
internals remain behind the scheduler library boundary.

Initial QFw integration should live near the QRC dispatch layer:

1. QPM accepts a circuit through `sync_run()` or `async_run()`.
2. QRC wraps the QFw `Circuit` as a scheduler task.
3. QRC submits the logical task to `qhw-scheduler`.
4. When the local QPU queue has room, QRC calls `next_task()`.
5. QRC launches the returned scheduled task through the existing execution path.
6. Completion, failure, timeout, and cancellation events are sent back through
   scheduler lifecycle APIs.

The adapter should provide callbacks for quantum-aware decisions. For example,
`estimate_cost()` can use shots, circuit depth, qubit count, and QPU timing
metadata. `split_task()` can split high-shot work if a time-slicing policy is
enabled. In that case, QRC submits one logical task, the scheduler creates
scheduled child tasks during submission, and QRC receives those children from
`next_task()`. Parent completion is reported when every child reaches a terminal
state.

## Language-Neutral Interface Contract

The repository should publish a language-neutral contract in
`docs/interface.md` alongside the C ABI.

That contract should define:

- Task envelope fields and state transitions.
- QPU profile fields, QPU handle lifecycle, and runtime state transitions.
- Scheduler selection semantics.
- Lifecycle event ordering.
- Assignment shape.
- Error model.
- Required behavior for cancellation and failure.
- Plugin policy expectations.
- Metadata key conventions for quantum workloads.

This allows a Rust, Python, or C++ implementation to claim compatibility by
matching the same data contract and observable behavior.

## Future QPU Partitioning

The first implementation treats one scheduler instance as the scheduler for one
QPU execution target. Today that execution target is expected to be a full QPU.
Future hardware may expose partitions of a QPU. The plan should leave room for
that while keeping the first implementation focused on one execution target.

There are two likely operating models.

### Externally Visible Partitions

In this model, the hardware, provider, or site resource manager exposes
partitions as visible execution targets. A partition may have its own endpoint,
queue, calibration view, allowed qubit set, or allocation identity. Jobs target
a specific partition after admission or reservation.

This model keeps `qhw-scheduler` simple. Each visible partition gets its own
scheduler instance:

```text
physical QPU
  partition 0 -> qhw-scheduler instance 0
  partition 1 -> qhw-scheduler instance 1
  partition 2 -> qhw-scheduler instance 2
```

The scheduler remains QPU-local. It schedules tasks that have already been
assigned to that partition. The resource manager, admission layer, or a higher
runtime layer decides which partition receives a job.

Scheduler responsibilities in this model:

- Manage the local queue for one partition.
- Track the partition profile and scheduler-owned runtime state separately.
- Use partition-specific metadata such as qubit count, allowed qubits, timing,
  calibration ID, provider queue capacity, and scheduler-observed queue depth.
- Select the next accepted task for that partition.
- Report lifecycle and policy statistics for that partition.

Responsibilities outside this scheduler:

- Decide which job receives which partition.
- Enforce isolation between partitions.
- Publish partition topology and calibration data.
- Coordinate policy across multiple partitions.

This is the preferred model if the site wants strong multi-tenancy. A job
receives a concrete partition, and local scheduling happens within that
partition. Partition assignment stays with the resource-management layer.

### Internal Dynamic Partitioning

In this model, the QPU is exposed as one execution target, but the scheduler or
runtime is expected to decide whether multiple quantum tasks can share the QPU
at the same time. This is closer to a CPU scheduler managing cores. It is also
much harder for QPUs.

The scheduler would need to understand whether two tasks are compatible on the
same physical chip. That decision can depend on:

- Disjoint qubit sets.
- Coupling graph regions.
- Control electronics and readout sharing.
- Crosstalk between active regions.
- Calibration validity for the selected regions.
- Measurement and reset constraints.
- Provider support for concurrent execution.
- Timing alignment between tasks.
- Fairness and accounting across tenants.

Scheduler responsibilities in this model would expand:

- Maintain a view of sub-QPU resources.
- Place each task onto a compatible qubit region.
- Track concurrent occupancy.
- Prevent conflicting tasks from running together.
- Account for partition-level noise and crosstalk.
- Recalculate placement when calibration or availability changes.

Defer this model past the first implementation. It changes the scheduler from a
local task ordering engine into a resource placement engine. That requires a
richer QPU model and more hardware-specific policy.

### Coexistence

Both models can coexist. A site may expose some partitions as allocatable
targets while also allowing a scheduler inside one partition to make smaller
placement decisions. The design uses explicit layering:

```text
resource manager or mesh layer
  -> chooses full QPU or visible partition
  -> creates or selects one qhw-scheduler instance
  -> qhw-scheduler orders tasks for that execution target
```

If dynamic sub-partitioning is needed later, it should be introduced as a new
policy family or a new scheduler execution-target profile. The base ABI can
still be useful if the QPU profile gains partition metadata and callbacks gain
placement support. The first implementation should stay focused on one
execution target.

The wording in this plan should therefore use "QPU execution target" rather
than "physical QPU" when describing the scheduler scope. An execution target can
be a full QPU today and a visible QPU partition later.

## Testing Strategy

The C tests should validate core behavior at the C layer.

Required C tests:

- Create and destroy scheduler contexts.
- Submit, update, cancel, and query tasks.
- Create, refresh, and query the local QPU object and profile.
- Query scheduler-owned QPU runtime state.
- FIFO ordering.
- Lifecycle transitions.
- Submission-time task expansion into child tasks.
- Parent aggregate completion after all child tasks reach terminal state.
- Thread-safe mode with concurrent task submission, selection, and lifecycle
  updates.
- User-managed mode with no-op locks and caller-serialized access.
- Plugin load and ABI rejection.
- Error handling and invalid arguments.
- Metadata copy and free behavior.

Python tests should validate binding behavior:

- Python objects map to C descriptors.
- Errors become Python exceptions.
- Python callbacks work.
- GIL handling remains deadlock-free.
- A QFw-shaped task payload passes through as an opaque scheduler payload.

Compatibility tests should compare selected policy behavior against existing
qSchedSim results where possible. The goal is to preserve policy decisions for
equivalent task streams.

## Development Phases

### Phase 0: Interface Specification

- Create the repository skeleton.
- Write `qhw_scheduler.h`, `qhw_scheduler_types.h`, and
  `qhw_scheduler_plugin.h`.
- Document task, QPU object lifecycle, QPU profile, QPU runtime, assignment,
  and lifecycle semantics.
- Define metadata conventions for quantum task size while keeping the core
  scheduler based on the common task envelope.

### Phase 1: Core

Create the C core, public headers, private headers, `src` implementation, and
`src/util` data structures. This phase should provide scheduler lifecycle,
QPU object lifecycle, task registry support, metadata copy/free helpers,
threading attributes, allocator handling, error handling, and plugin-loading
infrastructure.

The core phase should include tests for lifecycle, task registry behavior,
QPU lifecycle, metadata ownership, allocator behavior, error handling, and
plugin loading failure paths. It should not implement non-FIFO scheduling
policies.

Commit after this phase with the core implementation and tests.

### Phase 2: FIFO Scheduler

Implement `src/plugins/fifo.c` as the first policy plugin. The FIFO plugin
should use the utility list or ring helpers and should be loaded through the
same dynamic plugin path used by future policies.

This phase should include tests for plugin loading, FIFO policy selection,
FIFO insertion order, lifecycle callbacks, cancellation, and invalid plugin
handling. It should not add priority, round-robin, SJF, LJF, Weaver, or
time-slicing plugins.

Commit after this phase with the FIFO plugin and tests.

### Phase 3: Python Bindings

Add Python bindings only for the ABI implemented in phases 1 and 2. The first
binding should expose QPU creation, scheduler creation, FIFO plugin loading,
task submission, next-task selection, lifecycle updates, and error handling.

This phase should include Python tests and a small Python FIFO example.

Commit after this phase with the Python binding and tests.

### Phase 4: QFw QRC Integration

- Add a QFw adapter that wraps `Circuit` objects.
- Configure the standard FIFO plugin as the default policy.
- Add service configuration for scheduler policy selection.
- Add tests that verify FIFO compatibility and priority behavior.

### Phase 5: qSchedSim Adapter

- Add an adapter that feeds qSchedSim task events into the C scheduler.
- Compare FIFO, priority, round-robin, and weaver-like policy behavior.
- Keep SimPy outside the scheduler core.

### Phase 6: Additional Policy Plugins

- Add shortest-job-first and longest-job-first.
- Add weaver-style hybrid ordering.
- Add a time-slicing policy that uses the submission-time child task model.
- Add fairness and deadline policies if needed.

## Open Questions

- Should the first implementation target C11 only, or allow compiler-specific
  atomics and visibility attributes where available?
- Should the language-neutral specification define a central numeric key
  registry, or should sites allocate keys from `QHW_SCHED_KEY_USER_BASE`?
- Should `estimated_runtime_ns` remain a common task field, or should it move
  into metadata like all size information?
- Should the Python package expose plugin authoring in Python, or should Python
  plugins be treated as callbacks only?

## Initial Recommendation

Start with a C library, a stable C ABI, and dynamic policy plugins. Support both
thread-safe and user-managed modes from the first implementation. Make
thread-safe mode the default. Deliver FIFO as the first standard plugin, then
add priority and round-robin through the same plugin interface.

Build Python bindings after the C lifecycle, task registry, QPU object
lifecycle, QPU runtime state, and FIFO tests are stable. Integrate QFw at QRC
only after the binding and C API can preserve current FIFO behavior.

Defer the time-slicing policy until the task envelope, metadata model,
parent-child task registry, and lifecycle callbacks are proven. The parent-child
infrastructure should exist earlier so the policy can use a stable split
contract.
