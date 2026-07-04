#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "policy/qhw_group_map.h"
#include "qhw_ds_error.h"

#include <stdint.h>
#include <string.h>
#include <qhw_datastructures/qhw_hash_table.h>
#include <qhw_datastructures/qhw_list.h>

#define RR_BUCKETS 4096U

struct rr_group {
	struct qhw_group_key key;
	struct qhw_list_node ready;
	struct qhw_list_node active_link;
	uint64_t ready_count;
};

struct rr_task {
	qhw_sched_task_desc_t desc;
	struct rr_group *group;
	struct qhw_list_node group_link;
};

struct rr_state {
	qhw_sched_t *sched;
	struct qhw_group_map groups;
	struct qhw_hash_table tasks;
	struct qhw_list_node active_groups;
	qhw_sched_split_config_t split_config;
};

static void *rr_alloc(size_t size, void *user_data)
{
	return qhw_sched_alloc(user_data, size);
}

static void rr_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static struct rr_group *rr_group_find(
	struct rr_state *state,
	struct qhw_group_key key)
{
	return qhw_group_map_find(&state->groups, key);
}

static void rr_group_free(struct rr_state *state, struct rr_group *group)
{
	if (group == NULL) {
		return;
	}

	(void)qhw_group_map_remove(&state->groups, group->key);
	qhw_sched_free(state->sched, group);
}

static struct rr_group *rr_group_create(
	struct rr_state *state,
	struct qhw_group_key key)
{
	struct rr_group *group;

	group = qhw_sched_alloc(state->sched, sizeof(*group));
	if (group == NULL) {
		return NULL;
	}

	memset(group, 0, sizeof(*group));
	group->key = key;
	qhw_list_init(&group->ready);
	qhw_list_init(&group->active_link);

	if (qhw_group_map_insert(&state->groups, key, group) !=
		QHW_SCHED_OK) {
		qhw_sched_free(state->sched, group);
		return NULL;
	}

	return group;
}

static struct rr_group *rr_group_get(
	struct rr_state *state,
	struct qhw_group_key key)
{
	struct rr_group *group;

	group = rr_group_find(state, key);
	if (group != NULL) {
		return group;
	}

	return rr_group_create(state, key);
}

static void rr_task_free(struct rr_state *state, struct rr_task *task)
{
	if (task == NULL) {
		return;
	}

	qhw_sched_free(state->sched, task);
}

static void rr_task_value_free(void *value, void *user_data)
{
	rr_task_free(user_data, value);
}

static void rr_group_value_free(void *value, void *user_data)
{
	struct rr_state *state = user_data;

	qhw_sched_free(state->sched, value);
}

