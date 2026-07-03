%module _qhw_scheduler

%{
#include "qhw_scheduler/qhw_scheduler.h"
#include <stdlib.h>
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

typedef struct qhw_sched_py_metadata_hold {
	qhw_sched_kv_t *metadata;
	struct qhw_sched_py_metadata_hold *next;
} qhw_sched_py_metadata_hold_t;

typedef struct qhw_sched_py_split_callback {
	PyObject *split_callable;
	PyObject *cost_callable;
	qhw_sched_py_metadata_hold_t *metadata;
	char last_error[256];
} qhw_sched_py_split_callback_t;

static void qhw_sched_py_split_set_error(
	qhw_sched_py_split_callback_t *ctx,
	const char *message)
{
	if (ctx == NULL || message == NULL) {
		return;
	}

	strncpy(ctx->last_error, message, sizeof(ctx->last_error) - 1);
	ctx->last_error[sizeof(ctx->last_error) - 1] = '\0';
	if (PyErr_Occurred()) {
		PyErr_Clear();
	}
}

static void qhw_sched_py_split_clear_metadata(
	qhw_sched_py_split_callback_t *ctx)
{
	qhw_sched_py_metadata_hold_t *hold;

	if (ctx == NULL) {
		return;
	}

	hold = ctx->metadata;
	while (hold != NULL) {
		qhw_sched_py_metadata_hold_t *next = hold->next;

		free(hold->metadata);
		free(hold);
		hold = next;
	}
	ctx->metadata = NULL;
}

static void qhw_sched_py_split_reset(qhw_sched_py_split_callback_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	qhw_sched_py_split_clear_metadata(ctx);
	ctx->last_error[0] = '\0';
}

static int qhw_sched_py_split_track_metadata(
	qhw_sched_py_split_callback_t *ctx,
	qhw_sched_kv_t *metadata)
{
	qhw_sched_py_metadata_hold_t *hold;

	if (ctx == NULL || metadata == NULL) {
		return 0;
	}

	hold = (qhw_sched_py_metadata_hold_t *)calloc(1, sizeof(*hold));
	if (hold == NULL) {
		return -1;
	}

	hold->metadata = metadata;
	hold->next = ctx->metadata;
	ctx->metadata = hold;
	return 0;
}

static PyObject *qhw_sched_py_get_optional(
	PyObject *mapping,
	const char *key)
{
	PyObject *value;

	value = PyMapping_GetItemString(mapping, key);
	if (value == NULL) {
		PyErr_Clear();
	}
	return value;
}

static int qhw_sched_py_as_u64(PyObject *obj, uint64_t *out_value)
{
	unsigned long long value;

	value = PyLong_AsUnsignedLongLong(obj);
	if (PyErr_Occurred()) {
		return -1;
	}

	*out_value = (uint64_t)value;
	return 0;
}

static int qhw_sched_py_as_i64(PyObject *obj, int64_t *out_value)
{
	long long value;

	value = PyLong_AsLongLong(obj);
	if (PyErr_Occurred()) {
		return -1;
	}

	*out_value = (int64_t)value;
	return 0;
}

static int qhw_sched_py_metadata_item(
	PyObject *item,
	qhw_sched_kv_t *kv)
{
	PyObject *key_obj = NULL;
	PyObject *type_obj = NULL;
	PyObject *value_obj = NULL;
	uint64_t key;
	uint64_t type = QHW_SCHED_VALUE_U64;
	int rc = -1;

	if (PyMapping_Check(item)) {
		key_obj = PyMapping_GetItemString(item, "key");
		value_obj = PyMapping_GetItemString(item, "value");
		type_obj = qhw_sched_py_get_optional(item, "type");
		if (key_obj == NULL || value_obj == NULL) {
			goto out;
		}
	} else {
		PyObject *seq = PySequence_Fast(item,
			"metadata entries must be mappings or tuples");

		if (seq == NULL) {
			goto out;
		}
		if (PySequence_Fast_GET_SIZE(seq) < 2) {
			Py_DECREF(seq);
			goto out;
		}
		key_obj = PySequence_Fast_GET_ITEM(seq, 0);
		value_obj = PySequence_Fast_GET_ITEM(seq, 1);
		Py_INCREF(key_obj);
		Py_INCREF(value_obj);
		if (PySequence_Fast_GET_SIZE(seq) > 2) {
			type_obj = PySequence_Fast_GET_ITEM(seq, 2);
			Py_INCREF(type_obj);
		}
		Py_DECREF(seq);
	}

	if (qhw_sched_py_as_u64(key_obj, &key) != 0) {
		goto out;
	}
	if (type_obj != NULL &&
		qhw_sched_py_as_u64(type_obj, &type) != 0) {
		goto out;
	}

	memset(kv, 0, sizeof(*kv));
	kv->key = key;
	kv->type = (uint32_t)type;
	switch (kv->type) {
	case QHW_SCHED_VALUE_U64:
		rc = qhw_sched_py_as_u64(value_obj, &kv->value.u64);
		break;
	case QHW_SCHED_VALUE_I64:
		rc = qhw_sched_py_as_i64(value_obj, &kv->value.i64);
		break;
	case QHW_SCHED_VALUE_F64:
		kv->value.f64 = PyFloat_AsDouble(value_obj);
		rc = PyErr_Occurred() ? -1 : 0;
		break;
	case QHW_SCHED_VALUE_PTR:
		kv->value.ptr = PyLong_AsVoidPtr(value_obj);
		rc = PyErr_Occurred() ? -1 : 0;
		break;
	default:
		rc = -1;
		break;
	}

out:
	Py_XDECREF(key_obj);
	Py_XDECREF(type_obj);
	Py_XDECREF(value_obj);
	return rc;
}

static int qhw_sched_py_fill_metadata(
	qhw_sched_py_split_callback_t *ctx,
	PyObject *metadata_obj,
	qhw_sched_kv_t **out_metadata,
	size_t *out_count)
{
	PyObject *seq;
	qhw_sched_kv_t *metadata;
	Py_ssize_t count;
	Py_ssize_t i;

	*out_metadata = NULL;
	*out_count = 0;
	if (metadata_obj == NULL || metadata_obj == Py_None) {
		return 0;
	}

	seq = PySequence_Fast(metadata_obj, "metadata must be a sequence");
	if (seq == NULL) {
		return -1;
	}

	count = PySequence_Fast_GET_SIZE(seq);
	if (count == 0) {
		Py_DECREF(seq);
		return 0;
	}

	metadata = (qhw_sched_kv_t *)calloc((size_t)count, sizeof(*metadata));
	if (metadata == NULL) {
		Py_DECREF(seq);
		PyErr_NoMemory();
		return -1;
	}

	for (i = 0; i < count; i++) {
		if (qhw_sched_py_metadata_item(
			PySequence_Fast_GET_ITEM(seq, i),
			&metadata[i]) != 0) {
			free(metadata);
			Py_DECREF(seq);
			return -1;
		}
	}
	Py_DECREF(seq);

	if (qhw_sched_py_split_track_metadata(ctx, metadata) != 0) {
		free(metadata);
		PyErr_NoMemory();
		return -1;
	}

	*out_metadata = metadata;
	*out_count = (size_t)count;
	return 0;
}

static int qhw_sched_py_get_u64_field(
	PyObject *mapping,
	const char *key,
	uint64_t *out_value,
	int required)
{
	PyObject *value;
	int rc;

	value = qhw_sched_py_get_optional(mapping, key);
	if (value == NULL) {
		return required ? -1 : 0;
	}

	rc = qhw_sched_py_as_u64(value, out_value);
	Py_DECREF(value);
	return rc;
}

static int qhw_sched_py_get_i64_field(
	PyObject *mapping,
	const char *key,
	int64_t *out_value)
{
	PyObject *value;
	int rc;

	value = qhw_sched_py_get_optional(mapping, key);
	if (value == NULL) {
		return 0;
	}

	rc = qhw_sched_py_as_i64(value, out_value);
	Py_DECREF(value);
	return rc;
}

static PyObject *qhw_sched_py_task_dict(
	const qhw_sched_task_desc_t *task)
{
	return Py_BuildValue(
		"{s:K,s:K,s:K,s:K,s:L,s:K,s:K,s:K,s:K}",
		"task_id", (unsigned long long)task->task_id,
		"parent_task_id", (unsigned long long)task->parent_task_id,
		"owner_id", (unsigned long long)task->owner_id,
		"job_id", (unsigned long long)task->job_id,
		"priority", (long long)task->priority,
		"deadline_ns", (unsigned long long)task->deadline_ns,
		"estimated_runtime_ns",
		(unsigned long long)task->estimated_runtime_ns,
		"estimated_cost", (unsigned long long)task->estimated_cost,
		"payload_size", (unsigned long long)task->payload_size);
}

static PyObject *qhw_sched_py_kv_value(const qhw_sched_kv_t *kv)
{
	switch (kv->type) {
	case QHW_SCHED_VALUE_U64:
		return PyLong_FromUnsignedLongLong(
			(unsigned long long)kv->value.u64);
	case QHW_SCHED_VALUE_I64:
		return PyLong_FromLongLong((long long)kv->value.i64);
	case QHW_SCHED_VALUE_F64:
		return PyFloat_FromDouble(kv->value.f64);
	case QHW_SCHED_VALUE_PTR:
		return PyLong_FromVoidPtr(kv->value.ptr);
	default:
		Py_RETURN_NONE;
	}
}

static PyObject *qhw_sched_py_metadata_list(
	const qhw_sched_kv_t *metadata,
	size_t metadata_count)
{
	PyObject *list;
	size_t i;

	list = PyList_New((Py_ssize_t)metadata_count);
	if (list == NULL) {
		return NULL;
	}

	for (i = 0; i < metadata_count; i++) {
		PyObject *item;
		PyObject *value;

		value = qhw_sched_py_kv_value(&metadata[i]);
		if (value == NULL) {
			Py_DECREF(list);
			return NULL;
		}

		item = Py_BuildValue(
			"{s:K,s:I,s:O}",
			"key", (unsigned long long)metadata[i].key,
			"type", (unsigned int)metadata[i].type,
			"value", value);
		Py_DECREF(value);
		if (item == NULL) {
			Py_DECREF(list);
			return NULL;
		}

		PyList_SET_ITEM(list, (Py_ssize_t)i, item);
	}

	return list;
}

static int qhw_sched_py_add_metadata(
	PyObject *dict,
	const qhw_sched_kv_t *metadata,
	size_t metadata_count)
{
	PyObject *list;
	int rc;

	list = qhw_sched_py_metadata_list(metadata, metadata_count);
	if (list == NULL) {
		return -1;
	}

	rc = PyDict_SetItemString(dict, "metadata", list);
	Py_DECREF(list);
	return rc;
}

static PyObject *qhw_sched_py_task_dict_with_metadata(
	const qhw_sched_task_desc_t *task)
{
	PyObject *dict;

	dict = qhw_sched_py_task_dict(task);
	if (dict == NULL) {
		return NULL;
	}

	if (qhw_sched_py_add_metadata(dict, task->metadata,
		task->metadata_count) != 0) {
		Py_DECREF(dict);
		return NULL;
	}

	return dict;
}

static PyObject *qhw_sched_py_qpu_dict(
	const qhw_sched_qpu_profile_t *qpu)
{
	PyObject *dict;

	dict = Py_BuildValue(
		"{s:K,s:I,s:I}",
		"qpu_id", (unsigned long long)qpu->qpu_id,
		"num_qubits", (unsigned int)qpu->num_qubits,
		"flags", (unsigned int)qpu->flags);
	if (dict == NULL) {
		return NULL;
	}

	if (qhw_sched_py_add_metadata(dict, qpu->metadata,
		qpu->metadata_count) != 0) {
		Py_DECREF(dict);
		return NULL;
	}

	return dict;
}

static PyObject *qhw_sched_py_config_dict(
	const qhw_sched_split_config_t *config)
{
	return Py_BuildValue(
		"{s:K,s:K,s:K,s:K,s:K,s:K,s:K,s:K}",
		"shot_threshold", (unsigned long long)config->shot_threshold,
		"max_shots", (unsigned long long)config->max_shots,
		"min_remainder_shots",
		(unsigned long long)config->min_remainder_shots,
		"max_children", (unsigned long long)config->max_children,
		"requested_shots", (unsigned long long)config->requested_shots,
		"slice_shots", (unsigned long long)config->slice_shots,
		"slice_count", (unsigned long long)config->slice_count,
		"flags", (unsigned long long)config->flags);
}

static int qhw_sched_py_apply_child(
	qhw_sched_py_split_callback_t *ctx,
	const qhw_sched_task_desc_t *parent,
	PyObject *child_obj,
	qhw_sched_task_desc_t *child)
{
	PyObject *metadata_obj;

	if (!PyMapping_Check(child_obj)) {
		return -1;
	}

	if (qhw_sched_py_get_u64_field(child_obj, "task_id",
		&child->task_id, 1) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "parent_task_id",
			&child->parent_task_id, 0) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "owner_id",
			&child->owner_id, 0) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "job_id",
			&child->job_id, 0) != 0 ||
		qhw_sched_py_get_i64_field(child_obj, "priority",
			&child->priority) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "deadline_ns",
			&child->deadline_ns, 0) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "estimated_runtime_ns",
			&child->estimated_runtime_ns, 0) != 0 ||
		qhw_sched_py_get_u64_field(child_obj, "estimated_cost",
			&child->estimated_cost, 0) != 0) {
		return -1;
	}
	if (child->parent_task_id == QHW_SCHED_INVALID_TASK_ID) {
		child->parent_task_id = parent->task_id;
	}

	metadata_obj = qhw_sched_py_get_optional(child_obj, "metadata");
	child->metadata = NULL;
	child->metadata_count = 0;
	if (metadata_obj != NULL) {
		int rc = qhw_sched_py_fill_metadata(ctx, metadata_obj,
			(qhw_sched_kv_t **)&child->metadata,
			&child->metadata_count);

		Py_DECREF(metadata_obj);
		if (rc != 0) {
			return -1;
		}
	}
	return 0;
}

