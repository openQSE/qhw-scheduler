#define _POSIX_C_SOURCE 200809L

#include "qhw_scheduler/qhw_scheduler.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef QHW_FIFO_PLUGIN_PATH
#error "QHW_FIFO_PLUGIN_PATH must be defined"
#endif

#ifndef QHW_PRIORITY_PLUGIN_PATH
#error "QHW_PRIORITY_PLUGIN_PATH must be defined"
#endif

#ifndef QHW_ORDERED_PLUGIN_PATH
#error "QHW_ORDERED_PLUGIN_PATH must be defined"
#endif

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

#define WORKLOAD_METADATA_COUNT 5
#define WORKLOAD_MAX_ORDER_KEYS 4

struct workload_task {
	qhw_sched_task_desc_t desc;
	qhw_sched_kv_t metadata[WORKLOAD_METADATA_COUNT];
	uint64_t submit_seq;
	uint64_t cost;
};

struct workload_policy {
	const char *label;
	const char *policy_name;
	const char *plugin_path;
	qhw_sched_kv_t options[WORKLOAD_MAX_ORDER_KEYS];
	size_t option_count;
	uint64_t order_keys[WORKLOAD_MAX_ORDER_KEYS];
	size_t order_key_count;
	int verify_order;
};

struct workload_args {
	size_t task_count;
	int comprehensive;
};

static const struct workload_task *g_sort_tasks;
static const uint64_t *g_sort_keys;
static size_t g_sort_key_count;

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}

	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
		(uint64_t)ts.tv_nsec;
}

static qhw_sched_kv_t make_u64(uint64_t key, uint64_t value)
{
	qhw_sched_kv_t kv = {
		.key = key,
		.type = QHW_SCHED_VALUE_U64
	};

	kv.value.u64 = value;
	return kv;
}

static uint64_t task_metadata_u64(
	const struct workload_task *task,
	uint64_t key)
{
	size_t i;

	for (i = 0; i < task->desc.metadata_count; i++) {
		if (task->metadata[i].key == key &&
			task->metadata[i].type == QHW_SCHED_VALUE_U64) {
			return task->metadata[i].value.u64;
		}
	}

	return 0;
}

static uint64_t task_cost(const struct workload_task *task)
{
	uint64_t value;

	if (task->desc.estimated_runtime_ns != 0) {
		return task->desc.estimated_runtime_ns;
	}

	value = task_metadata_u64(task, QHW_SCHED_META_ESTIMATED_RUNTIME_NS);
	if (value != 0) {
		return value;
	}

	value = task_metadata_u64(task, QHW_SCHED_META_SHOTS);
	if (value != 0) {
		return value;
	}

	return 1;
}

static int compare_u64_asc(uint64_t left, uint64_t right)
{
	if (left == right) {
		return 0;
	}

	return left < right ? -1 : 1;
}

static int compare_i64_desc(int64_t left, int64_t right)
{
	if (left == right) {
		return 0;
	}

	return left > right ? -1 : 1;
}

static int compare_task_by_keys(
	const struct workload_task *left,
	const struct workload_task *right,
	const uint64_t *keys,
	size_t key_count)
{
	size_t i;

	for (i = 0; i < key_count; i++) {
		int cmp = 0;

		if (keys[i] == QHW_SCHED_ORDER_PRIORITY) {
			cmp = compare_i64_desc(left->desc.priority,
				right->desc.priority);
		} else if (keys[i] == QHW_SCHED_ORDER_SJF) {
			cmp = compare_u64_asc(left->cost, right->cost);
		} else if (keys[i] == QHW_SCHED_ORDER_LJF) {
			cmp = compare_u64_asc(right->cost, left->cost);
		} else if (keys[i] == QHW_SCHED_ORDER_FIFO) {
			cmp = compare_u64_asc(left->submit_seq,
				right->submit_seq);
		}

		if (cmp != 0) {
			return cmp;
		}
	}

	return compare_u64_asc(left->desc.task_id, right->desc.task_id);
}

