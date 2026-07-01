import unittest

from qhw_scheduler import (
    QHW_SCHED_TASK_COMPLETED,
    QHW_SCHED_TASK_QUEUED,
    QHW_SCHED_TASK_RUNNING,
    QHW_SCHED_THREAD_SAFE,
    QPU,
    Scheduler,
    kv_f64,
    kv_u64,
)


META_PROVIDER_ID = 1
META_MAX_SHOTS = 2
META_NATIVE_GATE_SET_ID = 3
META_READOUT_ERROR_PPM = 4
META_CALIBRATION_ID = 5
META_SHOTS = 100
META_QUBIT_COUNT = 101
META_CIRCUIT_DEPTH = 102
META_TWO_QUBIT_GATES = 103
META_ESTIMATED_FIDELITY_PPM = 104


class DetailedFifoSchedulerTests(unittest.TestCase):
    def test_realistic_qpu_and_task(self):
        qpu_metadata = [
            kv_u64(META_PROVIDER_ID, 42),
            kv_u64(META_MAX_SHOTS, 4096),
            kv_u64(META_NATIVE_GATE_SET_ID, 7),
            kv_u64(META_READOUT_ERROR_PPM, 18_000),
            kv_f64(META_CALIBRATION_ID, 20260701.01),
        ]
        qpu = QPU(
            qpu_id=0x1_0000_0001,
            num_qubits=20,
            flags=0x1,
            metadata=qpu_metadata,
        )
        sched = Scheduler(qpu, threading=QHW_SCHED_THREAD_SAFE)

        try:
            sched.load_standard_plugin("fifo")
            sched.set_policy("fifo")

            qasm_payload = b"""
OPENQASM 3.0;
include "stdgates.inc";
qubit[5] q;
bit[5] c;
h q[0];
cx q[0], q[1];
cx q[1], q[2];
rx(1.57079632679) q[3];
measure q -> c;
"""
            task_metadata = [
                kv_u64(META_SHOTS, 1024),
                kv_u64(META_QUBIT_COUNT, 5),
                kv_u64(META_CIRCUIT_DEPTH, 18),
                kv_u64(META_TWO_QUBIT_GATES, 2),
                kv_u64(META_ESTIMATED_FIDELITY_PPM, 930_000),
            ]

            sched.submit_task(
                task_id=0xCAFE_0001,
                parent_task_id=0,
                owner_id=1001,
                job_id=0x900D,
                priority=25,
                deadline_ns=1_800_000_000_000,
                estimated_runtime_ns=42_000_000,
                payload=qasm_payload,
                metadata=task_metadata,
            )

            profile = qpu.profile()
            self.assertEqual(profile.qpu_id, 0x1_0000_0001)
            self.assertEqual(profile.num_qubits, 20)
            self.assertEqual(profile.flags, 0x1)
            self.assertEqual(profile.metadata_count, len(qpu_metadata))
            self.assertEqual(sched.task_count(), 1)
            self.assertEqual(
                sched.task_state(0xCAFE_0001),
                QHW_SCHED_TASK_QUEUED,
            )

            assignment = sched.select_next_assignment()
            print("selected detailed fifo task:")
            print(f"  task_id: 0x{assignment.task_id:x}")
            print(f"  estimated_runtime_ns: {assignment.estimated_runtime_ns}")
            print(f"  payload_size: {assignment.payload_size}")
            print(
                "  payload_preview: "
                f"{assignment.payload_bytes.splitlines()[1].decode()}"
            )

            self.assertEqual(assignment.task_id, 0xCAFE_0001)
            self.assertEqual(assignment.estimated_runtime_ns, 42_000_000)
            self.assertEqual(assignment.payload_bytes, qasm_payload)

            sched.task_started(assignment.task_id)
            self.assertEqual(
                sched.task_state(assignment.task_id),
                QHW_SCHED_TASK_RUNNING,
            )
            sched.task_completed(assignment.task_id)
            self.assertEqual(
                sched.task_state(assignment.task_id),
                QHW_SCHED_TASK_COMPLETED,
            )

            runtime = qpu.runtime()
            print("qpu runtime after completion:")
            print(f"  completed_count: {runtime.completed_count}")
            print(f"  failed_count: {runtime.failed_count}")
            print(f"  cancelled_count: {runtime.cancelled_count}")
            print(f"  running_task_id: {runtime.running_task_id}")
            self.assertEqual(runtime.completed_count, 1)
            self.assertEqual(runtime.failed_count, 0)
            self.assertEqual(runtime.cancelled_count, 0)
            self.assertEqual(runtime.running_task_id, 0)
        finally:
            sched.close()
            qpu.close()


if __name__ == "__main__":
    unittest.main()
