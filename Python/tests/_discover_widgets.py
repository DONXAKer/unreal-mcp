"""Quick discovery: запустить PIE → распечатать widget tree → остановить.

Используется один раз при написании нового сценарного теста — увидеть
реальные runtime-имена виджетов в текущей сцене.

Запуск:
    python -m tests._discover_widgets
"""

from __future__ import annotations

import json
import sys

from tests._pie_common import pie_send, unwrap_result, wait_for_pie_ready
from tests._smoke_common import SmokeFailure


def main() -> int:
    try:
        pre = unwrap_result(pie_send("pie_status", {}), -1, "pie_status")
        if not pre.get("is_running"):
            unwrap_result(pie_send("pie_start", {}), -1, "pie_start")
            wait_for_pie_ready(timeout_s=15.0)

        tree = unwrap_result(pie_send("get_widget_tree", {}), -1, "get_widget_tree")
        print(json.dumps(tree, indent=2, ensure_ascii=False))
        return 0
    except SmokeFailure as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        # Не останавливаем PIE если он был запущен до нас — оставляем юзеру.
        pass


if __name__ == "__main__":
    sys.exit(main())
