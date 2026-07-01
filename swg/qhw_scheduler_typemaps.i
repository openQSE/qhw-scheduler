%define QHW_SCHED_APPEND_OUTPUT(obj)
	if ((!$result) || ($result == Py_None)) {
		$result = obj;
	} else {
		PyObject *old_result = $result;
		PyObject *tuple = PyTuple_New(1);
		PyTuple_SetItem(tuple, 0, obj);
		if (!PyTuple_Check(old_result)) {
			PyObject *first = PyTuple_New(1);
			PyTuple_SetItem(first, 0, old_result);
			$result = PySequence_Concat(first, tuple);
			Py_DECREF(first);
		} else {
			$result = PySequence_Concat(old_result, tuple);
			Py_DECREF(old_result);
		}
		Py_DECREF(tuple);
	}
%enddef

%typemap(in, numinputs=0) qhw_sched_qpu_t **out_qpu
	(qhw_sched_qpu_t *tmp) {
	tmp = NULL;
	$1 = &tmp;
}

%typemap(argout) qhw_sched_qpu_t **out_qpu {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr(*$1),
		$*1_descriptor,
		0);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_t **out_sched
	(qhw_sched_t *tmp) {
	tmp = NULL;
	$1 = &tmp;
}

%typemap(argout) qhw_sched_t **out_sched {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr(*$1),
		$*1_descriptor,
		0);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_qpu_profile_t *out_profile
	(qhw_sched_qpu_profile_t *tmp) {
	tmp = (qhw_sched_qpu_profile_t *)calloc(1, sizeof(*tmp));
	if (tmp == NULL) {
		PyErr_NoMemory();
		SWIG_fail;
	}
	tmp->struct_size = sizeof(*tmp);
	$1 = tmp;
}

%typemap(argout) qhw_sched_qpu_profile_t *out_profile {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr($1),
		$1_descriptor,
		SWIG_POINTER_OWN);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_qpu_runtime_t *out_runtime
	(qhw_sched_qpu_runtime_t *tmp) {
	tmp = (qhw_sched_qpu_runtime_t *)calloc(1, sizeof(*tmp));
	if (tmp == NULL) {
		PyErr_NoMemory();
		SWIG_fail;
	}
	tmp->struct_size = sizeof(*tmp);
	$1 = tmp;
}

%typemap(argout) qhw_sched_qpu_runtime_t *out_runtime {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr($1),
		$1_descriptor,
		SWIG_POINTER_OWN);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_assignment_t *out_assignment
	(qhw_sched_assignment_t *tmp) {
	tmp = (qhw_sched_assignment_t *)calloc(1, sizeof(*tmp));
	if (tmp == NULL) {
		PyErr_NoMemory();
		SWIG_fail;
	}
	tmp->struct_size = sizeof(*tmp);
	$1 = tmp;
}

%typemap(argout) qhw_sched_assignment_t *out_assignment {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr($1),
		$1_descriptor,
		SWIG_POINTER_OWN);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_task_state_t *out_state
	(qhw_sched_task_state_t tmp) {
	tmp = QHW_SCHED_TASK_UNKNOWN;
	$1 = &tmp;
}

%typemap(argout) qhw_sched_task_state_t *out_state {
	PyObject *obj = SWIG_From_int((int)*$1);
	QHW_SCHED_APPEND_OUTPUT(obj)
}

%typemap(in, numinputs=0) qhw_sched_policy_list_t **out_list
	(qhw_sched_policy_list_t *tmp) {
	tmp = NULL;
	$1 = &tmp;
}

%typemap(argout) qhw_sched_policy_list_t **out_list {
	PyObject *obj = SWIG_NewPointerObj(
		SWIG_as_voidptr(*$1),
		$*1_descriptor,
		0);
	QHW_SCHED_APPEND_OUTPUT(obj)
}
