import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("validation_support", ROOT / "scripts/validation_support.py")
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


class ValidationSupportTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="soh-validation-report-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_exit_codes_do_not_hide_missing_suites(self):
        for statuses, expected in (([], ("FAILED", 1)), (["PASSED"], ("PASSED", 0)),
                                   (["PASSED", "FAILED", "NOT_RUN"], ("FAILED", 1)),
                                   (["PASSED", "SKIPPED"], ("PARTIAL", 2)),
                                   (["BLOCKED"], ("PARTIAL", 2)), (["NOT_RUN"], ("PARTIAL", 2))):
            with self.subTest(statuses=statuses):
                self.assertEqual(support.overall_status([{"status": status} for status in statuses]), expected)

    def test_junit_counts_actual_cases_not_declared_counts(self):
        path = self.root / "report.xml"
        path.write_text('<testsuites><testsuite tests="99"><testcase/><testcase><failure/></testcase>'
                        '<testcase><error/></testcase><testcase><skipped/></testcase></testsuite></testsuites>')
        self.assertEqual(support.read_junit(path), {"tests": 4, "failed": 2, "skipped": 1})

    def test_empty_or_malformed_report_is_not_green(self):
        path = self.root / "report.xml"
        path.write_text('<testsuite tests="0"/>')
        with self.assertRaises(ValueError):
            support.read_junit(path)
        path.write_text('not XML')
        with self.assertRaises(ET.ParseError):
            support.read_junit(path)
        with self.assertRaises(ValueError):
            support.run_unittest_suite(unittest.TestSuite(), path, self.root / "test.log")

    def test_unittest_reports_pass_failure_skip_and_subtest(self):
        class Cases(unittest.TestCase):
            def test_pass(self):
                self.assertEqual(2 + 2, 4)

            def test_fail(self):
                self.fail("intentional negative control")

            @unittest.skip("intentional missing prerequisite")
            def test_skip(self):
                pass

            def test_subtest(self):
                with self.subTest(value=1):
                    self.fail("intentional failing subtest")

        for name, expected in (("test_pass", "PASSED"), ("test_fail", "FAILED"),
                               ("test_skip", "SKIPPED"), ("test_subtest", "FAILED")):
            with self.subTest(name=name):
                report = self.root / f"{name}.xml"
                log = self.root / f"{name}.log"
                result = support.run_unittest_suite(unittest.TestSuite([Cases(name)]), report, log)
                self.assertEqual(result["status"], expected)
                self.assertEqual(result["tests"], 1)
                self.assertTrue(log.is_file())
                self.assertEqual(support.read_junit(report)["failed"], int(expected == "FAILED"))

    def test_e2e_placeholder_returns_partial_with_report(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/check.py"), "e2e", "--report-dir",
                                 str(self.root)], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads(next(self.root.glob("*/summary.json")).read_text())
        self.assertEqual(report["status"], "PARTIAL")
        self.assertEqual(report["suites"][0]["status"], "NOT_RUN")

    def test_missing_header_is_blocked_not_passed(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/check.py"), "env", "--report-dir",
                                 str(self.root), "--imgui-include-dir", str(self.root / "missing")],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads(next(self.root.glob("*/summary.json")).read_text())
        self.assertEqual(report["suites"][0]["status"], "BLOCKED")


if __name__ == "__main__":
    unittest.main()
