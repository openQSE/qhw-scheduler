#include "test_ordered_util.h"

static int test_sjf_priority_fifo(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY,
			QHW_SCHED_ORDER_PRIORITY),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 1, 100),
		make_test_task(2, 10, 100),
		make_test_task(3, 50, 200)
	};
	qhw_sched_task_id_t expected[] = { 2, 1, 3 };

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_ljf_priority_fifo(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_LJF),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY,
			QHW_SCHED_ORDER_PRIORITY),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 1, 500),
		make_test_task(2, 10, 500),
		make_test_task(3, 50, 100)
	};
	qhw_sched_task_id_t expected[] = { 2, 1, 3 };

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_sjf_round_robin_fifo(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY,
			QHW_SCHED_ORDER_ROUND_ROBIN),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 0, 100),
		make_test_task(2, 0, 100),
		make_test_task(3, 0, 100),
		make_test_task(4, 0, 100),
		make_test_task(5, 0, 900)
	};
	qhw_sched_task_id_t expected[] = { 1, 3, 2, 4, 5 };

	tasks[0].job_id = 10;
	tasks[1].job_id = 10;
	tasks[2].job_id = 20;
	tasks[3].job_id = 20;
	tasks[4].job_id = 30;

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_ljf_round_robin_fifo(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_LJF),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY,
			QHW_SCHED_ORDER_ROUND_ROBIN),
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 0, 500),
		make_test_task(2, 0, 500),
		make_test_task(3, 0, 500),
		make_test_task(4, 0, 500),
		make_test_task(5, 0, 100)
	};
	qhw_sched_task_id_t expected[] = { 1, 3, 2, 4, 5 };

	tasks[0].job_id = 10;
	tasks[1].job_id = 10;
	tasks[2].job_id = 20;
	tasks[3].job_id = 20;
	tasks[4].job_id = 30;

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

int main(void)
{
	CHECK(test_sjf_priority_fifo() == 0);
	CHECK(test_ljf_priority_fifo() == 0);
	CHECK(test_sjf_round_robin_fifo() == 0);
	CHECK(test_ljf_round_robin_fifo() == 0);
	return 0;
}
