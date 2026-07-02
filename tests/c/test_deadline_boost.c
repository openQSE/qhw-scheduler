#include "policy/qhw_deadline_boost.h"

#include <stdint.h>
#include <limits.h>
#include <stdio.h>

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

static int test_disabled_boost_keeps_base_priority(void)
{
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 0;
	CHECK(qhw_deadline_boost_compute(&config, 5, 1000, 100, 900,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 5);
	CHECK(result.next_refresh_ns == QHW_DEADLINE_BOOST_NEVER);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NONE);
	return 0;
}

static int test_boost_tiers_follow_urgency(void)
{
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 1;

	CHECK(qhw_deadline_boost_compute(&config, 10, 1000, 100, 0,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 10);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NONE);

	CHECK(qhw_deadline_boost_compute(&config, 10, 1000, 100, 910,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 20);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NORMAL);

	CHECK(qhw_deadline_boost_compute(&config, 10, 1000, 100, 960,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 60);
	CHECK(result.tier == QHW_DEADLINE_BOOST_URGENT);

	CHECK(qhw_deadline_boost_compute(&config, 10, 1000, 100, 970,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 110);
	CHECK(result.tier == QHW_DEADLINE_BOOST_CRITICAL);
	return 0;
}

static int test_expired_deadline_is_critical(void)
{
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 1;

	CHECK(qhw_deadline_boost_compute(&config, -3, 1000, 10, 1000,
		&result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 97);
	CHECK(result.next_refresh_ns == QHW_DEADLINE_BOOST_NEVER);
	CHECK(result.tier == QHW_DEADLINE_BOOST_CRITICAL);
	return 0;
}

static int test_next_refresh_points_to_next_threshold(void)
{
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 1;

	CHECK(qhw_deadline_boost_compute(&config, 1, 1000, 100, 0,
		&result) == QHW_SCHED_OK);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NONE);
	CHECK(result.next_refresh_ns == 900);

	CHECK(qhw_deadline_boost_compute(&config, 1, 1000, 100, 910,
		&result) == QHW_SCHED_OK);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NORMAL);
	CHECK(result.next_refresh_ns == 950);
	return 0;
}

static int test_boost_addition_saturates(void)
{
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 1;
	config.critical_boost = 100;
	CHECK(qhw_deadline_boost_compute(&config, INT64_MAX, 1000, 10,
		1000, &result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == INT64_MAX);
	CHECK(result.tier == QHW_DEADLINE_BOOST_CRITICAL);

	config.critical_boost = -100;
	CHECK(qhw_deadline_boost_compute(&config, INT64_MIN, 1000, 10,
		1000, &result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == INT64_MIN);
	CHECK(result.tier == QHW_DEADLINE_BOOST_CRITICAL);
	return 0;
}

static int test_large_runtime_preserves_urgency_ratio(void)
{
	const uint64_t day_ns = UINT64_C(24) * UINT64_C(60) *
		UINT64_C(60) * UINT64_C(1000000000);
	struct qhw_deadline_boost_config config;
	struct qhw_deadline_boost_result result;
	uint64_t runtime_ns = UINT64_C(300) * day_ns;
	uint64_t slack_ns = UINT64_C(250) * day_ns;
	uint64_t expected_refresh = UINT64_C(100) * day_ns;

	qhw_deadline_boost_config_init(&config);
	config.enabled = 1;

	CHECK(qhw_deadline_boost_compute(&config, 5, slack_ns,
		runtime_ns, 0, &result) == QHW_SCHED_OK);
	CHECK(result.effective_priority == 15);
	CHECK(result.tier == QHW_DEADLINE_BOOST_NORMAL);
	CHECK(result.next_refresh_ns == expected_refresh);
	return 0;
}

int main(void)
{
	CHECK(test_disabled_boost_keeps_base_priority() == 0);
	CHECK(test_boost_tiers_follow_urgency() == 0);
	CHECK(test_expired_deadline_is_critical() == 0);
	CHECK(test_next_refresh_points_to_next_threshold() == 0);
	CHECK(test_boost_addition_saturates() == 0);
	CHECK(test_large_runtime_preserves_urgency_ratio() == 0);
	return 0;
}
