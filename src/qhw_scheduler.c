#include "qhw_scheduler_internal.h"
#include "policy/qhw_policy_metadata.h"
#include "qhw_ds_error.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void *hash_alloc(size_t size, void *user_data)
{
	return qhw_alloc(user_data, size);
}

static void hash_free(void *ptr, void *user_data)
{
	qhw_free(user_data, ptr);
}

static qhw_sched_threading_t get_threading(const qhw_sched_attr_t *attr)
{
	if (attr == NULL || attr->threading == 0) {
		return QHW_SCHED_THREAD_SAFE;
	}

	return attr->threading;
}

static int task_state_is_terminal(qhw_sched_task_state_t state)
{
	return state == QHW_SCHED_TASK_COMPLETED ||
		state == QHW_SCHED_TASK_FAILED ||
		state == QHW_SCHED_TASK_CANCELLED;
}

static int policy_is_active(qhw_sched_t *sched)
{
	return sched != NULL && sched->policy.desc.select_next != NULL;
}

static int metadata_get_u64(
	const qhw_sched_kv_t *metadata,
	size_t metadata_count,
	uint64_t key,
	uint64_t *out_value)
{
	size_t i;

	if (metadata == NULL || out_value == NULL) {
		return 0;
	}

	for (i = 0; i < metadata_count; i++) {
		if (metadata[i].key == key &&
			metadata[i].type == QHW_SCHED_VALUE_U64) {
			*out_value = metadata[i].value.u64;
			return 1;
		}
	}

	return 0;
}

static qhw_sched_rc_t get_policy_split_config(
	qhw_sched_t *sched,
	qhw_sched_split_config_t *config)
{
	qhw_sched_split_config_init(config);
	if (sched->policy.desc.get_split_config == NULL) {
		return QHW_SCHED_OK;
	}

	return sched->policy.desc.get_split_config(sched->policy.state,
		config);
}

static uint64_t min_nonzero(uint64_t left, uint64_t right)
{
	if (left == 0) {
		return right;
	}

	if (right == 0) {
		return left;
	}

	return left < right ? left : right;
}

