import ctypes
import ctypes.util
import os
import sys
from pathlib import Path


QHW_SCHED_OK = 0
QHW_SCHED_TASK_QUEUED = 1
QHW_SCHED_TASK_RUNNING = 2
QHW_SCHED_TASK_COMPLETED = 3
QHW_SCHED_TASK_FAILED = 4
QHW_SCHED_TASK_CANCELLED = 5
QHW_SCHED_TASK_ASSIGNED = 6
QHW_SCHED_TASK_WAITING = 7
QHW_SCHED_THREAD_SAFE = 1
QHW_SCHED_THREAD_USER = 2
QHW_SCHED_VALUE_U64 = 1
QHW_SCHED_VALUE_I64 = 2
QHW_SCHED_VALUE_F64 = 3
QHW_SCHED_VALUE_PTR = 4
QHW_SCHED_META_SHOTS = 1
QHW_SCHED_META_SLICE_INDEX = 9
QHW_SCHED_META_SLICE_COUNT = 10
QHW_SCHED_META_MAX_SHOTS = 13
QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD = 100
QHW_SCHED_OPT_SLICE_MAX_SHOTS = 101


class SchedulerError(RuntimeError):
    pass


def _shared_library_name():
    if sys.platform == "darwin":
        return "libqhw_scheduler.dylib"
    if os.name == "nt":
        return "qhw_scheduler.dll"
    return "libqhw_scheduler.so"


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
    source_build_dir = source_build_dir / "qhw_scheduler"
    yield source_build_dir


def _package_file(*parts):
    for package_dir in _package_dirs():
        path = package_dir.joinpath(*parts)
        if path.is_file():
            return path
    return None


class KVValue(ctypes.Union):
    _fields_ = [
        ("u64", ctypes.c_uint64),
        ("i64", ctypes.c_int64),
        ("f64", ctypes.c_double),
        ("ptr", ctypes.c_void_p),
    ]


