#include "qhw_scheduler_internal.h"

#include <stdlib.h>
#include <string.h>

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

qhw_sched_rc_t qhw_sched_submit_task(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task)
{
	struct qhw_task_record *record;
	qhw_sched_rc_t rc;

	if (sched == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	rc = qhw_task_table_insert(&sched->tasks, &sched->allocator, task,
		sched->enqueue_seq_next++);
	if (rc == QHW_SCHED_OK) {
		qpu_runtime_task_queued(sched->qpu);
		if (sched->policy.desc.on_task_submit != NULL) {
			record = qhw_task_table_find(&sched->tasks, task->task_id);
			rc = sched->policy.desc.on_task_submit(
				sched->policy.state,
				&record->desc);
			if (rc != QHW_SCHED_OK) {
				qpu_runtime_task_unqueued(sched->qpu);
				qhw_task_table_remove(&sched->tasks,
					&sched->allocator,
					task->task_id);
			}
		}
	}
	sched->lock_ops.unlock(&sched->lock);
	return rc;
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
		if (sched->policy.desc.on_task_finished != NULL) {
			rc = sched->policy.desc.on_task_finished(
				sched->policy.state,
				task_id,
				state);
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
