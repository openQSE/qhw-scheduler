#include "qhw_heap.h"

static void qhw_heap_swap(void **left, void **right)
{
	void *tmp = *left;

	*left = *right;
	*right = tmp;
}

void qhw_heap_init(
	struct qhw_heap *heap,
	void **items,
	size_t capacity,
	qhw_heap_compare_fn compare)
{
	heap->items = items;
	heap->count = 0;
	heap->capacity = capacity;
	heap->compare = compare;
}

int qhw_heap_push(struct qhw_heap *heap, void *item)
{
	size_t index;

	if (heap == NULL || heap->items == NULL || heap->compare == NULL ||
		heap->count == heap->capacity) {
		return -1;
	}

	index = heap->count++;
	heap->items[index] = item;
	while (index > 0) {
		size_t parent = (index - 1) / 2;

		if (heap->compare(heap->items[index], heap->items[parent]) >= 0) {
			break;
		}
		qhw_heap_swap(&heap->items[index], &heap->items[parent]);
		index = parent;
	}

	return 0;
}

void *qhw_heap_pop(struct qhw_heap *heap)
{
	void *result;
	size_t index;

	if (heap == NULL || heap->count == 0) {
		return NULL;
	}

	result = heap->items[0];
	heap->items[0] = heap->items[--heap->count];
	index = 0;
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
		qhw_heap_swap(&heap->items[index], &heap->items[best]);
		index = best;
	}

	return result;
}

void *qhw_heap_peek(struct qhw_heap *heap)
{
	if (heap == NULL || heap->count == 0) {
		return NULL;
	}

	return heap->items[0];
}

