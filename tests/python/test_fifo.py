import unittest

from qhw_scheduler import QPU, Scheduler


class FifoSchedulerTests(unittest.TestCase):
    def test_fifo_order(self):
        qpu = QPU(qpu_id=1, num_qubits=20)
        sched = Scheduler(qpu)

        try:
            sched.load_standard_plugin("fifo")
            sched.set_policy("fifo")

            submitted = list(range(1, 11))
            for task_id in submitted:
                sched.submit_task(task_id)

            selected = [sched.select_next() for _ in submitted]
            print(f"fifo selected order: {selected}")
            self.assertEqual(selected, submitted)
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
