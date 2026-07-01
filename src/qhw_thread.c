#include "qhw_scheduler_internal.h"

static void mutex_lock(struct qhw_mutex *lock)
{
	(void)pthread_mutex_lock(&lock->mutex);
}

static void mutex_unlock(struct qhw_mutex *lock)
{
	(void)pthread_mutex_unlock(&lock->mutex);
}

static void mutex_destroy(struct qhw_mutex *lock)
{
	(void)pthread_mutex_destroy(&lock->mutex);
}

static void noop_lock(struct qhw_mutex *lock)
{
	(void)lock;
}

static void noop_unlock(struct qhw_mutex *lock)
{
	(void)lock;
}

static void noop_destroy(struct qhw_mutex *lock)
{
	(void)lock;
}

qhw_sched_rc_t qhw_thread_init(
	qhw_sched_threading_t threading,
	struct qhw_mutex *lock,
	struct qhw_lock_ops *ops)
{
	if (lock == NULL || ops == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (threading == QHW_SCHED_THREAD_SAFE) {
		if (pthread_mutex_init(&lock->mutex, NULL) != 0) {
			return QHW_SCHED_ERR_STATE;
		}
		ops->lock = mutex_lock;
		ops->unlock = mutex_unlock;
		ops->destroy = mutex_destroy;
		return QHW_SCHED_OK;
	}

	if (threading == QHW_SCHED_THREAD_USER) {
		ops->lock = noop_lock;
		ops->unlock = noop_unlock;
		ops->destroy = noop_destroy;
		return QHW_SCHED_OK;
	}

	return QHW_SCHED_ERR_INVALID_ARG;
}

