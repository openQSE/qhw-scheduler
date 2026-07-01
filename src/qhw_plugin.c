#include "qhw_scheduler_internal.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static char *qhw_strdup(
	struct qhw_allocator *allocator,
	const char *value)
{
	size_t len;
	char *copy;

	if (value == NULL) {
		return NULL;
	}

	len = strlen(value) + 1;
	copy = qhw_alloc(allocator, len);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, value, len);
	return copy;
}

static uint64_t threading_to_plugin_flag(qhw_sched_threading_t threading)
{
	if (threading == QHW_SCHED_THREAD_SAFE) {
		return QHW_SCHED_PLUGIN_THREAD_SAFE;
	}

	if (threading == QHW_SCHED_THREAD_USER) {
		return QHW_SCHED_PLUGIN_THREAD_USER;
	}

	return 0;
}

static qhw_sched_rc_t validate_plugin_desc(
	qhw_sched_t *sched,
	const qhw_sched_plugin_desc_t *desc)
{
	uint64_t supported_thread_flags = QHW_SCHED_PLUGIN_THREAD_ALL;
	uint64_t required_thread_flag;
	size_t abi_size;

	if (desc == NULL) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	abi_size = offsetof(qhw_sched_plugin_desc_t, abi_version) +
		sizeof(desc->abi_version);
	if (desc->struct_size < abi_size) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	if (desc->abi_version != QHW_SCHED_ABI_VERSION ||
		desc->struct_size < sizeof(*desc) ||
		desc->name == NULL ||
		desc->init == NULL ||
		desc->fini == NULL ||
		desc->on_task_submit == NULL ||
		desc->select_next == NULL ||
		desc->on_task_started == NULL ||
		desc->on_task_finished == NULL) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	if (desc->capabilities != 0) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	if (desc->thread_flags == 0 ||
		(desc->thread_flags & ~supported_thread_flags) != 0) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	required_thread_flag = threading_to_plugin_flag(sched->threading);
	if ((desc->thread_flags & required_thread_flag) == 0) {
		return QHW_SCHED_ERR_PLUGIN;
	}

	return QHW_SCHED_OK;
}

void qhw_plugin_registry_fini(
	struct qhw_plugin_registry *registry,
	struct qhw_allocator *allocator)
{
	size_t i;

	if (registry == NULL) {
		return;
	}

	for (i = 0; i < registry->count; i++) {
		if (registry->items[i].dl_handle != NULL) {
			(void)dlclose(registry->items[i].dl_handle);
		}
		qhw_free(allocator, registry->items[i].path);
	}

	qhw_free(allocator, registry->items);
	registry->items = NULL;
	registry->count = 0;
	registry->capacity = 0;
}

