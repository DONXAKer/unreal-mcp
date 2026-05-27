"""E2E смок: Login → MainMenu → FindGameButton → WBP_Matchmaking → Cancel → MainMenu.

Single-player smoke матчмейкинга — проверяет что клиент успешно встаёт в очередь
и может её покинуть. Не ждёт MATCH_FOUND (для этого нужен второй игрок —
см. будущий smoke_pie_2players).

Requires:
    1. Unreal Editor с UnrealMCP плагином v2.12.0+ (invoke_button_click).
    2. WarCard сервер (default :8081).
    3. Тестовый юзер — создаётся через ensure_test_user.

Шаги:
    1. ensure_test_user
    2. ensure_pie (start если не запущен)
    3. wait WBP_Login_C_0
    4. login flow (set_text x2 + invoke_button_click)
    5. wait WBP_MainMenu_C_0
    6. invoke_button_click FindGameButton
    7. wait WBP_Matchmaking_C_0 (10s — серверу нужно подтвердить join queue)
    8. screenshot matchmaking.png
    9. invoke_button_click CancelButton
    10. wait WBP_MainMenu_C_0 (возврат)
"""

from __future__ import annotations

import sys

from tests._fixtures import FixtureError, ensure_test_user, test_credentials
from tests._pie_common import (
    pie_send,
    stop_pie_safe,
    unwrap_result,
    wait_for_pie_ready,
    wait_for_widget_or_fail,
)
from tests._smoke_common import SmokeFailure, run_steps


def main(argv: list[str]) -> int:
    login, password, _ = test_credentials()

    def s_ensure_user():
        try:
            ensure_test_user(login, password)
        except FixtureError as exc:
            raise SmokeFailure(1, "ensure_test_user", f"Сервер недоступен: {exc}", {}) from exc

    def s_ensure_pie():
        status = unwrap_result(pie_send("pie_status", {}), 2, "pie_status (pre)")
        if not status.get("is_running"):
            unwrap_result(pie_send("pie_start", {}), 2, "pie_start")
            wait_for_pie_ready(timeout_s=15.0)
        else:
            print("    INFO: PIE уже запущен — переиспользую")

    def s_login_if_needed():
        # Если уже на MainMenu — пропускаем login.
        if pie_send("find_widget", {"widget_name": "WBP_MainMenu_C_0"}).get("result", {}).get("found"):
            print("    INFO: уже на MainMenu — пропускаю login")
            return
        wait_for_widget_or_fail("WBP_Login_C_0", timeout_s=5.0)
        unwrap_result(pie_send("set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login}),
                      3, "set username")
        unwrap_result(pie_send("set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": password}),
                      3, "set password")
        unwrap_result(pie_send("invoke_button_click", {"widget_name": "LoginButton"}),
                      3, "invoke LoginButton")
        wait_for_widget_or_fail("WBP_MainMenu_C_0", timeout_s=10.0)

    def s_find_game():
        unwrap_result(pie_send("invoke_button_click", {"widget_name": "FindGameButton"}),
                      4, "invoke FindGameButton")

    def s_wait_queue():
        wait_for_widget_or_fail("WBP_Matchmaking_C_0", timeout_s=10.0)

    def s_screenshot_queue():
        pie_send("pie_screenshot", {"filename": "matchmaking_queue.png"})

    def s_cancel():
        unwrap_result(pie_send("invoke_button_click", {"widget_name": "CancelButton"}),
                      7, "invoke CancelButton")

    def s_wait_back_to_menu():
        # Ожидание возврата к MainMenu (либо WBP_Matchmaking исчез, либо MainMenu снова visible).
        wait_for_widget_or_fail("WBP_MainMenu_C_0", timeout_s=10.0)

    steps = [
        ("ensure_test_user",  s_ensure_user),
        ("ensure_pie",        s_ensure_pie),
        ("login if needed",   s_login_if_needed),
        ("invoke FindGame",   s_find_game),
        ("wait WBP_Matchmaking", s_wait_queue),
        ("screenshot queue",  s_screenshot_queue),
        ("invoke Cancel",     s_cancel),
        ("wait back to menu", s_wait_back_to_menu),
    ]

    try:
        return run_steps("smoke_pie_matchmaking", steps)
    finally:
        stop_pie_safe()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
