#include "qhw_scheduler_internal.h"

#include <string.h>

static int is_split_option(uint64_t key)
{
	return key == QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD ||
		key == QHW_SCHED_OPT_SLICE_MAX_SHOTS ||
		key == QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS ||
		key == QHW_SCHED_OPT_SLICE_MAX_CHILDREN;
}

static void set_config_u64(
	qhw_sched_split_config_t *config,
	uint64_t key,
	uint64_t value)
{
	switch (key) {
	case QHW_SCHED_OPT_SLICE_SHOT_THRESHOLD:
		config->shot_threshold = value;
		return;
	case QHW_SCHED_OPT_SLICE_MAX_SHOTS:
		config->max_shots = value;
		return;
	case QHW_SCHED_OPT_SLICE_MIN_REMAINDER_SHOTS:
		config->min_remainder_shots = value;
		return;
	case QHW_SCHED_OPT_SLICE_MAX_CHILDREN:
		config->max_children = value;
		return;
	default:
		return;
	}
}

void qhw_sched_split_config_init(qhw_sched_split_config_t *config)
{
	if (config == NULL) {
		return;
	}

	memset(config, 0, sizeof(*config));
	config->struct_size = sizeof(*config);
}

qhw_sched_rc_t qhw_sched_split_config_parse_options(
	qhw_sched_split_config_t *config,
	const qhw_sched_kv_t *options,
	size_t option_count)
{
	size_t i;

	if (config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (option_count > 0 && options == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	for (i = 0; i < option_count; i++) {
		if (!is_split_option(options[i].key)) {
			continue;
		}

		if (options[i].type != QHW_SCHED_VALUE_U64) {
			return QHW_SCHED_ERR_INVALID_ARG;
		}

		set_config_u64(config, options[i].key, options[i].value.u64);
	}

	return QHW_SCHED_OK;
}
