#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>

#ifndef QHW_FIFO_PLUGIN_PATH
#error "QHW_FIFO_PLUGIN_PATH must be defined"
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
		.qpu_id = 10,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, QHW_FIFO_PLUGIN_PATH) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, "fifo", NULL, 0) == QHW_SCHED_OK);
	return 0;
}

static int test_fifo_order(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t first = {
		.struct_size = sizeof(first),
		.task_id = 1
	};
	qhw_sched_task_desc_t second = {
		.struct_size = sizeof(second),
		.task_id = 2
	};

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 1);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 2);
	CHECK(qhw_sched_select_next(sched, &assignment) ==
		QHW_SCHED_ERR_NOT_FOUND);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_cancel_removes_ready_task(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_assignment_t assignment;
	qhw_sched_task_desc_t first = {
		.struct_size = sizeof(first),
		.task_id = 3
	};
	qhw_sched_task_desc_t second = {
		.struct_size = sizeof(second),
		.task_id = 4
	};

	CHECK(make_scheduler(&qpu, &sched) == 0);
	CHECK(qhw_sched_submit_task(sched, &first) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &second) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_cancelled(sched, 3) == QHW_SCHED_OK);
	CHECK(qhw_sched_select_next(sched, &assignment) == QHW_SCHED_OK);
	CHECK(assignment.task_id == 4);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

int main(void)
{
	CHECK(test_fifo_order() == 0);
	CHECK(test_cancel_removes_ready_task() == 0);
	return 0;
}

