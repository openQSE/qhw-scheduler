#include "test_ordered_util.h"

static int test_ljf_orders_by_estimated_runtime(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_LJF)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 0, 300),
		make_test_task(2, 0, 100),
		make_test_task(3, 0, 200)
	};
	qhw_sched_task_id_t expected[] = { 1, 3, 2 };

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_ljf_uses_shots_when_runtime_is_missing(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_LJF)
	};
	qhw_sched_kv_t first_metadata[] = {
		make_test_u64(QHW_SCHED_META_SHOTS, 1000)
	};
	qhw_sched_kv_t second_metadata[] = {
		make_test_u64(QHW_SCHED_META_SHOTS, 100)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(11, 0, 0),
		make_test_task(12, 0, 0)
	};
	qhw_sched_task_id_t expected[] = { 11, 12 };

	tasks[0].metadata = first_metadata;
	tasks[0].metadata_count =
		sizeof(first_metadata) / sizeof(first_metadata[0]);
	tasks[1].metadata = second_metadata;
	tasks[1].metadata_count =
		sizeof(second_metadata) / sizeof(second_metadata[0]);

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

int main(void)
{
	CHECK(test_ljf_orders_by_estimated_runtime() == 0);
	CHECK(test_ljf_uses_shots_when_runtime_is_missing() == 0);
	return 0;
}
