"""Regression tests for native-result validation, including Unreal's zero-exit failure case."""
import json
from pathlib import Path
import tempfile
import unittest
from validate_unreal import read_results, REQUIRED_TESTS


class NativeReportTests(unittest.TestCase):
    def check_report(self, data, succeeds):
        with tempfile.TemporaryDirectory() as folder:
            report = Path(folder) / "index.json"
            data.setdefault("tests", [{"fullTestPath": name, "state": "Success"} for name in REQUIRED_TESTS])
            report.write_text(json.dumps(data), encoding="utf-8")
            if succeeds:
                self.assertEqual(read_results(report)["succeeded"], data["succeeded"])
            else:
                with self.assertRaises(RuntimeError):
                    read_results(report)

    def test_success(self):
        self.check_report({"succeeded": 18, "failed": 0}, True)

    def test_failed_report_overrides_process_exit(self):
        self.check_report({"succeeded": 5, "failed": 1}, False)

    def test_empty_report_fails(self):
        self.check_report({"succeeded": 0, "failed": 0}, False)

    def test_incomplete_report_fails(self):
        self.check_report({"succeeded": 4, "notRun": 1}, False)

    def test_missing_regressions_cannot_hide_behind_old_green_suite(self):
        self.check_report({"succeeded": 5, "failed": 0, "tests": []}, False)


if __name__ == "__main__":
    unittest.main()
