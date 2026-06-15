"""Runner-compatible wrapper над smoke_pie.

Обнаруживается `__runner.discover_test_modules()` (имя начинается с test_).
Запуск:
    через MCP: mcp__unreal-mcp__wc_run_tests (если recipe сконфигурирован)
    напрямую: python -m tests.test_pie_smoke

Делегирует всю логику в smoke_pie.main и конвертирует exit code → dict.
"""

from __future__ import annotations

from typing import Any


def run() -> dict[str, Any]:
    from tests.smoke_pie import main as smoke_main

    exit_code = smoke_main([])
    return {
        "ok": exit_code == 0,
        "exit_code": exit_code,
    }


if __name__ == "__main__":
    import json
    import sys

    result = run()
    print(json.dumps(result, indent=2))
    sys.exit(0 if result["ok"] else 1)
