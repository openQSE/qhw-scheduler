#include "qhw_scheduler_internal.h"

#include <stdlib.h>
#include <string.h>

qhw_sched_rc_t qhw_sched_qpu_create(
	const qhw_sched_qpu_profile_t *profile,
	qhw_sched_qpu_t **out_qpu)
{
	qhw_sched_qpu_t *qpu;

	if (profile == NULL || out_qpu == NULL || profile->qpu_id == 0) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	qpu = calloc(1, sizeof(*qpu));
	if (qpu == NULL) {
		return QHW_SCHED_ERR_NO_MEMORY;
	}

	qpu->profile = *profile;
	qpu->runtime.struct_size = sizeof(qpu->runtime);
	qpu->refcount = 1;

	if (profile->metadata_count > 0) {
		size_t bytes = profile->metadata_count * sizeof(*profile->metadata);

		qpu->metadata = malloc(bytes);
		if (qpu->metadata == NULL) {
			free(qpu);
			return QHW_SCHED_ERR_NO_MEMORY;
		}

		memcpy(qpu->metadata, profile->metadata, bytes);
		qpu->profile.metadata = qpu->metadata;
	}

	*out_qpu = qpu;
	return QHW_SCHED_OK;
}

void qhw_sched_qpu_destroy(qhw_sched_qpu_t *qpu)
{
	qhw_qpu_release(qpu);
}

void qhw_qpu_retain(qhw_sched_qpu_t *qpu)
{
	if (qpu != NULL) {
		qpu->refcount++;
	}
}

void qhw_qpu_release(qhw_sched_qpu_t *qpu)
{
	if (qpu == NULL) {
		return;
	}

	qpu->refcount--;
	if (qpu->refcount == 0) {
		free(qpu->metadata);
		free(qpu);
	}
}

qhw_sched_rc_t qhw_sched_qpu_get_profile(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_profile_t *out_profile)
{
	if (qpu == NULL || out_profile == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_profile = qpu->profile;
	return QHW_SCHED_OK;
}

qhw_sched_rc_t qhw_sched_qpu_get_runtime(
	qhw_sched_qpu_t *qpu,
	qhw_sched_qpu_runtime_t *out_runtime)
{
	if (qpu == NULL || out_runtime == NULL) {
		return QHW_SCHED_ERR_INVALID_ARG;
	}

	*out_runtime = qpu->runtime;
	return QHW_SCHED_OK;
}
