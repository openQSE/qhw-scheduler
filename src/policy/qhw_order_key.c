#include "policy/qhw_order_key.h"

#include <string.h>

static int order_key_supported(uint64_t key)
{
	return key == QHW_SCHED_ORDER_PRIORITY ||
		key == QHW_SCHED_ORDER_SJF ||
		key == QHW_SCHED_ORDER_FIFO;
}

static qhw_sched_rc_t order_config_append_key(
	struct qhw_order_config *config,
	uint64_t key)
{
	if (!order_key_supported(key)) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	if (config->key_count >= QHW_ORDER_KEY_MAX) {
		return QHW_SCHED_ERR_UNSUPPORTED;
	}

	config->keys[config->key_count++] = key;
	return QHW_SCHED_OK;
}

static qhw_sched_rc_t parse_order_key(
	struct qhw_order_config *config,
	const qhw_sched_kv_t *option)
{
	if (option->type != QHW_SCHED_VALUE_U64) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	return order_config_append_key(config, option->value.u64);
}

static qhw_sched_rc_t parse_now_option(
	struct qhw_order_config *config,
	const qhw_sched_kv_t *option)
{
	if (option->type != QHW_SCHED_VALUE_U64) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	config->now_ns = option->value.u64;
	config->use_static_now = 1;
	return QHW_SCHED_OK;
}

static int compare_i64_desc(int64_t left, int64_t right)
{
	if (left == right) {
		return 0;
	}

	return left > right ? -1 : 1;
}

static int compare_u64_asc(uint64_t left, uint64_t right)
{
	if (left == right) {
		return 0;
	}

	return left < right ? -1 : 1;
}

void qhw_order_config_init(struct qhw_order_config *config)
{
	if (config == NULL) {
		return;
	}

	memset(config, 0, sizeof(*config));
	config->keys[0] = QHW_SCHED_ORDER_PRIORITY;
	config->keys[1] = QHW_SCHED_ORDER_FIFO;
	config->key_count = 2;
	qhw_deadline_boost_config_init(&config->boost);
}

qhw_sched_rc_t qhw_order_config_parse_options(
	struct qhw_order_config *config,
	const qhw_sched_kv_t *options,
	size_t option_count)
{
	size_t i;
	int saw_order_key = 0;
	qhw_sched_rc_t rc;

	if (config == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (option_count > 0 && options == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = qhw_deadline_boost_config_parse_options(&config->boost,
		options, option_count);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	for (i = 0; i < option_count; i++) {
		if (options[i].key == QHW_SCHED_OPT_ORDER_KEY) {
			if (!saw_order_key) {
				config->key_count = 0;
				saw_order_key = 1;
			}
			rc = parse_order_key(config, &options[i]);
		} else if (options[i].key == QHW_SCHED_OPT_DEADLINE_NOW_NS) {
			rc = parse_now_option(config, &options[i]);
		} else {
			rc = QHW_SCHED_OK;
		}

		if (rc != QHW_SCHED_OK) {
			return rc;
		}
	}

	if (config->key_count == 0) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	return QHW_SCHED_OK;
}

int qhw_order_config_uses_cost(const struct qhw_order_config *config)
{
	size_t i;

	if (config == NULL) {
		return 0;
	}

	for (i = 0; i < config->key_count; i++) {
		if (config->keys[i] == QHW_SCHED_ORDER_SJF ||
			config->keys[i] == QHW_SCHED_ORDER_LJF) {
			return 1;
		}
	}

	return 0;
}

uint64_t qhw_order_now_ns(const struct qhw_order_config *config)
{
	if (config != NULL && config->use_static_now) {
		return config->now_ns;
	}

	return qhw_deadline_boost_now_ns();
}

qhw_sched_rc_t qhw_order_refresh_task(
	const struct qhw_order_config *config,
	struct qhw_ready_task *task)
{
	struct qhw_deadline_boost_result result;
	qhw_sched_rc_t rc;

	if (config == NULL || task == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = qhw_deadline_boost_compute(&config->boost,
		task->base_priority,
		task->desc.deadline_ns,
		task->desc.estimated_runtime_ns,
		qhw_order_now_ns(config),
		&result);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	task->effective_priority = result.effective_priority;
	task->next_refresh_ns = result.next_refresh_ns;
	return QHW_SCHED_OK;
}

int qhw_order_compare(
	const struct qhw_order_config *config,
	const struct qhw_ready_task *left,
	const struct qhw_ready_task *right)
{
	size_t i;

	for (i = 0; i < config->key_count; i++) {
		int cmp = 0;

		if (config->keys[i] == QHW_SCHED_ORDER_PRIORITY) {
			cmp = compare_i64_desc(left->effective_priority,
				right->effective_priority);
		} else if (config->keys[i] == QHW_SCHED_ORDER_SJF) {
			cmp = compare_u64_asc(left->estimated_cost,
				right->estimated_cost);
		} else if (config->keys[i] == QHW_SCHED_ORDER_FIFO) {
			cmp = compare_u64_asc(left->seq, right->seq);
		}

		if (cmp != 0) {
			return cmp;
		}
	}

	return compare_u64_asc(left->desc.task_id, right->desc.task_id);
}