static int expected_index_compare(const void *left_arg, const void *right_arg)
{
	size_t left_index = *(const size_t *)left_arg;
	size_t right_index = *(const size_t *)right_arg;
	const struct workload_task *left = &g_sort_tasks[left_index];
	const struct workload_task *right = &g_sort_tasks[right_index];

	return compare_task_by_keys(left, right, g_sort_keys,
		g_sort_key_count);
}

static void init_ordered_policy(
	struct workload_policy *policy,
	const char *label,
	const uint64_t *keys,
	size_t key_count)
{
	size_t i;

	memset(policy, 0, sizeof(*policy));
	policy->label = label;
	policy->policy_name = "ordered";
	policy->plugin_path = QHW_ORDERED_PLUGIN_PATH;
	policy->verify_order = 1;
	for (i = 0; i < key_count; i++) {
		policy->options[i] = make_u64(QHW_SCHED_OPT_ORDER_KEY,
			keys[i]);
		policy->order_keys[i] = keys[i];
	}
	policy->option_count = key_count;
	policy->order_key_count = key_count;
}

static void init_named_policy(
	struct workload_policy *policy,
	const char *label,
	const char *policy_name,
	const char *plugin_path)
{
	memset(policy, 0, sizeof(*policy));
	policy->label = label;
	policy->policy_name = policy_name;
	policy->plugin_path = plugin_path;
}

static void init_priority_policy(struct workload_policy *policy)
{
	static const uint64_t keys[] = {
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_FIFO
	};

	init_named_policy(policy, "priority", "priority",
		QHW_PRIORITY_PLUGIN_PATH);
	policy->verify_order = 1;
	memcpy(policy->order_keys, keys, sizeof(keys));
	policy->order_key_count = sizeof(keys) / sizeof(keys[0]);
}

static void init_fifo_policy(struct workload_policy *policy)
{
	static const uint64_t keys[] = {
		QHW_SCHED_ORDER_FIFO
	};

	init_named_policy(policy, "fifo", "fifo", QHW_FIFO_PLUGIN_PATH);
	policy->verify_order = 1;
	memcpy(policy->order_keys, keys, sizeof(keys));
	policy->order_key_count = sizeof(keys) / sizeof(keys[0]);
}

static void init_round_robin_policy(struct workload_policy *policy)
{
	init_named_policy(policy, "round_robin", "round_robin",
		QHW_ROUND_ROBIN_PLUGIN_PATH);
}