class KV(ctypes.Structure):
    _fields_ = [
        ("key", ctypes.c_uint64),
        ("type", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("value", KVValue),
    ]


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


def _metadata_array(metadata):
    if not metadata:
        return None, 0

    array_type = KV * len(metadata)
    array = array_type(*metadata)
    return array, len(metadata)


class QPUProfile(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("qpu_id", ctypes.c_uint64),
        ("num_qubits", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("metadata", ctypes.POINTER(KV)),
        ("metadata_count", ctypes.c_size_t),
    ]


class QPURuntime(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("queued_count", ctypes.c_uint64),
        ("completed_count", ctypes.c_uint64),
        ("failed_count", ctypes.c_uint64),
        ("cancelled_count", ctypes.c_uint64),
        ("running_task_id", ctypes.c_uint64),
    ]


class SchedulerAttr(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("threading", ctypes.c_int),
        ("allocator", ctypes.c_void_p),
        ("flags", ctypes.c_uint64),
    ]


class TaskDesc(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("task_id", ctypes.c_uint64),
        ("parent_task_id", ctypes.c_uint64),
        ("owner_id", ctypes.c_uint64),
        ("job_id", ctypes.c_uint64),
        ("priority", ctypes.c_int64),
        ("deadline_ns", ctypes.c_uint64),
        ("estimated_runtime_ns", ctypes.c_uint64),
        ("payload", ctypes.c_void_p),
        ("payload_size", ctypes.c_size_t),
        ("metadata", ctypes.POINTER(KV)),
        ("metadata_count", ctypes.c_size_t),
    ]


class Assignment(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("task_id", ctypes.c_uint64),
        ("parent_task_id", ctypes.c_uint64),
        ("slice_index", ctypes.c_uint64),
        ("slice_count", ctypes.c_uint64),
        ("payload", ctypes.c_void_p),
        ("payload_size", ctypes.c_size_t),
        ("estimated_runtime_ns", ctypes.c_uint64),
    ]


def _load_library():
    mode = getattr(ctypes, "RTLD_GLOBAL", 0)
    package_lib = _package_file("native", _shared_library_name())
    if package_lib is not None:
        return ctypes.CDLL(os.fspath(package_lib), mode=mode)

    found = ctypes.util.find_library("qhw_scheduler")
    if found:
        return ctypes.CDLL(found, mode=mode)

    raise SchedulerError(
        "failed to find qhw-scheduler native library")


_LIB = _load_library()


def _bind():
    lib = _LIB

    lib.qhw_sched_qpu_create.argtypes = [
        ctypes.POINTER(QPUProfile),
        ctypes.POINTER(ctypes.c_void_p),
    ]
    lib.qhw_sched_qpu_create.restype = ctypes.c_int
    lib.qhw_sched_qpu_destroy.argtypes = [ctypes.c_void_p]
    lib.qhw_sched_qpu_destroy.restype = None
    lib.qhw_sched_qpu_get_profile.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(QPUProfile),
    ]
    lib.qhw_sched_qpu_get_profile.restype = ctypes.c_int
    lib.qhw_sched_qpu_get_runtime.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(QPURuntime),
    ]
    lib.qhw_sched_qpu_get_runtime.restype = ctypes.c_int
    lib.qhw_sched_create.argtypes = [
        ctypes.c_char_p,
        ctypes.POINTER(SchedulerAttr),
        ctypes.c_void_p,
        ctypes.POINTER(KV),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    lib.qhw_sched_create.restype = ctypes.c_int
    lib.qhw_sched_destroy.argtypes = [ctypes.c_void_p]
    lib.qhw_sched_destroy.restype = None
    lib.qhw_sched_load_plugin.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.qhw_sched_load_plugin.restype = ctypes.c_int
    lib.qhw_sched_set_policy.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.POINTER(KV),
        ctypes.c_size_t,
    ]
    lib.qhw_sched_set_policy.restype = ctypes.c_int
    lib.qhw_sched_submit_task.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(TaskDesc),
    ]
    lib.qhw_sched_submit_task.restype = ctypes.c_int
    lib.qhw_sched_select_next.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(Assignment),
    ]
    lib.qhw_sched_select_next.restype = ctypes.c_int
    lib.qhw_sched_task_update_priority.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_int64,
    ]
    lib.qhw_sched_task_update_priority.restype = ctypes.c_int
    lib.qhw_sched_task_started.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_started.restype = ctypes.c_int
    lib.qhw_sched_task_completed.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_completed.restype = ctypes.c_int
    lib.qhw_sched_task_failed.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_failed.restype = ctypes.c_int
    lib.qhw_sched_task_cancelled.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_cancelled.restype = ctypes.c_int
    lib.qhw_sched_task_get_state.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_int),
    ]
    lib.qhw_sched_task_get_state.restype = ctypes.c_int
    lib.qhw_sched_task_count.argtypes = [ctypes.c_void_p]
    lib.qhw_sched_task_count.restype = ctypes.c_size_t


_bind()


def _check(rc, message):
    if rc != QHW_SCHED_OK:
        raise SchedulerError(f"{message}: rc={rc}")


