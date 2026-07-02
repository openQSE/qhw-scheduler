# qhw-scheduler

`qhw-scheduler` is a QPU-local scheduler library. The C library owns task
tracking and scheduler policy loading. Scheduler policies are built as dynamic
plugins. The current implementation includes FIFO, priority, ordered, and
round-robin policy plugins and a thin Python wrapper over the C ABI.

## Prerequisites

- CMake 3.20 or newer
- A C11 compiler
- Python 3.9 or newer for Python tests and bindings
- SWIG and Python development headers for the generated SWIG binding

## Build

```bash
git clone https://github.com/openQSE/qhw-scheduler.git
cd qhw-scheduler
cmake -S . -B build
cmake --build build
```

Build with an explicit release configuration:

```bash
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
```

Build without Python tests:

```bash
cmake -S . -B build -D QHW_SCHED_BUILD_PYTHON=OFF
cmake --build build
```

## Build The SWIG Binding

The SWIG binding is required when Python bindings are enabled. C-only builds can
disable Python with `QHW_SCHED_BUILD_PYTHON=OFF`.

Generate and build the SWIG extension with the default Python:

```bash
cmake -S . -B build -D QHW_SCHED_BUILD_SWIG=ON
cmake --build build
```

Use an explicit Python interpreter when the active environment does not provide
development headers:

```bash
cmake -S . -B build-swig \
  -D QHW_SCHED_BUILD_SWIG=ON \
  -D Python3_EXECUTABLE=/usr/bin/python3.10
cmake --build build-swig
```

The generated files are placed in the build tree:

```text
build-swig/swig/qhw_scheduler_wrap.c
build-swig/python/qhw_scheduler/_qhw_scheduler.py
build-swig/python/qhw_scheduler/_qhw_scheduler_c.so
```

Validate the generated SWIG module:

```bash
ctest --test-dir build-swig --output-on-failure
PYTHONPATH=build-swig/python \
  /usr/bin/python3.10 -S tests/python/test_private_import.py
```

The generated files are private implementation details. User code should import
the public package API instead:

```python
from qhw_scheduler import QPU, Scheduler
```

## Install

Install into a local prefix:

```bash
cmake --install build --prefix "$PWD/install"
```

Install into a system prefix:

```bash
cmake --install build --prefix /usr/local
```

The local install layout includes:

```text
install/include/qhw_scheduler/
install/lib/libqhw_scheduler.so
install/lib/qhw_scheduler/plugins/qhw_sched_fifo.so
install/lib/qhw_scheduler/plugins/qhw_sched_ordered.so
install/lib/qhw_scheduler/plugins/qhw_sched_priority.so
install/lib/qhw_scheduler/plugins/qhw_sched_round_robin.so
```

Install the Python package in editable mode:

```bash
python3 -m pip install -e .
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Run the workload tests only:

```bash
ctest --test-dir build -R workload --output-on-failure
```

Run the workload test directly with a larger task set:

```bash
./build/test_workload --mode comprehensive --tasks 65536
```

Run the Python binding test directly against the build tree:

```bash
PYTHONPATH=build/python python3 tests/python/test_fifo.py
```

Run a simple FIFO smoke test against the installed Python package:

```bash
python3 - <<'PY'
from qhw_scheduler import QPU, Scheduler

qpu = QPU(qpu_id=1, num_qubits=20)
sched = Scheduler(qpu)
sched.load_standard_plugin("fifo")
sched.set_policy("fifo")
sched.submit_task(1)
sched.submit_task(2)
print(sched.select_next())
print(sched.select_next())
sched.close()
qpu.close()
PY
```

The expected output is:

```text
1
2
```

## Usage

<details>
<summary>C Usage</summary>

Build and install the library first:

```bash
cmake -S . -B build -D CMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$PWD/install"
```

Example application:

```c
#include <qhw_scheduler/qhw_scheduler.h>

#include <stdio.h>

#define CHECK(rc, what) do { \
	if ((rc) != QHW_SCHED_OK) { \
		fprintf(stderr, "%s failed: %d\n", (what), (rc)); \
		return 1; \
	} \
} while (0)

