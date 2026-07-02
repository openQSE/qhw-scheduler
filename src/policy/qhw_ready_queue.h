#ifndef QHW_READY_QUEUE_H
#define QHW_READY_QUEUE_H

#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "util/qhw_hash_table.h"
#include "util/qhw_heap.h"
#include "util/qhw_list.h"

#include <stdint.h>

enum qhw_ready_queue_kind {
	QHW_READY_QUEUE_FIFO = 1,
	QHW_READY_QUEUE_HEAP = 2
};

enum qhw_ready_queue_flags {
	QHW_READY_QUEUE_F_ESTIMATE_COST = 1U << 0
};

struct qhw_ready_queue;

struct qhw_ready_task {
	qhw_sched_task_desc_t desc;
	int64_t base_priority;
	int64_t effective_priority;
	uint64_t estimated_cost;
	uint64_t next_refresh_ns;
	uint64_t seq;
	size_t heap_index;
	size_t refresh_heap_index;
	struct qhw_list_node link;
	struct qhw_ready_queue *queue;
};

typedef int (*qhw_ready_queue_compare_fn)(
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right,
	void *user_data);

struct qhw_ready_queue {
	qhw_sched_t *sched;
	enum qhw_ready_queue_kind kind;
	qhw_ready_queue_compare_fn compare;
	void *compare_user_data;
	uint32_t flags;
	struct qhw_list_node fifo;
	struct qhw_heap heap;
	struct qhw_hash_table by_id;
	uint64_t next_seq;
};

qhw_sched_rc_t qhw_ready_queue_init(
	struct qhw_ready_queue *queue,
	qhw_sched_t *sched,
	enum qhw_ready_queue_kind kind,
	uint32_t flags,
	qhw_ready_queue_compare_fn compare,
	void *compare_user_data);

void qhw_ready_queue_fini(struct qhw_ready_queue *queue);

qhw_sched_rc_t qhw_ready_queue_insert(
	struct qhw_ready_queue *queue,
	const qhw_sched_task_desc_t *task);

qhw_sched_rc_t qhw_ready_queue_pop(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t *out_task_id);

struct qhw_ready_task *qhw_ready_queue_peek(
	struct qhw_ready_queue *queue);

struct qhw_ready_task *qhw_ready_queue_find(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_ready_queue_remove(
	struct qhw_ready_queue *queue,
	qhw_sched_task_id_t task_id);

qhw_sched_rc_t qhw_ready_queue_reorder(
	struct qhw_ready_queue *queue,
	struct qhw_ready_task *task);

#endif
