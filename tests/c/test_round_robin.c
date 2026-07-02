#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>

#ifndef QHW_ROUND_ROBIN_PLUGIN_PATH
#error "QHW_ROUND_ROBIN_PLUGIN_PATH must be defined"
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
		.qpu_id = 30,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, QHW_ROUND_ROBIN_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, "round_robin", NULL, 0) ==
		QHW_SCHED_OK);
	return 0;
}

static qhw_sched_task_desc_t make_task(
	qhw_sched_task_id_t task_id,
	uint64_t job_id,
	uint64_t reservation_id)
{
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = task_id,
		.job_id = job_id,
		.reservation_id = reservation_id
	};

	return task;
}

static int expect_next(qhw_sched_t *sched, qhw_sched_task_id_t task_id)
{
	qhw_sched_assignment_t assignment;

	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == task_id);
	return 0;
}

static int test_round_robin_by_job(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_task_desc_t a1 = make_task(1, 10, 0);
	qhw_sched_task_desc_t a2 = make_task(2, 10, 0);
	qhw_sched_task_desc_t b1 = make_task(3, 20, 0);
	qhw_sched_task_desc_t b2 = make_task(4, 20, 0);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &a1) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &a2) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &b1) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &b2) == QHW_SCHED_OK);

	CHECK(expect_next(sched, 1) == 0);
	CHECK(expect_next(sched, 3) == 0);
	CHECK(expect_next(sched, 2) == 0);
	CHECK(expect_next(sched, 4) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_reservation_groups_override_job(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_task_desc_t r1a = make_task(11, 100, 7);
	qhw_sched_task_desc_t r1b = make_task(12, 200, 7);
	qhw_sched_task_desc_t r2a = make_task(13, 100, 8);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &r1a) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &r1b) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &r2a) == QHW_SCHED_OK);

	CHECK(expect_next(sched, 11) == 0);
	CHECK(expect_next(sched, 13) == 0);
	CHECK(expect_next(sched, 12) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_ungrouped_tasks_are_singletons(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_task_desc_t first = make_task(21, 0, 0);
	qhw_sched_task_desc_t second = make_task(22, 0, 0);
	qhw_sched_task_desc_t third = make_task(23, 0, 0);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &third) == QHW_SCHED_OK);

	CHECK(expect_next(sched, 21) == 0);
	CHECK(expect_next(sched, 22) == 0);
	CHECK(expect_next(sched, 23) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_group_namespaces_do_not_collide(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_task_desc_t job_a = make_task(20, 10, 0);
	qhw_sched_task_desc_t job_b = make_task(21, 10, 0);
	qhw_sched_task_desc_t singleton = make_task(10, 0, 0);
	qhw_sched_task_desc_t res_a = make_task(30, 99, 10);
	qhw_sched_task_desc_t res_b = make_task(31, 99, 10);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &job_a) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &job_b) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &singleton) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &res_a) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &res_b) == QHW_SCHED_OK);

	CHECK(expect_next(sched, 20) == 0);
	CHECK(expect_next(sched, 10) == 0);
	CHECK(expect_next(sched, 30) == 0);
	CHECK(expect_next(sched, 21) == 0);
	CHECK(expect_next(sched, 31) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_cancel_removes_group_when_empty(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_task_desc_t a1 = make_task(31, 10, 0);
	qhw_sched_task_desc_t a2 = make_task(32, 10, 0);
	qhw_sched_task_desc_t b1 = make_task(33, 20, 0);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &a1) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &a2) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &b1) == QHW_SCHED_OK);

	CHECK(qhw_sched_task_cancelled(sched, 31) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_cancelled(sched, 32) == QHW_SCHED_OK);
	CHECK(expect_next(sched, 33) == 0);
	CHECK(qhw_sched_select_next(sched, &(qhw_sched_assignment_t){0}) ==
		QHW_SCHED_ERR_NOT_FOUND);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_policy_replay_preserves_round_robin_order(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t a1 = make_task(41, 10, 0);
	qhw_sched_task_desc_t a2 = make_task(42, 10, 0);
	qhw_sched_task_desc_t b1 = make_task(43, 20, 0);

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &a1) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &a2) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &b1) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 41);
	CHECK(qhw_sched_set_policy(sched, "round_robin", NULL, 0) ==
		QHW_SCHED_OK);
	CHECK(expect_next(sched, 42) == 0);
	CHECK(expect_next(sched, 43) == 0);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

int main(void)
{
	CHECK(test_round_robin_by_job() == 0);
	CHECK(test_reservation_groups_override_job() == 0);
	CHECK(test_ungrouped_tasks_are_singletons() == 0);
	CHECK(test_group_namespaces_do_not_collide() == 0);
	CHECK(test_cancel_removes_group_when_empty() == 0);
	CHECK(test_policy_replay_preserves_round_robin_order() == 0);
	return 0;
}
