import unittest

from qhw_scheduler import (
    QHW_SCHED_META_MAX_SHOTS,
    QHW_SCHED_META_PARENT_TASK_ID,
    QHW_SCHED_META_SHOTS,
    QHW_SCHED_META_SLICE_COUNT,
    QHW_SCHED_META_SLICE_INDEX,
    QHW_SCHED_TASK_COMPLETED,
    QHW_SCHED_TASK_WAITING,
    QPU,
    Scheduler,
    SchedulerError,
    kv_u64,
    metadata_u64,
)


class SplitCallbackTests(unittest.TestCase):
    def _scheduler(self):
        qpu = QPU(
            qpu_id=8,
            num_qubits=20,
            metadata=[kv_u64(QHW_SCHED_META_MAX_SHOTS, 100)],
        )
        sched = Scheduler(qpu)
        sched.load_standard_plugin("fifo")
        sched.set_policy("fifo")
        return qpu, sched

    def test_python_split_callback_creates_child_tasks(self):
        qpu, sched = self._scheduler()
        calls = []

        def split_task(task, config):
            calls.append((task, config))
            remaining = config["requested_shots"]
            children = []
            for index in range(config["slice_count"]):
                shots = min(config["slice_shots"], remaining)
                children.append({
                    "task_id": task["task_id"] + 100 + index,
                    "parent_task_id": task["task_id"],
                    "priority": task["priority"],
                    "estimated_runtime_ns": shots,
                    "metadata": [
                        metadata_u64(QHW_SCHED_META_SHOTS, shots),
                        metadata_u64(
                            QHW_SCHED_META_PARENT_TASK_ID,
                            task["task_id"],
                        ),
                        metadata_u64(QHW_SCHED_META_SLICE_INDEX, index),
                        metadata_u64(
                            QHW_SCHED_META_SLICE_COUNT,
                            config["slice_count"],
                        ),
                    ],
                })
                remaining -= shots
            return children

        try:
            sched.set_split_callback(split_task)
            sched.submit_task(
                1000,
                priority=7,
                estimated_runtime_ns=250,
                payload=b"parent-payload",
                metadata=[kv_u64(QHW_SCHED_META_SHOTS, 250)],
            )

            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][1]["slice_shots"], 100)
            self.assertEqual(calls[0][1]["slice_count"], 3)
            self.assertEqual(sched.task_count(), 4)
            self.assertEqual(sched.task_state(1000), QHW_SCHED_TASK_WAITING)

            assignment = sched.select_next_assignment()
            self.assertEqual(assignment.task_id, 1100)
            self.assertEqual(assignment.parent_task_id, 1000)
            self.assertEqual(assignment.slice_index, 0)
            self.assertEqual(assignment.slice_count, 3)
            self.assertEqual(assignment.payload_bytes, b"parent-payload")
            self.assertEqual(assignment.estimated_runtime_ns, 100)
            sched.task_started(assignment.task_id)
            sched.task_completed(assignment.task_id)

            assignment = sched.select_next_assignment()
            self.assertEqual(assignment.task_id, 1101)
            self.assertEqual(assignment.slice_index, 1)
            sched.task_started(assignment.task_id)
            sched.task_completed(assignment.task_id)

            assignment = sched.select_next_assignment()
            self.assertEqual(assignment.task_id, 1102)
            self.assertEqual(assignment.slice_index, 2)
            self.assertEqual(assignment.estimated_runtime_ns, 50)
            sched.task_started(assignment.task_id)
            sched.task_completed(assignment.task_id)

            self.assertEqual(sched.task_state(1000), QHW_SCHED_TASK_COMPLETED)
        finally:
            sched.close()
            qpu.close()

    def test_python_split_callback_error_is_reported(self):
        qpu, sched = self._scheduler()

        def bad_split(_task, _config):
            return []

        try:
            sched.set_split_callback(bad_split)
            with self.assertRaises(SchedulerError) as ctx:
                sched.submit_task(
                    2000,
                    metadata=[kv_u64(QHW_SCHED_META_SHOTS, 250)],
                )
            self.assertIn("wrong child count", str(ctx.exception))
            self.assertEqual(sched.task_count(), 0)
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
