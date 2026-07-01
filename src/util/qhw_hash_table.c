#include "qhw_hash_table.h"

#include <string.h>

static size_t qhw_hash_index(uint64_t key, size_t bucket_count)
{
	key ^= key >> 33;
	key *= UINT64_C(0xff51afd7ed558ccd);
	key ^= key >> 33;
	key *= UINT64_C(0xc4ceb9fe1a85ec53);
	key ^= key >> 33;
	return (size_t)(key % bucket_count);
}

int qhw_hash_table_init(
	struct qhw_hash_table *table,
	size_t bucket_count,
	qhw_util_alloc_fn alloc_fn,
	qhw_util_free_fn free_fn,
	void *user_data)
{
	size_t bytes;

	if (table == NULL || bucket_count == 0 ||
		alloc_fn == NULL || free_fn == NULL) {
		return -1;
	}

	bytes = bucket_count * sizeof(*table->buckets);
	table->buckets = alloc_fn(bytes, user_data);
	if (table->buckets == NULL) {
		return -1;
	}

	memset(table->buckets, 0, bytes);
	table->bucket_count = bucket_count;
	table->count = 0;
	table->alloc_fn = alloc_fn;
	table->free_fn = free_fn;
	table->user_data = user_data;
	return 0;
}

void qhw_hash_table_fini(
	struct qhw_hash_table *table,
	void (*free_value)(void *value, void *user_data),
	void *free_value_data)
{
	size_t i;

	if (table == NULL || table->buckets == NULL) {
		return;
	}

	for (i = 0; i < table->bucket_count; i++) {
		struct qhw_hash_entry *entry = table->buckets[i];

		while (entry != NULL) {
			struct qhw_hash_entry *next = entry->next;

			if (free_value != NULL) {
				free_value(entry->value, free_value_data);
			}
			table->free_fn(entry, table->user_data);
			entry = next;
		}
	}

	table->free_fn(table->buckets, table->user_data);
	memset(table, 0, sizeof(*table));
}

int qhw_hash_table_insert(
	struct qhw_hash_table *table,
	uint64_t key,
	void *value)
{
	size_t index;
	struct qhw_hash_entry *entry;

	if (table == NULL || table->buckets == NULL || value == NULL) {
		return -1;
	}

	if (qhw_hash_table_find(table, key) != NULL) {
		return -1;
	}

	entry = table->alloc_fn(sizeof(*entry), table->user_data);
	if (entry == NULL) {
		return -1;
	}

	index = qhw_hash_index(key, table->bucket_count);
	entry->key = key;
	entry->value = value;
	entry->next = table->buckets[index];
	table->buckets[index] = entry;
	table->count++;
	return 0;
}

void *qhw_hash_table_find(
	struct qhw_hash_table *table,
	uint64_t key)
{
	size_t index;
	struct qhw_hash_entry *entry;

	if (table == NULL || table->buckets == NULL) {
		return NULL;
	}

	index = qhw_hash_index(key, table->bucket_count);
	entry = table->buckets[index];
	while (entry != NULL) {
		if (entry->key == key) {
			return entry->value;
		}
		entry = entry->next;
	}

	return NULL;
}

void *qhw_hash_table_remove(
	struct qhw_hash_table *table,
	uint64_t key)
{
	size_t index;
	struct qhw_hash_entry **cursor;

	if (table == NULL || table->buckets == NULL) {
		return NULL;
	}

	index = qhw_hash_index(key, table->bucket_count);
	cursor = &table->buckets[index];
	while (*cursor != NULL) {
		struct qhw_hash_entry *entry = *cursor;

		if (entry->key == key) {
			void *value = entry->value;

			*cursor = entry->next;
			table->free_fn(entry, table->user_data);
			table->count--;
			return value;
		}
		cursor = &entry->next;
	}

	return NULL;
}

