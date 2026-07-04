#include "qhw_scheduler_internal.h"
#include "qhw_ds_error.h"

#include <stdint.h>
#include <string.h>

#define QHW_TASK_BUCKETS 1024U

static void *util_alloc(size_t size, void *user_data)
{
	return qhw_alloc(user_data, size);
}

static void util_free(void *ptr, void *user_data)
{
	qhw_free(user_data, ptr);
}

static void task_record_free(void *value, void *user_data)
{
	struct qhw_task_record *record = value;
	struct qhw_allocator *allocator = user_data;

	if (record == NULL) {
		return;
	}

	qhw_free(allocator, record->metadata);
	qhw_free(allocator, record);
}

qhw_sched_rc_t qhw_task_table_init(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator)
{
	if (table == NULL || allocator == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_hash_table_init(&table->by_id, QHW_TASK_BUCKETS,
		util_alloc, util_free, allocator) != 0) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	qhw_list_init(&table->enqueue_order);
	table->count = 0;
	return QHW_SCHED_OK;
}

void qhw_task_table_fini(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator)
{
	if (table == NULL) {
		return;
	}

	qhw_hash_table_fini(&table->by_id, task_record_free, allocator);
	table->count = 0;
}

qhw_sched_rc_t qhw_task_table_insert_state(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	const qhw_sched_task_desc_t *task,
	uint64_t enqueue_seq,
	qhw_sched_task_state_t state)
{
	struct qhw_task_record *record;
	int rc;

	if (table == NULL || allocator == NULL || task == NULL ||
		task->task_id == QHW_SCHED_INVALID_TASK_ID) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	record = qhw_alloc(allocator, sizeof(*record));
	if (record == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	memset(record, 0, sizeof(*record));
	record->desc = *task;
	record->state = state;
	record->enqueue_seq = enqueue_seq;
	qhw_list_init(&record->enqueue_link);

	if (qhw_hash_table_find(&table->by_id, task->task_id) != NULL) {
		qhw_free(allocator, record);
		return QHW_SCHED_ERR_EXISTS;
	}

	if (task->metadata_count > 0) {
		size_t bytes;

		if (task->metadata == NULL ||
			task->metadata_count > (size_t)-1 / sizeof(*task->metadata)) {
			qhw_free(allocator, record);
			return QHW_SCHED_ERR_INVALID_ARG;
		}

		bytes = task->metadata_count * sizeof(*task->metadata);
		record->metadata = qhw_alloc(allocator, bytes);
		if (record->metadata == NULL) {
			qhw_free(allocator, record);
			return QHW_SCHED_ERR_NO_MEMORY;
		}

		memcpy(record->metadata, task->metadata, bytes);
		record->desc.metadata = record->metadata;
	} else {
		record->desc.metadata = NULL;
	}

	rc = qhw_hash_table_insert(&table->by_id, task->task_id, record);
	if (rc != 0) {
		task_record_free(record, allocator);
		return qhw_hash_insert_rc_to_sched_rc(rc);
	}

	qhw_list_push_back(&table->enqueue_order, &record->enqueue_link);
	table->count++;
	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_task_table_insert(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	const qhw_sched_task_desc_t *task,
	uint64_t enqueue_seq)
{
	return qhw_task_table_insert_state(table, allocator, task, enqueue_seq,
		QHW_SCHED_TASK_QUEUED);
}

void qhw_task_table_remove(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	qhw_sched_task_id_t task_id)
{
	struct qhw_task_record *record;

	if (table == NULL || allocator == NULL) {
		return;
	}

	record = qhw_hash_table_remove(&table->by_id, task_id);
	if (record == NULL) {
		return;
	}

	qhw_list_remove(&record->enqueue_link);
	task_record_free(record, allocator);
	if (table->count > 0) {
		table->count--;
	}
}

struct qhw_task_record *qhw_task_table_find(
	struct qhw_task_table *table,
	qhw_sched_task_id_t task_id)
{
	if (table == NULL) {
		return NULL;
	}

	return qhw_hash_table_find(&table->by_id, task_id);
}

qhw_sched_rc_t qhw_task_table_set_state(
	struct qhw_task_table *table,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t state)
{
	struct qhw_task_record *record;

	record = qhw_task_table_find(table, task_id);
	if (record == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	record->state = state;
	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_task_table_for_each_queued(
	struct qhw_task_table *table,
	qhw_task_record_fn fn,
	void *user_data)
{
	struct qhw_list_node *node;

	if (table == NULL || fn == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	node = table->enqueue_order.next;
	while (node != &table->enqueue_order) {
		struct qhw_task_record *record;
		qhw_sched_rc_t rc;

		record = qhw_container_of(node, struct qhw_task_record,
			enqueue_link);
		node = node->next;
		if (record->state != QHW_SCHED_TASK_QUEUED) {
			continue;
		}

		rc = fn(record, user_data);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return QHW_SCHED_OK;
}
