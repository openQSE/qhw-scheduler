import os
import sys
from pathlib import Path

from . import _qhw_scheduler as _swig


_SWIG_CONSTANTS = tuple(
    name for name in dir(_swig) if name.startswith("QHW_SCHED_")
)
for _name in _SWIG_CONSTANTS:
    globals()[_name] = getattr(_swig, _name)

KV = _swig.qhw_sched_kv_t
KVValue = _swig.qhw_sched_kv_value
QPUProfile = _swig.qhw_sched_qpu_profile_t
QPURuntime = _swig.qhw_sched_qpu_runtime_t
SchedulerAttr = _swig.qhw_sched_attr_t
TaskDesc = _swig.qhw_sched_task_desc_t

__all__ = [
    *_SWIG_CONSTANTS,
    "Assignment",
    "KV",
    "KVValue",
    "QPU",
    "QPUProfile",
    "QPURuntime",
    "Scheduler",
    "SchedulerAttr",
    "SchedulerError",
    "TaskDesc",
    "kv_f64",
    "kv_i64",
    "kv_ptr",
    "kv_u64",
]


class SchedulerError(RuntimeError):
    def __init__(self, message, rc=None):
        self.rc = rc
        super().__init__(message)


def _check(rc, message):
    if rc != QHW_SCHED_OK:
        raise SchedulerError(f"{message}: rc={rc}", rc=rc)


def _plugin_library_name(name):
    stems = {
        "fifo": "qhw_sched_fifo",
        "priority": "qhw_sched_priority",
    }
    stem = stems.get(name)
    if stem is None:
        raise SchedulerError(f"unknown standard scheduler plugin: {name}")
    if sys.platform == "darwin":
        return f"{stem}.dylib"
    if os.name == "nt":
        return f"{stem}.dll"
    return f"{stem}.so"


def _package_dirs():
    package_dir = Path(__file__).resolve().parent
    yield package_dir

    source_build_dir = package_dir.parent.parent / "build" / "python"
    yield source_build_dir / "qhw_scheduler"


def _package_file(*parts):
    for package_dir in _package_dirs():
        path = package_dir.joinpath(*parts)
        if path.is_file():
            return path
    return None


def kv_u64(key, value, flags=0):
    kv = KV()
    kv.key = key
    kv.type = QHW_SCHED_VALUE_U64
    kv.flags = flags
    kv.value.u64 = value
    return kv


def kv_i64(key, value, flags=0):
    kv = KV()
    kv.key = key
    kv.type = QHW_SCHED_VALUE_I64
    kv.flags = flags
    kv.value.i64 = value
    return kv


def kv_f64(key, value, flags=0):
    kv = KV()
    kv.key = key
    kv.type = QHW_SCHED_VALUE_F64
    kv.flags = flags
    kv.value.f64 = value
    return kv


def kv_ptr(key, value, flags=0):
    kv = KV()
    kv.key = key
    kv.type = QHW_SCHED_VALUE_PTR
    kv.flags = flags
    kv.value.ptr = value
    return kv


class _MetadataArray:
    def __init__(self, metadata):
        self.ptr = None
        self.count = 0
        if not metadata:
            return

        self.count = len(metadata)
        self.ptr = _swig.qhw_sched_kv_array_create(self.count)
        if self.ptr is None:
            raise SchedulerError("failed to allocate metadata array")

        try:
            for index, item in enumerate(metadata):
                rc = _swig.qhw_sched_kv_array_set(
                    self.ptr,
                    self.count,
                    index,
                    item,
                )
                _check(rc, "failed to set metadata item")
        except Exception:
            self.close()
            raise

    def close(self):
        if self.ptr is not None:
            _swig.qhw_sched_kv_array_destroy(self.ptr)
            self.ptr = None
            self.count = 0

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _tb):
        self.close()


class Assignment:
    def __init__(self, raw):
        self._raw = raw
        self.task_id = raw.task_id
        self.parent_task_id = raw.parent_task_id
        self.slice_index = raw.slice_index
        self.slice_count = raw.slice_count
        self.payload = raw.payload
        self.payload_size = raw.payload_size
        self.estimated_runtime_ns = raw.estimated_runtime_ns
        self.payload_bytes = _swig.qhw_sched_payload_to_bytes(
            raw.payload,
            raw.payload_size,
        )


class QPU:
    def __init__(self, qpu_id, num_qubits, flags=0, metadata=None):
        profile = QPUProfile()
        profile.qpu_id = qpu_id
        profile.num_qubits = num_qubits
        profile.flags = flags
        with _MetadataArray(metadata) as metadata_array:
            profile.metadata = metadata_array.ptr
            profile.metadata_count = metadata_array.count
            rc, handle = _swig.qhw_sched_qpu_create(profile)
        _check(rc, "failed to create QPU")
        self._handle = handle

    @property
    def handle(self):
        return self._handle

    def close(self):
        if self._handle is not None:
            _swig.qhw_sched_qpu_destroy(self._handle)
            self._handle = None

    def profile(self):
        rc, profile = _swig.qhw_sched_qpu_get_profile(self._handle)
        _check(rc, "failed to get QPU profile")
        return profile

    def runtime(self):
        rc, runtime = _swig.qhw_sched_qpu_get_runtime(self._handle)
        _check(rc, "failed to get QPU runtime")
        return runtime

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _tb):
        self.close()

    def __del__(self):
        self.close()


