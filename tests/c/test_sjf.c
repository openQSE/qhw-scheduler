#include "test_ordered_util.h"

static int test_sjf_orders_by_estimated_runtime(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(1, 0, 300),
		make_test_task(2, 0, 100),
		make_test_task(3, 0, 200)
	};
	qhw_sched_task_id_t expected[] = { 2, 3, 1 };

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_sjf_uses_shots_when_runtime_is_missing(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF)
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
	qhw_sched_task_id_t expected[] = { 12, 11 };

	tasks[0].metadata = first_metadata;
	tasks[0].metadata_count =
		sizeof(first_metadata) / sizeof(first_metadata[0]);
	tasks[1].metadata = second_metadata;
	tasks[1].metadata_count =
		sizeof(second_metadata) / sizeof(second_metadata[0]);

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

static int test_sjf_prefers_runtime_metadata_over_shots(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF)
	};
	qhw_sched_kv_t first_metadata[] = {
		make_test_u64(QHW_SCHED_META_ESTIMATED_RUNTIME_NS, 1000),
		make_test_u64(QHW_SCHED_META_SHOTS, 1)
	};
	qhw_sched_kv_t second_metadata[] = {
		make_test_u64(QHW_SCHED_META_ESTIMATED_RUNTIME_NS, 100),
		make_test_u64(QHW_SCHED_META_SHOTS, 10000)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(21, 0, 0),
		make_test_task(22, 0, 0)
	};
	qhw_sched_task_id_t expected[] = { 22, 21 };

	tasks[0].metadata = first_metadata;
	tasks[0].metadata_count =
		sizeof(first_metadata) / sizeof(first_metadata[0]);
	tasks[1].metadata = second_metadata;
	tasks[1].metadata_count =
		sizeof(second_metadata) / sizeof(second_metadata[0]);

	return run_ordered_case(options, sizeof(options) / sizeof(options[0]),
		tasks, sizeof(tasks) / sizeof(tasks[0]), expected);
}

struct cost_ctx {
	size_t called;
};

static qhw_sched_rc_t estimate_cost(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_qpu_profile_t *qpu,
	uint64_t *out_cost,
	void *user_data)
{
	struct cost_ctx *ctx = user_data;

	if (ctx == NULL || task == NULL || qpu == NULL ||
		out_cost == NULL || qpu->num_qubits != 20) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	ctx->called++;
	if (task->task_id == 31) {
		*out_cost = 5;
	} else if (task->task_id == 32) {
		*out_cost = 50;
	} else {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	return QHW_SCHED_OK;
}

static int test_sjf_uses_cost_callback(void)
{
	qhw_sched_kv_t options[] = {
		make_test_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF)
	};
	qhw_sched_task_desc_t tasks[] = {
		make_test_task(31, 0, 1000),
		make_test_task(32, 0, 1)
	};
	struct cost_ctx ctx = { 0 };
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.estimate_cost = estimate_cost,
		.user_data = &ctx
	};
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;

	CHECK(make_ordered_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(set_ordered_options(sched, options,
		sizeof(options) / sizeof(options[0])) == 0);
	CHECK(submit_tasks(sched, tasks,
		sizeof(tasks) / sizeof(tasks[0])) == 0);
	CHECK(ctx.called == 2);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 31);
	CHECK(assignment.estimated_cost == 5);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 32);
	CHECK(assignment.estimated_cost == 50);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

int main(void)
{
	CHECK(test_sjf_orders_by_estimated_runtime() == 0);
	CHECK(test_sjf_uses_shots_when_runtime_is_missing() == 0);
	CHECK(test_sjf_prefers_runtime_metadata_over_shots() == 0);
	CHECK(test_sjf_uses_cost_callback() == 0);
	return 0;
}
