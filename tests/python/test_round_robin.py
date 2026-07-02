import unittest

from qhw_scheduler import QPU, Scheduler


class RoundRobinSchedulerTests(unittest.TestCase):
    def test_round_robin_by_job(self):
        qpu = QPU(qpu_id=30, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("round_robin")
            sched.set_policy("round_robin")

            sched.submit_task(1, job_id=10)
            sched.submit_task(2, job_id=10)
            sched.submit_task(3, job_id=20)
            sched.submit_task(4, job_id=20)

            selected = [sched.select_next() for _ in range(4)]
            print(f"round_robin selected order: {selected}")
            self.assertEqual(selected, [1, 3, 2, 4])
        finally:
            sched.close()
            qpu.close()

    def test_reservation_overrides_job(self):
        qpu = QPU(qpu_id=31, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("round_robin")
            sched.set_policy("round_robin")

            sched.submit_task(11, job_id=100, reservation_id=7)
            sched.submit_task(12, job_id=200, reservation_id=7)
            sched.submit_task(13, job_id=100, reservation_id=8)

            selected = [sched.select_next() for _ in range(3)]
            self.assertEqual(selected, [11, 13, 12])
        finally:
            sched.close()
            qpu.close()

    def test_ungrouped_tasks_keep_fifo_order(self):
        qpu = QPU(qpu_id=32, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("round_robin")
            sched.set_policy("round_robin")

            for task_id in range(21, 25):
                sched.submit_task(task_id)

            selected = [sched.select_next() for _ in range(4)]
            self.assertEqual(selected, [21, 22, 23, 24])
        finally:
            sched.close()
            qpu.close()

    def test_group_namespaces_do_not_collide(self):
        qpu = QPU(qpu_id=34, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("round_robin")
            sched.set_policy("round_robin")

            sched.submit_task(20, job_id=10)
            sched.submit_task(21, job_id=10)
            sched.submit_task(10)
            sched.submit_task(30, job_id=99, reservation_id=10)
            sched.submit_task(31, job_id=99, reservation_id=10)

            selected = [sched.select_next() for _ in range(5)]
            self.assertEqual(selected, [20, 10, 30, 21, 31])
        finally:
            sched.close()
            qpu.close()

    def test_cancelled_group_is_removed(self):
        qpu = QPU(qpu_id=33, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("round_robin")
            sched.set_policy("round_robin")

            sched.submit_task(31, job_id=10)
            sched.submit_task(32, job_id=10)
            sched.submit_task(33, job_id=20)
            sched.task_cancelled(31)
            sched.task_cancelled(32)

            self.assertEqual(sched.select_next(), 33)
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
