import unittest

from ordered_util import order_option, selected_order
from qhw_scheduler import (
    QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_OPT_ORDER_KEY,
    QHW_SCHED_ORDER_SJF,
    QPU,
    Scheduler,
    kv_u64,
)


class SJFSchedulerTests(unittest.TestCase):
    def test_sjf_orders_by_estimated_runtime(self):
        self.assertEqual(
            selected_order(
                [order_option(QHW_SCHED_ORDER_SJF)],
                [
                    {"task_id": 1, "estimated_runtime_ns": 300},
                    {"task_id": 2, "estimated_runtime_ns": 100},
                    {"task_id": 3, "estimated_runtime_ns": 200},
                ],
            ),
            [2, 3, 1],
        )

    def test_sjf_uses_shots_when_runtime_is_missing(self):
        self.assertEqual(
            selected_order(
                [order_option(QHW_SCHED_ORDER_SJF)],
                [
                    {
                        "task_id": 11,
                        "metadata": [kv_u64(QHW_SCHED_META_SHOTS, 1000)],
                    },
                    {
                        "task_id": 12,
                        "metadata": [kv_u64(QHW_SCHED_META_SHOTS, 100)],
                    },
                ],
            ),
            [12, 11],
        )

    def test_sjf_prefers_runtime_metadata_over_shots(self):
        self.assertEqual(
            selected_order(
                [order_option(QHW_SCHED_ORDER_SJF)],
                [
                    {
                        "task_id": 21,
                        "metadata": [
                            kv_u64(
                                QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
                                1000,
                            ),
                            kv_u64(QHW_SCHED_META_SHOTS, 1),
                        ],
                    },
                    {
                        "task_id": 22,
                        "metadata": [
                            kv_u64(
                                QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
                                100,
                            ),
                            kv_u64(QHW_SCHED_META_SHOTS, 10000),
                        ],
                    },
                ],
            ),
            [22, 21],
        )

    def test_sjf_uses_cost_callback(self):
        calls = []

        def estimate_cost(task, qpu):
            self.assertEqual(qpu["num_qubits"], 20)
            calls.append(task["task_id"])
            return 5 if task["task_id"] == 31 else 50

        with QPU(qpu_id=91, num_qubits=20) as qpu:
            with Scheduler(qpu) as sched:
                sched.load_standard_plugin("ordered")
                sched.set_cost_callback(estimate_cost)
                sched.set_policy("ordered", options=[
                    kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
                ])
                sched.submit_task(31, estimated_runtime_ns=1000)
                sched.submit_task(32, estimated_runtime_ns=1)

                first = sched.select_next_assignment()
                second = sched.select_next_assignment()

        self.assertEqual(calls, [31, 32])
        self.assertEqual(first.task_id, 31)
        self.assertEqual(first.estimated_cost, 5)
        self.assertEqual(second.task_id, 32)
        self.assertEqual(second.estimated_cost, 50)


if __name__ == "__main__":
    unittest.main()
