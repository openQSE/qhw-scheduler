#ifndef QHW_HASH_TABLE_H
#define QHW_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>

typedef void *(*qhw_util_alloc_fn)(size_t size, void *user_data);
typedef void *(*qhw_util_realloc_fn)(
	void *ptr,
	size_t size,
	void *user_data);
typedef void (*qhw_util_free_fn)(void *ptr, void *user_data);

struct qhw_hash_entry {
	uint64_t key;
	void *value;
	struct qhw_hash_entry *next;
};

struct qhw_hash_table {
	struct qhw_hash_entry **buckets;
	size_t bucket_count;
	size_t count;
	qhw_util_alloc_fn alloc_fn;
	qhw_util_free_fn free_fn;
	void *user_data;
};

int qhw_hash_table_init(
	struct qhw_hash_table *table,
	size_t bucket_count,
	qhw_util_alloc_fn alloc_fn,
	qhw_util_free_fn free_fn,
	void *user_data);

void qhw_hash_table_fini(
	struct qhw_hash_table *table,
	void (*free_value)(void *value, void *user_data),
	void *free_value_data);

int qhw_hash_table_insert(
	struct qhw_hash_table *table,
	uint64_t key,
	void *value);

void *qhw_hash_table_find(
	struct qhw_hash_table *table,
	uint64_t key);

void *qhw_hash_table_remove(
	struct qhw_hash_table *table,
	uint64_t key);

#endif