static qhw_sched_rc_t qhw_sched_py_split_trampoline(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_split_config_t *config,
	qhw_sched_task_desc_t *children,
	size_t child_count,
	void *user_data)
{
	qhw_sched_py_split_callback_t *ctx = user_data;
	PyGILState_STATE gil;
	PyObject *py_task = NULL;
	PyObject *py_config = NULL;
	PyObject *result = NULL;
	PyObject *seq = NULL;
	size_t i;
	qhw_sched_rc_t rc = QHW_SCHED_OK;

	if (ctx == NULL || ctx->split_callable == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	gil = PyGILState_Ensure();
	py_task = qhw_sched_py_task_dict_with_metadata(task);
	py_config = qhw_sched_py_config_dict(config);
	if (py_task == NULL || py_config == NULL) {
		qhw_sched_py_split_set_error(ctx,
			"failed to build split callback arguments");
		rc = QHW_SCHED_ERR_NO_MEMORY;
		goto out;
	}

	result = PyObject_CallFunctionObjArgs(ctx->split_callable, py_task,
		py_config, NULL);
	if (result == NULL) {
		qhw_sched_py_split_set_error(ctx, "split callback raised");
		rc = QHW_SCHED_ERR_INVALID_ARG;
		goto out;
	}

	seq = PySequence_Fast(result,
		"split callback must return a sequence");
	if (seq == NULL) {
		qhw_sched_py_split_set_error(ctx,
			"split callback returned a non-sequence");
		rc = QHW_SCHED_ERR_INVALID_ARG;
		goto out;
	}
	if ((size_t)PySequence_Fast_GET_SIZE(seq) != child_count) {
		qhw_sched_py_split_set_error(ctx,
			"split callback returned the wrong child count");
		rc = QHW_SCHED_ERR_INVALID_ARG;
		goto out;
	}

	for (i = 0; i < child_count; i++) {
		if (qhw_sched_py_apply_child(ctx, task,
			PySequence_Fast_GET_ITEM(seq, (Py_ssize_t)i),
			&children[i]) != 0) {
			qhw_sched_py_split_set_error(ctx,
				"split callback returned an invalid child");
			rc = QHW_SCHED_ERR_INVALID_ARG;
			goto out;
		}
	}

out:
	Py_XDECREF(seq);
	Py_XDECREF(result);
	Py_XDECREF(py_config);
	Py_XDECREF(py_task);
	PyGILState_Release(gil);
	return rc;
}

static qhw_sched_rc_t qhw_sched_py_cost_trampoline(
	const qhw_sched_task_desc_t *task,
	const qhw_sched_qpu_profile_t *qpu,
	uint64_t *out_cost,
	void *user_data)
{
	qhw_sched_py_split_callback_t *ctx = user_data;
	PyGILState_STATE gil;
	PyObject *py_task = NULL;
	PyObject *py_qpu = NULL;
	PyObject *result = NULL;
	qhw_sched_rc_t rc = QHW_SCHED_OK;

	if (ctx == NULL || ctx->cost_callable == NULL ||
		task == NULL || qpu == NULL || out_cost == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	gil = PyGILState_Ensure();
	py_task = qhw_sched_py_task_dict_with_metadata(task);
	py_qpu = qhw_sched_py_qpu_dict(qpu);
	if (py_task == NULL || py_qpu == NULL) {
		qhw_sched_py_split_set_error(ctx,
			"failed to build cost callback arguments");
		rc = QHW_SCHED_ERR_NO_MEMORY;
		goto out;
	}

	result = PyObject_CallFunctionObjArgs(ctx->cost_callable, py_task,
		py_qpu, NULL);
	if (result == NULL) {
		qhw_sched_py_split_set_error(ctx, "cost callback raised");
		rc = QHW_SCHED_ERR_INVALID_ARG;
		goto out;
	}

	if (qhw_sched_py_as_u64(result, out_cost) != 0) {
		qhw_sched_py_split_set_error(ctx,
			"cost callback returned an invalid cost");
		rc = QHW_SCHED_ERR_INVALID_ARG;
	}

out:
	Py_XDECREF(result);
	Py_XDECREF(py_qpu);
	Py_XDECREF(py_task);
	PyGILState_Release(gil);
	return rc;
}

static qhw_sched_py_split_callback_t *qhw_sched_python_callback_create(void)
{
	qhw_sched_py_split_callback_t *ctx;

	ctx = (qhw_sched_py_split_callback_t *)calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	return ctx;
}

static int qhw_sched_python_callback_set_split(
	qhw_sched_py_split_callback_t *ctx,
	PyObject *callable)
{
	if (ctx == NULL) {
		PyErr_SetString(PyExc_ValueError, "callback context is NULL");
		return -1;
	}

	if (callable != Py_None && !PyCallable_Check(callable)) {
		PyErr_SetString(PyExc_TypeError, "split callback must be callable");
		return -1;
	}

	Py_XINCREF(callable == Py_None ? NULL : callable);
	Py_XDECREF(ctx->split_callable);
	ctx->split_callable = callable == Py_None ? NULL : callable;
	return 0;
}

static int qhw_sched_python_callback_set_cost(
	qhw_sched_py_split_callback_t *ctx,
	PyObject *callable)
{
	if (ctx == NULL) {
		PyErr_SetString(PyExc_ValueError, "callback context is NULL");
		return -1;
	}

	if (callable != Py_None && !PyCallable_Check(callable)) {
		PyErr_SetString(PyExc_TypeError, "cost callback must be callable");
		return -1;
	}

	Py_XINCREF(callable == Py_None ? NULL : callable);
	Py_XDECREF(ctx->cost_callable);
	ctx->cost_callable = callable == Py_None ? NULL : callable;
	return 0;
}

static int qhw_sched_python_callback_has_callbacks(
	qhw_sched_py_split_callback_t *ctx)
{
	return ctx != NULL &&
		(ctx->split_callable != NULL || ctx->cost_callable != NULL);
}

static void qhw_sched_python_split_callback_destroy(
	qhw_sched_py_split_callback_t *ctx)
{
	PyGILState_STATE gil;

	if (ctx == NULL) {
		return;
	}

	qhw_sched_py_split_clear_metadata(ctx);
	gil = PyGILState_Ensure();
	Py_XDECREF(ctx->split_callable);
	Py_XDECREF(ctx->cost_callable);
	PyGILState_Release(gil);
	free(ctx);
}

static const char *qhw_sched_python_split_callback_last_error(
	qhw_sched_py_split_callback_t *ctx)
{
	if (ctx == NULL || ctx->last_error[0] == '\0') {
		return "";
	}

	return ctx->last_error;
}

static qhw_sched_rc_t qhw_sched_set_python_callbacks(
	qhw_sched_t *sched,
	qhw_sched_py_split_callback_t *ctx)
{
	qhw_sched_callbacks_t callbacks;

	if (!qhw_sched_python_callback_has_callbacks(ctx)) {
		return qhw_sched_set_callbacks(sched, NULL);
	}

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.struct_size = sizeof(callbacks);
	if (ctx->split_callable != NULL) {
		callbacks.split_task = qhw_sched_py_split_trampoline;
	}
	if (ctx->cost_callable != NULL) {
		callbacks.estimate_cost = qhw_sched_py_cost_trampoline;
	}
	callbacks.user_data = ctx;
	return qhw_sched_set_callbacks(sched, &callbacks);
}

static qhw_sched_rc_t qhw_sched_load_plugin_allow_threads(
	qhw_sched_t *sched,
	const char *shared_object_path)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_load_plugin(sched, shared_object_path);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_set_policy_allow_threads(
	qhw_sched_t *sched,
	const char *policy_name,
	const qhw_sched_kv_t *options,
	size_t option_count)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_set_policy(sched, policy_name, options, option_count);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_submit_task_allow_threads(
	qhw_sched_t *sched,
	const qhw_sched_task_desc_t *task,
	qhw_sched_py_split_callback_t *callback_ctx)
{
	qhw_sched_rc_t rc;

	qhw_sched_py_split_reset(callback_ctx);
	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_submit_task(sched, task);
	Py_END_ALLOW_THREADS
	qhw_sched_py_split_clear_metadata(callback_ctx);
	return rc;
}

static qhw_sched_rc_t qhw_sched_select_next_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_assignment_t *out_assignment)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_select_next(sched, out_assignment);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_update_priority_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	int64_t priority)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_update_priority(sched, task_id, priority);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_started_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_started(sched, task_id);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_completed_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_completed(sched, task_id);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_failed_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_failed(sched, task_id);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_cancelled_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_cancelled(sched, task_id);
	Py_END_ALLOW_THREADS
	return rc;
}

static qhw_sched_rc_t qhw_sched_task_get_state_allow_threads(
	qhw_sched_t *sched,
	qhw_sched_task_id_t task_id,
	qhw_sched_task_state_t *out_state)
{
	qhw_sched_rc_t rc;

	Py_BEGIN_ALLOW_THREADS
	rc = qhw_sched_task_get_state(sched, task_id, out_state);
	Py_END_ALLOW_THREADS
	return rc;
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
