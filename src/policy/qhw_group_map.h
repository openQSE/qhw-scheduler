#ifndef QHW_GROUP_MAP_H
#define QHW_GROUP_MAP_H

#include "qhw_scheduler/qhw_scheduler_plugin.h"

#include <stdint.h>
#include <qhw_datastructures/qhw_hash_table.h>

enum qhw_group_kind {
	QHW_GROUP_RESERVATION = 1,
	QHW_GROUP_JOB = 2,
	QHW_GROUP_TASK = 3
};

struct qhw_group_key {
	enum qhw_group_kind kind;
	uint64_t id;
};

struct qhw_group_map {
	qhw_sched_t *sched;
	struct qhw_hash_table reservation_groups;
	struct qhw_hash_table job_groups;
	struct qhw_hash_table task_groups;
};

struct qhw_group_key qhw_group_key_from_task(
	const qhw_sched_task_desc_t *task);

qhw_sched_rc_t qhw_group_map_init(
	struct qhw_group_map *map,
	qhw_sched_t *sched);

void qhw_group_map_fini(
	struct qhw_group_map *map,
	void (*free_value)(void *value, void *user_data),
	void *user_data);

void *qhw_group_map_find(
	struct qhw_group_map *map,
	struct qhw_group_key key);

qhw_sched_rc_t qhw_group_map_insert(
	struct qhw_group_map *map,
	struct qhw_group_key key,
	void *value);

void *qhw_group_map_remove(
	struct qhw_group_map *map,
	struct qhw_group_key key);

#endif
