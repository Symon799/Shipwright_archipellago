"""Reporting helpers around CTest and unittest, not a second test framework."""

import io
import time
import unittest
import xml.etree.ElementTree as ET


def overall_status(suites):
    if not suites:
        return "FAILED", 1
    if any(item["status"] == "FAILED" for item in suites):
        return "FAILED", 1
    if any(item["status"] != "PASSED" for item in suites):
        return "PARTIAL", 2
    return "PASSED", 0


def read_junit(path):
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    if not cases:
        raise ValueError("The selected suite discovered zero tests")
    return {
        "tests": len(cases),
        "failed": sum(case.find("failure") is not None or case.find("error") is not None for case in cases),
        "skipped": sum(case.find("skipped") is not None for case in cases),
    }


class JUnitResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cases = []
        self.started = 0

    def startTest(self, test):
        self.started = time.perf_counter()
        super().startTest(test)

    def record(self, test, kind=None, text=""):
        self.cases.append((test.id(), time.perf_counter() - self.started, kind, text))

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record(test, "failure", self._exc_info_to_string(err, test))

    def addError(self, test, err):
        super().addError(test, err)
        self.record(test, "error", self._exc_info_to_string(err, test))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.record(test, "skipped", reason)

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err is not None:
            self.record(subtest, "failure", self._exc_info_to_string(err, test))

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self.record(test, "skipped", "Expected failure (not a validated behavior)")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.record(test, "failure", "Unexpected success")


def run_unittest_suite(suite, junit_path, log_path):
    if suite.countTestCases() == 0:
        raise ValueError("The selected Python suite discovered zero tests")
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2, resultclass=JUnitResult).run(suite)
    log_path.write_text(stream.getvalue(), encoding="utf-8")
    root = ET.Element("testsuite", name="python", tests=str(len(result.cases)),
                      failures=str(sum(case[2] == "failure" for case in result.cases)),
                      errors=str(sum(case[2] == "error" for case in result.cases)),
                      skipped=str(sum(case[2] == "skipped" for case in result.cases)))
    for name, duration, kind, text in result.cases:
        case = ET.SubElement(root, "testcase", name=name, time=f"{duration:.6f}")
        if kind:
            ET.SubElement(case, kind).text = text
    ET.ElementTree(root).write(junit_path, encoding="utf-8", xml_declaration=True)
    counts = read_junit(junit_path)
    if not result.wasSuccessful() or counts["failed"]:
        status = "FAILED"
    elif counts["skipped"]:
        status = "SKIPPED"
    else:
        status = "PASSED"
    return {"status": status, **counts}
