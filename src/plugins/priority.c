#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "policy/qhw_deadline_boost.h"
#include "policy/qhw_deadline_refresh.h"
#include "policy/qhw_ready_queue.h"

#include <stdint.h>
#include <string.h>

struct priority_state {
	qhw_sched_t *sched;
	struct qhw_ready_queue ready;
	struct qhw_deadline_refresh_queue refresh;
	struct qhw_deadline_boost_config boost;
	qhw_sched_split_config_t split_config;
	uint64_t now_ns;
	int use_static_now;
};

static int is_now_option(const qhw_sched_kv_t *option)
{
	return option->key == QHW_SCHED_OPT_DEADLINE_NOW_NS;
}

static qhw_sched_rc_t parse_now_option(
	struct priority_state *state,
	const qhw_sched_kv_t *options,
	size_t option_count)
{
	size_t i;

	if (option_count > 0 && options == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	for (i = 0; i < option_count; i++) {
		if (!is_now_option(&options[i])) {
			continue;
		}

		if (options[i].type != QHW_SCHED_VALUE_U64) {
			return QHW_SCHED_ERR_INVALID_ARG;
		}

		state->now_ns = options[i].value.u64;
		state->use_static_now = 1;
	}

	return QHW_SCHED_OK;
}

static uint64_t priority_now_ns(struct priority_state *state)
{
	if (state->use_static_now) {
		return state->now_ns;
	}

	return qhw_deadline_boost_now_ns();
}

static int priority_compare(
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right)
{
	if (left->effective_priority != right->effective_priority) {
		return left->effective_priority > right->effective_priority ?
			-1 : 1;
	}

	if (left->seq != right->seq) {
		return left->seq < right->seq ? -1 : 1;
	}

	if (left->desc.task_id == right->desc.task_id) {
		return 0;
	}

	return left->desc.task_id < right->desc.task_id ? -1 : 1;
}

static qhw_sched_rc_t priority_refresh_task(
	struct priority_state *state,
	struct qhw_ready_task *task)
{
	struct qhw_deadline_boost_result result;
	qhw_sched_rc_t rc;
	int64_t old_effective_priority;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	old_effective_priority = task->effective_priority;
	qhw_deadline_refresh_remove(&state->refresh, task);
	rc = qhw_deadline_boost_compute(&state->boost,
		task->base_priority,
		task->desc.deadline_ns,
		task->desc.estimated_runtime_ns,
		priority_now_ns(state),
		&result);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	task->effective_priority = result.effective_priority;
	task->next_refresh_ns = result.next_refresh_ns;
	if (old_effective_priority != task->effective_priority) {
		rc = qhw_ready_queue_reorder(&state->ready, task);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return qhw_deadline_refresh_insert(&state->refresh, task);
}

static qhw_sched_rc_t priority_refresh_expired(struct priority_state *state)
{
	uint64_t now_ns = priority_now_ns(state);
	struct qhw_ready_task *task;
	qhw_sched_rc_t rc;

	while ((task = qhw_deadline_refresh_pop_expired(&state->refresh,
		now_ns)) != NULL) {
		rc = priority_refresh_task(state, task);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct priority_state *state;

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
	qhw_deadline_boost_config_init(&state->boost);
	if (qhw_sched_split_config_parse_options(&state->split_config,
		options, option_count) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_INVALID_ARG;
	}
	if (qhw_deadline_boost_config_parse_options(&state->boost,
		options, option_count) != QHW_SCHED_OK ||
		parse_now_option(state, options, option_count) !=
			QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_ready_queue_init(&state->ready, sched,
		QHW_READY_QUEUE_HEAP, priority_compare) != QHW_SCHED_OK) {
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

static void priority_fini(void *policy_state)
{
	struct priority_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	qhw_deadline_refresh_fini(&state->refresh);
	qhw_ready_queue_fini(&state->ready);
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t priority_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct priority_state *state = policy_state;
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

	rc = priority_refresh_task(state, ready_task);
	if (rc != QHW_SCHED_OK) {
		qhw_deadline_refresh_remove(&state->refresh, ready_task);
		(void)qhw_ready_queue_remove(&state->ready, task->task_id);
		return rc;
	}

	return QHW_SCHED_OK;
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

	rc = priority_refresh_expired(state);
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
	if (priority_refresh_task(state, task) != QHW_SCHED_OK) {
		task->base_priority = old_base_priority;
		(void)priority_refresh_task(state, task);
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

	qhw_deadline_refresh_remove(&state->refresh,
		qhw_ready_queue_find(&state->ready, task_id));
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
