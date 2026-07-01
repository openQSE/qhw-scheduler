#ifndef QHW_SCHEDULER_INTERNAL_H
#define QHW_SCHEDULER_INTERNAL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "qhw_scheduler/qhw_scheduler.h"
#include "qhw_scheduler/qhw_scheduler_plugin.h"
#include "util/qhw_hash_table.h"
#include "util/qhw_list.h"

struct qhw_allocator {
	void *(*alloc)(size_t size, void *user_data);
	void *(*realloc)(void *ptr, size_t size, void *user_data);
	void (*free)(void *ptr, void *user_data);
	void *user_data;
};

struct qhw_mutex {
	pthread_mutex_t mutex;
};

struct qhw_lock_ops {
	void (*lock)(struct qhw_mutex *lock);
	void (*unlock)(struct qhw_mutex *lock);
	void (*destroy)(struct qhw_mutex *lock);
};

struct qhw_sched_error {
	qhw_sched_rc_t code;
	char message[256];
};

struct qhw_task_record {
	qhw_sched_task_desc_t desc;
	qhw_sched_kv_t *metadata;
	qhw_sched_task_state_t state;
	uint64_t enqueue_seq;
	size_t child_count;
	size_t completed_child_count;
	size_t failed_child_count;
	size_t cancelled_child_count;
	struct qhw_list_node enqueue_link;
};

struct qhw_task_table {
	struct qhw_hash_table by_id;
	struct qhw_list_node enqueue_order;
	size_t count;
};

struct qhw_sched_qpu {
	qhw_sched_qpu_profile_t profile;
	qhw_sched_kv_t *metadata;
	qhw_sched_qpu_runtime_t runtime;
	struct qhw_mutex lock;
	struct qhw_lock_ops lock_ops;
	uint64_t refcount;
};

struct qhw_sched_plugin {
	qhw_sched_plugin_desc_t desc;
	char *path;
	void *dl_handle;
};

struct qhw_policy_ops {
	qhw_sched_plugin_desc_t desc;
	void *state;
};

struct qhw_plugin_registry {
	struct qhw_sched_plugin *items;
	size_t count;
	size_t capacity;
};

struct qhw_sched {
	qhw_sched_threading_t threading;
	struct qhw_mutex lock;
	struct qhw_lock_ops lock_ops;
	struct qhw_allocator allocator;
	struct qhw_sched_error last_error;
	struct qhw_task_table tasks;
	qhw_sched_qpu_t *qpu;
	struct qhw_plugin_registry plugins;
	struct qhw_policy_ops policy;
	qhw_sched_callbacks_t callbacks;
	uint64_t enqueue_seq_next;
};

void *qhw_alloc(struct qhw_allocator *allocator, size_t size);
void *qhw_realloc(struct qhw_allocator *allocator, void *ptr, size_t size);
void qhw_free(struct qhw_allocator *allocator, void *ptr);
qhw_sched_rc_t qhw_allocator_init(
	struct qhw_allocator *allocator,
	const qhw_sched_allocator_t *attr_allocator);

void qhw_error_set(
	struct qhw_sched_error *error,
	qhw_sched_rc_t code,
	const char *message);

qhw_sched_rc_t qhw_thread_init(
	qhw_sched_threading_t threading,
	struct qhw_mutex *lock,
	struct qhw_lock_ops *ops);

qhw_sched_rc_t qhw_task_table_init(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator);
void qhw_task_table_fini(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator);
qhw_sched_rc_t qhw_task_table_insert(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	const qhw_sched_task_desc_t *task,
	uint64_t enqueue_seq);
qhw_sched_rc_t qhw_task_table_insert_state(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	const qhw_sched_task_desc_t *task,
	uint64_t enqueue_seq,
	qhw_sched_task_state_t state);
void qhw_task_table_remove(
	struct qhw_task_table *table,
	struct qhw_allocator *allocator,
	qhw_sched_task_id_t task_id);
struct qhw_task_record *qhw_task_table_find(
	struct qhw_task_table *table,
	qhw_sched_task_id_t task_id);
qhw_sched_rc_t qhw_task_table_set_state(
	struct qhw_task_table *table,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t state);
typedef qhw_sched_rc_t (*qhw_task_record_fn)(
	struct qhw_task_record *record,
	void *user_data);
qhw_sched_rc_t qhw_task_table_for_each_queued(
	struct qhw_task_table *table,
	qhw_task_record_fn fn,
	void *user_data);

void qhw_qpu_retain(qhw_sched_qpu_t *qpu);
void qhw_qpu_release(qhw_sched_qpu_t *qpu);

void qhw_plugin_registry_fini(
	struct qhw_plugin_registry *registry,
	struct qhw_allocator *allocator);
qhw_sched_rc_t qhw_plugin_registry_add(
	struct qhw_plugin_registry *registry,
	struct qhw_allocator *allocator,
	const qhw_sched_plugin_desc_t *desc,
	const char *path,
	void *dl_handle);
struct qhw_sched_plugin *qhw_plugin_registry_find(
	struct qhw_plugin_registry *registry,
	const char *name);

#endif
