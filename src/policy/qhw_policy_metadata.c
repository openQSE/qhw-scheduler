#include "policy/qhw_policy_metadata.h"

int qhw_policy_metadata_get_u64(
	const qhw_sched_kv_t *metadata,
	size_t metadata_count,
	uint64_t key,
	uint64_t *out_value)
{
	size_t i;

	if (out_value == NULL) {
		return 0;
	}

	if (metadata_count > 0 && metadata == NULL) {
		return 0;
	}

	for (i = 0; i < metadata_count; i++) {
		if (metadata[i].key == key &&
			metadata[i].type == QHW_SCHED_VALUE_U64) {
			*out_value = metadata[i].value.u64;
			return 1;
		}
	}

	return 0;
}

uint64_t qhw_policy_task_estimated_cost(
	const qhw_sched_task_desc_t *task)
{
	uint64_t runtime_ns;
	uint64_t shots;

	if (task == NULL) {
		return 1;
	}

	if (task->estimated_cost != 0) {
		return task->estimated_cost;
	}

	if (task->estimated_runtime_ns != 0) {
		return task->estimated_runtime_ns;
	}

	if (qhw_policy_metadata_get_u64(task->metadata,
		task->metadata_count,
		QHW_SCHED_META_ESTIMATED_RUNTIME_NS, &runtime_ns) &&
		runtime_ns != 0) {
		return runtime_ns;
	}

	if (qhw_policy_metadata_get_u64(task->metadata,
		task->metadata_count, QHW_SCHED_META_SHOTS, &shots) &&
		shots != 0) {
		return shots;
	}

	return 1;
}
