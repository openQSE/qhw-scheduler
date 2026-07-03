import unittest

from ordered_util import order_option, selected_order
from qhw_scheduler import (
    QHW_SCHED_ORDER_FIFO,
    QHW_SCHED_ORDER_LJF,
    QHW_SCHED_ORDER_PRIORITY,
    QHW_SCHED_ORDER_ROUND_ROBIN,
    QHW_SCHED_ORDER_SJF,
)


class OrderedCompositionTests(unittest.TestCase):
    def test_sjf_priority_fifo(self):
        self.assertEqual(
            selected_order(
                [
                    order_option(QHW_SCHED_ORDER_SJF),
                    order_option(QHW_SCHED_ORDER_PRIORITY),
                    order_option(QHW_SCHED_ORDER_FIFO),
                ],
                [
                    {"task_id": 1, "priority": 1, "estimated_runtime_ns": 100},
                    {"task_id": 2, "priority": 10, "estimated_runtime_ns": 100},
                    {"task_id": 3, "priority": 50, "estimated_runtime_ns": 200},
                ],
            ),
            [2, 1, 3],
        )

    def test_ljf_priority_fifo(self):
        self.assertEqual(
            selected_order(
                [
                    order_option(QHW_SCHED_ORDER_LJF),
                    order_option(QHW_SCHED_ORDER_PRIORITY),
                    order_option(QHW_SCHED_ORDER_FIFO),
                ],
                [
                    {"task_id": 1, "priority": 1, "estimated_runtime_ns": 500},
                    {"task_id": 2, "priority": 10, "estimated_runtime_ns": 500},
                    {"task_id": 3, "priority": 50, "estimated_runtime_ns": 100},
                ],
            ),
            [2, 1, 3],
        )

    def test_sjf_round_robin_fifo(self):
        self.assertEqual(
            selected_order(
                [
                    order_option(QHW_SCHED_ORDER_SJF),
                    order_option(QHW_SCHED_ORDER_ROUND_ROBIN),
                    order_option(QHW_SCHED_ORDER_FIFO),
                ],
                [
                    {"task_id": 1, "job_id": 10, "estimated_runtime_ns": 100},
                    {"task_id": 2, "job_id": 10, "estimated_runtime_ns": 100},
                    {"task_id": 3, "job_id": 20, "estimated_runtime_ns": 100},
                    {"task_id": 4, "job_id": 20, "estimated_runtime_ns": 100},
                    {"task_id": 5, "job_id": 30, "estimated_runtime_ns": 900},
                ],
            ),
            [1, 3, 2, 4, 5],
        )

    def test_ljf_round_robin_fifo(self):
        self.assertEqual(
            selected_order(
                [
                    order_option(QHW_SCHED_ORDER_LJF),
                    order_option(QHW_SCHED_ORDER_ROUND_ROBIN),
                    order_option(QHW_SCHED_ORDER_FIFO),
                ],
                [
                    {"task_id": 1, "job_id": 10, "estimated_runtime_ns": 500},
                    {"task_id": 2, "job_id": 10, "estimated_runtime_ns": 500},
                    {"task_id": 3, "job_id": 20, "estimated_runtime_ns": 500},
                    {"task_id": 4, "job_id": 20, "estimated_runtime_ns": 500},
                    {"task_id": 5, "job_id": 30, "estimated_runtime_ns": 100},
                ],
            ),
            [1, 3, 2, 4, 5],
        )


if __name__ == "__main__":
    unittest.main()
