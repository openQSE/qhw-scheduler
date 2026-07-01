#include "qhw_scheduler_internal.h"

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

qhw_sched_rc_t qhw_task_table_insert(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	const qhw_sched_task_desc_t *task,
	uint64_t enqueue_seq)
{
	struct qhw_task_record *record;

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
	record->state = QHW_SCHED_TASK_QUEUED;
	record->enqueue_seq = enqueue_seq;

	if (task->metadata_count > 0) {
		size_t bytes = task->metadata_count * sizeof(*task->metadata);

		record->metadata = qhw_alloc(allocator, bytes);
		if (record->metadata == NULL) {
			qhw_free(allocator, record);
			return QHW_SCHED_ERR_NO_MEMORY;
		}

		memcpy(record->metadata, task->metadata, bytes);
		record->desc.metadata = record->metadata;
	}

	if (qhw_hash_table_insert(&table->by_id, task->task_id, record) != 0) {
		task_record_free(record, allocator);
		return QHW_SCHED_ERR_EXISTS;
	}

	table->count++;
	return QHW_SCHED_OK;
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

