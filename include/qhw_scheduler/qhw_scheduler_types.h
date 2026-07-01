#ifndef QHW_SCHEDULER_TYPES_H
#define QHW_SCHEDULER_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QHW_SCHED_ABI_VERSION 1U
#define QHW_SCHED_INVALID_TASK_ID UINT64_C(0)

typedef enum qhw_sched_rc {
	QHW_SCHED_OK = 0,
	QHW_SCHED_ERR_INVALID_ARG = -1,
	QHW_SCHED_ERR_NO_MEMORY = -2,
	QHW_SCHED_ERR_NOT_FOUND = -3,
	QHW_SCHED_ERR_EXISTS = -4,
	QHW_SCHED_ERR_PLUGIN = -5,
	QHW_SCHED_ERR_UNSUPPORTED = -6,
	QHW_SCHED_ERR_STATE = -7
} qhw_sched_rc_t;

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

typedef uint64_t qhw_sched_task_id_t;

typedef enum qhw_sched_value_type {
	QHW_SCHED_VALUE_U64 = 1,
	QHW_SCHED_VALUE_I64 = 2,
	QHW_SCHED_VALUE_F64 = 3,
	QHW_SCHED_VALUE_PTR = 4
} qhw_sched_value_type_t;

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
	QHW_SCHED_KEY_USER_BASE = UINT64_C(0x100000000)
} qhw_sched_builtin_key_t;

typedef struct qhw_sched_kv {
	uint64_t key;
	uint32_t type;
	uint32_t flags;
	union {
		uint64_t u64;
		int64_t i64;
		double f64;
		void *ptr;
	} value;
} qhw_sched_kv_t;

typedef enum qhw_sched_task_state {
	QHW_SCHED_TASK_UNKNOWN = 0,
	QHW_SCHED_TASK_QUEUED = 1,
	QHW_SCHED_TASK_RUNNING = 2,
	QHW_SCHED_TASK_COMPLETED = 3,
	QHW_SCHED_TASK_FAILED = 4,
	QHW_SCHED_TASK_CANCELLED = 5,
	QHW_SCHED_TASK_ASSIGNED = 6,
	QHW_SCHED_TASK_WAITING = 7
} qhw_sched_task_state_t;

typedef struct qhw_sched_qpu_profile {
	size_t struct_size;
	uint64_t qpu_id;
	uint32_t num_qubits;
	uint32_t flags;
	const qhw_sched_kv_t *metadata;
	size_t metadata_count;
} qhw_sched_qpu_profile_t;

typedef struct qhw_sched_qpu_runtime {
	size_t struct_size;
	uint64_t queued_count;
	uint64_t completed_count;
	uint64_t failed_count;
	uint64_t cancelled_count;
	qhw_sched_task_id_t running_task_id;
} qhw_sched_qpu_runtime_t;

typedef struct qhw_sched_task_desc {
	size_t struct_size;
	qhw_sched_task_id_t task_id;
	qhw_sched_task_id_t parent_task_id;
	uint64_t owner_id;
	uint64_t job_id;
	int64_t priority;
	uint64_t deadline_ns;
	uint64_t estimated_runtime_ns;
	const void *payload;
	size_t payload_size;
	const qhw_sched_kv_t *metadata;
	size_t metadata_count;
} qhw_sched_task_desc_t;

typedef struct qhw_sched_assignment {
	size_t struct_size;
	qhw_sched_task_id_t task_id;
	qhw_sched_task_id_t parent_task_id;
	uint64_t slice_index;
	uint64_t slice_count;
	const void *payload;
	size_t payload_size;
	uint64_t estimated_runtime_ns;
} qhw_sched_assignment_t;

typedef struct qhw_sched_split_config {
	size_t struct_size;
	uint64_t shot_threshold;
	uint64_t max_shots;
	uint64_t min_remainder_shots;
	uint64_t max_children;
	uint64_t requested_shots;
	uint64_t slice_shots;
	size_t slice_count;
	uint64_t flags;
} qhw_sched_split_config_t;

typedef qhw_sched_rc_t (*qhw_sched_split_task_fn)(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data);

typedef struct qhw_sched_callbacks {
	size_t struct_size;
	qhw_sched_split_task_fn split_task;
	void *user_data;
} qhw_sched_callbacks_t;

typedef struct qhw_sched_policy_info {
	size_t struct_size;
	const char *name;
	const char *version;
	const char *description;
	uint64_t capabilities;
	uint64_t thread_flags;
	const char *plugin_path;
} qhw_sched_policy_info_t;

#ifdef __cplusplus
}
#endif

#endif