qhw_sched_rc_t qhw_plugin_registry_add(
	struct qhw_plugin_registry *registry,
	struct qhw_allocator *allocator,
	const qhw_sched_plugin_desc_t *desc,
	const char *path,
	void *dl_handle)
{
	struct qhw_sched_plugin *slot;
	size_t i;

	if (registry == NULL || allocator == NULL || desc == NULL ||
		desc->name == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	for (i = 0; i < registry->count; i++) {
		if (strcmp(registry->items[i].desc.name, desc->name) == 0) {
			return QHW_SCHED_ERR_EXISTS;
		}
	}

	if (registry->count == registry->capacity) {
		size_t next = registry->capacity == 0 ? 4 : registry->capacity * 2;
		size_t bytes = next * sizeof(*registry->items);
		void *items = qhw_realloc(allocator, registry->items, bytes);

		if (items == NULL) {
			return QHW_SCHED_ERR_NO_MEMORY;
		}

		registry->items = items;
		registry->capacity = next;
	}

	slot = &registry->items[registry->count];
	memset(slot, 0, sizeof(*slot));
	slot->desc = *desc;
	slot->path = qhw_strdup(allocator, path);
	if (path != NULL && slot->path == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	slot->dl_handle = dl_handle;
	registry->count++;
	return QHW_SCHED_OK;
}

struct qhw_sched_plugin *qhw_plugin_registry_find(
	struct qhw_plugin_registry *registry,
	const char *name)
{
	size_t i;

	if (registry == NULL || name == NULL) {
		return NULL;
	}

	for (i = 0; i < registry->count; i++) {
		if (strcmp(registry->items[i].desc.name, name) == 0) {
			return &registry->items[i];
		}
	}

	return NULL;
}

qhw_sched_rc_t qhw_sched_load_plugin(
	qhw_sched_t *sched,
	const char *shared_object_path)
{
	void *handle;
	qhw_sched_plugin_descriptor_fn descriptor_fn;
	const qhw_sched_plugin_desc_t *desc;
	qhw_sched_rc_t rc;

	if (sched == NULL || shared_object_path == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	handle = dlopen(shared_object_path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		qhw_error_set(&sched->last_error, QHW_SCHED_ERR_PLUGIN, dlerror());
		return QHW_SCHED_ERR_PLUGIN;
	}

	descriptor_fn = (qhw_sched_plugin_descriptor_fn)
		dlsym(handle, "qhw_sched_plugin_descriptor");
	if (descriptor_fn == NULL) {
		qhw_error_set(&sched->last_error, QHW_SCHED_ERR_PLUGIN, dlerror());
		(void)dlclose(handle);
		return QHW_SCHED_ERR_PLUGIN;
	}

	desc = descriptor_fn();
	if (validate_plugin_desc(sched, desc) != QHW_SCHED_OK) {
		qhw_error_set(&sched->last_error, QHW_SCHED_ERR_PLUGIN,
			"invalid scheduler plugin descriptor");
		(void)dlclose(handle);
		return QHW_SCHED_ERR_PLUGIN;
	}

	sched->lock_ops.lock(&sched->lock);
	rc = qhw_plugin_registry_add(&sched->plugins, &sched->allocator,
		desc, shared_object_path, handle);
	sched->lock_ops.unlock(&sched->lock);

	if (rc != QHW_SCHED_OK) {
		(void)dlclose(handle);
	}

	return rc;
}

qhw_sched_rc_t qhw_sched_list_policies(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t **out_policies,
	size_t *out_count)
{
	size_t i;
	qhw_sched_policy_info_t *policies;

	if (sched == NULL || out_policies == NULL || out_count == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	if (sched->plugins.count == 0) {
		*out_policies = NULL;
		*out_count = 0;
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_OK;
	}

	policies = qhw_alloc(&sched->allocator,
		sched->plugins.count * sizeof(*policies));
	if (policies == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	for (i = 0; i < sched->plugins.count; i++) {
		const qhw_sched_plugin_desc_t *desc = &sched->plugins.items[i].desc;

		policies[i].struct_size = sizeof(policies[i]);
		policies[i].name = desc->name;
		policies[i].version = desc->version;
		policies[i].description = desc->description;
		policies[i].capabilities = desc->capabilities;
		policies[i].thread_flags = desc->thread_flags;
		policies[i].plugin_path = sched->plugins.items[i].path;
	}

	*out_policies = policies;
	*out_count = sched->plugins.count;
	sched->lock_ops.unlock(&sched->lock);
	return QHW_SCHED_OK;
}

void qhw_sched_free_policy_info_array(
	qhw_sched_t *sched,
	qhw_sched_policy_info_t *policies)
{
	if (sched != NULL) {
		qhw_free(&sched->allocator, policies);
	}
}

struct replay_policy_ctx {
	const qhw_sched_plugin_desc_t *desc;
	void *state;
};

static qhw_sched_rc_t replay_queued_task(
	struct qhw_task_record *record,
	void *user_data)
{
	struct replay_policy_ctx *ctx = user_data;

	return ctx->desc->on_task_submit(ctx->state, &record->desc);
}

qhw_sched_rc_t qhw_sched_set_policy(
	qhw_sched_t *sched,
	const char *policy_name,
	const qhw_sched_kv_t *options,
	size_t option_count)
{
	struct qhw_sched_plugin *plugin;
	struct replay_policy_ctx replay;
	void *state = NULL;
	qhw_sched_rc_t rc;

	if (sched == NULL || policy_name == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	sched->lock_ops.lock(&sched->lock);
	plugin = qhw_plugin_registry_find(&sched->plugins, policy_name);
	if (plugin == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_NOT_FOUND;
	}

	if (option_count > 0 && options == NULL) {
		sched->lock_ops.unlock(&sched->lock);
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	rc = plugin->desc.init(sched, options, option_count, &state);
	if (rc != QHW_SCHED_OK) {
		sched->lock_ops.unlock(&sched->lock);
		return rc;
	}

	replay.desc = &plugin->desc;
	replay.state = state;
	rc = qhw_task_table_for_each_queued(&sched->tasks,
		replay_queued_task,
		&replay);
	if (rc != QHW_SCHED_OK) {
		plugin->desc.fini(state);
		sched->lock_ops.unlock(&sched->lock);
		return rc;
	}

	if (sched->policy.desc.fini != NULL) {
		sched->policy.desc.fini(sched->policy.state);
	}

	sched->policy.desc = plugin->desc;
	sched->policy.state = state;
	sched->lock_ops.unlock(&sched->lock);
	return QHW_SCHED_OK;
}
