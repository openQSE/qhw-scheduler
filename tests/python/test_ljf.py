import unittest

from ordered_util import order_option, selected_order
from qhw_scheduler import (
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_ORDER_LJF,
    kv_u64,
)


class LJFSchedulerTests(unittest.TestCase):
    def test_ljf_orders_by_estimated_runtime(self):
        self.assertEqual(
            selected_order(
                [order_option(QHW_SCHED_ORDER_LJF)],
                [
                    {"task_id": 1, "estimated_runtime_ns": 300},
                    {"task_id": 2, "estimated_runtime_ns": 100},
                    {"task_id": 3, "estimated_runtime_ns": 200},
                ],
            ),
            [1, 3, 2],
        )

    def test_ljf_uses_shots_when_runtime_is_missing(self):
        self.assertEqual(
            selected_order(
                [order_option(QHW_SCHED_ORDER_LJF)],
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
            [11, 12],
        )


if __name__ == "__main__":
    unittest.main()
