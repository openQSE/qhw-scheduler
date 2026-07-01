#include "qhw_scheduler/qhw_scheduler_plugin.h"

static const qhw_sched_plugin_desc_t bad_desc = {
	.struct_size = sizeof(bad_desc),
	.abi_version = QHW_SCHED_ABI_VERSION,
	.name = "bad"
};

const qhw_sched_plugin_desc_t *qhw_sched_plugin_descriptor(void)
{
	return &bad_desc;
}