int main(void)
{
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 1,
		.num_qubits = 20
	};
	qhw_sched_kv_t order[] = {
		{
			.key = QHW_SCHED_OPT_ORDER_KEY,
			.type = QHW_SCHED_VALUE_U64,
			.value.u64 = QHW_SCHED_ORDER_PRIORITY
		},
		{
			.key = QHW_SCHED_OPT_ORDER_KEY,
			.type = QHW_SCHED_VALUE_U64,
			.value.u64 = QHW_SCHED_ORDER_SJF
		},
		{
			.key = QHW_SCHED_OPT_ORDER_KEY,
			.type = QHW_SCHED_VALUE_U64,
			.value.u64 = QHW_SCHED_ORDER_FIFO
		}
	};
	qhw_sched_task_desc_t first = {
		.struct_size = sizeof(first),
		.task_id = 1,
		.job_id = 100,
		.priority = 10,
		.estimated_runtime_ns = 5000
	};
	qhw_sched_task_desc_t second = {
		.struct_size = sizeof(second),
		.task_id = 2,
		.job_id = 100,
		.priority = 10,
		.estimated_runtime_ns = 1000
	};
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_rc_t rc;

	CHECK(qhw_sched_qpu_create(&profile, &qpu), "qpu create");
	CHECK(qhw_sched_create(NULL, NULL, qpu, NULL, 0, &sched),
		"scheduler create");
	CHECK(qhw_sched_load_plugin(sched,
		"./install/lib/qhw_scheduler/plugins/qhw_sched_ordered.so"),
		"load ordered plugin");
	CHECK(qhw_sched_set_policy(sched, "ordered", order,
		sizeof(order) / sizeof(order[0])), "set ordered policy");

	CHECK(qhw_sched_submit_task(sched, &first), "submit first");
	CHECK(qhw_sched_submit_task(sched, &second), "submit second");
	CHECK(qhw_sched_select_next(sched, &assignment), "select next");

	printf("selected task: %llu\n",
		(unsigned long long)assignment.task_id);
	CHECK(qhw_sched_task_started(sched, assignment.task_id), "started");
	CHECK(qhw_sched_task_completed(sched, assignment.task_id),
		"completed");

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}
```

Compile the example against a local install:

```bash
cc -std=c11 -I"$PWD/install/include" app.c \
  -L"$PWD/install/lib" -lqhw_scheduler \
  -Wl,-rpath,"$PWD/install/lib" \
  -o app
./app
```

The ordered policy above uses `priority,sjf,fifo`. Equal-priority tasks are
ordered by shortest estimated runtime, then FIFO insertion order.

</details>

<details>
<summary>Python Usage</summary>

Install the Python package in editable mode:

```bash
python3 -m pip install -e .
```

Example application:

```python
from qhw_scheduler import (
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_OPT_ORDER_KEY,
    QHW_SCHED_ORDER_FIFO,
    QHW_SCHED_ORDER_PRIORITY,
    QHW_SCHED_ORDER_SJF,
    QPU,
    Scheduler,
    kv_u64,
)

with QPU(qpu_id=1, num_qubits=20) as qpu:
    with Scheduler(qpu) as sched:
        sched.load_standard_plugin("ordered")
        sched.set_policy("ordered", options=[
            kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_PRIORITY),
            kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
            kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO),
        ])

        sched.submit_task(
            task_id=1,
            owner_id=1000,
            job_id=10,
            priority=5,
            estimated_runtime_ns=5000,
            metadata=[kv_u64(QHW_SCHED_META_SHOTS, 1024)],
        )
        sched.submit_task(
            task_id=2,
            owner_id=1000,
            job_id=10,
            priority=5,
            estimated_runtime_ns=1000,
            metadata=[kv_u64(QHW_SCHED_META_SHOTS, 1024)],
        )

        assignment = sched.select_next_assignment()
        print(f"selected task: {assignment.task_id}")
        sched.task_started(assignment.task_id)
        sched.task_completed(assignment.task_id)

        runtime = qpu.runtime()
        print(f"completed tasks: {runtime.completed_count}")
```

The Python wrapper uses the same scheduler policies and metadata keys as the C
API. `load_standard_plugin()` loads the installed policy plugin from the Python
package layout.

</details>
