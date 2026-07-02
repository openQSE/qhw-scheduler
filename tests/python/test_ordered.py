import unittest

from qhw_scheduler import (
    QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE,
    QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST,
    QHW_SCHED_OPT_DEADLINE_NOW_NS,
    QHW_SCHED_OPT_ORDER_KEY,
    QHW_SCHED_META_ESTIMATED_RUNTIME_NS,
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_ORDER_FIFO,
    QHW_SCHED_ORDER_PRIORITY,
    QHW_SCHED_ORDER_SJF,
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

    def test_sjf_orders_by_estimated_runtime(self):
        qpu = QPU(qpu_id=33, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
            ])
            sched.submit_task(1, estimated_runtime_ns=300)
            sched.submit_task(2, estimated_runtime_ns=100)
            sched.submit_task(3, estimated_runtime_ns=200)
            self.assertEqual(
                [sched.select_next() for _ in range(3)],
                [2, 3, 1],
            )
        finally:
            sched.close()
            qpu.close()

    def test_sjf_uses_shots_when_runtime_is_missing(self):
        qpu = QPU(qpu_id=34, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
            ])
            sched.submit_task(
                1,
                metadata=[kv_u64(QHW_SCHED_META_SHOTS, 1000)],
            )
            sched.submit_task(
                2,
                metadata=[kv_u64(QHW_SCHED_META_SHOTS, 100)],
            )
            self.assertEqual(sched.select_next(), 2)
            self.assertEqual(sched.select_next(), 1)
        finally:
            sched.close()
            qpu.close()

    def test_sjf_prefers_runtime_metadata_over_shots(self):
        qpu = QPU(qpu_id=38, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
            ])
            sched.submit_task(
                1,
                metadata=[
                    kv_u64(QHW_SCHED_META_ESTIMATED_RUNTIME_NS, 1000),
                    kv_u64(QHW_SCHED_META_SHOTS, 1),
                ],
            )
            sched.submit_task(
                2,
                metadata=[
                    kv_u64(QHW_SCHED_META_ESTIMATED_RUNTIME_NS, 100),
                    kv_u64(QHW_SCHED_META_SHOTS, 10000),
                ],
            )
            self.assertEqual(sched.select_next(), 2)
            self.assertEqual(sched.select_next(), 1)
        finally:
            sched.close()
            qpu.close()

    def test_priority_then_sjf(self):
        qpu = QPU(qpu_id=35, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("ordered")
            sched.set_policy("ordered", options=[
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_PRIORITY),
                kv_u64(QHW_SCHED_OPT_ORDER_KEY, QHW_SCHED_ORDER_SJF),
            ])
            sched.submit_task(1, priority=10, estimated_runtime_ns=500)
            sched.submit_task(2, priority=10, estimated_runtime_ns=100)
            sched.submit_task(3, priority=1, estimated_runtime_ns=1)
            self.assertEqual(
                [sched.select_next() for _ in range(3)],
                [2, 1, 3],
            )
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
