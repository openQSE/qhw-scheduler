#define _POSIX_C_SOURCE 200809L

#include "policy/qhw_deadline_boost.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define QHW_BOOST_SCALE UINT64_C(1000)

static int64_t add_i64_saturating(int64_t base, int64_t delta)
{
	if (delta > 0 && base > INT64_MAX - delta) {
		return INT64_MAX;
	}

	if (delta < 0 && base < INT64_MIN - delta) {
		return INT64_MIN;
	}

	return base + delta;
}

#if defined(__SIZEOF_INT128__)
static uint64_t scale_div_ceil_u64(uint64_t value, uint64_t divisor)
{
	__uint128_t product;
	__uint128_t quotient;

	if (divisor == 0) {
		return UINT64_MAX;
	}

	product = (__uint128_t)value * QHW_BOOST_SCALE;
	quotient = product / divisor;
	if ((product % divisor) != 0) {
		quotient++;
	}

	if (quotient > UINT64_MAX) {
		return UINT64_MAX;
	}

	return (uint64_t)quotient;
}
#else
static uint64_t scaled_remainder_ceil_u64(
	uint64_t remainder,
	uint64_t divisor)
{
	uint64_t quotient = 0;
	uint64_t acc = 0;
	uint64_t i;

	if (remainder == 0) {
		return 0;
	}

	for (i = 0; i < QHW_BOOST_SCALE; i++) {
		if (acc >= divisor - remainder) {
			acc -= divisor - remainder;
			quotient++;
		} else {
			acc += remainder;
		}
	}

	return quotient + (acc != 0);
}

static uint64_t scale_div_ceil_u64(uint64_t value, uint64_t divisor)
{
	uint64_t quotient;
	uint64_t remainder;
	uint64_t scaled;
	uint64_t extra;

	if (divisor == 0) {
		return UINT64_MAX;
	}

	quotient = value / divisor;
	remainder = value % divisor;
	if (quotient > UINT64_MAX / QHW_BOOST_SCALE) {
		return UINT64_MAX;
	}

	scaled = quotient * QHW_BOOST_SCALE;
	extra = scaled_remainder_ceil_u64(remainder, divisor);
	if (extra > UINT64_MAX - scaled) {
		return UINT64_MAX;
	}

	return scaled + extra;
}
#endif

static uint64_t urgency_permille(
	uint64_t estimated_runtime_ns,
	uint64_t slack_ns)
{
	if (slack_ns == 0) {
		return UINT64_MAX;
	}

	return scale_div_ceil_u64(estimated_runtime_ns, slack_ns);
}

static uint64_t next_threshold_time(
	uint64_t deadline_ns,
	uint64_t estimated_runtime_ns,
	uint64_t threshold_permille,
	uint64_t now_ns)
{
	uint64_t window_ns;
	uint64_t next_ns;

	if (threshold_permille == 0) {
		return QHW_DEADLINE_BOOST_NEVER;
	}

	window_ns = scale_div_ceil_u64(estimated_runtime_ns,
		threshold_permille);
	if (window_ns >= deadline_ns) {
		next_ns = 1;
	} else {
		next_ns = deadline_ns - window_ns;
	}

	if (next_ns <= now_ns) {
		if (now_ns == UINT64_MAX) {
			return QHW_DEADLINE_BOOST_NEVER;
		}
		next_ns = now_ns + 1;
	}

	return next_ns;
}

void qhw_deadline_boost_config_init(
	struct qhw_deadline_boost_config *config)
{
	if (config == NULL) {
		return;
	}

	memset(config, 0, sizeof(*config));
	config->normal_threshold_permille = 1000;
	config->urgent_threshold_permille = 2000;
	config->critical_threshold_permille = 3000;
	config->normal_boost = 10;
	config->urgent_boost = 50;
	config->critical_boost = 100;
}

uint64_t qhw_deadline_boost_now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}

	return ((uint64_t)ts.tv_sec * UINT64_C(1000000000)) +
		(uint64_t)ts.tv_nsec;
}

qhw_sched_rc_t qhw_deadline_boost_compute(
	const struct qhw_deadline_boost_config *config,
	int64_t base_priority,
	uint64_t deadline_ns,
	uint64_t estimated_runtime_ns,
	uint64_t now_ns,
	struct qhw_deadline_boost_result *result)
{
	uint64_t urgency;

	if (config == NULL || result == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	memset(result, 0, sizeof(*result));
	result->effective_priority = base_priority;
	result->next_refresh_ns = QHW_DEADLINE_BOOST_NEVER;
	result->tier = QHW_DEADLINE_BOOST_NONE;

	if (!config->enabled || deadline_ns == 0 ||
		estimated_runtime_ns == 0) {
		return QHW_SCHED_OK;
	}

	if (now_ns >= deadline_ns) {
		result->effective_priority = add_i64_saturating(
			base_priority, config->critical_boost);
		result->tier = QHW_DEADLINE_BOOST_CRITICAL;
		return QHW_SCHED_OK;
	}

	urgency = urgency_permille(estimated_runtime_ns,
		deadline_ns - now_ns);
	if (urgency > config->critical_threshold_permille) {
		result->effective_priority = add_i64_saturating(
			base_priority, config->critical_boost);
		result->tier = QHW_DEADLINE_BOOST_CRITICAL;
	} else if (urgency > config->urgent_threshold_permille) {
		result->effective_priority = add_i64_saturating(
			base_priority, config->urgent_boost);
		result->next_refresh_ns = next_threshold_time(deadline_ns,
			estimated_runtime_ns,
			config->critical_threshold_permille,
			now_ns);
		result->tier = QHW_DEADLINE_BOOST_URGENT;
	} else if (urgency > config->normal_threshold_permille) {
		result->effective_priority = add_i64_saturating(
			base_priority, config->normal_boost);
		result->next_refresh_ns = next_threshold_time(deadline_ns,
			estimated_runtime_ns,
			config->urgent_threshold_permille,
			now_ns);
		result->tier = QHW_DEADLINE_BOOST_NORMAL;
	} else {
		result->next_refresh_ns = next_threshold_time(deadline_ns,
			estimated_runtime_ns,
			config->normal_threshold_permille,
			now_ns);
	}

	return QHW_SCHED_OK;
}
