"""JUnit XML writer для WarCard test reports.

Минимальная JUnit-совместимая разметка — достаточная для GitHub Actions
test reporters, IDE интеграций (PyCharm/IntelliJ), и Jenkins.

Использование:
    from tests._junit_writer import TestResult, write_junit_xml

    results = [
        TestResult(name="smoke_pie", duration_s=12.3, status="passed"),
        TestResult(name="smoke_pie_login", duration_s=3.1, status="skipped",
                   message="WC_TEST_LOGIN not set"),
        TestResult(name="test_fragile", duration_s=1.2, status="failed",
                   message="ScreenshotMismatch", details="similarity 0.81 < 0.95"),
    ]
    write_junit_xml("warcard.python", results, "TestResults/python-junit.xml")
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


@dataclass
class TestResult:
    name: str
    duration_s: float
    status: str          # "passed" | "failed" | "skipped" | "error"
    message: str = ""    # short summary
    details: str = ""    # long traceback / stdout
    classname: str = ""  # default = derived from name


def _xml_escape(s: str) -> str:
    # ElementTree сам экранирует, эта функция нужна для CDATA-подобных кусков.
    return (s or "").replace("\x00", "").strip()


def write_junit_xml(suite_name: str, results: list[TestResult], output_path: str | Path) -> Path:
    """Сериализовать список TestResult в JUnit XML.

    Returns:
        Absolute Path к созданному файлу.
    """
    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)

    total = len(results)
    failures = sum(1 for r in results if r.status == "failed")
    errors = sum(1 for r in results if r.status == "error")
    skipped = sum(1 for r in results if r.status == "skipped")
    total_time = sum(r.duration_s for r in results)

    testsuite = ET.Element("testsuite", attrib={
        "name": suite_name,
        "tests": str(total),
        "failures": str(failures),
        "errors": str(errors),
        "skipped": str(skipped),
        "time": f"{total_time:.3f}",
    })

    for r in results:
        case = ET.SubElement(testsuite, "testcase", attrib={
            "name": r.name,
            "classname": r.classname or suite_name,
            "time": f"{r.duration_s:.3f}",
        })
        if r.status == "failed":
            failure = ET.SubElement(case, "failure", attrib={
                "message": _xml_escape(r.message) or "test failed",
                "type": "AssertionError",
            })
            failure.text = _xml_escape(r.details)
        elif r.status == "error":
            error = ET.SubElement(case, "error", attrib={
                "message": _xml_escape(r.message) or "test errored",
                "type": "RuntimeError",
            })
            error.text = _xml_escape(r.details)
        elif r.status == "skipped":
            ET.SubElement(case, "skipped", attrib={
                "message": _xml_escape(r.message) or "skipped",
            })

    # Pretty-print через manual indent (доступно в py 3.9+).
    ET.indent(testsuite, space="  ")
    tree = ET.ElementTree(testsuite)
    tree.write(out, encoding="utf-8", xml_declaration=True)
    return out.resolve()


if __name__ == "__main__":
    sample = [
        TestResult(name="dummy_pass", duration_s=0.1, status="passed"),
        TestResult(name="dummy_skip", duration_s=0.0, status="skipped", message="env not set"),
    ]
    path = write_junit_xml("warcard.demo", sample, "TestResults/demo-junit.xml")
    print(f"wrote {path}")
