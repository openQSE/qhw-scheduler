#ifndef TEST_ORDERED_UTIL_H
#define TEST_ORDERED_UTIL_H

#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>

#ifndef QHW_ORDERED_PLUGIN_PATH
#error "QHW_ORDERED_PLUGIN_PATH must be defined"
#endif

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

static qhw_sched_task_desc_t make_test_task(
	qhw_sched_task_id_t task_id,
	int64_t priority,
	uint64_t runtime_ns)
{
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = task_id,
		.priority = priority,
		.estimated_runtime_ns = runtime_ns
	};

	return task;
}

static qhw_sched_kv_t make_test_u64(uint64_t key, uint64_t value)
{
	qhw_sched_kv_t option = {
		.key = key,
		.type = QHW_SCHED_VALUE_U64
	};

	option.value.u64 = value;
	return option;
}

static int make_ordered_scheduler(
	qhw_sched_qpu_t **qpu,
	qhw_sched_t **sched)
{
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 90,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, QHW_ORDERED_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	return 0;
}

static int set_ordered_options(
	qhw_sched_t *sched,
	qhw_sched_kv_t *options,
	size_t option_count)
{
	CHECK(qhw_sched_set_policy(sched, "ordered", options,
		option_count) == QHW_SCHED_OK);
	return 0;
}

static int submit_tasks(
	qhw_sched_t *sched,
	qhw_sched_task_desc_t *tasks,
	size_t task_count)
{
	size_t i;

	for (i = 0; i < task_count; i++) {
		CHECK(qhw_sched_submit_task(sched, &tasks[i]) ==
			QHW_SCHED_OK);
	}

	return 0;
}

static int expect_order(
	qhw_sched_t *sched,
	const qhw_sched_task_id_t *expected,
	size_t expected_count)
{
	size_t i;

	for (i = 0; i < expected_count; i++) {
		qhw_sched_assignment_t assignment;

		CHECK(qhw_sched_select_next(sched, &assignment) ==
			QHW_SCHED_OK);
		CHECK(assignment.task_id == expected[i]);
	}

	return 0;
}

static int run_ordered_case(
	qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_task_desc_t *tasks,
	size_t task_count,
	const qhw_sched_task_id_t *expected)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;

	CHECK(make_ordered_scheduler(&qpu, &sched) == 0);
	CHECK(set_ordered_options(sched, options, option_count) == 0);
	CHECK(submit_tasks(sched, tasks, task_count) == 0);
	CHECK(expect_order(sched, expected, task_count) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

#endif