static qhw_sched_rc_t compute_split_config(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task,
	qhw_sched_split_config_t *config,
	int *out_should_split)
{
	uint64_t requested_shots;
	uint64_t qpu_max_shots = 0;
	uint64_t threshold;
	uint64_t max_shots;
	uint64_t child_count;
	qhw_sched_rc_t rc;

	*out_should_split = 0;
	rc = get_policy_split_config(sched, config);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	if (!metadata_get_u64(task->metadata, task->metadata_count,
		QHW_SCHED_META_SHOTS, &requested_shots)) {
		return QHW_SCHED_OK;
	}

	metadata_get_u64(sched->qpu->profile.metadata,
		sched->qpu->profile.metadata_count,
		QHW_SCHED_META_MAX_SHOTS,
		&qpu_max_shots);

	max_shots = min_nonzero(config->max_shots, qpu_max_shots);
	if (max_shots == 0) {
		return QHW_SCHED_OK;
	}

	threshold = config->shot_threshold;
	if (threshold == 0) {
		threshold = max_shots;
	}
	if (threshold == 0 || threshold > max_shots) {
		threshold = max_shots;
	}

	if (requested_shots <= threshold) {
		return QHW_SCHED_OK;
	}

	child_count = requested_shots / max_shots;
	if (requested_shots % max_shots != 0) {
		child_count++;
	}

	if (config->max_children != 0 &&
		child_count > config->max_children) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	if (child_count > (uint64_t)((size_t)-1)) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	if (sched->callbacks.split_task == NULL) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	config->requested_shots = requested_shots;
	config->slice_shots = max_shots;
	config->slice_count = (size_t)child_count;
	*out_should_split = 1;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t validate_child_tasks(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *parent,
	const qhw_sched_task_desc_t *children,
	const qhw_sched_split_config_t *config,
	size_t child_count)
{
	struct qhw_hash_table seen;
	struct qhw_hash_table seen_slices;
	size_t bucket_count;
	size_t i;
	uint64_t total_shots = 0;
	qhw_sched_rc_t rc = QHW_SCHED_OK;

	if (config == NULL || child_count != config->slice_count ||
		child_count > ((size_t)-2) / 2) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	bucket_count = child_count * 2 + 1;
	if (qhw_hash_table_init(&seen, bucket_count, hash_alloc,
		hash_free, &sched->allocator) != 0) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}
	if (qhw_hash_table_init(&seen_slices, bucket_count, hash_alloc,
		hash_free, &sched->allocator) != 0) {
		qhw_hash_table_fini(&seen, NULL, NULL);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	for (i = 0; i < child_count; i++) {
		uint64_t child_shots;
		uint64_t slice_index;
		uint64_t slice_count;
		int insert_rc;

		if (children[i].task_id == QHW_SCHED_INVALID_TASK_ID ||
			children[i].task_id == parent->task_id ||
			children[i].parent_task_id != parent->task_id ||
			qhw_task_table_find(&sched->tasks,
				children[i].task_id) != NULL ||
			qhw_hash_table_find(&seen, children[i].task_id) != NULL) {
			rc = QHW_SCHED_ERR_INVALID_ARG;
			break;
		}

		if (!metadata_get_u64(children[i].metadata,
			children[i].metadata_count,
			QHW_SCHED_META_SLICE_INDEX,
			&slice_index) ||
			!metadata_get_u64(children[i].metadata,
				children[i].metadata_count,
				QHW_SCHED_META_SLICE_COUNT,
				&slice_count) ||
			slice_count != child_count ||
			slice_index >= child_count ||
			qhw_hash_table_find(&seen_slices,
				slice_index) != NULL) {
			rc = QHW_SCHED_ERR_INVALID_ARG;
			break;
		}

		if (!metadata_get_u64(children[i].metadata,
			children[i].metadata_count,
			QHW_SCHED_META_SHOTS,
			&child_shots) ||
			child_shots == 0 ||
			child_shots > config->slice_shots ||
			child_shots > config->requested_shots - total_shots) {
			rc = QHW_SCHED_ERR_INVALID_ARG;
			break;
		}

		insert_rc = qhw_hash_table_insert(&seen_slices,
			slice_index, (void *)&children[i]);
		if (insert_rc != QHW_HASH_TABLE_OK) {
			rc = insert_rc == QHW_HASH_TABLE_ERR_EXISTS ?
				QHW_SCHED_ERR_INVALID_ARG :
				qhw_hash_insert_rc_to_sched_rc(insert_rc);
			break;
		}

		total_shots += child_shots;

		insert_rc = qhw_hash_table_insert(&seen,
			children[i].task_id, (void *)&children[i]);
		if (insert_rc != QHW_HASH_TABLE_OK) {
			rc = insert_rc == QHW_HASH_TABLE_ERR_EXISTS ?
				QHW_SCHED_ERR_INVALID_ARG :
				qhw_hash_insert_rc_to_sched_rc(insert_rc);
			break;
		}
	}

	if (rc == QHW_SCHED_OK &&
		total_shots != config->requested_shots) {
		rc = QHW_SCHED_ERR_INVALID_ARG;
	}

	qhw_hash_table_fini(&seen_slices, NULL, NULL);
	qhw_hash_table_fini(&seen, NULL, NULL);
	return rc;
}

static void qpu_runtime_task_queued(qhw_sched_qpu_t *qpu)
{
	qpu->lock_ops.lock(&qpu->lock);
	qpu->runtime.queued_count++;
	qpu->lock_ops.unlock(&qpu->lock);
}

static void qpu_runtime_task_unqueued(qhw_sched_qpu_t *qpu)
{
	qpu->lock_ops.lock(&qpu->lock);
	if (qpu->runtime.queued_count > 0) {
		qpu->runtime.queued_count--;
	}
	qpu->lock_ops.unlock(&qpu->lock);
}

static qhw_sched_rc_t qpu_runtime_task_started(
	qhw_sched_qpu_t *qpu,
	qhw_sched_task_id_t task_id)
{
	qhw_sched_rc_t rc = QHW_SCHED_OK;

	qpu->lock_ops.lock(&qpu->lock);
	if (qpu->runtime.running_task_id != QHW_SCHED_INVALID_TASK_ID) {
		rc = QHW_SCHED_ERR_STATE;
	} else {
		qpu->runtime.running_task_id = task_id;
	}
	qpu->lock_ops.unlock(&qpu->lock);
	return rc;
}

static void qpu_runtime_task_unstarted(
	qhw_sched_qpu_t *qpu,
	qhw_sched_task_id_t task_id)
{
	qpu->lock_ops.lock(&qpu->lock);
	if (qpu->runtime.running_task_id == task_id) {
		qpu->runtime.running_task_id = QHW_SCHED_INVALID_TASK_ID;
	}
	qpu->lock_ops.unlock(&qpu->lock);
}

static void qpu_runtime_task_finished(
	qhw_sched_qpu_t *qpu,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t state)
{
	qpu->lock_ops.lock(&qpu->lock);
	if (qpu->runtime.running_task_id == task_id) {
		qpu->runtime.running_task_id = QHW_SCHED_INVALID_TASK_ID;
	}
	if (state == QHW_SCHED_TASK_COMPLETED) {
		qpu->runtime.completed_count++;
	} else if (state == QHW_SCHED_TASK_FAILED) {
		qpu->runtime.failed_count++;
	} else if (state == QHW_SCHED_TASK_CANCELLED) {
		qpu->runtime.cancelled_count++;
	}
	qpu->lock_ops.unlock(&qpu->lock);
}

static void fill_assignment_from_record(
	qhw_sched_assignment_t *assignment,
	const struct qhw_task_record *record)
{
	uint64_t value;

	memset(assignment, 0, sizeof(*assignment));
	assignment->struct_size = sizeof(*assignment);
	assignment->task_id = record->desc.task_id;
	assignment->parent_task_id = record->desc.parent_task_id;
	assignment->slice_count = 1;
	if (metadata_get_u64(record->desc.metadata, record->desc.metadata_count,
		QHW_SCHED_META_SLICE_INDEX, &value)) {
		assignment->slice_index = value;
	}
	if (metadata_get_u64(record->desc.metadata, record->desc.metadata_count,
		QHW_SCHED_META_SLICE_COUNT, &value)) {
		assignment->slice_count = value;
	}
	assignment->payload = record->desc.payload;
	assignment->payload_size = record->desc.payload_size;
	assignment->estimated_runtime_ns = record->desc.estimated_runtime_ns;
	assignment->estimated_cost = record->desc.estimated_cost;
}

static qhw_sched_rc_t estimate_task_cost(
	const qhw_sched_callbacks_t *callbacks,
	const qhw_sched_qpu_profile_t *qpu,
	const qhw_sched_task_desc_t *task,
	qhw_sched_task_desc_t *out_task)
{
	uint64_t cost;
	qhw_sched_rc_t rc;

	if (task == NULL || out_task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_task = *task;
	if (callbacks != NULL && callbacks->estimate_cost != NULL) {
		if (qpu == NULL) {
			return QHW_SCHED_ERR_INVALID_ARG;
		}

		cost = 0;
		rc = callbacks->estimate_cost(task, qpu, &cost,
			callbacks->user_data);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
		if (cost == 0) {
			return QHW_SCHED_ERR_INVALID_ARG;
		}
		out_task->estimated_cost = cost;
		return QHW_SCHED_OK;
	}

	out_task->estimated_cost = qhw_policy_task_estimated_cost(out_task);
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t estimate_child_costs(
	const qhw_sched_callbacks_t *callbacks,
	const qhw_sched_qpu_profile_t *qpu,
	qhw_sched_task_desc_t *children,
	size_t child_count)
{
	size_t i;

	if (children == NULL && child_count > 0) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	for (i = 0; i < child_count; i++) {
		qhw_sched_task_desc_t estimated;
		qhw_sched_rc_t rc;

		rc = estimate_task_cost(callbacks, qpu, &children[i],
			&estimated);
		if (rc != QHW_SCHED_OK) {
			return rc;
		}
		children[i] = estimated;
	}

	return QHW_SCHED_OK;
}

static qhw_sched_rc_t enqueue_task_locked(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task)
{
	struct qhw_task_record *record;
	qhw_sched_rc_t rc;

	rc = qhw_task_table_insert(&sched->tasks, &sched->allocator, task,
		sched->enqueue_seq_next++);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	qpu_runtime_task_queued(sched->qpu);
	if (sched->policy.desc.on_task_submit == NULL) {
		return QHW_SCHED_OK;
	}

	record = qhw_task_table_find(&sched->tasks, task->task_id);
	rc = sched->policy.desc.on_task_submit(
		sched->policy.state,
		&record->desc);
	if (rc != QHW_SCHED_OK) {
		qpu_runtime_task_unqueued(sched->qpu);
		qhw_task_table_remove(&sched->tasks, &sched->allocator,
			task->task_id);
	}

	return rc;
}

static void remove_queued_task_locked(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	if (sched->policy.desc.on_task_finished != NULL) {
		(void)sched->policy.desc.on_task_finished(
			sched->policy.state,
			task_id,
			QHW_SCHED_TASK_CANCELLED);
	}
	qpu_runtime_task_unqueued(sched->qpu);
	qhw_task_table_remove(&sched->tasks, &sched->allocator, task_id);
}

static qhw_sched_rc_t submit_split_task_locked(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *parent,
	const qhw_sched_task_desc_t *children,
	size_t child_count)
{
	struct qhw_task_record *parent_record;
	size_t i;
	qhw_sched_rc_t rc;

	rc = qhw_task_table_insert_state(&sched->tasks, &sched->allocator,
		parent, sched->enqueue_seq_next++, QHW_SCHED_TASK_WAITING);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	parent_record = qhw_task_table_find(&sched->tasks, parent->task_id);
	parent_record->child_count = child_count;
	for (i = 0; i < child_count; i++) {
		rc = enqueue_task_locked(sched, &children[i]);
		if (rc != QHW_SCHED_OK) {
			while (i > 0) {
				i--;
				remove_queued_task_locked(sched,
					children[i].task_id);
			}
			qhw_task_table_remove(&sched->tasks,
				&sched->allocator,
				parent->task_id);
			return rc;
		}
	}

	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_sched_create(
	const char *policy_name,
	const qhw_sched_attr_t *attr,
	qhw_sched_qpu_t *qpu,
	const qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_t **out_sched)
{
	qhw_sched_t *sched;
	qhw_sched_threading_t threading;
	qhw_sched_rc_t rc;

	if (out_sched == NULL || qpu == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (option_count > 0 && options == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (policy_name != NULL || option_count > 0) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	sched = calloc(1, sizeof(*sched));
	if (sched == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	rc = qhw_allocator_init(&sched->allocator,
		attr == NULL ? NULL : attr->allocator);
	if (rc != QHW_SCHED_OK) {
		free(sched);
		return rc;
	}

	threading = get_threading(attr);
	rc = qhw_thread_init(threading, &sched->lock, &sched->lock_ops);
	if (rc != QHW_SCHED_OK) {
		free(sched);
		return rc;
	}

	rc = qhw_task_table_init(&sched->tasks, &sched->allocator);
	if (rc != QHW_SCHED_OK) {
		sched->lock_ops.destroy(&sched->lock);
		free(sched);
		return rc;
	}

	sched->threading = threading;
	sched->qpu = qpu;
	sched->enqueue_seq_next = 1;
	qhw_qpu_retain(qpu);
	qhw_error_set(&sched->last_error, QHW_SCHED_OK, "");

	*out_sched = sched;
	return QHW_SCHED_OK;
}

void qhw_sched_destroy(qhw_sched_t *sched)
{
	if (sched == NULL) {
		return;
	}

	if (sched->policy.desc.fini != NULL) {
		sched->policy.desc.fini(sched->policy.state);
	}
	qhw_plugin_registry_fini(&sched->plugins, &sched->allocator);
	qhw_task_table_fini(&sched->tasks, &sched->allocator);
	qhw_qpu_release(sched->qpu);
	sched->lock_ops.destroy(&sched->lock);
	free(sched);
}

qhw_sched_threading_t qhw_sched_get_threading(qhw_sched_t *sched)
{
	if (sched == NULL) {
		return 0;
	}

	return sched->threading;
}

qhw_sched_rc_t qhw_sched_set_callbacks(
	qhw_sched_t *sched,
	const qhw_sched_callbacks_t *callbacks)
{
	if (sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	if (callbacks == NULL) {
		memset(&sched->callbacks, 0, sizeof(sched->callbacks));
	} else {
		if (callbacks->struct_size < sizeof(*callbacks)) {
			sched->lock_ops.unlock(&sched->lock);
			return QHW_SCHED_ERR_INVALID_ARG;
		}
		sched->callbacks = *callbacks;
	}
	sched->lock_ops.unlock(&sched->lock);
	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_sched_submit_task(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task)
{
	qhw_sched_task_desc_t estimated_task;
	qhw_sched_task_desc_t *children = NULL;
	qhw_sched_split_config_t split_config;
	qhw_sched_split_config_t latest_config;
	qhw_sched_callbacks_t callbacks;
	int should_split = 0;
	int latest_should_split = 0;
	qhw_sched_rc_t rc;
	size_t i;

	if (sched == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	if (qhw_task_table_find(&sched->tasks, task->task_id) != NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_EXISTS;
	}

	rc = compute_split_config(sched, task, &split_config, &should_split);
	if (rc != QHW_SCHED_OK || !should_split) {
		if (rc == QHW_SCHED_OK) {
			callbacks = sched->callbacks;
			if (callbacks.estimate_cost != NULL) {
				sched->lock_ops.unlock(&sched->lock);
				rc = estimate_task_cost(&callbacks,
					&sched->qpu->profile, task,
					&estimated_task);
				sched->lock_ops.lock(&sched->lock);
				if (rc == QHW_SCHED_OK &&
					qhw_task_table_find(&sched->tasks,
						task->task_id) != NULL) {
					rc = QHW_SCHED_ERR_EXISTS;
				}
				if (rc == QHW_SCHED_OK) {
					rc = compute_split_config(sched, task,
						&latest_config,
						&latest_should_split);
				}
				if (rc == QHW_SCHED_OK &&
					latest_should_split) {
					rc = QHW_SCHED_ERR_STATE;
				}
			} else {
				rc = estimate_task_cost(&callbacks,
					&sched->qpu->profile, task,
					&estimated_task);
			}
		}
		if (rc == QHW_SCHED_OK) {
			rc = enqueue_task_locked(sched, &estimated_task);
		}
		sched->lock_ops.unlock(&sched->lock);
		return rc;
	}

	callbacks = sched->callbacks;
	if (split_config.slice_count > (size_t)-1 / sizeof(*children)) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	children = qhw_alloc(&sched->allocator,
		split_config.slice_count * sizeof(*children));
	if (children == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	for (i = 0; i < split_config.slice_count; i++) {
		children[i] = *task;
		children[i].task_id = QHW_SCHED_INVALID_TASK_ID;
		children[i].parent_task_id = task->task_id;
	}

	sched->lock_ops.unlock(&sched->lock);
	rc = callbacks.split_task(task, &split_config, children,
		split_config.slice_count, callbacks.user_data);
	if (rc == QHW_SCHED_OK) {
		rc = estimate_child_costs(&callbacks, &sched->qpu->profile,
			children, split_config.slice_count);
	}
	sched->lock_ops.lock(&sched->lock);
	if (rc == QHW_SCHED_OK) {
		if (qhw_task_table_find(&sched->tasks, task->task_id) != NULL) {
			rc = QHW_SCHED_ERR_EXISTS;
		}
	}
	if (rc == QHW_SCHED_OK) {
		rc = compute_split_config(sched, task, &latest_config,
			&latest_should_split);
	}
	if (rc == QHW_SCHED_OK &&
		(!latest_should_split ||
			latest_config.slice_count != split_config.slice_count ||
			latest_config.slice_shots != split_config.slice_shots)) {
		rc = QHW_SCHED_ERR_STATE;
	}
	if (rc == QHW_SCHED_OK) {
		rc = validate_child_tasks(sched, task, children,
			&split_config, split_config.slice_count);
	}
	if (rc == QHW_SCHED_OK) {
		rc = submit_split_task_locked(sched, task, children,
			split_config.slice_count);
	}

	qhw_free(&sched->allocator, children);
	sched->lock_ops.unlock(&sched->lock);
	return rc;
}

static void update_parent_after_child_locked(
	qhw_sched_t *sched,
	const struct qhw_task_record *child,
	qhw_sched_task_state_t state)
{
	struct qhw_task_record *parent;
	size_t terminal_count;
	qhw_sched_task_state_t parent_state = QHW_SCHED_TASK_COMPLETED;

	if (child->desc.parent_task_id == QHW_SCHED_INVALID_TASK_ID) {
		return;
	}

	parent = qhw_task_table_find(&sched->tasks,
		child->desc.parent_task_id);
	if (parent == NULL || parent->state != QHW_SCHED_TASK_WAITING) {
		return;
	}

	if (state == QHW_SCHED_TASK_COMPLETED) {
		parent->completed_child_count++;
	} else if (state == QHW_SCHED_TASK_FAILED) {
		parent->failed_child_count++;
	} else if (state == QHW_SCHED_TASK_CANCELLED) {
		parent->cancelled_child_count++;
	}

	terminal_count = parent->completed_child_count +
		parent->failed_child_count +
		parent->cancelled_child_count;
	if (terminal_count < parent->child_count) {
		return;
	}

	if (parent->failed_child_count > 0) {
		parent_state = QHW_SCHED_TASK_FAILED;
	} else if (parent->cancelled_child_count > 0) {
		parent_state = QHW_SCHED_TASK_CANCELLED;
	}

	(void)qhw_task_table_set_state(&sched->tasks,
		parent->desc.task_id,
		parent_state);
}

qhw_sched_rc_t qhw_sched_select_next(
	qhw_sched_t *sched,
	qhw_sched_assignment_t *out_assignment)
{
	struct qhw_task_record *record;
	qhw_sched_rc_t rc;

	if (sched == NULL || out_assignment == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	if (sched->policy.desc.select_next == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	rc = sched->policy.desc.select_next(
		sched->policy.state,
		out_assignment);
	if (rc == QHW_SCHED_OK) {
		record = qhw_task_table_find(&sched->tasks,
			out_assignment->task_id);
		if (record == NULL) {
			rc = QHW_SCHED_ERR_NOT_FOUND;
		} else if (record->state != QHW_SCHED_TASK_QUEUED) {
			rc = QHW_SCHED_ERR_STATE;
		} else {
			rc = qhw_task_table_set_state(&sched->tasks,
				out_assignment->task_id,
				QHW_SCHED_TASK_ASSIGNED);
			if (rc == QHW_SCHED_OK) {
				qpu_runtime_task_unqueued(sched->qpu);
				fill_assignment_from_record(out_assignment,
					record);
			}
		}
	}
	sched->lock_ops.unlock(&sched->lock);
	return rc;
}

qhw_sched_rc_t qhw_sched_task_update_priority(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	struct qhw_task_record *record;
	int64_t old_priority;
	qhw_sched_rc_t rc = QHW_SCHED_OK;

	if (sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	record = qhw_task_table_find(&sched->tasks, task_id);
	if (record == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	if (record->state != QHW_SCHED_TASK_QUEUED) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_STATE;
	}

	old_priority = record->desc.priority;
	record->desc.priority = priority;
	if (sched->policy.desc.on_task_priority_changed != NULL) {
		rc = sched->policy.desc.on_task_priority_changed(
			sched->policy.state,
			task_id,
			priority);
		if (rc != QHW_SCHED_OK) {
			record->desc.priority = old_priority;
		}
	}
	sched->lock_ops.unlock(&sched->lock);
	return rc;
}

qhw_sched_rc_t qhw_sched_task_started(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	struct qhw_task_record *record;
	qhw_sched_task_state_t old_state;
	qhw_sched_rc_t rc;

	if (sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	record = qhw_task_table_find(&sched->tasks, task_id);
	if (record == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	if (record->state != QHW_SCHED_TASK_ASSIGNED &&
		(record->state != QHW_SCHED_TASK_QUEUED ||
			policy_is_active(sched))) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_STATE;
	}
	old_state = record->state;

	rc = qpu_runtime_task_started(sched->qpu, task_id);
	if (rc != QHW_SCHED_OK) {
		sched->lock_ops.unlock(&sched->lock);
		return rc;
	}

	rc = qhw_task_table_set_state(&sched->tasks, task_id,
		QHW_SCHED_TASK_RUNNING);
	if (rc == QHW_SCHED_OK) {
		if (old_state == QHW_SCHED_TASK_QUEUED) {
			qpu_runtime_task_unqueued(sched->qpu);
		}
		if (sched->policy.desc.on_task_started != NULL) {
			rc = sched->policy.desc.on_task_started(
				sched->policy.state,
				task_id);
		}
	}
	if (rc != QHW_SCHED_OK) {
		(void)qhw_task_table_set_state(&sched->tasks, task_id,
			old_state);
		if (old_state == QHW_SCHED_TASK_QUEUED) {
			qpu_runtime_task_queued(sched->qpu);
		}
		qpu_runtime_task_unstarted(sched->qpu, task_id);
	}
	sched->lock_ops.unlock(&sched->lock);
	return rc;
}

static qhw_sched_rc_t finish_task(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t state)
{
	struct qhw_task_record *record;
	qhw_sched_task_state_t old_state;
	qhw_sched_rc_t rc;
	qhw_sched_rc_t notify_rc = QHW_SCHED_OK;

	if (sched == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	record = qhw_task_table_find(&sched->tasks, task_id);
	if (record == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	if (task_state_is_terminal(record->state)) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_STATE;
	}

	if (record->state == QHW_SCHED_TASK_WAITING) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_STATE;
	}

	if (state == QHW_SCHED_TASK_COMPLETED &&
		record->state != QHW_SCHED_TASK_RUNNING) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_STATE;
	}
	old_state = record->state;

	rc = qhw_task_table_set_state(&sched->tasks, task_id, state);
	if (rc == QHW_SCHED_OK) {
		if (old_state == QHW_SCHED_TASK_QUEUED) {
			qpu_runtime_task_unqueued(sched->qpu);
		}
		qpu_runtime_task_finished(sched->qpu, task_id, state);
		update_parent_after_child_locked(sched, record, state);
		if (sched->policy.desc.on_task_finished != NULL) {
			notify_rc = sched->policy.desc.on_task_finished(
				sched->policy.state,
				task_id,
				state);
		}
		if (notify_rc != QHW_SCHED_OK) {
			rc = notify_rc;
		}
	}
	sched->lock_ops.unlock(&sched->lock);
	return rc;
}

qhw_sched_rc_t qhw_sched_task_completed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	return finish_task(sched, task_id, QHW_SCHED_TASK_COMPLETED);
}

qhw_sched_rc_t qhw_sched_task_failed(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	return finish_task(sched, task_id, QHW_SCHED_TASK_FAILED);
}

qhw_sched_rc_t qhw_sched_task_cancelled(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	return finish_task(sched, task_id, QHW_SCHED_TASK_CANCELLED);
}

qhw_sched_rc_t qhw_sched_task_get_state(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t *out_state)
{
	struct qhw_task_record *record;

	if (sched == NULL || out_state == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	record = qhw_task_table_find(&sched->tasks, task_id);
	if (record == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	*out_state = record->state;
	sched->lock_ops.unlock(&sched->lock);
	return QHW_SCHED_OK;
}

size_t qhw_sched_task_count(qhw_sched_t *sched)
{
	size_t count;

	if (sched == NULL) {
		return 0;
	}

	sched->lock_ops.lock(&sched->lock);
	count = sched->tasks.count;
	sched->lock_ops.unlock(&sched->lock);
	return count;
}
