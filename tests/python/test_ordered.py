import unittest

from qhw_scheduler import (
    QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE,
    QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST,
    QHW_SCHED_OPT_DEADLINE_NOW_NS,
    QHW_SCHED_OPT_ORDER_KEY,
    QHW_SCHED_ORDER_FIFO,
    QHW_SCHED_ORDER_PRIORITY,
    QPU,
    Scheduler,
    kv_i64,
    kv_u64,
)


class OrderedSchedulerTests(unittest.TestCase):
    def test_default_order_is_priority_then_fifo(self):
        qpu = QPU(qpu_id=30, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered")
            sched.submit_task(1, priority=1)
            sched.submit_task(2, priority=10)
            sched.submit_task(3, priority=10)
            self.assertEqual(
                [sched.select_next() for _ in range(3)],
                [2, 3, 1],
            )
        finally:
            sched.close()
            qpu.close()

    def test_order_can_be_fifo_only(self):
        qpu = QPU(qpu_id=31, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_FIFO),
            ])
            sched.submit_task(1, priority=1)
            sched.submit_task(2, priority=100)
            self.assertEqual(sched.select_next(), 1)
            self.assertEqual(sched.select_next(), 2)
        finally:
            sched.close()
            qpu.close()

    def test_deadline_boost_combines_with_priority_key(self):
        qpu = QPU(qpu_id=32, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE, 1),
                kv_u64(QHW_SCHED_OPT_DEADLINE_NOW_NS, 970),
                kv_i64(QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST, 100),
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_PRIORITY),
            ])
            sched.submit_task(
                1,
                priority=1,
                deadline_ns=1000,
                estimated_runtime_ns=100,
            )
            sched.submit_task(2, priority=50)
            self.assertEqual(sched.select_next(), 1)
            self.assertEqual(sched.select_next(), 2)
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
