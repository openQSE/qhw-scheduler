#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "util/qhw_hash_table.h"
#include "util/qhw_heap.h"

#include <stdint.h>
#include <string.h>

#define PRIORITY_BUCKETS 4096U

struct priority_task {
	qhw_sched_task_desc_t desc;
	uint64_t seq;
	size_t heap_index;
};

struct priority_state {
	qhw_sched_t *sched;
	struct qhw_heap ready;
	uint64_t next_seq;
	struct qhw_hash_table by_id;
};

static void *priority_alloc(size_t size, void *user_data)
{
	return qhw_sched_alloc(user_data, size);
}

static void *priority_realloc(void *ptr, size_t size, void *user_data)
{
	return qhw_sched_realloc(user_data, ptr, size);
}

static void priority_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static int priority_compare(const void *left_arg, const void *right_arg)
{
	const struct priority_task *left = left_arg;
	const struct priority_task *right = right_arg;

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

static void priority_update_index(
	void *item,
	size_t index,
	void *user_data)
{
	struct priority_task *task = item;

	(void)user_data;
	task->heap_index = index;
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
	state->next_seq = 1;
	if (qhw_heap_init(&state->ready, priority_compare,
		priority_update_index, NULL, priority_realloc,
		priority_free, sched) != 0) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	if (qhw_hash_table_init(&state->by_id, PRIORITY_BUCKETS,
		priority_alloc, priority_free, sched) != 0) {
		qhw_heap_fini(&state->ready);
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void priority_fini(void *policy_state)
{
	struct priority_state *state = policy_state;
	struct priority_task *task;

	if (state == NULL) {
		return;
	}

	while ((task = qhw_heap_pop(&state->ready)) != NULL) {
		qhw_sched_free(state->sched, task);
	}
	qhw_heap_fini(&state->ready);
	qhw_hash_table_fini(&state->by_id, NULL, NULL);
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t priority_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct priority_state *state = policy_state;
	struct priority_task *item;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	item = qhw_sched_alloc(state->sched, sizeof(*item));
	if (item == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	memset(item, 0, sizeof(*item));

	item->desc = *task;
	item->seq = state->next_seq++;
	if (qhw_hash_table_insert(&state->by_id, task->task_id, item) != 0) {
		qhw_sched_free(state->sched, item);
		return QHW_SCHED_ERR_EXISTS;
	}

	if (qhw_heap_push(&state->ready, item) != 0) {
		(void)qhw_hash_table_remove(&state->by_id, task->task_id);
		qhw_sched_free(state->sched, item);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t priority_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct priority_state *state = policy_state;
	struct priority_task *task;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_heap_pop(&state->ready);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	(void)qhw_hash_table_remove(&state->by_id, task->desc.task_id);
	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->struct_size = sizeof(*out_assignment);
	out_assignment->task_id = task->desc.task_id;
	out_assignment->payload = task->desc.payload;
	out_assignment->payload_size = task->desc.payload_size;
	out_assignment->estimated_runtime_ns = task->desc.estimated_runtime_ns;
	qhw_sched_free(state->sched, task);
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

static qhw_sched_rc_t priority_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	struct priority_state *state = policy_state;
	struct priority_task *task;
	int64_t old_priority;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_hash_table_find(&state->by_id, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	old_priority = task->desc.priority;
	task->desc.priority = priority;
	if (qhw_heap_reheapify_at(&state->ready, task->heap_index) != 0) {
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
	struct priority_task *task;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_hash_table_find(&state->by_id, task_id);
	if (task == NULL) {
		return QHW_SCHED_OK;
	}

	(void)qhw_hash_table_remove(&state->by_id, task_id);
	(void)qhw_heap_remove_at(&state->ready, task->heap_index);
	qhw_sched_free(state->sched, task);
	return QHW_SCHED_OK;
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
	.on_task_priority_changed = priority_on_task_priority_changed,
	.on_task_started = priority_on_task_started,
	.on_task_finished = priority_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &priority_desc;
}
