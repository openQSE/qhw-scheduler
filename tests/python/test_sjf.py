import unittest

from ordered_util import order_option, selected_order
from qhw_scheduler import (
    QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_ORDER_SJF,
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


if __name__ == "__main__":
    unittest.main()
