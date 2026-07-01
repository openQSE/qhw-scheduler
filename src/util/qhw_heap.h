#ifndef QHW_HEAP_H
#define QHW_HEAP_H

#include <stddef.h>

typedef int (*qhw_heap_compare_fn)(const void *left, const void *right);
typedef void (*qhw_heap_index_fn)(
	void *item,
	size_t index,
	void *user_data);
typedef void *(*qhw_heap_realloc_fn)(
	void *ptr,
	size_t size,
	void *user_data);
typedef void (*qhw_heap_free_fn)(void *ptr, void *user_data);

struct qhw_heap {
	void **items;
	size_t count;
	size_t capacity;
	qhw_heap_compare_fn compare;
	qhw_heap_index_fn update_index;
	void *index_user_data;
	qhw_heap_realloc_fn realloc_fn;
	qhw_heap_free_fn free_fn;
	void *alloc_user_data;
};

int qhw_heap_init(
	struct qhw_heap *heap,
	qhw_heap_compare_fn compare,
	qhw_heap_index_fn update_index,
	void *index_user_data,
	qhw_heap_realloc_fn realloc_fn,
	qhw_heap_free_fn free_fn,
	void *alloc_user_data);
void qhw_heap_fini(struct qhw_heap *heap);
int qhw_heap_reserve(struct qhw_heap *heap, size_t min_capacity);
int qhw_heap_push(struct qhw_heap *heap, void *item);
int qhw_heap_reheapify_at(struct qhw_heap *heap, size_t index);
void *qhw_heap_pop(struct qhw_heap *heap);
void *qhw_heap_remove_at(struct qhw_heap *heap, size_t index);
void *qhw_heap_peek(struct qhw_heap *heap);

#endif
