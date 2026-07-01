# QFw Scheduling And Crediting Plan

## Purpose

This document is an initial development plan for moving reusable scheduling
and crediting logic from `qSchedSim` into standalone packages that QFw can
consume. It is intentionally a plan only. It does not propose moving code into
QFw directly.

The split should preserve two separate concepts:

- Scheduling decides the order in which accepted quantum tasks are dispatched.
- Crediting decides whether a hybrid job should be admitted before it starts
  submitting quantum work.

## Current qSchedSim Structure

`qSchedSim/src/qschedsim/algorithms` contains task scheduling policies. The
current implementations include FIFO, priority, round-robin, weaver,
time-sliced weaver, and related ordering helpers. These algorithms are useful,
but they are currently coupled to the simulator runtime:

- They assume a SimPy environment.
- They operate on qSchedSim hybrid-job dictionaries and SimPy queues.
- They emit simulator events through `SharedData`.
- They enqueue selected tasks into `SharedData.exec_queue`.

`qSchedSim/src/qschedsim/credits` contains credit/admission policies. The
current policies include rate-based, time-based, and unlimited credit systems.
These are also simulator-bound today:

- They consume qSchedSim hybrid-job dictionaries.
- They call back into `QScheduler.add_hybrid_job`.
- They use simulator device objects to calculate baseline credits and rates.
- They use SimPy coroutine control flow.

The reusable ideas are sound, but the code should not be imported into QFw in
its current form. The first step should be extracting runtime-neutral
interfaces, then adapting both qSchedSim and QFw to those interfaces.

## Proposed Repository Split

### `qhw-scheduler`

This repository would contain reusable quantum-task scheduling policies.

Recommended form:

- An installable Python package.
- No QFw, DEFw, or SimPy dependency in the core package.
- A small public API with task records, scheduler state, and scheduler classes.
- Optional adapters for qSchedSim and QFw.

Candidate import package name:

- `qhw_scheduler`

Core responsibilities:

- Accept task records.
- Track runnable tasks.
- Track the devices whose queues or assignments it owns.
- Select the next task, or next task/device assignment, according to the
  configured policy.
- Accept lifecycle hooks when a task starts, completes, fails, times out, or is
  cancelled.
- Accept device-state hooks when a device becomes busy, idle, unavailable, or
  available again.
- Export queue and policy statistics.

The scheduler should expose two API surfaces.

Task-facing APIs:

- `submit_task(task)`: add a quantum task to the scheduler.
- `submit_tasks(tasks)`: add a batch of quantum tasks.
- `cancel_task(task_id, reason=None)`: remove a pending task or mark a running
  task for cancellation if the backend can support that later.
- `update_task(task_id, **updates)`: update task metadata such as priority,
  deadline, estimated runtime, or reservation ID.
- `get_task(task_id)`: return the scheduler's view of a task.
- `pending_tasks()`: return pending tasks for diagnostics.
- `next_task(device_id=None)`: select the next runnable task for a single
  device or device-local queue.
- `next_assignment()`: select a task and target device for schedulers that own
  multiple devices.

Device-facing APIs:

- `register_device(device_profile)`: add a device that this scheduler may use.
- `unregister_device(device_id)`: remove a device from the scheduler's view.
- `update_device(device_id, **updates)`: update static or dynamic device
  metadata used by policy decisions.
- `device_available(device_id)`: mark a device as available for new work.
- `device_unavailable(device_id, reason=None)`: mark a device as unavailable.
- `task_started(task_id, device_id, metadata=None)`: notify the scheduler that a
  selected task began execution.
- `task_completed(task_id, device_id, result_summary=None)`: notify successful
  completion and allow the scheduler to update accounting.
- `task_failed(task_id, device_id, error_summary=None)`: notify failure and
  allow retry, penalty, or failure accounting policies.
- `task_timed_out(task_id, device_id, error_summary=None)`: notify timeout as a
  distinct failure mode.
- `task_cancelled(task_id, device_id=None, reason=None)`: notify cancellation.

Design decision:

The scheduler must not couple directly to QFw, DEFw, SimPy, or hardware
services. It should provide hooks that adapters call when events occur. In QFw,
the QPM/QRC layer observes service and task lifecycle events and forwards them
to `qhw_scheduler`. In qSchedSim, a SimPy adapter forwards simulator events to
the same APIs. This keeps the scheduler reusable for a single QPU, a set of
per-device queues, or a higher-level multi-QPU runtime layer.

Initial algorithms to port:

- FIFO.
- Priority.
- Round-robin.
- Ordered shortest/priority/hybrid policy from weaver.
- Time-slice behavior as a later step, because it changes task shape by
  splitting one logical task into parts.

### `qhw-credits`

This repository would contain reusable admission-control and credit policies.

Recommended form:

- An installable Python package.
- No QFw, DEFw, or SimPy dependency in the core package.
- Explicit request, device, decision, and reservation data models.
- Credit policies that calculate admission decisions without mutating QFw
  service state directly.

Candidate import package name:

- `qhw_credits`

Core responsibilities:

