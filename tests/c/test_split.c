#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>

#ifndef QHW_FIFO_PLUGIN_PATH
#error "QHW_FIFO_PLUGIN_PATH must be defined"
#endif

#ifndef QHW_FAIL_FINISH_PLUGIN_PATH
#error "QHW_FAIL_FINISH_PLUGIN_PATH must be defined"
#endif

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

struct split_ctx {
	qhw_sched_kv_t child_metadata[4][4];
	uint64_t expected_slice_shots;
	size_t expected_child_count;
	int called;
};

static qhw_sched_kv_t kv_u64(uint64_t key, uint64_t value)
{
	qhw_sched_kv_t kv = {
		.key = key,
		.type = QHW_SCHED_VALUE_U64
	};

	kv.value.u64 = value;
	return kv;
}

static qhw_sched_rc_t split_task(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data)
{
	struct split_ctx *ctx = user_data;
	size_t i;
	uint64_t remaining;

	if (ctx == NULL || task == NULL || config == NULL ||
		children == NULL ||
		child_count != ctx->expected_child_count ||
		config->slice_shots != ctx->expected_slice_shots) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	remaining = config->requested_shots;
	for (i = 0; i < child_count; i++) {
		uint64_t shots = remaining > config->slice_shots ?
			config->slice_shots : remaining;

		children[i].task_id = task->task_id + 100 + i;
		children[i].parent_task_id = task->task_id;
		children[i].metadata = ctx->child_metadata[i];
		children[i].metadata_count = 4;
		children[i].estimated_runtime_ns = shots;
		ctx->child_metadata[i][0] = kv_u64(QHW_SCHED_META_SHOTS,
			shots);
		ctx->child_metadata[i][1] = kv_u64(
			QHW_SCHED_META_PARENT_TASK_ID,
			task->task_id);
		ctx->child_metadata[i][2] = kv_u64(QHW_SCHED_META_SLICE_INDEX,
			i);
		ctx->child_metadata[i][3] = kv_u64(QHW_SCHED_META_SLICE_COUNT,
			child_count);
		remaining -= shots;
	}

	ctx->called++;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t split_task_bad_total(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data)
{
	struct split_ctx *ctx = user_data;
	size_t i;

	if (ctx == NULL || task == NULL || config == NULL ||
		children == NULL ||
		child_count != ctx->expected_child_count ||
		config->slice_shots != ctx->expected_slice_shots) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	for (i = 0; i < child_count; i++) {
		children[i].task_id = task->task_id + 100 + i;
		children[i].parent_task_id = task->task_id;
		children[i].metadata = ctx->child_metadata[i];
		children[i].metadata_count = 4;
		children[i].estimated_runtime_ns = config->slice_shots;
		ctx->child_metadata[i][0] = kv_u64(QHW_SCHED_META_SHOTS,
			config->slice_shots);
		ctx->child_metadata[i][1] = kv_u64(
			QHW_SCHED_META_PARENT_TASK_ID,
			task->task_id);
		ctx->child_metadata[i][2] = kv_u64(QHW_SCHED_META_SLICE_INDEX,
			i);
		ctx->child_metadata[i][3] = kv_u64(QHW_SCHED_META_SLICE_COUNT,
			child_count);
	}

	ctx->called++;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t split_task_missing_slice_metadata(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data)
{
	struct split_ctx *ctx = user_data;
	size_t i;
	uint64_t remaining;

	if (ctx == NULL || task == NULL || config == NULL ||
		children == NULL ||
		child_count != ctx->expected_child_count ||
		config->slice_shots != ctx->expected_slice_shots) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	remaining = config->requested_shots;
	for (i = 0; i < child_count; i++) {
		uint64_t shots = remaining > config->slice_shots ?
			config->slice_shots : remaining;

		children[i].task_id = task->task_id + 100 + i;
		children[i].parent_task_id = task->task_id;
		children[i].metadata = ctx->child_metadata[i];
		children[i].metadata_count = 2;
		ctx->child_metadata[i][0] = kv_u64(QHW_SCHED_META_SHOTS,
			shots);
		ctx->child_metadata[i][1] = kv_u64(
			QHW_SCHED_META_PARENT_TASK_ID,
			task->task_id);
		remaining -= shots;
	}

	ctx->called++;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t split_task_duplicate_slice_index(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data)
{
	struct split_ctx *ctx = user_data;
	size_t i;
	uint64_t remaining;

	if (ctx == NULL || task == NULL || config == NULL ||
		children == NULL ||
		child_count != ctx->expected_child_count ||
		config->slice_shots != ctx->expected_slice_shots) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	remaining = config->requested_shots;
	for (i = 0; i < child_count; i++) {
		uint64_t shots = remaining > config->slice_shots ?
			config->slice_shots : remaining;

		children[i].task_id = task->task_id + 100 + i;
		children[i].parent_task_id = task->task_id;
		children[i].metadata = ctx->child_metadata[i];
		children[i].metadata_count = 4;
		ctx->child_metadata[i][0] = kv_u64(QHW_SCHED_META_SHOTS,
			shots);
		ctx->child_metadata[i][1] = kv_u64(
			QHW_SCHED_META_PARENT_TASK_ID,
			task->task_id);
		ctx->child_metadata[i][2] = kv_u64(QHW_SCHED_META_SLICE_INDEX,
			0);
		ctx->child_metadata[i][3] = kv_u64(QHW_SCHED_META_SLICE_COUNT,
			child_count);
		remaining -= shots;
	}

	ctx->called++;
	return QHW_SCHED_OK;
}

static int make_scheduler_with_plugin(
	uint64_t qpu_max_shots,
	const qhw_sched_kv_t *options,
	size_t option_count,
	const char *plugin_path,
	const char *policy_name,
	qhw_sched_qpu_t **qpu,
	qhw_sched_t **sched)
{
	qhw_sched_kv_t qpu_metadata[] = {
		kv_u64(QHW_SCHED_META_MAX_SHOTS, qpu_max_shots)
	};
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 30,
		.num_qubits = 20,
		.metadata = qpu_metadata,
		.metadata_count = 1
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, plugin_path) == QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, policy_name, options,
		option_count) ==
		QHW_SCHED_OK);
	return 0;
}

