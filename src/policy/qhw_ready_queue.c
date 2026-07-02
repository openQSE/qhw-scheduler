#include "policy/qhw_ready_queue.h"

#include <stdint.h>
#include <string.h>

#define QHW_READY_QUEUE_BUCKETS 4096U

static void *ready_alloc(size_t size, void *user_data)
{
	return qhw_sched_alloc(user_data, size);
}

static void *ready_realloc(void *ptr, size_t size, void *user_data)
{
	return qhw_sched_realloc(user_data, ptr, size);
}

static void ready_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static int ready_heap_compare(const void *left_arg, const void *right_arg)
{
	const struct qhw_ready_task *left = left_arg;
	const struct qhw_ready_task *right = right_arg;

	return left->queue->compare(left, right);
}

static void ready_heap_update_index(
	void *item,
	size_t index,
	void *user_data)
{
	struct qhw_ready_task *task = item;

	(void)user_data;
	task->heap_index = index;
}

static void ready_task_free(struct qhw_ready_queue *queue,
	struct qhw_ready_task *task)
{
	if (task == NULL) {
		return;
	}

	qhw_sched_free(queue->sched, task);
}

qhw_sched_rc_t qhw_ready_queue_init(
	struct qhw_ready_queue *queue,
	qhw_sched_t *sched,
	enum qhw_ready_queue_kind kind,
	qhw_ready_queue_compare_fn compare)
{
	if (queue == NULL || sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (kind != QHW_READY_QUEUE_FIFO && kind != QHW_READY_QUEUE_HEAP) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (kind == QHW_READY_QUEUE_HEAP && compare == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	memset(queue, 0, sizeof(*queue));
	queue->sched = sched;
	queue->kind = kind;
	queue->compare = compare;
	queue->next_seq = 1;
	qhw_list_init(&queue->fifo);

	if (qhw_hash_table_init(&queue->by_id, QHW_READY_QUEUE_BUCKETS,
		ready_alloc, ready_free, sched) != 0) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	if (kind == QHW_READY_QUEUE_HEAP &&
		qhw_heap_init(&queue->heap, ready_heap_compare,
			ready_heap_update_index, NULL, ready_realloc,
			ready_free, sched) != 0) {
		qhw_hash_table_fini(&queue->by_id, NULL, NULL);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	return QHW_SCHED_OK;
}

void qhw_ready_queue_fini(struct qhw_ready_queue *queue)
{
	if (queue == NULL) {
		return;
	}

	if (queue->kind == QHW_READY_QUEUE_HEAP) {
		struct qhw_ready_task *task;

		while ((task = qhw_heap_pop(&queue->heap)) != NULL) {
			ready_task_free(queue, task);
		}
		qhw_heap_fini(&queue->heap);
	} else {
		while (!qhw_list_empty(&queue->fifo)) {
			struct qhw_list_node *node;
			struct qhw_ready_task *task;

			node = qhw_list_pop_front(&queue->fifo);
			task = qhw_container_of(node, struct qhw_ready_task,
				link);
			ready_task_free(queue, task);
		}
	}

	qhw_hash_table_fini(&queue->by_id, NULL, NULL);
	memset(queue, 0, sizeof(*queue));
}

qhw_sched_rc_t qhw_ready_queue_insert(
	struct qhw_ready_queue *queue,
	const qhw_sched_task_desc_t *task)
{
	struct qhw_ready_task *item;

	if (queue == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	item = qhw_sched_alloc(queue->sched, sizeof(*item));
	if (item == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	memset(item, 0, sizeof(*item));

	item->desc = *task;
	item->base_priority = task->priority;
	item->effective_priority = task->priority;
	item->next_refresh_ns = UINT64_MAX;
	item->seq = queue->next_seq++;
	item->refresh_heap_index = (size_t)-1;
	item->queue = queue;
	qhw_list_init(&item->link);

	if (qhw_hash_table_insert(&queue->by_id, task->task_id, item) != 0) {
		ready_task_free(queue, item);
		return QHW_SCHED_ERR_EXISTS;
	}

	if (queue->kind == QHW_READY_QUEUE_HEAP) {
		if (qhw_heap_push(&queue->heap, item) != 0) {
			(void)qhw_hash_table_remove(&queue->by_id,
				task->task_id);
			ready_task_free(queue, item);
			return QHW_SCHED_ERR_NO_MEMORY;
		}
	} else {
		qhw_list_push_back(&queue->fifo, &item->link);
	}

	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_ready_queue_pop(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t *out_task_id)
{
	struct qhw_ready_task *task;

	if (queue == NULL || out_task_id == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (queue->kind == QHW_READY_QUEUE_HEAP) {
		task = qhw_heap_pop(&queue->heap);
	} else {
		struct qhw_list_node *node;

		node = qhw_list_pop_front(&queue->fifo);
		if (node == NULL) {
			task = NULL;
		} else {
			task = qhw_container_of(node, struct qhw_ready_task,
				link);
		}
	}

	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	*out_task_id = task->desc.task_id;
	(void)qhw_hash_table_remove(&queue->by_id, task->desc.task_id);
	ready_task_free(queue, task);
	return QHW_SCHED_OK;
}

struct qhw_ready_task *qhw_ready_queue_peek(
	struct qhw_ready_queue *queue)
{
	struct qhw_list_node *node;

	if (queue == NULL) {
		return NULL;
	}

	if (queue->kind == QHW_READY_QUEUE_HEAP) {
		return qhw_heap_peek(&queue->heap);
	}

	node = queue->fifo.next;
	if (node == &queue->fifo) {
		return NULL;
	}

	return qhw_container_of(node, struct qhw_ready_task, link);
}

struct qhw_ready_task *qhw_ready_queue_find(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t task_id)
{
	if (queue == NULL) {
		return NULL;
	}

	return qhw_hash_table_find(&queue->by_id, task_id);
}

qhw_sched_rc_t qhw_ready_queue_remove(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t task_id)
{
	struct qhw_ready_task *task;

	if (queue == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = qhw_hash_table_find(&queue->by_id, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	if (queue->kind == QHW_READY_QUEUE_HEAP) {
		if (qhw_heap_remove_at(&queue->heap,
			task->heap_index) != task) {
			return QHW_SCHED_ERR_STATE;
		}
	} else {
		qhw_list_remove(&task->link);
	}
	(void)qhw_hash_table_remove(&queue->by_id, task_id);
	ready_task_free(queue, task);
	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_ready_queue_reorder(
	struct qhw_ready_queue *queue,
	struct qhw_ready_task *task)
{
	if (queue == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (queue->kind != QHW_READY_QUEUE_HEAP) {
		return QHW_SCHED_OK;
	}

	if (qhw_heap_reheapify_at(&queue->heap, task->heap_index) != 0) {
		return QHW_SCHED_ERR_STATE;
	}

	return QHW_SCHED_OK;
}
