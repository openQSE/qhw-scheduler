#ifndef QHW_SCHEDULER_PLUGIN_H
#define QHW_SCHEDULER_PLUGIN_H

#include "qhw_scheduler_types.h"

#define QHW_SCHED_PLUGIN_THREAD_SAFE UINT64_C(1)
#define QHW_SCHED_PLUGIN_THREAD_USER UINT64_C(2)
#define QHW_SCHED_PLUGIN_THREAD_ALL \
	(QHW_SCHED_PLUGIN_THREAD_SAFE | QHW_SCHED_PLUGIN_THREAD_USER)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qhw_sched qhw_sched_t;

void *qhw_sched_alloc(qhw_sched_t *sched, size_t size);
void *qhw_sched_realloc(qhw_sched_t *sched, void *ptr, size_t size);
void qhw_sched_free(qhw_sched_t *sched, void *ptr);

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
} qhw_sched_plugin_desc_t;

typedef const qhw_sched_plugin_desc_t *(*qhw_sched_plugin_descriptor_fn)(void);

#ifdef __cplusplus
}
#endif

#endif
