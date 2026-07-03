#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "policy/qhw_deadline_refresh.h"
#include "policy/qhw_group_map.h"
#include "policy/qhw_order_key.h"
#include "policy/qhw_ready_queue.h"
#include "util/qhw_hash_table.h"
#include "util/qhw_heap.h"

#include <string.h>

#define ORDERED_GROUP_TASK_BUCKETS 4096U

struct ordered_state;

struct ordered_group {
	struct ordered_state *state;
	struct qhw_group_key key;
	struct qhw_ready_queue ready;
	size_t heap_index;
	uint64_t ticket;
};

struct ordered_task_index {
	struct ordered_group *group;
};

struct ordered_state {
	qhw_sched_t *sched;
	struct qhw_order_config order;
	struct qhw_ready_queue ready;
	struct qhw_deadline_refresh_queue refresh;
	struct qhw_group_map groups;
	struct qhw_hash_table group_tasks;
	struct qhw_heap group_heap;
	qhw_sched_split_config_t split_config;
	uint32_t ready_flags;
	uint64_t next_group_ticket;
	int use_round_robin;
};

static int ordered_compare(
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right,
	void *user_data)
{
	return qhw_order_compare(user_data, left, right);
}

static void *ordered_alloc(size_t size, void *user_data)
{
	return qhw_sched_alloc(user_data, size);
}

static void *ordered_realloc(void *ptr, size_t size, void *user_data)
{
	return qhw_sched_realloc(user_data, ptr, size);
}

static void ordered_free(void *ptr, void *user_data)
{
	qhw_sched_free(user_data, ptr);
}

static int ordered_group_compare(const void *left_arg, const void *right_arg)
{
	const struct ordered_group *left = left_arg;
	const struct ordered_group *right = right_arg;
	struct qhw_ready_task *left_task;
	struct qhw_ready_task *right_task;

	left_task = qhw_ready_queue_peek((struct qhw_ready_queue *)&left->ready);
	right_task = qhw_ready_queue_peek(
		(struct qhw_ready_queue *)&right->ready);
	return qhw_order_compare_groups(&left->state->order, left_task,
		right_task, left->ticket, right->ticket);
}

static void ordered_group_update_index(
	void *item,
	size_t index,
	void *user_data)
{
	struct ordered_group *group = item;

	(void)user_data;
	group->heap_index = index;
}

static qhw_sched_rc_t ordered_group_reheapify(
	struct ordered_state *state,
	struct ordered_group *group)
{
	if (state == NULL || group == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (group->heap_index == (size_t)-1) {
		return QHW_SCHED_OK;
	}

	if (qhw_heap_reheapify_at(&state->group_heap,
		group->heap_index) != 0) {
		return QHW_SCHED_ERR_STATE;
	}

	return QHW_SCHED_OK;
}

static void ordered_group_free(
	struct ordered_state *state,
	struct ordered_group *group)
{
	if (group == NULL) {
		return;
	}

	qhw_ready_queue_fini(&group->ready);
	qhw_sched_free(state->sched, group);
}

static void ordered_group_value_free(void *value, void *user_data)
{
	ordered_group_free(user_data, value);
}

static struct ordered_group *ordered_group_create(
	struct ordered_state *state,
	struct qhw_group_key key)
{
	struct ordered_group *group;

	group = qhw_sched_alloc(state->sched, sizeof(*group));
	if (group == NULL) {
		return NULL;
	}
	memset(group, 0, sizeof(*group));
	group->state = state;
	group->key = key;
	group->heap_index = (size_t)-1;
	group->ticket = state->next_group_ticket++;

	if (qhw_ready_queue_init(&group->ready, state->sched,
		QHW_READY_QUEUE_HEAP, state->ready_flags,
		ordered_compare, &state->order) != QHW_SCHED_OK) {
		qhw_sched_free(state->sched, group);
		return NULL;
	}

	if (qhw_group_map_insert(&state->groups, key, group) !=
		QHW_SCHED_OK) {
		qhw_ready_queue_fini(&group->ready);
		qhw_sched_free(state->sched, group);
		return NULL;
	}

	return group;
}

static struct ordered_group *ordered_group_get(
	struct ordered_state *state,
	struct qhw_group_key key)
{
	struct ordered_group *group;

	group = qhw_group_map_find(&state->groups, key);
	if (group != NULL) {
		return group;
	}

	return ordered_group_create(state, key);
}

static void ordered_group_destroy(
	struct ordered_state *state,
	struct ordered_group *group)
{
	if (group == NULL) {
		return;
	}

