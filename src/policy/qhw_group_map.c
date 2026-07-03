#include "policy/qhw_group_map.h"

#include <string.h>

#define QHW_GROUP_MAP_BUCKETS 4096U

static void *group_map_alloc(size_t size, void *user_data)
{
	return qhw_sched_alloc(user_data, size);
}

static void group_map_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static struct qhw_hash_table *group_map_table(
	struct qhw_group_map *map,
	enum qhw_group_kind kind)
{
	switch (kind) {
	case QHW_GROUP_RESERVATION:
		return &map->reservation_groups;
	case QHW_GROUP_JOB:
		return &map->job_groups;
	case QHW_GROUP_TASK:
		return &map->task_groups;
	default:
		return NULL;
	}
}

struct qhw_group_key qhw_group_key_from_task(
	const qhw_sched_task_desc_t *task)
{
	struct qhw_group_key key;

	memset(&key, 0, sizeof(key));
	if (task == NULL) {
		return key;
	}

	if (task->reservation_id != 0) {
		key.kind = QHW_GROUP_RESERVATION;
		key.id = task->reservation_id;
		return key;
	}

	if (task->job_id != 0) {
		key.kind = QHW_GROUP_JOB;
		key.id = task->job_id;
		return key;
	}

	key.kind = QHW_GROUP_TASK;
	key.id = task->task_id;
	return key;
}

qhw_sched_rc_t qhw_group_map_init(
	struct qhw_group_map *map,
	qhw_sched_t *sched)
{
	if (map == NULL || sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	memset(map, 0, sizeof(*map));
	map->sched = sched;

	if (qhw_hash_table_init(&map->reservation_groups,
		QHW_GROUP_MAP_BUCKETS, group_map_alloc, group_map_free,
		sched) != 0) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	if (qhw_hash_table_init(&map->job_groups,
		QHW_GROUP_MAP_BUCKETS, group_map_alloc, group_map_free,
		sched) != 0) {
		qhw_hash_table_fini(&map->reservation_groups, NULL, NULL);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	if (qhw_hash_table_init(&map->task_groups,
		QHW_GROUP_MAP_BUCKETS, group_map_alloc, group_map_free,
		sched) != 0) {
		qhw_hash_table_fini(&map->job_groups, NULL, NULL);
		qhw_hash_table_fini(&map->reservation_groups, NULL, NULL);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	return QHW_SCHED_OK;
}

void qhw_group_map_fini(
	struct qhw_group_map *map,
	void (*free_value)(void *value, void *user_data),
	void *user_data)
{
	if (map == NULL) {
		return;
	}

	qhw_hash_table_fini(&map->reservation_groups, free_value, user_data);
	qhw_hash_table_fini(&map->job_groups, free_value, user_data);
	qhw_hash_table_fini(&map->task_groups, free_value, user_data);
	memset(map, 0, sizeof(*map));
}

void *qhw_group_map_find(
	struct qhw_group_map *map,
	struct qhw_group_key key)
{
	struct qhw_hash_table *table;

	table = group_map_table(map, key.kind);
	if (table == NULL) {
		return NULL;
	}

	return qhw_hash_table_find(table, key.id);
}

qhw_sched_rc_t qhw_group_map_insert(
	struct qhw_group_map *map,
	struct qhw_group_key key,
	void *value)
{
	struct qhw_hash_table *table;

	if (value == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	table = group_map_table(map, key.kind);
	if (table == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_hash_table_insert(table, key.id, value) != 0) {
		return QHW_SCHED_ERR_EXISTS;
	}

	return QHW_SCHED_OK;
}

void *qhw_group_map_remove(
	struct qhw_group_map *map,
	struct qhw_group_key key)
{
	struct qhw_hash_table *table;

	table = group_map_table(map, key.kind);
	if (table == NULL) {
		return NULL;
	}

	return qhw_hash_table_remove(table, key.id);
}
