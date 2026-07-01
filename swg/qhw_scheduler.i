%module _qhw_scheduler

%{
#include "qhw_scheduler/qhw_scheduler.h"
%}

%include <stdint.i>
%include <typemaps.i>

%include "qhw_scheduler/qhw_scheduler_types.h"
%include "qhw_scheduler/qhw_scheduler_plugin.h"
%include "qhw_scheduler/qhw_scheduler.h"