static size_t init_policies(
	struct workload_policy *policies,
	size_t policy_capacity,
	int comprehensive)
{
	static const uint64_t priority_fifo[] = {
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t sjf_fifo[] = {
		QHW_SCHED_ORDER_SJF,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t ljf_fifo[] = {
		QHW_SCHED_ORDER_LJF,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t sjf_priority_fifo[] = {
		QHW_SCHED_ORDER_SJF,
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t ljf_priority_fifo[] = {
		QHW_SCHED_ORDER_LJF,
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t priority_sjf_fifo[] = {
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_SJF,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t priority_ljf_fifo[] = {
		QHW_SCHED_ORDER_PRIORITY,
		QHW_SCHED_ORDER_LJF,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t round_robin_fifo[] = {
		QHW_SCHED_ORDER_ROUND_ROBIN,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t sjf_round_robin_fifo[] = {
		QHW_SCHED_ORDER_SJF,
		QHW_SCHED_ORDER_ROUND_ROBIN,
		QHW_SCHED_ORDER_FIFO
	};
	static const uint64_t ljf_round_robin_fifo[] = {
		QHW_SCHED_ORDER_LJF,
		QHW_SCHED_ORDER_ROUND_ROBIN,
		QHW_SCHED_ORDER_FIFO
	};
	size_t count = 0;

	if (policy_capacity < 13) {
		return 0;
	}

	init_fifo_policy(&policies[count++]);
	init_priority_policy(&policies[count++]);
	init_round_robin_policy(&policies[count++]);
	init_ordered_policy(&policies[count++], "ordered:priority,fifo",
		priority_fifo,
		sizeof(priority_fifo) / sizeof(priority_fifo[0]));
	init_ordered_policy(&policies[count++], "ordered:sjf,fifo",
		sjf_fifo, sizeof(sjf_fifo) / sizeof(sjf_fifo[0]));
	init_ordered_policy(&policies[count++], "ordered:ljf,fifo",
		ljf_fifo, sizeof(ljf_fifo) / sizeof(ljf_fifo[0]));
	init_ordered_policy(&policies[count++], "ordered:round_robin,fifo",
		round_robin_fifo,
		sizeof(round_robin_fifo) / sizeof(round_robin_fifo[0]));
	policies[count - 1].verify_order = 0;

	if (!comprehensive) {
		return count;
	}

	init_ordered_policy(&policies[count++],
		"ordered:sjf,priority,fifo",
		sjf_priority_fifo,
		sizeof(sjf_priority_fifo) / sizeof(sjf_priority_fifo[0]));
	init_ordered_policy(&policies[count++],
		"ordered:ljf,priority,fifo",
		ljf_priority_fifo,
		sizeof(ljf_priority_fifo) / sizeof(ljf_priority_fifo[0]));
	init_ordered_policy(&policies[count++],
		"ordered:priority,sjf,fifo",
		priority_sjf_fifo,
		sizeof(priority_sjf_fifo) / sizeof(priority_sjf_fifo[0]));
	init_ordered_policy(&policies[count++],
		"ordered:priority,ljf,fifo",
		priority_ljf_fifo,
		sizeof(priority_ljf_fifo) / sizeof(priority_ljf_fifo[0]));
	init_ordered_policy(&policies[count++],
		"ordered:sjf,round_robin,fifo",
		sjf_round_robin_fifo,
		sizeof(sjf_round_robin_fifo) / sizeof(sjf_round_robin_fifo[0]));
	policies[count - 1].verify_order = 0;
	init_ordered_policy(&policies[count++],
		"ordered:ljf,round_robin,fifo",
		ljf_round_robin_fifo,
		sizeof(ljf_round_robin_fifo) / sizeof(ljf_round_robin_fifo[0]));
	policies[count - 1].verify_order = 0;

	return count;
}

static void generate_task(struct workload_task *task, size_t index)
{
	uint64_t id = (uint64_t)index + 1;
	uint64_t shots = 64 + ((id * 37) % 8192);
	uint64_t depth = 1 + ((id * 11) % 256);
	uint64_t qubits = 1 + ((id * 7) % 20);
	uint64_t two_qubit = (id * 5) % depth;
	uint64_t runtime = 500 + shots * 4 + depth * 23 + two_qubit * 101;

	memset(task, 0, sizeof(*task));
	task->submit_seq = id;
	task->metadata[0] = make_u64(QHW_SCHED_META_SHOTS, shots);
	task->metadata[1] = make_u64(QHW_SCHED_META_DEPTH, depth);
	task->metadata[2] = make_u64(QHW_SCHED_META_NUM_QUBITS, qubits);
	task->metadata[3] = make_u64(QHW_SCHED_META_TWO_QUBIT_GATES,
		two_qubit);
	task->metadata[4] = make_u64(QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
		runtime);

	task->desc.struct_size = sizeof(task->desc);
	task->desc.task_id = id;
	task->desc.owner_id = 100 + (id % 17);
	task->desc.priority = (int64_t)((id * 13) % 64);
	task->desc.deadline_ns = UINT64_C(1000000) + id * 1000 + runtime;
	task->desc.metadata = task->metadata;
	task->desc.metadata_count = WORKLOAD_METADATA_COUNT;

	if (id % 10 == 0) {
		task->desc.estimated_runtime_ns = runtime / 2;
	}
	if (id % 3 != 0) {
		task->desc.job_id = 1000 + (id % 97);
	}
	if (id % 5 == 0) {
		task->desc.reservation_id = 2000 + (id % 23);
	}

	task->cost = task_cost(task);
}

static int generate_workload(
	struct workload_task **out_tasks,
	size_t task_count)
{
	struct workload_task *tasks;
	size_t i;

	tasks = calloc(task_count, sizeof(*tasks));
	if (tasks == NULL) {
		return 1;
	}

	for (i = 0; i < task_count; i++) {
		generate_task(&tasks[i], i);
	}

	*out_tasks = tasks;
	return 0;
}

static int make_scheduler(
	const struct workload_policy *policy,
	qhw_sched_qpu_t **qpu,
	qhw_sched_t **sched)
{
	qhw_sched_qpu_profile_t profile = {
		.struct_size = sizeof(profile),
		.qpu_id = 100,
		.num_qubits = 20
	};

	CHECK(qhw_sched_qpu_create(&profile, qpu) == QHW_SCHED_OK);
	CHECK(qhw_sched_create(NULL, NULL, *qpu, NULL, 0,
		sched) == QHW_SCHED_OK);
	CHECK(qhw_sched_load_plugin(*sched, policy->plugin_path) ==
		QHW_SCHED_OK);
	CHECK(qhw_sched_set_policy(*sched, policy->policy_name,
		policy->options, policy->option_count) == QHW_SCHED_OK);
	return 0;
}

static int build_expected_order(
	const struct workload_task *tasks,
	const struct workload_policy *policy,
	size_t task_count,
	size_t *expected)
{
	size_t i;

	if (!policy->verify_order) {
		return 0;
	}

	for (i = 0; i < task_count; i++) {
		expected[i] = i;
	}

	g_sort_tasks = tasks;
	g_sort_keys = policy->order_keys;
	g_sort_key_count = policy->order_key_count;
	qsort(expected, task_count, sizeof(expected[0]),
		expected_index_compare);
	return 0;
}

static int submit_workload(
	qhw_sched_t *sched,
	const struct workload_task *tasks,
	size_t task_count)
{
	size_t i;

	for (i = 0; i < task_count; i++) {
		CHECK(qhw_sched_submit_task(sched, &tasks[i].desc) ==
			QHW_SCHED_OK);
	}

	return 0;
}

static int drain_workload(
	qhw_sched_t *sched,
	const struct workload_task *tasks,
	const struct workload_policy *policy,
	const size_t *expected,
	size_t task_count)
{
	unsigned char *seen;
	size_t selected = 0;

	seen = calloc(task_count + 1, sizeof(*seen));
	if (seen == NULL) {
		return 1;
	}

	while (selected < task_count) {
		qhw_sched_assignment_t assignment;
		qhw_sched_task_id_t task_id;

		CHECK(qhw_sched_select_next(sched, &assignment) ==
			QHW_SCHED_OK);
		task_id = assignment.task_id;
		CHECK(task_id > 0);
		CHECK(task_id <= task_count);
		CHECK(!seen[task_id]);
		seen[task_id] = 1;

		if (policy->verify_order) {
			qhw_sched_task_id_t expected_id;

			expected_id = tasks[expected[selected]].desc.task_id;
			CHECK(task_id == expected_id);
		}

		CHECK(qhw_sched_task_started(sched, task_id) == QHW_SCHED_OK);
		CHECK(qhw_sched_task_completed(sched, task_id) ==
			QHW_SCHED_OK);
		selected++;
	}

	CHECK(qhw_sched_select_next(sched, &(qhw_sched_assignment_t){0}) ==
		QHW_SCHED_ERR_NOT_FOUND);
	free(seen);
	return 0;
}

static int run_policy_workload(
	const struct workload_task *tasks,
	const struct workload_policy *policy,
	size_t task_count)
{
	qhw_sched_qpu_t *qpu = NULL;
	qhw_sched_t *sched = NULL;
	qhw_sched_qpu_runtime_t runtime = {
		.struct_size = sizeof(runtime)
	};
	size_t *expected = NULL;
	uint64_t start_ns;
	uint64_t submit_done_ns;
	uint64_t drain_done_ns;
	double submit_sec;
	double drain_sec;

	expected = calloc(task_count, sizeof(*expected));
	if (expected == NULL) {
		return 1;
	}

	CHECK(make_scheduler(policy, &qpu, &sched) == 0);
	CHECK(build_expected_order(tasks, policy, task_count, expected) == 0);

	start_ns = now_ns();
	CHECK(submit_workload(sched, tasks, task_count) == 0);
	submit_done_ns = now_ns();
	CHECK(drain_workload(sched, tasks, policy, expected,
		task_count) == 0);
	drain_done_ns = now_ns();

	CHECK(qhw_sched_qpu_get_runtime(qpu, &runtime) == QHW_SCHED_OK);
	CHECK(runtime.queued_count == 0);
	CHECK(runtime.completed_count == task_count);
	CHECK(runtime.failed_count == 0);
	CHECK(runtime.cancelled_count == 0);
	CHECK(runtime.running_task_id == QHW_SCHED_INVALID_TASK_ID);

	submit_sec = (double)(submit_done_ns - start_ns) / 1000000000.0;
	drain_sec = (double)(drain_done_ns - submit_done_ns) / 1000000000.0;
	printf("%-28s tasks=%zu submit=%.6fs drain=%.6fs\n",
		policy->label, task_count, submit_sec, drain_sec);

	free(expected);
	qhw_sched_destroy(sched);
	qhw_sched_qpu_destroy(qpu);
	return 0;
}

static int run_workload(const struct workload_args *args)
{
	struct workload_policy policies[13];
	struct workload_task *tasks = NULL;
	size_t policy_count;
	size_t i;

	CHECK(generate_workload(&tasks, args->task_count) == 0);
	policy_count = init_policies(policies,
		sizeof(policies) / sizeof(policies[0]),
		args->comprehensive);
	CHECK(policy_count > 0);

	printf("workload mode=%s tasks=%zu policies=%zu\n",
		args->comprehensive ? "comprehensive" : "basic",
		args->task_count, policy_count);
	printf("timing scope: submit/drain hot path only; setup, plugin load, "
		"and expected-order sorting excluded\n");
	for (i = 0; i < policy_count; i++) {
		CHECK(run_policy_workload(tasks, &policies[i],
			args->task_count) == 0);
	}

	free(tasks);
	return 0;
}

static int parse_size(const char *arg, size_t *out_value)
{
	char *end = NULL;
	unsigned long value;

	value = strtoul(arg, &end, 10);
	if (end == NULL || *end != '\0' || value == 0) {
		return 1;
	}

	*out_value = (size_t)value;
	return 0;
}

static int parse_args(
	int argc,
	char **argv,
	struct workload_args *args)
{
	int i;

	args->task_count = 4096;
	args->comprehensive = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0) {
			i++;
			if (i >= argc) {
				return 1;
			}
			if (strcmp(argv[i], "basic") == 0) {
				args->comprehensive = 0;
			} else if (strcmp(argv[i], "comprehensive") == 0) {
				args->comprehensive = 1;
			} else {
				return 1;
			}
		} else if (strcmp(argv[i], "--tasks") == 0) {
			i++;
			if (i >= argc || parse_size(argv[i],
				&args->task_count) != 0) {
				return 1;
			}
		} else {
			return 1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct workload_args args;

	if (parse_args(argc, argv, &args) != 0) {
		fprintf(stderr,
			"usage: %s [--mode basic|comprehensive] [--tasks N]\n",
			argv[0]);
		return 1;
	}

	return run_workload(&args);
}
