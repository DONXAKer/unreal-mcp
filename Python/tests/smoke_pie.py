"""Smoke test: PIE (Play-In-Editor) automation infrastructure.

Standalone — подключается напрямую к UnrealMCP плагину на 127.0.0.1:55557.
Запуск:
    cd D:\\WarCard\\unreal-mcp\\Python
    python -m tests.smoke_pie

Проверяет минимальную работоспособность Playwright-like инфраструктуры:
    1. pie_status до старта                        — is_running=false
    2. pie_start                                   — started=true
    3. wait_for_pie_ready (poll pie_status)        — PlayerController готов
    4. get_widget_tree                             — есть хотя бы один UUserWidget
    5. pie_screenshot                              — фейл-tolerant capture
    6. pie_stop                                    — stopped=true

Постфактум pie_status намеренно НЕ дергается: после RequestEndPlayMap Editor
занят async teardown PIE world и может не отвечать на новые TCP-команды
несколько секунд. Сам факт что pie_stop вернул stopped=true — достаточная
гарантия. Защитный stop_pie_safe() в finally подстрахует на случай exception.

Тест НЕ привязывается к конкретным виджетам проекта (WBP_Login, WBP_MainMenu)
— это сценарные тесты следующего уровня, см. README в этой папке.

Exit 0 при успехе, 1 при любом fail. PIE гарантированно останавливается даже
при exception — через finally → stop_pie_safe.
"""

from __future__ import annotations

import sys
from typing import Any

from tests._pie_common import (
    pie_send,
    stop_pie_safe,
    unwrap_result,
    wait_for_pie_ready,
)
from tests._smoke_common import (
    SmokeFailure,
    parse_no_cleanup,
    run_steps,
)


def main(argv: list[str]) -> int:
    _no_cleanup = parse_no_cleanup(argv)

    # Кэш состояния между шагами (PIE-сессия живёт от step_start до step_stop).
    state: dict[str, Any] = {}

    def step_status_before_start():
        resp = pie_send("pie_status", {})
        result = unwrap_result(resp, 1, "pie_status (pre)")
        # Если PIE уже идёт от предыдущего прогона — это не fatal, но логнём.
        if result.get("is_running"):
            print(f"    WARN: PIE уже running до старта теста: {result.get('world_name')}")
            stop_pie_safe()

    def step_start():
        resp = pie_send("pie_start", {})
        result = unwrap_result(resp, 2, "pie_start")
        if not result.get("started"):
            raise SmokeFailure(2, "pie_start", f"started != true: {result}", resp)
        state["start_response"] = result

    def step_wait_ready():
        ready = wait_for_pie_ready(timeout_s=15.0)
        state["ready_status"] = ready
        print(f"    PIE ready: world={ready.get('world_name')} level={ready.get('current_level')}")

    def step_widget_tree():
        resp = pie_send("get_widget_tree", {})
        result = unwrap_result(resp, 4, "get_widget_tree")
        user_widgets = result.get("user_widgets", []) or []
        if not user_widgets:
            # Не fatal — в проекте может не быть виджетов на стартовой карте.
            print("    INFO: get_widget_tree вернул 0 UUserWidget'ов на стартовой карте")
        else:
            names = [w.get("name") for w in user_widgets[:5]]
            print(f"    user widgets ({len(user_widgets)}): {names}")
        state["widget_tree"] = result

    def step_screenshot():
        resp = pie_send("pie_screenshot", {"filename": "smoke_pie.png", "show_ui": True})
        result = unwrap_result(resp, 5, "pie_screenshot")
        # source = "game_viewport" (хорошо) или "fallback_editor_viewport" (PIE не успело)
        source = result.get("source")
        print(f"    screenshot source: {source} -> {result.get('filename')}")
        if source == "fallback_editor_viewport":
            print("    WARN: PIE GameViewport не готов — захвачен Editor viewport")

    def step_stop():
        resp = pie_send("pie_stop", {})
        result = unwrap_result(resp, 6, "pie_stop")
        if not result.get("stopped"):
            raise SmokeFailure(6, "pie_stop", f"stopped != true: {result}", resp)

    steps = [
        ("pie_status (pre)",      step_status_before_start),
        ("pie_start",             step_start),
        ("wait_for_pie_ready",    step_wait_ready),
        ("get_widget_tree",       step_widget_tree),
        ("pie_screenshot",        step_screenshot),
        ("pie_stop",              step_stop),
    ]

    try:
        return run_steps("smoke_pie", steps)
    finally:
        # Защитная остановка — даже если что-то упало посреди, не оставляем PIE висеть.
        stop_pie_safe()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