	if (group->heap_index != (size_t)-1) {
		(void)qhw_heap_remove_at(&state->group_heap,
			group->heap_index);
		group->heap_index = (size_t)-1;
	}
	(void)qhw_group_map_remove(&state->groups, group->key);
	ordered_group_free(state, group);
}

static void ordered_task_index_free(void *value, void *user_data)
{
	qhw_sched_free(user_data, value);
}

static struct ordered_group *ordered_task_group(
	struct ordered_state *state,
	qhw_sched_task_id_t task_id)
{
	struct ordered_task_index *index;

	index = qhw_hash_table_find(&state->group_tasks, task_id);
	if (index == NULL) {
		return NULL;
	}

	return index->group;
}

static struct qhw_ready_task *ordered_find_ready_task(
	struct ordered_state *state,
	qhw_sched_task_id_t task_id)
{
	struct ordered_group *group;

	if (!state->use_round_robin) {
		return qhw_ready_queue_find(&state->ready, task_id);
	}

	group = ordered_task_group(state, task_id);
	if (group == NULL) {
		return NULL;
	}

	return qhw_ready_queue_find(&group->ready, task_id);
}

static qhw_sched_rc_t ordered_remove_group_task(
	struct ordered_state *state,
	qhw_sched_task_id_t task_id)
{
	struct ordered_task_index *index;
	struct ordered_group *group;
	qhw_sched_rc_t rc;

	index = qhw_hash_table_find(&state->group_tasks, task_id);
	if (index == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	group = index->group;
	rc = qhw_ready_queue_remove(&group->ready, task_id);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	(void)qhw_hash_table_remove(&state->group_tasks, task_id);
	qhw_sched_free(state->sched, index);
	if (qhw_ready_queue_peek(&group->ready) == NULL) {
		ordered_group_destroy(state, group);
		return QHW_SCHED_OK;
	}

	return ordered_group_reheapify(state, group);
}

static qhw_sched_rc_t ordered_refresh_task(
	struct ordered_state *state,
	struct qhw_ready_task *task)
{
	qhw_sched_rc_t rc;
	int64_t old_effective_priority;
	struct ordered_group *group = NULL;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (state->use_round_robin) {
		group = ordered_task_group(state, task->desc.task_id);
	}

	old_effective_priority = task->effective_priority;
	qhw_deadline_refresh_remove(&state->refresh, task);
	rc = qhw_order_refresh_task(&state->order, task);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	if (old_effective_priority != task->effective_priority) {
		if (group != NULL) {
			rc = qhw_ready_queue_reorder(&group->ready, task);
		} else {
			rc = qhw_ready_queue_reorder(&state->ready, task);
		}
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}
	if (group != NULL) {
		rc = ordered_group_reheapify(state, group);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return qhw_deadline_refresh_insert(&state->refresh, task);
}

static qhw_sched_rc_t ordered_refresh_expired(struct ordered_state *state)
{
	uint64_t now_ns = qhw_order_now_ns(&state->order);
	struct qhw_ready_task *task;
	qhw_sched_rc_t rc;

	while ((task = qhw_deadline_refresh_pop_expired(&state->refresh,
		now_ns)) != NULL) {
		rc = ordered_refresh_task(state, task);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_init(
	qhw_sched_t *sched,
	const qhw_sched_kv_t *options,
	size_t option_count,
	void **out_policy_state)
{
	struct ordered_state *state;
	uint32_t ready_flags = 0;

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
	qhw_order_config_init(&state->order);
	if (qhw_sched_split_config_parse_options(&state->split_config,
		options, option_count) != QHW_SCHED_OK ||
		qhw_order_config_parse_options(&state->order, options,
			option_count) != QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (qhw_order_config_uses_cost(&state->order)) {
		ready_flags |= QHW_READY_QUEUE_F_ESTIMATE_COST;
	}
	state->ready_flags = ready_flags;
	state->next_group_ticket = 1;
	state->use_round_robin =
		qhw_order_config_uses_round_robin(&state->order);

	if (qhw_deadline_refresh_init(&state->refresh, sched) !=
		QHW_SCHED_OK) {
		qhw_sched_free(sched, state);
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	if (state->use_round_robin) {
		if (qhw_group_map_init(&state->groups, sched) !=
			QHW_SCHED_OK ||
			qhw_hash_table_init(&state->group_tasks,
				ORDERED_GROUP_TASK_BUCKETS, ordered_alloc,
				ordered_free, sched) != 0 ||
			qhw_heap_init(&state->group_heap,
				ordered_group_compare,
				ordered_group_update_index, NULL,
				ordered_realloc, ordered_free, sched) != 0) {
			qhw_hash_table_fini(&state->group_tasks, NULL, NULL);
			qhw_group_map_fini(&state->groups, NULL, NULL);
			qhw_deadline_refresh_fini(&state->refresh);
			qhw_sched_free(sched, state);
			return QHW_SCHED_ERR_NO_MEMORY;
		}
	} else {
		if (qhw_ready_queue_init(&state->ready, sched,
			QHW_READY_QUEUE_HEAP, ready_flags, ordered_compare,
			&state->order) != QHW_SCHED_OK) {
			qhw_deadline_refresh_fini(&state->refresh);
			qhw_sched_free(sched, state);
			return QHW_SCHED_ERR_NO_MEMORY;
		}
	}

	*out_policy_state = state;
	return QHW_SCHED_OK;
}

static void ordered_fini(void *policy_state)
{
	struct ordered_state *state = policy_state;

	if (state == NULL) {
		return;
	}

	qhw_deadline_refresh_fini(&state->refresh);
	if (state->use_round_robin) {
		qhw_hash_table_fini(&state->group_tasks,
			ordered_task_index_free, state->sched);
		qhw_heap_fini(&state->group_heap);
		qhw_group_map_fini(&state->groups, ordered_group_value_free,
			state);
	} else {
		qhw_ready_queue_fini(&state->ready);
	}
	qhw_sched_free(state->sched, state);
}

static qhw_sched_rc_t ordered_submit_round_robin(
	struct ordered_state *state,
	const qhw_sched_task_desc_t *task)
{
	struct ordered_group *group;
	struct ordered_task_index *index;
	struct qhw_ready_task *ready_task;
	struct qhw_group_key key;
	int group_was_empty;
	qhw_sched_rc_t rc;

	key = qhw_group_key_from_task(task);
	group = ordered_group_get(state, key);
	if (group == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	group_was_empty = qhw_ready_queue_peek(&group->ready) == NULL;
	index = qhw_sched_alloc(state->sched, sizeof(*index));
	if (index == NULL) {
		if (group_was_empty) {
			ordered_group_destroy(state, group);
		}
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	index->group = group;

	if (qhw_hash_table_insert(&state->group_tasks,
		task->task_id, index) != 0) {
		qhw_sched_free(state->sched, index);
		if (group_was_empty) {
			ordered_group_destroy(state, group);
		}
		return QHW_SCHED_ERR_EXISTS;
	}

	rc = qhw_ready_queue_insert(&group->ready, task);
	if (rc != QHW_SCHED_OK) {
		(void)qhw_hash_table_remove(&state->group_tasks,
			task->task_id);
		qhw_sched_free(state->sched, index);
		if (group_was_empty) {
			ordered_group_destroy(state, group);
		}
		return rc;
	}

	ready_task = qhw_ready_queue_find(&group->ready, task->task_id);
	if (ready_task == NULL) {
		(void)ordered_remove_group_task(state, task->task_id);
		return QHW_SCHED_ERR_STATE;
	}

	rc = ordered_refresh_task(state, ready_task);
	if (rc != QHW_SCHED_OK) {
		qhw_deadline_refresh_remove(&state->refresh, ready_task);
		(void)ordered_remove_group_task(state, task->task_id);
		return rc;
	}

	if (group_was_empty) {
		if (qhw_heap_push(&state->group_heap, group) != 0) {
			qhw_deadline_refresh_remove(&state->refresh, ready_task);
			(void)ordered_remove_group_task(state, task->task_id);
			return QHW_SCHED_ERR_NO_MEMORY;
		}
	} else {
		rc = ordered_group_reheapify(state, group);
		if (rc != QHW_SCHED_OK) {
			qhw_deadline_refresh_remove(&state->refresh, ready_task);
			(void)ordered_remove_group_task(state, task->task_id);
			return rc;
		}
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_select_round_robin(
	struct ordered_state *state,
	qhw_sched_task_id_t *out_task_id)
{
	struct ordered_group *group;
	struct qhw_ready_task *ready_task;
	qhw_sched_task_id_t task_id;
	struct ordered_task_index *index;
	qhw_sched_rc_t rc;

	group = qhw_heap_pop(&state->group_heap);
	if (group == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}
	group->heap_index = (size_t)-1;

	ready_task = qhw_ready_queue_peek(&group->ready);
	if (ready_task == NULL) {
		ordered_group_destroy(state, group);
		return QHW_SCHED_ERR_STATE;
	}

	task_id = ready_task->desc.task_id;
	qhw_deadline_refresh_remove(&state->refresh, ready_task);
	rc = qhw_ready_queue_pop(&group->ready, &task_id);
	if (rc != QHW_SCHED_OK) {
		ordered_group_destroy(state, group);
		return rc;
	}

	index = qhw_hash_table_remove(&state->group_tasks, task_id);
	if (index != NULL) {
		qhw_sched_free(state->sched, index);
	}

	if (qhw_ready_queue_peek(&group->ready) == NULL) {
		(void)qhw_group_map_remove(&state->groups, group->key);
		ordered_group_free(state, group);
	} else {
		group->ticket = state->next_group_ticket++;
		if (qhw_heap_push(&state->group_heap, group) != 0) {
			return QHW_SCHED_ERR_NO_MEMORY;
		}
	}

	*out_task_id = task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_submit(
	void *policy_state,
	const qhw_sched_task_desc_t *task)
{
	struct ordered_state *state = policy_state;
	struct qhw_ready_task *ready_task;
	qhw_sched_rc_t rc;

	if (state == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (state->use_round_robin) {
		return ordered_submit_round_robin(state, task);
	}

	rc = qhw_ready_queue_insert(&state->ready, task);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	ready_task = qhw_ready_queue_find(&state->ready, task->task_id);
	if (ready_task == NULL) {
		return QHW_SCHED_ERR_STATE;
	}

	rc = ordered_refresh_task(state, ready_task);
	if (rc != QHW_SCHED_OK) {
		qhw_deadline_refresh_remove(&state->refresh, ready_task);
		(void)qhw_ready_queue_remove(&state->ready, task->task_id);
		return rc;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_select_next(
	void *policy_state,
	qhw_sched_assignment_t *out_assignment)
{
	struct ordered_state *state = policy_state;
	qhw_sched_task_id_t task_id;
	qhw_sched_rc_t rc;

	if (state == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = ordered_refresh_expired(state);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	if (state->use_round_robin) {
		rc = ordered_select_round_robin(state, &task_id);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	} else {
		qhw_deadline_refresh_remove(&state->refresh,
			qhw_ready_queue_peek(&state->ready));
		rc = qhw_ready_queue_pop(&state->ready, &task_id);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	memset(out_assignment, 0, sizeof(*out_assignment));
	out_assignment->task_id = task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_get_split_config(
	void *policy_state,
	qhw_sched_split_config_t *out_config)
{
	struct ordered_state *state = policy_state;

	if (state == NULL || out_config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_config = state->split_config;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_priority_changed(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	struct ordered_state *state = policy_state;
	struct qhw_ready_task *task;
	int64_t old_base_priority;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	task = ordered_find_ready_task(state, task_id);
	if (task == NULL) {
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	old_base_priority = task->base_priority;
	task->base_priority = priority;
	if (ordered_refresh_task(state, task) != QHW_SCHED_OK) {
		task->base_priority = old_base_priority;
		(void)ordered_refresh_task(state, task);
		return QHW_SCHED_ERR_STATE;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_started(
	void *policy_state,
	qhw_sched_task_id_t task_id)
{
	(void)policy_state;
	(void)task_id;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t ordered_on_task_finished(
	void *policy_state,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t terminal_state)
{
	struct ordered_state *state = policy_state;
	qhw_sched_rc_t rc;

	(void)terminal_state;

	if (state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (state->use_round_robin) {
		struct qhw_ready_task *task;

		task = ordered_find_ready_task(state, task_id);
		qhw_deadline_refresh_remove(&state->refresh, task);
		rc = ordered_remove_group_task(state, task_id);
	} else {
		qhw_deadline_refresh_remove(&state->refresh,
			qhw_ready_queue_find(&state->ready, task_id));
		rc = qhw_ready_queue_remove(&state->ready, task_id);
	}
	return rc == QHW_SCHED_ERR_NOT_FOUND ? QHW_SCHED_OK : rc;
}

static const qhw_sched_plugin_desc_t ordered_desc = {
	.struct_size = sizeof(ordered_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "ordered",
	.version = "0.1.0",
	.description = "configurable ordered scheduler policy",
	.thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL,
	.init = ordered_init,
	.fini = ordered_fini,
	.on_task_submit = ordered_on_task_submit,
	.select_next = ordered_select_next,
	.get_split_config = ordered_get_split_config,
	.on_task_priority_changed = ordered_on_task_priority_changed,
	.on_task_started = ordered_on_task_started,
	.on_task_finished = ordered_on_task_finished
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &ordered_desc;
}
