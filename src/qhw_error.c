#include "qhw_scheduler_internal.h"

#include <stdio.h>
#include <string.h>

void qhw_error_set(
	struct qhw_sched_error *error,
	qhw_sched_rc_t code,
	const char *message)
{
	if (error == NULL) {
		return;
	}

	error->code = code;
	if (message == NULL) {
		message = "";
	}

	(void)snprintf(error->message, sizeof(error->message), "%s", message);
}

const char *qhw_sched_last_error(qhw_sched_t *sched)
{
	if (sched == NULL) {
		return "scheduler handle is NULL";
	}

	return sched->last_error.message;
}

