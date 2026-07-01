#ifndef QHW_SCHEDULER_H
#define QHW_SCHEDULER_H

#include "qhw_scheduler_plugin.h"
#include "qhw_scheduler_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qhw_sched qhw_sched_t;
typedef struct qhw_sched_qpu qhw_sched_qpu_t;

qhw_sched_rc_t qhw_sched_qpu_create(
	const qhw_sched_qpu_profile_t *profile,
	qhw_sched_qpu_t **out_qpu);

void qhw_sched_qpu_destroy(qhw_sched_qpu_t *qpu);

qhw_sched_rc_t qhw_sched_qpu_get_profile(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_profile_t *out_profile);

qhw_sched_rc_t qhw_sched_qpu_get_runtime(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_runtime_t *out_runtime);

qhw_sched_rc_t qhw_sched_create(
	const char *policy_name,
	const qhw_sched_attr_t *attr,
	qhw_sched_qpu_t *qpu,
	const qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_t **out_sched);

void qhw_sched_destroy(qhw_sched_t *sched);

qhw_sched_threading_t qhw_sched_get_threading(qhw_sched_t *sched);

const char *qhw_sched_last_error(qhw_sched_t *sched);

qhw_sched_rc_t qhw_sched_load_plugin(
	qhw_sched_t *sched,
	const char *shared_object_path);

qhw_sched_rc_t qhw_sched_list_policies(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t **out_policies,
	size_t *out_count);

void qhw_sched_free_policy_info_array(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t *policies);

qhw_sched_rc_t qhw_sched_set_policy(
	qhw_sched_t *sched,
	const char *policy_name,
	const qhw_sched_kv_t *options,
	size_t option_count);

qhw_sched_rc_t qhw_sched_submit_task(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task);

qhw_sched_rc_t qhw_sched_select_next(
	qhw_sched_t *sched,
	qhw_sched_assignment_t *out_assignment);

qhw_sched_rc_t qhw_sched_task_started(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_sched_task_completed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_sched_task_failed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_sched_task_cancelled(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_sched_task_get_state(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t *out_state);

size_t qhw_sched_task_count(qhw_sched_t *sched);

#ifdef __cplusplus
}
#endif

#endif