class QPU:
    def __init__(self, qpu_id, num_qubits, flags=0, metadata=None):
        metadata_array, metadata_count = _metadata_array(metadata)
        profile = QPUProfile()
        profile.struct_size = ctypes.sizeof(profile)
        profile.qpu_id = qpu_id
        profile.num_qubits = num_qubits
        profile.flags = flags
        profile.metadata = metadata_array
        profile.metadata_count = metadata_count
        handle = ctypes.c_void_p()
        rc = _LIB.qhw_sched_qpu_create(profile, ctypes.byref(handle))
        _check(rc, "failed to create QPU")
        self._handle = handle

    @property
    def handle(self):
        return self._handle

    def close(self):
        if self._handle:
            _LIB.qhw_sched_qpu_destroy(self._handle)
            self._handle = None

    def profile(self):
        profile = QPUProfile()
        profile.struct_size = ctypes.sizeof(profile)
        rc = _LIB.qhw_sched_qpu_get_profile(
            self._handle,
            ctypes.byref(profile),
        )
        _check(rc, "failed to get QPU profile")
        return profile

    def runtime(self):
        runtime = QPURuntime()
        runtime.struct_size = ctypes.sizeof(runtime)
        rc = _LIB.qhw_sched_qpu_get_runtime(
            self._handle,
            ctypes.byref(runtime),
        )
        _check(rc, "failed to get QPU runtime")
        return runtime

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
        options_array, option_count = _metadata_array(options)
        attr = SchedulerAttr()
        attr.struct_size = ctypes.sizeof(attr)
        attr.threading = threading
        attr.flags = flags
        handle = ctypes.c_void_p()
        policy = policy_name.encode() if policy_name else None
        rc = _LIB.qhw_sched_create(
            policy,
            ctypes.byref(attr),
            qpu.handle,
            options_array,
            option_count,
            ctypes.byref(handle),
        )
        _check(rc, "failed to create scheduler")
        self._handle = handle
        self._payloads = {}
        self._metadata = {}

    def load_plugin(self, path):
        rc = _LIB.qhw_sched_load_plugin(
            self._handle,
            os.fsencode(path),
        )
        _check(rc, "failed to load scheduler plugin")

    def load_standard_plugin(self, name):
        plugin = _package_file("plugins", _plugin_library_name(name))
        if plugin is None:
            raise SchedulerError(
                f"failed to find standard scheduler plugin: {name}")
        self.load_plugin(plugin)

    def set_policy(self, name, options=None):
        options_array, option_count = _metadata_array(options)
        rc = _LIB.qhw_sched_set_policy(
            self._handle,
            name.encode(),
            options_array,
            option_count,
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
        task.struct_size = ctypes.sizeof(task)
        task.task_id = task_id
        task.parent_task_id = parent_task_id
        task.owner_id = owner_id
        task.job_id = job_id
        task.priority = priority
        task.deadline_ns = deadline_ns
        task.estimated_runtime_ns = estimated_runtime_ns

        if payload is not None:
            payload_buffer = ctypes.create_string_buffer(payload)
            self._payloads[task_id] = payload_buffer
            task.payload = ctypes.cast(payload_buffer, ctypes.c_void_p)
            task.payload_size = len(payload)

        metadata_array, metadata_count = _metadata_array(metadata)
        if metadata_array is not None:
            self._metadata[task_id] = metadata_array
            task.metadata = metadata_array
            task.metadata_count = metadata_count

        rc = _LIB.qhw_sched_submit_task(self._handle, ctypes.byref(task))
        if rc != QHW_SCHED_OK:
            self._payloads.pop(task_id, None)
            self._metadata.pop(task_id, None)
        _check(rc, "failed to submit task")

    def select_next_assignment(self):
        assignment = Assignment()
        assignment.struct_size = ctypes.sizeof(assignment)
        rc = _LIB.qhw_sched_select_next(
            self._handle,
            ctypes.byref(assignment),
        )
        _check(rc, "failed to select next task")
        return assignment

    def select_next(self):
        assignment = self.select_next_assignment()
        return assignment.task_id

    def task_update_priority(self, task_id, priority):
        rc = _LIB.qhw_sched_task_update_priority(
            self._handle,
            task_id,
            priority,
        )
        _check(rc, "failed to update task priority")

    def task_started(self, task_id):
        rc = _LIB.qhw_sched_task_started(self._handle, task_id)
        _check(rc, "failed to mark task started")

    def task_completed(self, task_id):
        rc = _LIB.qhw_sched_task_completed(self._handle, task_id)
        _check(rc, "failed to mark task completed")
        self._payloads.pop(task_id, None)
        self._metadata.pop(task_id, None)

    def task_failed(self, task_id):
        rc = _LIB.qhw_sched_task_failed(self._handle, task_id)
        _check(rc, "failed to mark task failed")
        self._payloads.pop(task_id, None)
        self._metadata.pop(task_id, None)

    def task_cancelled(self, task_id):
        rc = _LIB.qhw_sched_task_cancelled(self._handle, task_id)
        _check(rc, "failed to mark task cancelled")
        self._payloads.pop(task_id, None)
        self._metadata.pop(task_id, None)

    def task_state(self, task_id):
        state = ctypes.c_int()
        rc = _LIB.qhw_sched_task_get_state(
            self._handle,
            task_id,
            ctypes.byref(state),
        )
        _check(rc, "failed to get task state")
        return state.value

    def task_count(self):
        return _LIB.qhw_sched_task_count(self._handle)

    def close(self):
        if self._handle:
            _LIB.qhw_sched_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()
