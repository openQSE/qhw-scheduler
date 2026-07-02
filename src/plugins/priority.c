#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "policy/qhw_ready_queue.h"

#include <stdint.h>
#include <string.h>

struct priority_state {
	qhw_sched_t *sched;
	struct qhw_ready_queue ready;
	qhw_sched_split_config_t split_config;
};

static int priority_compare(
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right)
{
	if (left->desc.priority != right->desc.priority) {
		return left->desc.priority > right->desc.priority ? -1 : 1;
	}

	if (left->seq != right->seq) {
		return left->seq < right->seq ? -1 : 1;
	}

	if (left->desc.task_id == right->desc.task_id) {
		return 0;
	}

	return left->desc.task_id < right->desc.task_id ? -1 : 1;
}

static qhw_sched_rc_t priority_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct priority_state *state;

	(void)options;
	(void)option_count;

	if (sched == NULL || out_policy_state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	state = qhw_sched_alloc(sched, sizeof(*state));
	if (state == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	memset(state, 0, sizeof(*state));

	state->sched = sched;
	qhw_sched_split_config_init(&state->split_config);
	if (qhw_sched_split_config_parse_options(&state->split_config,
		options, option_count) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_ready_queue_init(&state->ready, sched,
		QHW_READY_QUEUE_HEAP, priority_compare) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void priority_fini(void *policy_state)
{
	struct priority_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	qhw_ready_queue_fini(&state->ready);
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t priority_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct priority_state *state = policy_state;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	return qhw_ready_queue_insert(&state->ready, task);
}

static qhw_sched_rc_t priority_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct priority_state *state = policy_state;
	qhw_sched_task_id_t task_id;
	qhw_sched_rc_t rc;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = qhw_ready_queue_pop(&state->ready, &task_id);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}
	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->task_id = task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_get_split_config(
	void *policy_state,
	qhw_sched_split_config_t *out_config)
{
	struct priority_state *state = policy_state;

	if (state == NULL || out_config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_config = state->split_config;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	struct priority_state *state = policy_state;
	struct qhw_ready_task *task;
	int64_t old_priority;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_ready_queue_find(&state->ready, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	old_priority = task->desc.priority;
	task->desc.priority = priority;
	if (qhw_ready_queue_reorder(&state->ready, task) != QHW_SCHED_OK) {
		task->desc.priority = old_priority;
		return QHW_SCHED_ERR_STATE;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	struct priority_state *state = policy_state;
	qhw_sched_rc_t rc;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = qhw_ready_queue_remove(&state->ready, task_id);
	return rc == QHW_SCHED_ERR_NOT_FOUND ? QHW_SCHED_OK : rc;
}

static const qhw_sched_plugin_desc_t priority_desc = {
	.struct_size = sizeof(priority_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "priority",
	.version = "0.1.0",
	.description = "priority scheduler policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = priority_init,
	.fini = priority_fini,
	.on_task_submit = priority_on_task_submit,
	.select_next = priority_select_next,
	.get_split_config = priority_get_split_config,
	.on_task_priority_changed = priority_on_task_priority_changed,
	.on_task_started = priority_on_task_started,
	.on_task_finished = priority_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &priority_desc;
}
