#include "qhw_ring.h"

void qhw_ring_init(struct qhw_ring *ring, void **items, size_t capacity)
{
	ring->items = items;
	ring->capacity = capacity;
	ring->head = 0;
	ring->tail = 0;
	ring->count = 0;
}

int qhw_ring_push(struct qhw_ring *ring, void *item)
{
	if (ring == NULL || ring->items == NULL ||
		ring->count == ring->capacity) {
		return -1;
	}

	ring->items[ring->tail] = item;
	ring->tail = (ring->tail + 1) % ring->capacity;
	ring->count++;
	return 0;
}

void *qhw_ring_pop(struct qhw_ring *ring)
{
	void *item;

	if (qhw_ring_empty(ring)) {
		return NULL;
	}

	item = ring->items[ring->head];
	ring->head = (ring->head + 1) % ring->capacity;
	ring->count--;
	return item;
}

int qhw_ring_empty(const struct qhw_ring *ring)
{
	return ring == NULL || ring->count == 0;
}

