#include "qhw_scheduler/qhw_scheduler.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
			__FILE__, __LINE__, #expr); \
		return 1; \
	} \
} while (0)

static int test_lifecycle(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 1,
		.num_qubits = 20
	};
	qhw_sched_qpu_runtime_t runtime;
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 42,
		.owner_id = 7,
		.job_id = 9
	};
	qhw_sched_task_state_t state;

	CHECK(qhw_sched_qpu_create(&profile, &qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, qpu, NULL, 0,
		&sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_get_threading(sched) == QHW_SCHED_THREAD_SAFE);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_count(sched) == 1);
	CHECK(qhw_sched_task_get_state(sched, 42, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_QUEUED);
	CHECK(qhw_sched_task_started(sched, 42) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_get_state(sched, 42, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_RUNNING);
	CHECK(qhw_sched_task_completed(sched, 42) == QHW_SCHED_OK);
	CHECK(qhw_sched_task_get_state(sched, 42, &state) == QHW_SCHED_OK);
	CHECK(state == QHW_SCHED_TASK_COMPLETED);
	CHECK(qhw_sched_qpu_get_runtime(qpu, &runtime) == QHW_SCHED_OK);
	CHECK(runtime.completed_count == 1);
	CHECK(runtime.running_task_id == QHW_SCHED_INVALID_TASK_ID);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_duplicate_task(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 2,
		.num_qubits = 8
	};
	qhw_sched_task_desc_t task = {
		.struct_size = sizeof(task),
		.task_id = 11
	};

	CHECK(qhw_sched_qpu_create(&profile, &qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, qpu, NULL, 0,
		&sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_OK);
	CHECK(qhw_sched_submit_task(sched, &task) == QHW_SCHED_ERR_EXISTS);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int test_user_threading(void)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 3,
		.num_qubits = 4
	};
	qhw_sched_attr_t attr = {
		.struct_size = sizeof(attr),
		.threading = QHW_SCHED_THREAD_USER
	};

	CHECK(qhw_sched_qpu_create(&profile, &qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, &attr, qpu, NULL, 0,
		&sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_get_threading(sched) == QHW_SCHED_THREAD_USER);

	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

int main(void)
{
	CHECK(test_lifecycle() == 0);
	CHECK(test_duplicate_task() == 0);
	CHECK(test_user_threading() == 0);
	return 0;
}

