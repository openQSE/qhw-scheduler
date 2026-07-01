#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "util/qhw_list.h"

#include <string.h>

struct fifo_task {
	qhw_sched_task_desc_t desc;
	struct qhw_list_node link;
};

struct fifo_state {
	qhw_sched_t *sched;
	struct qhw_list_node ready;
};

static qhw_sched_rc_t fifo_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct fifo_state *state;

	(void)options;
	(void)option_count;

	if (sched == NULL || out_policy_state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	state = qhw_sched_alloc(sched, sizeof(*state));
	if (state == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	state->sched = sched;
	qhw_list_init(&state->ready);
	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void fifo_fini(void *policy_state)
{
	struct fifo_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	while (!qhw_list_empty(&state->ready)) {
		struct qhw_list_node *node = qhw_list_pop_front(&state->ready);
		struct fifo_task *task;

		task = qhw_container_of(node, struct fifo_task, link);
		qhw_sched_free(state->sched, task);
	}

	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t fifo_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct fifo_state *state = policy_state;
	struct fifo_task *item;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	item = qhw_sched_alloc(state->sched, sizeof(*item));
	if (item == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	memset(item, 0, sizeof(*item));
	item->desc = *task;
	qhw_list_push_back(&state->ready, &item->link);
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fifo_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct fifo_state *state = policy_state;
	struct qhw_list_node *node;
	struct fifo_task *task;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	node = qhw_list_pop_front(&state->ready);
	if (node == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	task = qhw_container_of(node, struct fifo_task, link);
	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->struct_size = sizeof(*out_assignment);
	out_assignment->task_id = task->desc.task_id;
	out_assignment->payload = task->desc.payload;
	out_assignment->payload_size = task->desc.payload_size;
	out_assignment->estimated_runtime_ns = task->desc.estimated_runtime_ns;
	qhw_sched_free(state->sched, task);
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fifo_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t fifo_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	struct fifo_state *state = policy_state;
	struct qhw_list_node *node;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	node = state->ready.next;
	while (node != &state->ready) {
		struct qhw_list_node *next = node->next;
		struct fifo_task *task;

		task = qhw_container_of(node, struct fifo_task, link);
		if (task->desc.task_id == task_id) {
			qhw_list_remove(node);
			qhw_sched_free(state->sched, task);
			return QHW_SCHED_OK;
		}
		node = next;
	}

	return QHW_SCHED_OK;
}

static const qhw_sched_plugin_desc_t fifo_desc = {
	.struct_size = sizeof(fifo_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "fifo",
	.version = "0.1.0",
	.description = "FIFO scheduler policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = fifo_init,
	.fini = fifo_fini,
	.on_task_submit = fifo_on_task_submit,
	.select_next = fifo_select_next,
	.on_task_started = fifo_on_task_started,
	.on_task_finished = fifo_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &fifo_desc;
}
