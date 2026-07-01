#include "qhw_heap.h"

#include <string.h>

#define QHW_HEAP_INITIAL_CAPACITY 64U

static void qhw_heap_set_index(
	struct qhw_heap *heap,
	void *item,
	size_t index)
{
	if (heap->update_index != NULL) {
		heap->update_index(item, index, heap->index_user_data);
	}
}

static void qhw_heap_swap(
	struct qhw_heap *heap,
	size_t left,
	size_t right)
{
	void *tmp = heap->items[left];

	heap->items[left] = heap->items[right];
	heap->items[right] = tmp;
	qhw_heap_set_index(heap, heap->items[left], left);
	qhw_heap_set_index(heap, heap->items[right], right);
}

static size_t qhw_heap_sift_up(struct qhw_heap *heap, size_t index)
{
	while (index > 0) {
		size_t parent = (index - 1) / 2;

		if (heap->compare(heap->items[index], heap->items[parent]) >= 0) {
			break;
		}

		qhw_heap_swap(heap, index, parent);
		index = parent;
	}

	return index;
}

static size_t qhw_heap_sift_down(struct qhw_heap *heap, size_t index)
{
	for (;;) {
		size_t left = (index * 2) + 1;
		size_t right = left + 1;
		size_t best = index;

		if (left < heap->count &&
			heap->compare(heap->items[left], heap->items[best]) < 0) {
			best = left;
		}
		if (right < heap->count &&
			heap->compare(heap->items[right], heap->items[best]) < 0) {
			best = right;
		}
		if (best == index) {
			break;
		}

		qhw_heap_swap(heap, index, best);
		index = best;
	}

	return index;
}

int qhw_heap_init(
	struct qhw_heap *heap,
	qhw_heap_compare_fn compare,
	qhw_heap_index_fn update_index,
	void *index_user_data,
	qhw_heap_realloc_fn realloc_fn,
	qhw_heap_free_fn free_fn,
	void *alloc_user_data)
{
	if (heap == NULL || compare == NULL || realloc_fn == NULL ||
		free_fn == NULL) {
		return -1;
	}

	memset(heap, 0, sizeof(*heap));
	heap->compare = compare;
	heap->update_index = update_index;
	heap->index_user_data = index_user_data;
	heap->realloc_fn = realloc_fn;
	heap->free_fn = free_fn;
	heap->alloc_user_data = alloc_user_data;
	return 0;
}

void qhw_heap_fini(struct qhw_heap *heap)
{
	if (heap == NULL || heap->free_fn == NULL) {
		return;
	}

	heap->free_fn(heap->items, heap->alloc_user_data);
	memset(heap, 0, sizeof(*heap));
}

int qhw_heap_reserve(struct qhw_heap *heap, size_t min_capacity)
{
	size_t next;
	size_t bytes;
	void *items;

	if (heap == NULL || heap->realloc_fn == NULL) {
		return -1;
	}

	if (heap->capacity >= min_capacity) {
		return 0;
	}

	next = heap->capacity == 0 ?
		QHW_HEAP_INITIAL_CAPACITY : heap->capacity;
	while (next < min_capacity) {
		if (next > (size_t)-1 / 2) {
			return -1;
		}
		next *= 2;
	}

	if (next > (size_t)-1 / sizeof(*heap->items)) {
		return -1;
	}

	bytes = next * sizeof(*heap->items);
	items = heap->realloc_fn(heap->items, bytes, heap->alloc_user_data);
	if (items == NULL) {
		return -1;
	}

	heap->items = items;
	heap->capacity = next;
	return 0;
}

int qhw_heap_push(struct qhw_heap *heap, void *item)
{
	size_t index;

	if (heap == NULL || item == NULL || heap->compare == NULL) {
		return -1;
	}

	if (qhw_heap_reserve(heap, heap->count + 1) != 0) {
		return -1;
	}

	index = heap->count++;
	heap->items[index] = item;
	qhw_heap_set_index(heap, item, index);
	(void)qhw_heap_sift_up(heap, index);
	return 0;
}

void *qhw_heap_remove_at(struct qhw_heap *heap, size_t index)
{
	void *result;

	if (heap == NULL || index >= heap->count) {
		return NULL;
	}

	result = heap->items[index];
	heap->count--;
	if (index != heap->count) {
		size_t next_index;

		heap->items[index] = heap->items[heap->count];
		qhw_heap_set_index(heap, heap->items[index], index);
		next_index = qhw_heap_sift_down(heap, index);
		(void)qhw_heap_sift_up(heap, next_index);
	}

	return result;
}

void *qhw_heap_pop(struct qhw_heap *heap)
{
	return qhw_heap_remove_at(heap, 0);
}

void *qhw_heap_peek(struct qhw_heap *heap)
{
	if (heap == NULL || heap->count == 0) {
		return NULL;
	}

	return heap->items[0];
}
