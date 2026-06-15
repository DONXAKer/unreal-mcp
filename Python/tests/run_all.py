"""Прогнать все smoke_*.py / test_*.py в текущей папке и сгенерировать junit.xml.

Запуск:
    cd D:\\WarCard\\unreal-mcp\\Python
    python -m tests.run_all
    python -m tests.run_all --filter pie         # только тесты с 'pie' в имени
    python -m tests.run_all --output ../TestResults/junit.xml

Каждый тест-модуль должен иметь либо:
    - функцию `run() -> dict`  с ключом 'ok' (test_*.py — runner-style)
    - либо функцию `main(argv) -> int`  где 0=pass, 1=fail (smoke_*.py — exit-code style)

SKIP детектится по тексту stdout начинающемуся со "SKIP:" — это конвенция
для тестов, которые не падают, а просто не могут выполниться (нет env, нет
сервера).
"""

from __future__ import annotations

import argparse
import importlib.util
import io
import sys
import time
import traceback
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from tests._junit_writer import TestResult, write_junit_xml


def _import_module(path: Path):
    spec = importlib.util.spec_from_file_location(f"_run_all.{path.stem}", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _run_one(path: Path) -> TestResult:
    start = time.monotonic()
    buf_out, buf_err = io.StringIO(), io.StringIO()
    try:
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            mod = _import_module(path)
            if hasattr(mod, "run"):
                result = mod.run()
                ok = bool(result.get("ok")) if isinstance(result, dict) else bool(result)
                exit_code = 0 if ok else 1
            elif hasattr(mod, "main"):
                exit_code = mod.main([])
            else:
                return TestResult(
                    name=path.stem,
                    duration_s=time.monotonic() - start,
                    status="error",
                    message="No run() or main() in module",
                )
        out = buf_out.getvalue()
        err = buf_err.getvalue()
        combined = (out + ("\n--- stderr ---\n" + err if err else "")).strip()

        if exit_code == 0:
            # Распознать SKIP по конвенции (тест печатает "SKIP: ..." в начало).
            first = combined.split("\n", 1)[0] if combined else ""
            if first.startswith("SKIP:") or "SKIP:" in (err or "").split("\n", 1)[0]:
                return TestResult(
                    name=path.stem, duration_s=time.monotonic() - start,
                    status="skipped", message=first[:200], details=combined,
                )
            return TestResult(
                name=path.stem, duration_s=time.monotonic() - start,
                status="passed", details=combined,
            )
        return TestResult(
            name=path.stem, duration_s=time.monotonic() - start,
            status="failed", message=f"exit code {exit_code}", details=combined,
        )
    except Exception as exc:
        return TestResult(
            name=path.stem, duration_s=time.monotonic() - start,
            status="error", message=f"{type(exc).__name__}: {exc}",
            details=buf_out.getvalue() + buf_err.getvalue() + "\n" + traceback.format_exc(),
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", default="", help="substring filter on test name")
    parser.add_argument("--output", default="TestResults/python-junit.xml")
    parser.add_argument("--suite", default="warcard.python.smoke")
    args = parser.parse_args(argv)

    tests_dir = Path(__file__).resolve().parent
    candidates = sorted(
        list(tests_dir.glob("smoke_*.py"))
        + list(tests_dir.glob("test_*.py"))
    )
    # Исключаем helpers и runners.
    candidates = [
        p for p in candidates
        if not p.stem.startswith("_") and p.stem not in {"run_all", "__runner"}
    ]
    if args.filter:
        candidates = [p for p in candidates if args.filter in p.stem]

    print(f"Running {len(candidates)} tests ({args.filter or 'no filter'})...")
    results: list[TestResult] = []
    for path in candidates:
        print(f"  ... {path.stem}", end=" ", flush=True)
        r = _run_one(path)
        results.append(r)
        print(f"[{r.status.upper()}] {r.duration_s:.2f}s")

    junit_path = write_junit_xml(args.suite, results, args.output)
    print(f"\nJUnit report: {junit_path}")

    passed = sum(1 for r in results if r.status == "passed")
    failed = sum(1 for r in results if r.status == "failed")
    errored = sum(1 for r in results if r.status == "error")
    skipped = sum(1 for r in results if r.status == "skipped")
    print(f"\nSummary: {passed} passed, {failed} failed, {errored} errored, {skipped} skipped")

    return 0 if (failed + errored) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
