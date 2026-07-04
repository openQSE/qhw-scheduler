#ifndef QHW_DS_ERROR_H
#define QHW_DS_ERROR_H

#include "qhw_scheduler/qhw_scheduler_types.h"

#include <qhw_datastructures/qhw_hash_table.h>

static inline qhw_sched_rc_t qhw_hash_insert_rc_to_sched_rc(int rc)
{
	switch (rc) {
	case QHW_HASH_TABLE_OK:
		return QHW_SCHED_OK;
	case QHW_HASH_TABLE_ERR_EXISTS:
		return QHW_SCHED_ERR_EXISTS;
	case QHW_HASH_TABLE_ERR_NO_MEMORY:
		return QHW_SCHED_ERR_NO_MEMORY;
	default:
		return QHW_SCHED_ERR_INVALID_ARG;
	}
}

#endif