- Accept hybrid workload descriptions.
- Compare requested work against device capability and current allocation.
- Return an admission decision.
- Create and track reservations when a job is accepted.
- Release or expire reservations.
- Provide accounting hooks for task completion and failure.

Initial policies to port:

- Unlimited admission, for baseline behavior and tests.
- Time-based credit accounting.
- Rate-based credit accounting.

## Why These Should Be Installable Packages

Both pieces should be installable Python libraries, not copied utility modules.
That keeps the ownership and API boundary clean:

- QFw can depend on stable package interfaces.
- qSchedSim can be updated to use the same package interfaces.
- Unit tests can exercise scheduling and crediting without starting DEFw or QFw.
- Future mesh/runtime layers can reuse the same policies.
- Sites can provide their own policies through entry points or import paths.

The packages can still be lightweight. They should expose plain Python classes
and functions, but they should be versioned and installed like `qhw-data` and
`qhw-iqm`.

## QFw Integration Points

### Scheduling In QFw

The best first QFw integration point is `QFw/services/util/qpm/util_qrc.py`.

That layer owns execution dispatch after a circuit has already been accepted by
the QPM service. Today `UTIL_QRC.async_run()` round-robins over worker queues
and inserts the circuit into the first worker that has capacity. This is the
closest QFw equivalent of a device-local quantum task scheduler.

The user intuition is mostly correct: QRC is where a richer scheduling
algorithm should first replace the current FIFO/round-robin behavior. The
important nuance is that QFw has two queues today:

- `UTIL_QPM.oor_queue` is a resource-wait queue for circuits that could not
  consume host slots yet.
- `UTIL_QRC` worker queues are execution queues for circuits that have already
  consumed QPM resources.

Phase one should update only QRC dispatch. That keeps the initial change
contained and preserves current QPM resource accounting.

The initial QFw scheduler flow should look like this:

1. `UTIL_QPM.async_run(info)` creates a `Circuit`.
2. `UTIL_QPM.common_run(cid)` consumes resources as it does today.
3. `UTIL_QPM` passes the `Circuit` to `UTIL_QRC.async_run(circuit)`.
4. `UTIL_QRC` wraps the `Circuit` in a scheduler task record.
5. The configured scheduler chooses which task a worker should run next.
6. QRC launches the selected circuit through the existing `run_circuit_async`.

The default scheduler should preserve current behavior as closely as possible.
FIFO or round-robin can be the default policy.

### Resource-Aware Scheduling In QFw

Resource-aware scheduling should not be introduced in QRC first. QRC only sees
circuits after QPM has consumed resources. If a policy needs to choose the next
task based on host slots, MPI ranks, device occupancy, or service capacity,
that decision belongs at the QPM resource queue.

That can be a second integration phase:

- Replace `UTIL_QPM.oor_queue` with a scheduler-backed pending queue.
- When resources are freed, ask the scheduler for the next task whose resource
  request can fit.
- Keep `consume_resources()` as the authority that validates the selected task.

This avoids mixing execution-worker scheduling with resource admission.

### Crediting In QFw

Crediting should not live in QRC. It is admission control for hybrid jobs, so it
must run before quantum tasks are submitted to a QPM service.

The cleanest QFw location is one layer above the existing circuit submission
path:

- Add QPM service APIs for hybrid-job admission and release.
- Keep `sync_run()` and `async_run()` as quantum task submission APIs.
- Require accepted hybrid jobs to include a reservation or job token when they
  submit quantum work.

Candidate QPM APIs:

```python
def submit_hybrid_job(self, workload_spec):
    pass

def release_hybrid_job(self, reservation_id):
    pass

def get_hybrid_job_admission(self, reservation_id):
    pass
```

The `workload_spec` is the QFw-facing equivalent of the data qSchedSim puts in
`hjob['data']`. It should include enough information for a credit system to
decide whether the job can be admitted.

Candidate workload fields:

- `job_id`
- `user`
- `priority`
- `walltime_seconds`
- `classical_runtime_seconds`
- `quantum_task_count`
- `max_qubits`
- `max_depth`
- `max_shots`
- `expected_quantum_runtime_seconds`, optional
- `device_requirements`, optional
- `metadata`, optional

Candidate admission result fields:

- `accepted`
- `reason`
- `reservation_id`
- `device_id`
- `allocated_credits`
- `allocated_rate`
- `estimated_quantum_time_seconds`
- `expires_at`
- `metadata`

For the first QFw prototype, existing services can implement this as unlimited
admission. That proves the API without changing current behavior. Hardware
services can then opt into rate-based or time-based admission.

## Relationship To SLURM And The Runtime Layer

The QFw credit system should not try to simulate SLURM. SLURM has already made
the allocation decision by the time the QFw services are running.

The QFw crediting layer should answer a narrower question:

Can this QPM service accept this hybrid job's expected quantum workload under
the service's configured admission policy?

For a future multi-QPU runtime layer, the flow should be:

1. SLURM grants access to one or more nodes/devices.
2. QFw starts one or more QPM services.
3. A QFw runtime/device-selection layer queries candidate services.
4. Candidate services run credit/admission checks.
5. The runtime chooses devices that accepted the job.
6. Quantum tasks are submitted to those accepted devices.