class Scheduler:
    def __init__(
        self,
        qpu,
        threading=QHW_SCHED_THREAD_SAFE,
        flags=0,
        options=None,
        policy_name=None,
    ):
        attr = SchedulerAttr()
        attr.threading = threading
        attr.flags = flags
        with _MetadataArray(options) as option_array:
            rc, handle = _swig.qhw_sched_create(
                policy_name,
                attr,
                qpu.handle,
                option_array.ptr,
                option_array.count,
            )
        _check(rc, "failed to create scheduler")
        self._handle = handle
        self._payloads = {}

    @property
    def handle(self):
        return self._handle

    def load_plugin(self, path):
        rc = _swig.qhw_sched_load_plugin(self._handle, os.fspath(path))
        _check(rc, "failed to load scheduler plugin")

    def load_standard_plugin(self, name):
        plugin = _package_file("plugins", _plugin_library_name(name))
        if plugin is None:
            raise SchedulerError(
                f"failed to find standard scheduler plugin: {name}")
        self.load_plugin(plugin)

    def set_policy(self, name, options=None):
        with _MetadataArray(options) as option_array:
            rc = _swig.qhw_sched_set_policy(
                self._handle,
                name,
                option_array.ptr,
                option_array.count,
            )
        _check(rc, "failed to set scheduler policy")

    def submit_task(
        self,
        task_id,
        parent_task_id=0,
        owner_id=0,
        job_id=0,
        priority=0,
        deadline_ns=0,
        estimated_runtime_ns=0,
        payload=None,
        metadata=None,
    ):
        task = TaskDesc()
        task.task_id = task_id
        task.parent_task_id = parent_task_id
        task.owner_id = owner_id
        task.job_id = job_id
        task.priority = priority
        task.deadline_ns = deadline_ns
        task.estimated_runtime_ns = estimated_runtime_ns

        payload_ptr = None
        if payload is not None:
            payload_size = memoryview(payload).nbytes
            if payload_size:
                payload_ptr = _swig.qhw_sched_payload_copy(payload)
                if payload_ptr is None:
                    raise SchedulerError("failed to allocate payload copy")
                task.payload = payload_ptr
            task.payload_size = payload_size

        try:
            with _MetadataArray(metadata) as metadata_array:
                task.metadata = metadata_array.ptr
                task.metadata_count = metadata_array.count
                rc = _swig.qhw_sched_submit_task(self._handle, task)
            _check(rc, "failed to submit task")
        except Exception:
            if payload_ptr is not None:
                _swig.qhw_sched_payload_destroy(payload_ptr)
            raise

        if payload_ptr is not None:
            self._payloads[task_id] = payload_ptr

    def select_next_assignment(self):
        rc, assignment = _swig.qhw_sched_select_next(self._handle)
        _check(rc, "failed to select next task")
        return Assignment(assignment)

    def select_next(self):
        assignment = self.select_next_assignment()
        return assignment.task_id

    def task_update_priority(self, task_id, priority):
        rc = _swig.qhw_sched_task_update_priority(
            self._handle,
            task_id,
            priority,
        )
        _check(rc, "failed to update task priority")

    def task_started(self, task_id):
        rc = _swig.qhw_sched_task_started(self._handle, task_id)
        _check(rc, "failed to mark task started")

    def task_completed(self, task_id):
        rc = _swig.qhw_sched_task_completed(self._handle, task_id)
        _check(rc, "failed to mark task completed")
        self._release_payload(task_id)

    def task_failed(self, task_id):
        rc = _swig.qhw_sched_task_failed(self._handle, task_id)
        _check(rc, "failed to mark task failed")
        self._release_payload(task_id)

    def task_cancelled(self, task_id):
        rc = _swig.qhw_sched_task_cancelled(self._handle, task_id)
        _check(rc, "failed to mark task cancelled")
        self._release_payload(task_id)

    def task_state(self, task_id):
        rc, state = _swig.qhw_sched_task_get_state(self._handle, task_id)
        _check(rc, "failed to get task state")
        return state

    def task_count(self):
        return _swig.qhw_sched_task_count(self._handle)

    def _release_payload(self, task_id):
        payload = self._payloads.pop(task_id, None)
        if payload is not None:
            _swig.qhw_sched_payload_destroy(payload)

    def close(self):
        if self._handle is not None:
            _swig.qhw_sched_destroy(self._handle)
            self._handle = None

        for payload in self._payloads.values():
            _swig.qhw_sched_payload_destroy(payload)
        self._payloads.clear()

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc, _tb):
        self.close()

    def __del__(self):
        self.close()
