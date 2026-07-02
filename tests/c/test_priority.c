#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef QHW_PRIORITY_PLUGIN_PATH
#error "QHW_PRIORITY_PLUGIN_PATH must be defined"
#endif

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

static int make_scheduler(qhw_sched_qpu_t **qpu, qhw_sched_t **sched)
{
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 20,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, QHW_PRIORITY_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, "priority", NULL, 0) ==
		QHW_SCHED_OK);
	return 0;
}

static qhw_sched_task_desc_t make_task(
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = task_id,
		.priority = priority
	};

	return task;
}

static qhw_sched_kv_t make_u64_option(uint64_t key, uint64_t value)
{
	qhw_sched_kv_t option = {
		.key = key,
		.type = QHW_SCHED_VALUE_U64
	};

	option.value.u64 = value;
	return option;
}

static qhw_sched_kv_t make_i64_option(uint64_t key, int64_t value)
{
	qhw_sched_kv_t option = {
		.key = key,
		.type = QHW_SCHED_VALUE_I64
	};

	option.value.i64 = value;
	return option;
}

struct fail_allocator_state {
	size_t call_count;
	size_t fail_at;
};

static int fail_allocator_should_fail(struct fail_allocator_state *state)
{
	state->call_count++;
	return state->call_count == state->fail_at;
}

static void *fail_alloc(size_t size, void *user_data)
{
	struct fail_allocator_state *state = user_data;

	if (fail_allocator_should_fail(state)) {
		return NULL;
	}

	return malloc(size);
}

static void *fail_realloc(void *ptr, size_t size, void *user_data)
{
	struct fail_allocator_state *state = user_data;

	if (fail_allocator_should_fail(state)) {
		return NULL;
	}

	return realloc(ptr, size);
}

static void fail_free(void *ptr, void *user_data)
{
	(void)user_data;
	free(ptr);
}

static int make_fault_scheduler(
	struct fail_allocator_state *fail_state,
	qhw_sched_kv_t *options,
	size_t option_count,
	qhw_sched_qpu_t **qpu,
	qhw_sched_t **sched)
{
	qhw_sched_allocator_t allocator = {
		.struct_size = sizeof(allocator),
		.alloc = fail_alloc,
		.realloc = fail_realloc,
		.free = fail_free,
		.user_data = fail_state
	};
	qhw_sched_attr_t attr = {
		.struct_size = sizeof(attr),
		.threading = QHW_SCHED_THREAD_USER,
		.allocator = &allocator
	};
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 22,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, &attr, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, QHW_PRIORITY_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, "priority", options,
		option_count) == QHW_SCHED_OK);
	return 0;
}

