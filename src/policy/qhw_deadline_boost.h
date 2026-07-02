#ifndef QHW_DEADLINE_BOOST_H
#define QHW_DEADLINE_BOOST_H

#include "qhw_scheduler/qhw_scheduler_plugin.h"

#include <stdint.h>

#define QHW_DEADLINE_BOOST_NEVER UINT64_MAX

enum qhw_deadline_boost_tier {
	QHW_DEADLINE_BOOST_NONE = 0,
	QHW_DEADLINE_BOOST_NORMAL = 1,
	QHW_DEADLINE_BOOST_URGENT = 2,
	QHW_DEADLINE_BOOST_CRITICAL = 3
};

struct qhw_deadline_boost_config {
	int enabled;
	uint64_t normal_threshold_permille;
	uint64_t urgent_threshold_permille;
	uint64_t critical_threshold_permille;
	int64_t normal_boost;
	int64_t urgent_boost;
	int64_t critical_boost;
};

struct qhw_deadline_boost_result {
	int64_t effective_priority;
	uint64_t next_refresh_ns;
	enum qhw_deadline_boost_tier tier;
};

void qhw_deadline_boost_config_init(
	struct qhw_deadline_boost_config *config);

qhw_sched_rc_t qhw_deadline_boost_config_parse_options(
	struct qhw_deadline_boost_config *config,
	const qhw_sched_kv_t *options,
	size_t option_count);

uint64_t qhw_deadline_boost_now_ns(void);

qhw_sched_rc_t qhw_deadline_boost_compute(
	const struct qhw_deadline_boost_config *config,
	int64_t base_priority,
	uint64_t deadline_ns,
	uint64_t estimated_runtime_ns,
	uint64_t now_ns,
	struct qhw_deadline_boost_result *result);

#endif