This makes the credit package useful both inside a single QPM service and
inside a future multi-device selection layer.

## Proposed Data Model

### Scheduler Task

The scheduler package should use a small task model that can wrap QFw circuits
without depending on QFw classes.

Candidate fields:

- `task_id`
- `job_id`
- `reservation_id`
- `created_at`
- `priority`
- `shots`
- `depth`
- `num_qubits`
- `estimated_runtime_seconds`
- `resource_request`
- `payload`
- `metadata`

For QFw, `payload` can be the existing `Circuit` object. For qSchedSim, it can
be a simulator `QuantumTask`.

### Device Profile

Both scheduling and crediting need a normalized view of device capacity.

Candidate fields:

- `device_id`
- `provider`
- `num_qubits`
- `max_shots`
- `baseline_qubits`
- `baseline_depth`
- `baseline_shots`
- `time_span_seconds`
- `rate`
- `queue_capacity`
- `metadata`

This can later be mapped from `qhw-data` device/coupling/calibration records,
but the scheduler and credit packages should not require the full qhw schema.

### Credit Request

Candidate fields:

- `job_id`
- `user`
- `priority`
- `walltime_seconds`
- `classical_runtime_seconds`
- `quantum_task_count`
- `max_qubits`
- `max_depth`
- `max_shots`
- `user_quantum_task_offset`
- `metadata`

This is the runtime-neutral equivalent of qSchedSim's hybrid job admission data.

## Development Phases

### Phase 1: Define Contracts

- Create `qhw-scheduler` with task, device profile, scheduler interface, and
  FIFO implementation.
- Create `qhw-credits` with workload request, admission decision, reservation,
  and unlimited credit implementation.
- Add unit tests independent of QFw and qSchedSim.

### Phase 2: Port qSchedSim Policies

- Port FIFO, priority, round-robin, and weaver ordering into `qhw-scheduler`.
- Port unlimited, time-based, and rate-based credit systems into `qhw-credits`.
- Keep SimPy out of the core packages.
- Add qSchedSim adapters so the simulator can run against the new packages.

### Phase 3: Integrate Scheduling Into QFw QRC

- Add a QFw scheduler adapter that wraps a `Circuit` as a scheduler task.
- Add scheduler selection to QFw service configuration.
- Replace direct worker-queue insertion in `UTIL_QRC.async_run()` with the
  selected scheduler.
- Preserve current behavior with the default scheduler.
- Add tests for FIFO compatibility and priority ordering.

### Phase 4: Integrate Resource-Aware Pending Queue

- Replace or wrap `UTIL_QPM.oor_queue` with scheduler-backed pending tasks.
- Keep `consume_resources()` as the final resource authority.
- Add policies that can choose the next runnable task based on current host
  slots and requested `np`.

This phase should be separate from QRC scheduling because it changes resource
wait behavior.

### Phase 5: Add QFw Crediting APIs

- Extend `service-apis/api_qpm/api_qpm.py` with hybrid-job admission APIs.
- Add no-op/unlimited implementations for TNQVM, NWQ-Sim, and IQM services.
- Add reservation IDs to circuit submission metadata.
- Add optional enforcement that quantum tasks must carry a valid reservation.

### Phase 6: Enable Real Credit Policies

- Add QPM service configuration for credit policy selection.
- Map QFw device metadata into `qhw-credits` device profiles.
- Enable rate-based and time-based policies per service.
- Add admission rejection paths with clear error messages.
- Add accounting for job completion, failure, and reservation expiry.

### Phase 7: Multi-Device Runtime Layer

- Add a runtime layer above QPM services that can query multiple devices.
- Use QPM admission decisions as part of device selection.
- Keep SLURM simulation out of scope. SLURM allocation remains an external
  decision.

## Open Design Questions

- Repository names: `qhw-scheduler` and `qhw-credits` are generic, but
  `qfw-scheduler` and `qfw-credits` make the immediate integration target more
  obvious. The generic names are preferable if qSchedSim and other tools will
  consume them directly.
- Should a hybrid job reservation be mandatory for all `async_run()` calls, or
  optional unless a service enables admission control?
- Should the QFw runtime layer perform admission before choosing a device, or
  should it query all candidate devices and choose among accepted reservations?
- Should time-slicing split tasks in the scheduler package, or should QFw split
  circuits before scheduling?
- How should reservations be cleaned up if a client dies without releasing the
  hybrid job?

## Initial Recommendation

Start with two installable packages:

- `qhw-scheduler` for execution-order policy.
- `qhw-credits` for hybrid-job admission policy.

Integrate scheduling into QFw first at `UTIL_QRC`, because it is the smallest
safe change and directly replaces the current worker dispatch policy. Keep the
default behavior compatible with today.

Integrate crediting second at the QPM API boundary, not in QRC. The crediting
system should admit or reject a hybrid job before its quantum tasks are
submitted. The first implementation should be unlimited admission, followed by
rate-based and time-based policies once the reservation API is stable.
