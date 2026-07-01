#include "qhw_scheduler_internal.h"

#include <stdlib.h>

static void *default_alloc(size_t size, void *user_data)
{
	(void)user_data;
	return malloc(size);
}

static void *default_realloc(void *ptr, size_t size, void *user_data)
{
	(void)user_data;
	return realloc(ptr, size);
}

static void default_free(void *ptr, void *user_data)
{
	(void)user_data;
	free(ptr);
}

void *qhw_alloc(struct qhw_allocator *allocator, size_t size)
{
	return allocator->alloc(size, allocator->user_data);
}

void *qhw_realloc(struct qhw_allocator *allocator, void *ptr, size_t size)
{
	return allocator->realloc(ptr, size, allocator->user_data);
}

void qhw_free(struct qhw_allocator *allocator, void *ptr)
{
	if (ptr != NULL) {
		allocator->free(ptr, allocator->user_data);
	}
}

void *qhw_sched_alloc(qhw_sched_t *sched, size_t size)
{
	if (sched == NULL) {
		return NULL;
	}

	return qhw_alloc(&sched->allocator, size);
}

void *qhw_sched_realloc(qhw_sched_t *sched, void *ptr, size_t size)
{
	if (sched == NULL) {
		return NULL;
	}

	return qhw_realloc(&sched->allocator, ptr, size);
}

void qhw_sched_free(qhw_sched_t *sched, void *ptr)
{
	if (sched != NULL) {
		qhw_free(&sched->allocator, ptr);
	}
}

qhw_sched_rc_t qhw_allocator_init(
	struct qhw_allocator *allocator,
	const qhw_sched_allocator_t *attr_allocator)
{
	if (allocator == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	if (attr_allocator == NULL) {
		allocator->alloc = default_alloc;
		allocator->realloc = default_realloc;
		allocator->free = default_free;
		allocator->user_data = NULL;
		return QHW_SCHED_OK;
	}

	if (attr_allocator->alloc == NULL ||
		attr_allocator->realloc == NULL ||
		attr_allocator->free == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	allocator->alloc = attr_allocator->alloc;
	allocator->realloc = attr_allocator->realloc;
	allocator->free = attr_allocator->free;
	allocator->user_data = attr_allocator->user_data;
	return QHW_SCHED_OK;
}
