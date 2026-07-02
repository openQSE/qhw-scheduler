#include "policy/qhw_deadline_refresh.h"
#include "policy/qhw_deadline_boost.h"

#include <string.h>

static void *refresh_realloc(void *ptr, size_t size, void *user_data)
{
	return qhw_sched_realloc(user_data, ptr, size);
}

static void refresh_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static int refresh_compare(const void *left_arg, const void *right_arg)
{
	const struct qhw_ready_task *left = left_arg;
	const struct qhw_ready_task *right = right_arg;

	if (left->next_refresh_ns != right->next_refresh_ns) {
		return left->next_refresh_ns < right->next_refresh_ns ? -1 : 1;
	}

	if (left->seq != right->seq) {
		return left->seq < right->seq ? -1 : 1;
	}

	if (left->desc.task_id == right->desc.task_id) {
		return 0;
	}

	return left->desc.task_id < right->desc.task_id ? -1 : 1;
}

static void refresh_update_index(
	void *item,
	size_t index,
	void *user_data)
{
	struct qhw_ready_task *task = item;

	(void)user_data;
	task->refresh_heap_index = index;
}

qhw_sched_rc_t qhw_deadline_refresh_init(
	struct qhw_deadline_refresh_queue *queue,
	qhw_sched_t *sched)
{
	if (queue == NULL || sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	memset(queue, 0, sizeof(*queue));
	queue->sched = sched;
	if (qhw_heap_init(&queue->heap, refresh_compare,
		refresh_update_index, NULL, refresh_realloc,
		refresh_free, sched) != 0) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	return QHW_SCHED_OK;
}

void qhw_deadline_refresh_fini(
	struct qhw_deadline_refresh_queue *queue)
{
	if (queue == NULL) {
		return;
	}

	qhw_heap_fini(&queue->heap);
	memset(queue, 0, sizeof(*queue));
}

qhw_sched_rc_t qhw_deadline_refresh_insert(
	struct qhw_deadline_refresh_queue *queue,
	struct qhw_ready_task *task)
{
	if (queue == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task->refresh_heap_index = (size_t)-1;
	if (task->next_refresh_ns == QHW_DEADLINE_BOOST_NEVER) {
		return QHW_SCHED_OK;
	}

	if (qhw_heap_push(&queue->heap, task) != 0) {
		task->refresh_heap_index = (size_t)-1;
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	return QHW_SCHED_OK;
}

void qhw_deadline_refresh_remove(
	struct qhw_deadline_refresh_queue *queue,
	struct qhw_ready_task *task)
{
	if (queue == NULL || task == NULL ||
		task->refresh_heap_index == (size_t)-1) {
		return;
	}

	(void)qhw_heap_remove_at(&queue->heap, task->refresh_heap_index);
	task->refresh_heap_index = (size_t)-1;
	task->next_refresh_ns = QHW_DEADLINE_BOOST_NEVER;
}

struct qhw_ready_task *qhw_deadline_refresh_pop_expired(
	struct qhw_deadline_refresh_queue *queue,
	uint64_t now_ns)
{
	struct qhw_ready_task *task;

	if (queue == NULL) {
		return NULL;
	}

	task = qhw_heap_peek(&queue->heap);
	if (task == NULL || task->next_refresh_ns > now_ns) {
		return NULL;
	}

	task = qhw_heap_pop(&queue->heap);
	if (task != NULL) {
		task->refresh_heap_index = (size_t)-1;
		task->next_refresh_ns = QHW_DEADLINE_BOOST_NEVER;
	}
	return task;
}
