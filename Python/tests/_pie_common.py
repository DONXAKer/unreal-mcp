"""Shared helpers for PIE (Play-In-Editor) smoke tests.

Эти хелперы лежат поверх стандартного `_smoke_common.send_command` и работают
с командами, добавленными в UnrealMCP плагин v2.4.0+:
    pie_start, pie_stop, pie_status,
    find_widget, click_widget_by_name, get_widget_tree, pie_screenshot,
    simulate_key.

Жизненный цикл PIE асинхронный — после `pie_start` PlayerController появляется
через несколько тиков. Хелпер `wait_for_pie_ready` ждёт пока plugin отчитается
`is_running=true && has_player_controller=true`, иначе тест мгновенно бросит
SmokeFailure.
"""

from __future__ import annotations

import time
from typing import Any

from tests._smoke_common import SmokeFailure, assert_success, send_command


def pie_send(command: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    """Тонкая обёртка над send_command для семантической читаемости PIE-тестов."""
    return send_command(command, params)


def unwrap_result(response: dict[str, Any], step_index: int, step_name: str) -> dict[str, Any]:
    """Развернуть {status:success, result:{...}} → result. Совместимо с raw shape."""
    return assert_success(response, step_index, step_name)


def wait_for_pie_ready(timeout_s: float = 10.0, poll_interval_s: float = 0.25) -> dict[str, Any]:
    """Опросить pie_status до готовности PlayerController.

    После `pie_start` UE нужно несколько тиков чтобы стартовать GameMode +
    PlayerController + GameViewport. Без этого input-команды бессмысленны.

    Returns:
        dict со снимком pie_status в момент готовности.

    Raises:
        SmokeFailure если за timeout_s PlayerController так и не появился.
    """
    deadline = time.monotonic() + timeout_s
    last: dict[str, Any] = {}
    while time.monotonic() < deadline:
        resp = pie_send("pie_status", {})
        status = unwrap_result(resp, -1, "pie_status")
        last = status
        if status.get("is_running") and status.get("has_player_controller"):
            return status
        time.sleep(poll_interval_s)
    raise SmokeFailure(
        -1,
        "wait_for_pie_ready",
        f"PIE не стартовал за {timeout_s}s. Last status: {last}",
        last,
    )


def wait_for_widget_or_fail(
    widget_name: str,
    timeout_s: float = 5.0,
    poll_interval_s: float = 0.2,
) -> dict[str, Any]:
    """Опросить find_widget до появления виджета.

    Используется в smoke-тестах вместо MCP-команды wait_for_widget (та живёт
    только в FastMCP-сервере, а smoke-тесты работают напрямую с плагином).

    Returns:
        result-секцию find_widget на момент когда found=true.

    Raises:
        SmokeFailure если виджет не появился за timeout_s.
    """
    deadline = time.monotonic() + timeout_s
    attempts = 0
    last: dict[str, Any] = {}
    while time.monotonic() < deadline:
        attempts += 1
        resp = pie_send("find_widget", {"widget_name": widget_name})
        result = unwrap_result(resp, -1, f"find_widget({widget_name})")
        last = result
        if result.get("found"):
            return result
        time.sleep(poll_interval_s)
    raise SmokeFailure(
        -1,
        "wait_for_widget_or_fail",
        f"Виджет '{widget_name}' не появился за {timeout_s}s ({attempts} попыток). Last: {last}",
        last,
    )


def stop_pie_safe() -> None:
    """Гарантированно остановить PIE — игнорировать ошибки (idempotent)."""
    try:
        pie_send("pie_stop", {})
    except SmokeFailure:
        pass
