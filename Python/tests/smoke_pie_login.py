"""E2E смок: вход в игру через WBP_Login -> ожидание MainMenu.

Requires:
    1. Unreal Editor открыт с client/Client.uproject (порт 55557 живой).
    2. Запущен WarCard сервер (default :8081, см. tests/_fixtures.py).
    3. Тестовый пользователь — создаётся автоматически через ensure_test_user
       либо берётся из env (WC_TEST_LOGIN/WC_TEST_PASSWORD).

Шаги:
    1. ensure_test_user — гарантировать что юзер в БД (через REST /api/auth/register)
    2. pie_status / pie_start если не запущен / wait_for_pie_ready
    3. wait WBP_Login_C_0
    4. set_text_on_widget(LoginUsernameInput, login) — Unicode-safe, без simulate_key
    5. set_text_on_widget(LoginPasswordInput, password)
    6. click LoginButton
    7. wait WBP_MainMenu_C_0 (timeout 10s — серверу время ответить)
    8. pie_screenshot login_success.png

Если шаг 7 фейлится — pie_screenshot login_failure.png + get_widget_tree
для диагностики (ErrorText.visible=true и его текст).
"""

from __future__ import annotations

import sys
from typing import Any

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

    state: dict[str, Any] = {}

    def s_ensure_user():
        try:
            result = ensure_test_user(login, password)
            print(f"    user '{login}' created={result.get('created')}")
        except FixtureError as exc:
            raise SmokeFailure(1, "ensure_test_user", f"Сервер недоступен: {exc}", {})

    def s_ensure_pie():
        status = unwrap_result(pie_send("pie_status", {}), 2, "pie_status (pre)")
        if not status.get("is_running"):
            unwrap_result(pie_send("pie_start", {}), 2, "pie_start")
            wait_for_pie_ready(timeout_s=15.0)
        else:
            print("    INFO: PIE уже запущен — переиспользую")

    def s_wait_login():
        wait_for_widget_or_fail("WBP_Login_C_0", timeout_s=10.0)

    def s_username():
        unwrap_result(
            pie_send("set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login}),
            4, "set_text_on_widget(username)",
        )

    def s_password():
        unwrap_result(
            pie_send("set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": password}),
            5, "set_text_on_widget(password)",
        )

    def s_submit():
        # invoke_button_click (v2.12.0): прямой broadcast делегата OnClicked,
        # без Slate event injection — надёжнее когда PIE viewport не focused.
        unwrap_result(pie_send("invoke_button_click", {"widget_name": "LoginButton"}), 6, "invoke LoginButton")

    def s_wait_menu():
        try:
            wait_for_widget_or_fail("WBP_MainMenu_C_0", timeout_s=10.0)
        except SmokeFailure:
            pie_send("pie_screenshot", {"filename": "login_failure.png"})
            tree = unwrap_result(pie_send("get_widget_tree", {}), 7, "get_widget_tree on failure")
            state["failure_tree"] = tree
            print("    DIAG: widget tree saved, screenshot login_failure.png")
            raise

    def s_screenshot():
        pie_send("pie_screenshot", {"filename": "login_success.png"})

    steps = [
        ("ensure_test_user", s_ensure_user),
        ("ensure_pie",       s_ensure_pie),
        ("wait WBP_Login",   s_wait_login),
        ("set username",     s_username),
        ("set password",     s_password),
        ("click LoginButton",s_submit),
        ("wait MainMenu",    s_wait_menu),
        ("screenshot",       s_screenshot),
    ]

    try:
        return run_steps("smoke_pie_login", steps)
    finally:
        stop_pie_safe()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
