%module _qhw_scheduler

%{
#include "qhw_scheduler/qhw_scheduler.h"
%}

%include <stdint.i>
%include <typemaps.i>

%include "qhw_scheduler/qhw_scheduler_types.h"
%include "qhw_scheduler/qhw_scheduler_plugin.h"

%nodefaultctor qhw_sched_policy_list_t;
%nodefaultdtor qhw_sched_policy_list_t;

%inline %{
typedef struct qhw_sched_policy_list {
	qhw_sched_t *sched;
	qhw_sched_policy_info_t *items;
	size_t count;
} qhw_sched_policy_list_t;
%}

%include "qhw_scheduler_typemaps.i"

%ignore qhw_sched_list_policies;
%ignore qhw_sched_free_policy_info_array;

%include "qhw_scheduler/qhw_scheduler.h"

%inline %{
static qhw_sched_rc_t qhw_sched_policy_list_create(
	qhw_sched_t *sched,
	qhw_sched_policy_list_t **out_list)
{
	qhw_sched_policy_info_t *items;
	qhw_sched_policy_list_t *list;
	qhw_sched_rc_t rc;
	size_t count;

	if (sched == NULL || out_list == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_list = NULL;
	items = NULL;
	count = 0;

	rc = qhw_sched_list_policies(sched, &items, &count);
	if (rc != QHW_SCHED_OK) {
		return rc;
	}

	list = (qhw_sched_policy_list_t *)calloc(1, sizeof(*list));
	if (list == NULL) {
		qhw_sched_free_policy_info_array(sched, items);
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	list->sched = sched;
	list->items = items;
	list->count = count;
	*out_list = list;
	return QHW_SCHED_OK;
}

static void qhw_sched_policy_list_destroy(qhw_sched_policy_list_t *list)
{
	if (list == NULL) {
		return;
	}

	if (list->items != NULL) {
		qhw_sched_free_policy_info_array(list->sched, list->items);
	}

	free(list);
}

static size_t qhw_sched_policy_list_count(qhw_sched_policy_list_t *list)
{
	if (list == NULL) {
		return 0;
	}

	return list->count;
}

static qhw_sched_policy_info_t *qhw_sched_policy_list_get(
	qhw_sched_policy_list_t *list,
	size_t index)
{
	if (list == NULL || index >= list->count) {
		return NULL;
	}

	return &list->items[index];
}
%}
