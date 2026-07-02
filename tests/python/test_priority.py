import unittest

from qhw_scheduler import (
    QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE,
    QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST,
    QHW_SCHED_OPT_DEADLINE_NOW_NS,
    QHW_SCHED_TASK_ASSIGNED,
    QPU,
    Scheduler,
    SchedulerError,
    kv_i64,
    kv_u64,
)


class PrioritySchedulerTests(unittest.TestCase):
    def test_priority_order_and_ties(self):
        qpu = QPU(qpu_id=2, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")

            tasks = [
                (1, 0),
                (2, 10),
                (3, 10),
                (4, -1),
                (5, 5),
            ]
            for task_id, priority in tasks:
                sched.submit_task(task_id, priority=priority)

            selected = [sched.select_next() for _ in tasks]
            print(f"priority selected order: {selected}")
            self.assertEqual(selected, [2, 3, 5, 1, 4])
        finally:
            sched.close()
            qpu.close()

    def test_cancelled_ready_task_is_removed(self):
        qpu = QPU(qpu_id=3, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")

            sched.submit_task(10, priority=100)
            sched.submit_task(11, priority=1)
            sched.task_cancelled(10)
            self.assertEqual(sched.select_next(), 11)
        finally:
            sched.close()
            qpu.close()

    def test_ready_heap_grows(self):
        qpu = QPU(qpu_id=4, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")

            for task_id in range(1, 76):
                sched.submit_task(task_id, priority=task_id)

            selected = [sched.select_next() for _ in range(75)]
            self.assertEqual(selected, list(range(75, 0, -1)))
        finally:
            sched.close()
            qpu.close()

    def test_policy_reset_skips_assigned_task(self):
        qpu = QPU(qpu_id=5, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")
            sched.submit_task(1, priority=10)
            sched.submit_task(2, priority=1)

            self.assertEqual(sched.select_next(), 1)
            self.assertEqual(sched.task_state(1), QHW_SCHED_TASK_ASSIGNED)
            sched.set_policy("priority")
            self.assertEqual(sched.select_next(), 2)
        finally:
            sched.close()
            qpu.close()

    def test_priority_update_reorders_ready_tasks(self):
        qpu = QPU(qpu_id=6, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")
            sched.submit_task(1, priority=1)
            sched.submit_task(2, priority=5)
            sched.submit_task(3, priority=10)

            sched.task_update_priority(1, 20)
            sched.task_update_priority(3, 0)
            self.assertEqual(sched.select_next(), 1)
            self.assertEqual(sched.select_next(), 2)
            self.assertEqual(sched.select_next(), 3)
        finally:
            sched.close()
            qpu.close()

    def test_priority_update_rejects_assigned_task(self):
        qpu = QPU(qpu_id=7, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority")
            sched.submit_task(1, priority=1)
            self.assertEqual(sched.select_next(), 1)
            with self.assertRaises(SchedulerError):
                sched.task_update_priority(1, 100)
        finally:
            sched.close()
            qpu.close()

    def test_deadline_boost_reorders_when_enabled(self):
        qpu = QPU(qpu_id=8, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("priority")
            sched.set_policy("priority", options=[
                kv_u64(QHW_SCHED_OPT_DEADLINE_BOOST_ENABLE, 1),
                kv_u64(QHW_SCHED_OPT_DEADLINE_NOW_NS, 970),
                kv_i64(QHW_SCHED_OPT_DEADLINE_CRITICAL_BOOST, 100),
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
