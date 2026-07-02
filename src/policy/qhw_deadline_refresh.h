#ifndef QHW_DEADLINE_REFRESH_H
#define QHW_DEADLINE_REFRESH_H

#include "policy/qhw_ready_queue.h"

struct qhw_deadline_refresh_queue {
	qhw_sched_t *sched;
	struct qhw_heap heap;
};

qhw_sched_rc_t qhw_deadline_refresh_init(
	struct qhw_deadline_refresh_queue *queue,
	qhw_sched_t *sched);

void qhw_deadline_refresh_fini(
	struct qhw_deadline_refresh_queue *queue);

qhw_sched_rc_t qhw_deadline_refresh_insert(
	struct qhw_deadline_refresh_queue *queue,
	struct qhw_ready_task *task);

void qhw_deadline_refresh_remove(
	struct qhw_deadline_refresh_queue *queue,
	struct qhw_ready_task *task);

struct qhw_ready_task *qhw_deadline_refresh_pop_expired(
	struct qhw_deadline_refresh_queue *queue,
	uint64_t now_ns);

#endif