static int test_priority_order(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t low = make_task(1, 1);
	qhw_sched_task_desc_t high = make_task(2, 10);
	qhw_sched_task_desc_t mid = make_task(3, 5);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &low) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &high) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &mid) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 2);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 3);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 1);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_tie_preserves_insert_order(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t first = make_task(11, 7);
	qhw_sched_task_desc_t second = make_task(12, 7);
	qhw_sched_task_desc_t third = make_task(13, 7);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &third) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 11);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 12);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 13);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_cancel_removes_ready_task(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t first = make_task(21, 100);
	qhw_sched_task_desc_t second = make_task(22, 10);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_cancelled(sched, 21) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 22);
	CHECK(qhw_sched_select_next(sched, &assignment) ==
		QHW_SCHED_ERR_NOT_FOUND);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_replays_existing_tasks(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 21,
		.num_qubits = 20
	};
	qhw_sched_task_desc_t first = make_task(31, 1);
	qhw_sched_task_desc_t second = make_task(32, 50);
	qhw_sched_task_desc_t third = make_task(33, 50);

	CHECK(qhw_sched_qpu_create(&profile, &qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, qpu, NULL, 0,
		&sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &third) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(sched, QHW_PRIORITY_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(sched, "priority", NULL, 0) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 32);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 33);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 31);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_reset_skips_assigned_task(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_state_t state;
	qhw_sched_task_desc_t first = make_task(41, 10);
	qhw_sched_task_desc_t second = make_task(42, 1);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 41);
	CHECK(qhw_sched_task_get_state(sched, 41, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_ASSIGNED);
	CHECK(qhw_sched_set_policy(sched, "priority", NULL, 0) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 42);
	CHECK(qhw_sched_select_next(sched, &assignment) ==
		QHW_SCHED_ERR_NOT_FOUND);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_update_reorders_ready_tasks(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t first = make_task(51, 1);
	qhw_sched_task_desc_t second = make_task(52, 5);
	qhw_sched_task_desc_t third = make_task(53, 10);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &third) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_update_priority(sched, 51, 20) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_task_update_priority(sched, 53, 0) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 51);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 52);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 53);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_update_rejects_assigned_task(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t task = make_task(61, 1);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 61);
	CHECK(qhw_sched_task_update_priority(sched, 61, 100) ==
		QHW_SCHED_ERR_STATE);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_grows_ready_heap(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	int i;

	CHECK(make_scheduler(&qpu, &sched) == 0);
	for (i = 1; i <= 100; i++) {
		qhw_sched_task_desc_t task = make_task(
			(qhw_sched_task_id_t)i,
			(int64_t)i);

		CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	}

	for (i = 100; i >= 1; i--) {
		CHECK(qhw_sched_select_next(sched, &assignment) ==
			QHW_SCHED_OK);
		CHECK(assignment.task_id == (qhw_sched_task_id_t)i);
	}

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_deadline_boost_disabled_by_default(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t late_low = make_task(71, 1);
	qhw_sched_task_desc_t high = make_task(72, 50);

	late_low.deadline_ns = 1000;
	late_low.estimated_runtime_ns = 100;

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &late_low) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &high) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 72);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_deadline_boost_reorders_priority_heap(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_kv_t options[] = {
		make_u64_option(QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE, 1),
		make_u64_option(QHW_SCHED_OPT_DEADLINE_NOW_NS, 970),
		make_i64_option(QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST, 100)
	};
	qhw_sched_task_desc_t urgent = make_task(81, 1);
	qhw_sched_task_desc_t high = make_task(82, 50);

	urgent.deadline_ns = 1000;
	urgent.estimated_runtime_ns = 100;

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_set_policy(sched, "priority", options,
		sizeof(options) / sizeof(options[0])) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &urgent) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &high) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 81);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 82);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_priority_submit_failure_rolls_back_policy_state(void)
{
	qhw_sched_kv_t options[] = {
		make_u64_option(QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE, 1),
		make_u64_option(QHW_SCHED_OPT_DEADLINE_NOW_NS, 0)
	};
	int saw_failure = 0;
	size_t delta;

	for (delta = 1; delta <= 16; delta++) {
		struct fail_allocator_state fail_state = {
			.call_count = 0,
			.fail_at = (size_t)-1
		};
		qhw_sched_qpu_t *qpu = NULL;
		qhw_sched_t *sched = NULL;
		qhw_sched_assignment_t assignment;
		qhw_sched_task_desc_t task = make_task(91, 1);
		qhw_sched_rc_t rc;

		task.deadline_ns = 1000;
		task.estimated_runtime_ns = 100;

		CHECK(make_fault_scheduler(&fail_state, options,
			sizeof(options) / sizeof(options[0]),
			&qpu, &sched) == 0);

		fail_state.fail_at = fail_state.call_count + delta;
		rc = qhw_sched_submit_task(sched, &task);
		fail_state.fail_at = (size_t)-1;
		if (rc != QHW_SCHED_OK) {
			saw_failure = 1;
			CHECK(qhw_sched_task_count(sched) == 0);
			CHECK(qhw_sched_submit_task(sched, &task) ==
				QHW_SCHED_OK);
			CHECK(qhw_sched_select_next(sched, &assignment) ==
				QHW_SCHED_OK);
			CHECK(assignment.task_id == 91);
			CHECK(qhw_sched_select_next(sched, &assignment) ==
				QHW_SCHED_ERR_NOT_FOUND);
		}

		qhw_sched_destroy(sched);
		qhw_sched_qpu_destroy(qpu);
	}

	CHECK(saw_failure);
	return 0;
}

int main(void)
{
	CHECK(test_priority_order() == 0);
	CHECK(test_priority_tie_preserves_insert_order() == 0);
	CHECK(test_priority_cancel_removes_ready_task() == 0);
	CHECK(test_priority_replays_existing_tasks() == 0);
	CHECK(test_priority_reset_skips_assigned_task() == 0);
	CHECK(test_priority_update_reorders_ready_tasks() == 0);
	CHECK(test_priority_update_rejects_assigned_task() == 0);
	CHECK(test_priority_grows_ready_heap() == 0);
	CHECK(test_deadline_boost_disabled_by_default() == 0);
	CHECK(test_deadline_boost_reorders_priority_heap() == 0);
	CHECK(test_priority_submit_failure_rolls_back_policy_state() == 0);
	return 0;
}
