#ifndef QHW_ORDER_KEY_H
#define QHW_ORDER_KEY_H

#include "policy/qhw_deadline_boost.h"
#include "policy/qhw_ready_queue.h"

#define QHW_ORDER_KEY_MAX 8

struct qhw_order_config {
	uint64_t keys[QHW_ORDER_KEY_MAX];
	size_t key_count;
	struct qhw_deadline_boost_config boost;
	uint64_t now_ns;
	int use_static_now;
};

void qhw_order_config_init(struct qhw_order_config *config);

qhw_sched_rc_t qhw_order_config_parse_options(
	struct qhw_order_config *config,
	const qhw_sched_kv_t *options,
	size_t option_count);

uint64_t qhw_order_now_ns(const struct qhw_order_config *config);

qhw_sched_rc_t qhw_order_refresh_task(
	const struct qhw_order_config *config,
	struct qhw_ready_task *task);

int qhw_order_compare(
	const struct qhw_order_config *config,
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right);

#endif
