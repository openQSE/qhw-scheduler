#ifndef QHW_HEAP_H
#define QHW_HEAP_H

#include <stddef.h>

typedef int (*qhw_heap_compare_fn)(const void *left, const void *right);

struct qhw_heap {
	void **items;
	size_t count;
	size_t capacity;
	qhw_heap_compare_fn compare;
};

void qhw_heap_init(
	struct qhw_heap *heap,
	void **items,
	size_t capacity,
	qhw_heap_compare_fn compare);
int qhw_heap_push(struct qhw_heap *heap, void *item);
void *qhw_heap_pop(struct qhw_heap *heap);
void *qhw_heap_peek(struct qhw_heap *heap);

#endif

