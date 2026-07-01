import ctypes
import ctypes.util
import os


QHW_SCHED_OK = 0
QHW_SCHED_TASK_QUEUED = 1
QHW_SCHED_TASK_RUNNING = 2
QHW_SCHED_TASK_COMPLETED = 3
QHW_SCHED_THREAD_SAFE = 1
QHW_SCHED_THREAD_USER = 2


class SchedulerError(RuntimeError):
    pass


class QPUProfile(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("qpu_id", ctypes.c_uint64),
        ("num_qubits", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("metadata", ctypes.c_void_p),
        ("metadata_count", ctypes.c_size_t),
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
        ("metadata", ctypes.c_void_p),
        ("metadata_count", ctypes.c_size_t),
    ]


class Assignment(ctypes.Structure):
    _fields_ = [
        ("struct_size", ctypes.c_size_t),
        ("task_id", ctypes.c_uint64),
        ("payload", ctypes.c_void_p),
        ("payload_size", ctypes.c_size_t),
        ("estimated_runtime_ns", ctypes.c_uint64),
    ]


def _load_library():
    path = os.environ.get("QHW_SCHED_LIBRARY")
    if path:
        return ctypes.CDLL(path)

    found = ctypes.util.find_library("qhw_scheduler")
    if found:
        return ctypes.CDLL(found)

    raise SchedulerError(
        "failed to find qhw-scheduler library. Set QHW_SCHED_LIBRARY.")


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
    lib.qhw_sched_create.argtypes = [
        ctypes.c_char_p,
        ctypes.POINTER(SchedulerAttr),
        ctypes.c_void_p,
        ctypes.c_void_p,
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
        ctypes.c_void_p,
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
    lib.qhw_sched_task_started.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_started.restype = ctypes.c_int
    lib.qhw_sched_task_completed.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    lib.qhw_sched_task_completed.restype = ctypes.c_int


_bind()


def _check(rc, message):
    if rc != QHW_SCHED_OK:
        raise SchedulerError(f"{message}: rc={rc}")


class QPU:
    def __init__(self, qpu_id, num_qubits):
        profile = QPUProfile()
        profile.struct_size = ctypes.sizeof(profile)
        profile.qpu_id = qpu_id
        profile.num_qubits = num_qubits
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

    def __del__(self):
        self.close()


class Scheduler:
    def __init__(self, qpu, threading=QHW_SCHED_THREAD_SAFE):
        attr = SchedulerAttr()
        attr.struct_size = ctypes.sizeof(attr)
        attr.threading = threading
        handle = ctypes.c_void_p()
        rc = _LIB.qhw_sched_create(
            None,
            ctypes.byref(attr),
            qpu.handle,
            None,
            0,
            ctypes.byref(handle),
        )
        _check(rc, "failed to create scheduler")
        self._handle = handle

    def load_plugin(self, path):
        rc = _LIB.qhw_sched_load_plugin(
            self._handle,
            os.fsencode(path),
        )
        _check(rc, "failed to load scheduler plugin")

    def set_policy(self, name):
        rc = _LIB.qhw_sched_set_policy(
            self._handle,
            name.encode(),
            None,
            0,
        )
        _check(rc, "failed to set scheduler policy")

    def submit_task(self, task_id, owner_id=0, job_id=0, priority=0):
        task = TaskDesc()
        task.struct_size = ctypes.sizeof(task)
        task.task_id = task_id
        task.owner_id = owner_id
        task.job_id = job_id
        task.priority = priority
        rc = _LIB.qhw_sched_submit_task(self._handle, ctypes.byref(task))
        _check(rc, "failed to submit task")

    def select_next(self):
        assignment = Assignment()
        assignment.struct_size = ctypes.sizeof(assignment)
        rc = _LIB.qhw_sched_select_next(
            self._handle,
            ctypes.byref(assignment),
        )
        _check(rc, "failed to select next task")
        return assignment.task_id

    def task_started(self, task_id):
        rc = _LIB.qhw_sched_task_started(self._handle, task_id)
        _check(rc, "failed to mark task started")

    def task_completed(self, task_id):
        rc = _LIB.qhw_sched_task_completed(self._handle, task_id)
        _check(rc, "failed to mark task completed")

    def close(self):
        if self._handle:
            _LIB.qhw_sched_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()

