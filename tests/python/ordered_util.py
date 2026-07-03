from qhw_scheduler import (
    QHW_SCHED_OPT_ORDER_KEY,
    QPU,
    Scheduler,
    kv_u64,
)


def order_option(order_key):
    return kv_u64(QHW_SCHED_OPT_ORDER_KEY, order_key)


def selected_order(options, tasks):
    with QPU(qpu_id=90, num_qubits=20) as qpu:
        with Scheduler(qpu) as sched:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=options)
            for task in tasks:
                task = dict(task)
                task_id = task.pop("task_id")
                sched.submit_task(task_id, **task)
            return [sched.select_next() for _ in tasks]