static qhw_sched_rc_t rr_remove_ready_task(
	struct rr_state *state,
	qhw_sched_task_id_t task_id)
{
	struct rr_task *task;
	struct rr_group *group;

	task = qhw_hash_table_find(&state->tasks, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	group = task->group;
	if (group == NULL || group->ready_count == 0) {
		return QHW_SCHED_ERR_STATE;
	}

	qhw_list_remove(&task->group_link);
	(void)qhw_hash_table_remove(&state->tasks, task_id);
	rr_task_free(state, task);

	group->ready_count--;
	if (group->ready_count == 0) {
		qhw_list_remove(&group->active_link);
		rr_group_free(state, group);
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct rr_state *state;
	qhw_sched_rc_t rc;

	if (sched == NULL || out_policy_state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	state = qhw_sched_alloc(sched, sizeof(*state));
	if (state == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	memset(state, 0, sizeof(*state));
	state->sched = sched;
	qhw_list_init(&state->active_groups);

	if (qhw_group_map_init(&state->groups, sched) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	if (qhw_hash_table_init(&state->tasks, RR_BUCKETS, rr_alloc,
		rr_free, sched) != 0) {
		qhw_group_map_fini(&state->groups, rr_group_value_free, state);
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	qhw_sched_split_config_init(&state->split_config);
	rc = qhw_sched_split_config_parse_options(&state->split_config,
		options, option_count);
	if (rc != QHW_SCHED_OK) {
		qhw_hash_table_fini(&state->tasks, NULL, NULL);
		qhw_group_map_fini(&state->groups, rr_group_value_free, state);
		qhw_sched_free(sched, state);
		return rc;
	}

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void rr_fini(void *policy_state)
{
	struct rr_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	qhw_hash_table_fini(&state->tasks, rr_task_value_free, state);
	qhw_group_map_fini(&state->groups, rr_group_value_free, state);
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t rr_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct rr_state *state = policy_state;
	struct rr_group *group;
	struct rr_task *item;
	struct qhw_group_key key;
	int insert_rc;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_hash_table_find(&state->tasks, task->task_id) != NULL) {
		return QHW_SCHED_ERR_EXISTS;
	}

	key = qhw_group_key_from_task(task);
	group = rr_group_get(state, key);
	if (group == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	item = qhw_sched_alloc(state->sched, sizeof(*item));
	if (item == NULL) {
		if (group->ready_count == 0) {
			rr_group_free(state, group);
		}
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	memset(item, 0, sizeof(*item));
	item->desc = *task;
	item->group = group;
	qhw_list_init(&item->group_link);

	insert_rc = qhw_hash_table_insert(&state->tasks, task->task_id,
		item);
	if (insert_rc != QHW_HASH_TABLE_OK) {
		rr_task_free(state, item);
		if (group->ready_count == 0) {
			rr_group_free(state, group);
		}
		return qhw_hash_insert_rc_to_sched_rc(insert_rc);
	}

	if (group->ready_count == 0) {
		qhw_list_push_back(&state->active_groups, &group->active_link);
	}
	qhw_list_push_back(&group->ready, &item->group_link);
	group->ready_count++;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct rr_state *state = policy_state;
	struct qhw_list_node *group_node;
	struct qhw_list_node *task_node;
	struct rr_group *group;
	struct rr_task *task;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	group_node = qhw_list_pop_front(&state->active_groups);
	if (group_node == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	group = qhw_container_of(group_node, struct rr_group, active_link);
	task_node = qhw_list_pop_front(&group->ready);
	if (task_node == NULL || group->ready_count == 0) {
		rr_group_free(state, group);
		return QHW_SCHED_ERR_STATE;
	}

	task = qhw_container_of(task_node, struct rr_task, group_link);
	group->ready_count--;
	(void)qhw_hash_table_remove(&state->tasks, task->desc.task_id);

	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->task_id = task->desc.task_id;
	rr_task_free(state, task);

	if (group->ready_count > 0) {
		qhw_list_push_back(&state->active_groups,
			&group->active_link);
	} else {
		rr_group_free(state, group);
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_get_split_config(
	void *policy_state,
	qhw_sched_split_config_t *out_config)
{
	struct rr_state *state = policy_state;

	if (state == NULL || out_config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_config = state->split_config;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	(void)policy_state;
	(void)task_id;
	(void)priority;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t rr_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	struct rr_state *state = policy_state;
	qhw_sched_rc_t rc;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = rr_remove_ready_task(state, task_id);
	return rc == QHW_SCHED_ERR_NOT_FOUND ? QHW_SCHED_OK : rc;
}

static const qhw_sched_plugin_desc_t rr_desc = {
	.struct_size = sizeof(rr_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "round_robin",
	.version = "0.1.0",
	.description = "round-robin scheduler policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = rr_init,
	.fini = rr_fini,
	.on_task_submit = rr_on_task_submit,
	.select_next = rr_select_next,
	.get_split_config = rr_get_split_config,
	.on_task_priority_changed = rr_on_task_priority_changed,
	.on_task_started = rr_on_task_started,
	.on_task_finished = rr_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &rr_desc;
}
