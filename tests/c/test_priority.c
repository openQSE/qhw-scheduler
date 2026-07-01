#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>

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

int main(void)
{
	CHECK(test_priority_order() == 0);
	CHECK(test_priority_tie_preserves_insert_order() == 0);
	CHECK(test_priority_cancel_removes_ready_task() == 0);
	CHECK(test_priority_replays_existing_tasks() == 0);
	CHECK(test_priority_reset_skips_assigned_task() == 0);
	CHECK(test_priority_grows_ready_heap() == 0);
	return 0;
}
