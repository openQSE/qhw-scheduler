#ifndef QHW_POLICY_METADATA_H
#define QHW_POLICY_METADATA_H

#include "qhw_scheduler/qhw_scheduler_plugin.h"

#include <stdint.h>

int qhw_policy_metadata_get_u64(
	const qhw_sched_kv_t *metadata,
	size_t metadata_count,
	uint64_t key,
	uint64_t *out_value);

uint64_t qhw_policy_task_estimated_cost(
	const qhw_sched_task_desc_t *task);

#endif
