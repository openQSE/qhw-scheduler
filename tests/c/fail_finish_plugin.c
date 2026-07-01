#include "qhw_scheduler/qhw_scheduler_plugin.h"

#include <string.h>

struct fail_task {
	qhw_sched_task_desc_t desc;
	struct fail_task *next;
};

struct fail_state {
	qhw_sched_t *sched;
	struct fail_task *head;
	struct fail_task *tail;
	qhw_sched_split_config_t split_config;
};

static qhw_sched_rc_t fail_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct fail_state *state;

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

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void fail_fini(void *policy_state)
{
	struct fail_state *state = policy_state;
	struct fail_task *task;

	if (state == NULL) {
		return;
	}

	task = state->head;
	while (task != NULL) {
		struct fail_task *next = task->next;

		qhw_sched_free(state->sched, task);
		task = next;
	}

	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t fail_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct fail_state *state = policy_state;
	struct fail_task *item;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	item = qhw_sched_alloc(state->sched, sizeof(*item));
	if (item == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	memset(item, 0, sizeof(*item));
	item->desc = *task;
	if (state->tail == NULL) {
		state->head = item;
	} else {
		state->tail->next = item;
	}
	state->tail = item;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fail_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct fail_state *state = policy_state;
	struct fail_task *task;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = state->head;
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	state->head = task->next;
	if (state->head == NULL) {
		state->tail = NULL;
	}

	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->struct_size = sizeof(*out_assignment);
	out_assignment->task_id = task->desc.task_id;
	qhw_sched_free(state->sched, task);
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fail_get_split_config(
	void *policy_state,
	qhw_sched_split_config_t *out_config)
{
	struct fail_state *state = policy_state;

	if (state == NULL || out_config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_config = state->split_config;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fail_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	(void)policy_state;
	(void)task_id;
	(void)priority;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fail_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fail_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	(void)policy_state;
	(void)task_id;
	(void)terminal_state;
	return QHW_SCHED_ERR_STATE;
}

static const qhw_sched_plugin_desc_t fail_desc = {
	.struct_size = sizeof(fail_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "fail_finish",
	.version = "0.1.0",
	.description = "finish notification failure test policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = fail_init,
	.fini = fail_fini,
	.on_task_submit = fail_on_task_submit,
	.select_next = fail_select_next,
	.get_split_config = fail_get_split_config,
	.on_task_priority_changed = fail_on_task_priority_changed,
	.on_task_started = fail_on_task_started,
	.on_task_finished = fail_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &fail_desc;
}
