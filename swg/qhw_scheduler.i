%module _qhw_scheduler

%{
#include "qhw_scheduler/qhw_scheduler.h"
#include <string.h>
%}

%include <stdint.i>
%include <typemaps.i>
%include <cdata.i>

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

%typemap(in) (const void *indata, size_t inlen)
	(Py_buffer view) {
	if (PyObject_GetBuffer($input, &view, PyBUF_SIMPLE) != 0) {
		PyErr_SetString(PyExc_TypeError, "expected bytes-like object");
		SWIG_fail;
	}

	$1 = (void *)view.buf;
	$2 = (size_t)view.len;
}

%typemap(freearg) (const void *indata, size_t inlen) {
	PyBuffer_Release(&view$argnum);
}

%typemap(out) PyObject * {
	$result = $1;
}

%inline %{
static qhw_sched_kv_t *qhw_sched_kv_array_create(size_t count)
{
	if (count == 0) {
		return NULL;
	}

	return (qhw_sched_kv_t *)calloc(count, sizeof(qhw_sched_kv_t));
}

static void qhw_sched_kv_array_destroy(qhw_sched_kv_t *items)
{
	free(items);
}

static qhw_sched_rc_t qhw_sched_kv_array_set(
	qhw_sched_kv_t *items,
	size_t count,
	size_t index,
	const qhw_sched_kv_t *value)
{
	if (items == NULL || value == NULL || index >= count) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	items[index] = *value;
	return QHW_SCHED_OK;
}

static void *qhw_sched_payload_copy(const void *indata, size_t inlen)
{
	void *payload;

	if (indata == NULL || inlen == 0) {
		return NULL;
	}

	payload = malloc(inlen);
	if (payload == NULL) {
		return NULL;
	}

	memcpy(payload, indata, inlen);
	return payload;
}

static void qhw_sched_payload_destroy(void *payload)
{
	free(payload);
}

static PyObject *qhw_sched_payload_to_bytes(
	const void *payload,
	size_t size)
{
	if (payload == NULL || size == 0) {
		return PyBytes_FromStringAndSize("", 0);
	}

	return PyBytes_FromStringAndSize((const char *)payload, size);
}
%}

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
