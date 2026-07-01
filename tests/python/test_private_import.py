import unittest


class PrivateImportTests(unittest.TestCase):
    def test_import_low_level_module(self):
        import qhw_scheduler._qhw_scheduler as private

        self.assertEqual(private.QHW_SCHED_OK, 0)
        self.assertTrue(hasattr(private, "qhw_sched_qpu_create"))
        self.assertTrue(hasattr(private, "qhw_sched_submit_task"))


if __name__ == "__main__":
    unittest.main()