static int make_scheduler(
	uint64_t qpu_max_shots,
	const qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_qpu_t **qpu,
	qhw_sched_t **sched)
{
	return make_scheduler_with_plugin(qpu_max_shots, options, option_count,
		QHW_FIFO_PLUGIN_PATH, "fifo", qpu, sched);
}

static int test_split_requires_callback(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 10,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &task) ==
		QHW_SCHED_ERR_UNSUPPORTED);
	CHECK(qhw_sched_task_count(sched) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_small_task_does_not_split(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 50)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 20,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_count(sched) == 1);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 20);
	CHECK(assignment.parent_task_id == QHW_SCHED_INVALID_TASK_ID);
	CHECK(assignment.slice_count == 1);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_qpu_limited_split(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_qpu_runtime_t runtime;
	qhw_sched_task_state_t state;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 1000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 4);
	CHECK(qhw_sched_task_get_state(sched, 1000, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_WAITING);
	CHECK(qhw_sched_qpu_get_runtime(qpu, &runtime) == QHW_SCHED_OK);
	CHECK(runtime.queued_count == 3);

	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 1100);
	CHECK(assignment.parent_task_id == 1000);
	CHECK(assignment.slice_index == 0);
	CHECK(assignment.slice_count == 3);
	CHECK(assignment.estimated_runtime_ns == 100);
	CHECK(qhw_sched_task_started(sched, 1100) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_completed(sched, 1100) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_get_state(sched, 1000, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_WAITING);

	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 1101);
	CHECK(qhw_sched_task_started(sched, 1101) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_completed(sched, 1101) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 1102);
	CHECK(assignment.estimated_runtime_ns == 50);
	CHECK(qhw_sched_task_started(sched, 1102) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_completed(sched, 1102) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_get_state(sched, 1000, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_COMPLETED);
	CHECK(qhw_sched_qpu_get_runtime(qpu, &runtime) == QHW_SCHED_OK);
	CHECK(runtime.queued_count == 0);
	CHECK(runtime.completed_count == 3);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_policy_threshold_forces_split_callback(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 1000,
		.expected_child_count = 1
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t options[] = {
		kv_u64(QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD, 100)
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 6000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(1000, options, 1, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 2);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_policy_threshold_capped_by_qpu_limit(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t options[] = {
		kv_u64(QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD, 1000)
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 7000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, options, 1, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 4);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_policy_slice_options_override_qpu_limit(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 80,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t options[] = {
		kv_u64(QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD, 100),
		kv_u64(QHW_SCHED_OPT_SLICE_MAX_SHOTS, 80)
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 170)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 2000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(1000, options, 2, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 4);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_min_remainder_option_does_not_drop_work(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t options[] = {
		kv_u64(QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD, 100),
		kv_u64(QHW_SCHED_OPT_SLICE_MAX_SHOTS, 100),
		kv_u64(QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS, 60)
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 3000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(200, options, 3, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 4);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_rejects_child_shot_total_mismatch(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task_bad_total,
		.user_data = &ctx
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 4000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) ==
		QHW_SCHED_ERR_INVALID_ARG);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_rejects_missing_slice_metadata(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task_missing_slice_metadata,
		.user_data = &ctx
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 8000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) ==
		QHW_SCHED_ERR_INVALID_ARG);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_rejects_duplicate_slice_index(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task_duplicate_slice_index,
		.user_data = &ctx
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 9000,
		.metadata = metadata,
		.metadata_count = 1
	};

	CHECK(make_scheduler(100, NULL, 0, &qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) ==
		QHW_SCHED_ERR_INVALID_ARG);
	CHECK(ctx.called == 1);
	CHECK(qhw_sched_task_count(sched) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_parent_updates_when_policy_finish_fails(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_state_t state;
	struct split_ctx ctx = {
		.expected_slice_shots = 100,
		.expected_child_count = 3
	};
	qhw_sched_callbacks_t callbacks = {
		.struct_size = sizeof(callbacks),
		.split_task = split_task,
		.user_data = &ctx
	};
	qhw_sched_kv_t metadata[] = {
		kv_u64(QHW_SCHED_META_SHOTS, 250)
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 5000,
		.metadata = metadata,
		.metadata_count = 1
	};
	size_t i;

	CHECK(make_scheduler_with_plugin(100, NULL, 0,
		QHW_FAIL_FINISH_PLUGIN_PATH, "fail_finish",
		&qpu, &sched) == 0);
	CHECK(qhw_sched_set_callbacks(sched, &callbacks) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);

	for (i = 0; i < 3; i++) {
		CHECK(qhw_sched_select_next(sched, &assignment) ==
			QHW_SCHED_OK);
		CHECK(qhw_sched_task_started(sched, assignment.task_id) ==
			QHW_SCHED_OK);
		CHECK(qhw_sched_task_completed(sched, assignment.task_id) ==
			QHW_SCHED_ERR_STATE);
	}

	CHECK(qhw_sched_task_get_state(sched, 5000, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_COMPLETED);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

int main(void)
{
	CHECK(test_split_requires_callback() == 0);
	CHECK(test_small_task_does_not_split() == 0);
	CHECK(test_qpu_limited_split() == 0);
	CHECK(test_policy_threshold_forces_split_callback() == 0);
	CHECK(test_policy_threshold_capped_by_qpu_limit() == 0);
	CHECK(test_policy_slice_options_override_qpu_limit() == 0);
	CHECK(test_min_remainder_option_does_not_drop_work() == 0);
	CHECK(test_rejects_child_shot_total_mismatch() == 0);
	CHECK(test_rejects_missing_slice_metadata() == 0);
	CHECK(test_rejects_duplicate_slice_index() == 0);
	CHECK(test_parent_updates_when_policy_finish_fails() == 0);
	return 0;
}
