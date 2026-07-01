import unittest
from pathlib import Path


class PrivateImportTests(unittest.TestCase):
    def test_import_low_level_module(self):
        import qhw_scheduler._qhw_scheduler as private

        self.assertEqual(private.QHW_SCHED_OK, 0)
        self.assertTrue(hasattr(private, "qhw_sched_qpu_create"))
        self.assertTrue(hasattr(private, "qhw_sched_submit_task"))

    def test_private_out_parameters(self):
        import qhw_scheduler._qhw_scheduler as private

        profile = private.qhw_sched_qpu_profile_t()
        profile.struct_size = 0
        profile.qpu_id = 100
        profile.num_qubits = 20

        rc, qpu = private.qhw_sched_qpu_create(profile)
        self.assertEqual(rc, private.QHW_SCHED_OK)
        self.assertIsNotNone(qpu)

        attr = private.qhw_sched_attr_t()
        attr.struct_size = 0
        attr.threading = private.QHW_SCHED_THREAD_SAFE

        rc, sched = private.qhw_sched_create(None, attr, qpu, None, 0)
        self.assertEqual(rc, private.QHW_SCHED_OK)
        self.assertIsNotNone(sched)

        try:
            rc, runtime = private.qhw_sched_qpu_get_runtime(qpu)
            self.assertEqual(rc, private.QHW_SCHED_OK)
            self.assertEqual(runtime.queued_count, 0)

            plugin = Path(private.__file__).parent
            plugin = plugin / "plugins" / "qhw_sched_fifo.so"
            rc = private.qhw_sched_load_plugin(sched, str(plugin))
            self.assertEqual(rc, private.QHW_SCHED_OK)
            rc = private.qhw_sched_set_policy(sched, "fifo", None, 0)
            self.assertEqual(rc, private.QHW_SCHED_OK)

            rc, policies = private.qhw_sched_policy_list_create(sched)
            self.assertEqual(rc, private.QHW_SCHED_OK)
            try:
                count = private.qhw_sched_policy_list_count(policies)
                self.assertGreaterEqual(count, 1)
                info = private.qhw_sched_policy_list_get(policies, 0)
                self.assertEqual(info.name, "fifo")
            finally:
                private.qhw_sched_policy_list_destroy(policies)

            task = private.qhw_sched_task_desc_t()
            task.struct_size = 0
            task.task_id = 500
            task.owner_id = 10
            task.job_id = 20
            task.priority = 7
            task.estimated_runtime_ns = 1000

            rc = private.qhw_sched_submit_task(sched, task)
            self.assertEqual(rc, private.QHW_SCHED_OK)
            rc, state = private.qhw_sched_task_get_state(sched, 500)
            self.assertEqual(rc, private.QHW_SCHED_OK)
            self.assertEqual(state, private.QHW_SCHED_TASK_QUEUED)

            rc, assignment = private.qhw_sched_select_next(sched)
            self.assertEqual(rc, private.QHW_SCHED_OK)
            self.assertEqual(assignment.task_id, 500)
        finally:
            private.qhw_sched_destroy(sched)
            private.qhw_sched_qpu_destroy(qpu)


if __name__ == "__main__":
    unittest.main()
