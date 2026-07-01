# qhw-scheduler

`qhw-scheduler` is a QPU-local scheduler library. The C library owns task
tracking and scheduler policy loading. Scheduler policies are built as dynamic
plugins. The current implementation includes the FIFO policy plugin and a thin
Python wrapper over the C ABI.

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

The SWIG binding is optional. It is enabled by default when Python bindings are
enabled, but CMake only builds it when both SWIG and matching Python
development headers are available. If either dependency is missing, the normal
Python wrapper still builds and CMake prints a warning.

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
```

Install the Python package in editable mode:

```bash
python3 -m pip install -e .
```

## Test

```bash
ctest --test-dir build --output-on-failure
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
