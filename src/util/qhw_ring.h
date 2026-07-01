#ifndef QHW_RING_H
#define QHW_RING_H

#include <stddef.h>

struct qhw_ring {
	void **items;
	size_t capacity;
	size_t head;
	size_t tail;
	size_t count;
};

void qhw_ring_init(struct qhw_ring *ring, void **items, size_t capacity);
int qhw_ring_push(struct qhw_ring *ring, void *item);
void *qhw_ring_pop(struct qhw_ring *ring);
int qhw_ring_empty(const struct qhw_ring *ring);

#endif

