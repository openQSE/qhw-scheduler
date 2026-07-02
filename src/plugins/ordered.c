#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "policy/qhw_deadline_refresh.h"
#include "policy/qhw_order_key.h"
#include "policy/qhw_ready_queue.h"

#include <string.h>

struct ordered_state {
	qhw_sched_t *sched;
	struct qhw_order_config order;
	struct qhw_ready_queue ready;
	struct qhw_deadline_refresh_queue refresh;
	qhw_sched_split_config_t split_config;
};

static int ordered_compare(
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right,
	void *user_data)
{
	return qhw_order_compare(user_data, left, right);
}

static qhw_sched_rc_t ordered_refresh_task(
	struct ordered_state *state,
	struct qhw_ready_task *task)
{
	qhw_sched_rc_t rc;
	int64_t old_effective_priority;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	old_effective_priority = task->effective_priority;
	qhw_deadline_refresh_remove(&state->refresh, task);
	rc = qhw_order_refresh_task(&state->order, task);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	if (old_effective_priority != task->effective_priority) {
		rc = qhw_ready_queue_reorder(&state->ready, task);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return qhw_deadline_refresh_insert(&state->refresh, task);
}

static qhw_sched_rc_t ordered_refresh_expired(struct ordered_state *state)
{
	uint64_t now_ns = qhw_order_now_ns(&state->order);
	struct qhw_ready_task *task;
	qhw_sched_rc_t rc;

	while ((task = qhw_deadline_refresh_pop_expired(&state->refresh,
		now_ns)) != NULL) {
		rc = ordered_refresh_task(state, task);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct ordered_state *state;

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
	qhw_order_config_init(&state->order);
	if (qhw_sched_split_config_parse_options(&state->split_config,
		options, option_count) != QHW_SCHED_OK ||
		qhw_order_config_parse_options(&state->order, options,
			option_count) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_ready_queue_init(&state->ready, sched,
		QHW_READY_QUEUE_HEAP, ordered_compare, &state->order) !=
			QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	if (qhw_deadline_refresh_init(&state->refresh, sched) !=
		QHW_SCHED_OK) {
		qhw_ready_queue_fini(&state->ready);
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void ordered_fini(void *policy_state)
{
	struct ordered_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	qhw_deadline_refresh_fini(&state->refresh);
	qhw_ready_queue_fini(&state->ready);
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t ordered_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct ordered_state *state = policy_state;
	struct qhw_ready_task *ready_task;
	qhw_sched_rc_t rc;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = qhw_ready_queue_insert(&state->ready, task);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	ready_task = qhw_ready_queue_find(&state->ready, task->task_id);
	if (ready_task == NULL) {
		return QHW_SCHED_ERR_STATE;
	}

	rc = ordered_refresh_task(state, ready_task);
	if (rc != QHW_SCHED_OK) {
		qhw_deadline_refresh_remove(&state->refresh, ready_task);
		(void)qhw_ready_queue_remove(&state->ready, task->task_id);
		return rc;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct ordered_state *state = policy_state;
	qhw_sched_task_id_t task_id;
	qhw_sched_rc_t rc;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = ordered_refresh_expired(state);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	qhw_deadline_refresh_remove(&state->refresh,
		qhw_ready_queue_peek(&state->ready));
	rc = qhw_ready_queue_pop(&state->ready, &task_id);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->task_id = task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_get_split_config(
	void *policy_state,
	qhw_sched_split_config_t *out_config)
{
	struct ordered_state *state = policy_state;

	if (state == NULL || out_config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_config = state->split_config;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	struct ordered_state *state = policy_state;
	struct qhw_ready_task *task;
	int64_t old_base_priority;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_ready_queue_find(&state->ready, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	old_base_priority = task->base_priority;
	task->base_priority = priority;
	if (ordered_refresh_task(state, task) != QHW_SCHED_OK) {
		task->base_priority = old_base_priority;
		(void)ordered_refresh_task(state, task);
		return QHW_SCHED_ERR_STATE;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	struct ordered_state *state = policy_state;
	qhw_sched_rc_t rc;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	qhw_deadline_refresh_remove(&state->refresh,
		qhw_ready_queue_find(&state->ready, task_id));
	rc = qhw_ready_queue_remove(&state->ready, task_id);
	return rc == QHW_SCHED_ERR_NOT_FOUND ? QHW_SCHED_OK : rc;
}

static const qhw_sched_plugin_desc_t ordered_desc = {
	.struct_size = sizeof(ordered_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "ordered",
	.version = "0.1.0",
	.description = "configurable ordered scheduler policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = ordered_init,
	.fini = ordered_fini,
	.on_task_submit = ordered_on_task_submit,
	.select_next = ordered_select_next,
	.get_split_config = ordered_get_split_config,
	.on_task_priority_changed = ordered_on_task_priority_changed,
	.on_task_started = ordered_on_task_started,
	.on_task_finished = ordered_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &ordered_desc;
}
