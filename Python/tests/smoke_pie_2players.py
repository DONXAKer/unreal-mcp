"""E2E смок: ДВА PIE-клиента → matchmaking → MATCH_FOUND → post-queue widget.

Stage 3 полного e2e: два клиента встают в очередь, сервер matches их, на обоих
появляется виджет следующей фазы (WBP_Draft / WBP_Mulligan / WBP_DeploymentScreen
— зависит от phase pipeline).

Requires:
    1. UnrealMCP плагин v2.12.0+ (multi-client PIE + invoke_button_click).
    2. WarCard сервер (default :8081) — должен поддерживать matchmaking
       двух разных юзеров.
    3. Два test-юзера в БД — создаются автоматически:
       warcard_test / Test1234   (default из _fixtures)
       warcard_test_2 / Test1234

Шаги:
    1. ensure оба юзера в БД через REST /api/auth/register
    2. pie_start(num_clients=2) — два PIE-клиента в одной сессии
    3. На каждом клиенте (controller_index 0 и 1):
         a. wait WBP_Login
         b. login с разными credentials
         c. wait WBP_MainMenu
         d. invoke FindGameButton
    4. Ждать пока **хотя бы один** post-queue widget появится на любом клиенте:
       WBP_Draft_C_0, WBP_Mulligan_C_0, WBP_DeploymentScreen_C_0.
    5. Screenshot обоих клиентов для diff.

Этот тест НЕ проходит дальше matchmaking — это smoke на сам факт MATCH_FOUND.
Stage 4 (draft / mulligan / deployment / battle) — отдельные тесты.
"""

from __future__ import annotations

import sys
import time

from tests._fixtures import FixtureError, ensure_test_user
from tests._pie_common import pie_send, stop_pie_safe, unwrap_result
from tests._smoke_common import SmokeFailure, run_steps

USER1 = ("warcard_test", "Test1234", "warcard_test@example.com")
USER2 = ("warcard_test_2", "Test1234", "warcard_test_2@example.com")
POST_QUEUE_WIDGETS = ["WBP_Draft_C_0", "WBP_Mulligan_C_0", "WBP_DeploymentScreen_C_0"]


def _send_raw(cmd, params, retries: int = 3, retry_delay: float = 0.5):
    """Send с retry — multi-PIE bridge может задумываться под concurrent load
    на 30s+, и одного timeout'а недостаточно (см. _inline_2p_stepwise.py: client 1
    login занимает до 13s end-to-end; find_widget при том может тайматься на
    промежуточных тиках). Retry создаёт новый socket — это снимает зависание
    на конкретной TCP-сессии без affecting bridge worker thread."""
    last_resp = None
    for attempt in range(retries):
        try:
            resp = pie_send(cmd, params)
            if isinstance(resp, dict):
                return resp.get("result", resp)
            return {}
        except SmokeFailure as exc:
            last_resp = exc
            if attempt < retries - 1:
                time.sleep(retry_delay)
                continue
            raise
    return {}


def _wait_pie_ready_multi(num_clients: int, timeout_s: float = 25.0) -> dict:
    """Опросить pie_status до появления num_clients ready PlayerController'ов."""
    deadline = time.monotonic() + timeout_s
    last = {}
    while time.monotonic() < deadline:
        st = _send_raw("pie_status", {})
        last = st
        clients = st.get("clients") or []
        ready = sum(1 for c in clients if c.get("controller_name"))
        if st.get("is_running") and ready >= num_clients:
            return st
        time.sleep(0.5)
    raise SmokeFailure(-1, "wait_pie_ready_multi",
                       f"Готовых клиентов меньше {num_clients} за {timeout_s}s. Last: {last}", last)


def _login_client(controller_index: int, login: str, password: str):
    """Login flow для одного клиента (controller_index)."""
    # Wait login widget
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        r = _send_raw("find_widget", {"widget_name": "WBP_Login_C_0", "controller_index": controller_index})
        if r.get("found"):
            break
        time.sleep(0.3)
    else:
        # Возможно уже на MainMenu (быстрый автологин по cached token)
        if not _send_raw("find_widget", {"widget_name": "WBP_MainMenu_C_0",
                                          "controller_index": controller_index}).get("found"):
            raise SmokeFailure(-1, f"login[c{controller_index}]",
                               "Ни WBP_Login_C_0, ни WBP_MainMenu_C_0 не найдены за 10s", {})

    # Set text + click
    _send_raw("set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login,
                                     "controller_index": controller_index})
    _send_raw("set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": password,
                                     "controller_index": controller_index})
    _send_raw("invoke_button_click", {"widget_name": "LoginButton",
                                       "controller_index": controller_index})

    # Wait MainMenu — client 1 наблюдается до 13s под нагрузкой multi-PIE,
    # поэтому даём 20s с запасом.
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        r = _send_raw("find_widget", {"widget_name": "WBP_MainMenu_C_0",
                                       "controller_index": controller_index})
        if r.get("found"):
            return
        time.sleep(0.3)
    raise SmokeFailure(-1, f"login[c{controller_index}]",
                       "WBP_MainMenu_C_0 не появился за 20s", {})


def _find_any_post_queue_widget(controller_index: int) -> str | None:
    for name in POST_QUEUE_WIDGETS:
        if _send_raw("find_widget", {"widget_name": name,
                                     "controller_index": controller_index}).get("found"):
            return name
    return None


def main(argv: list[str]) -> int:
    state = {}

    def s_ensure_users():
        try:
            ensure_test_user(USER1[0], USER1[1], USER1[2])
            ensure_test_user(USER2[0], USER2[1], USER2[2])
        except FixtureError as exc:
            raise SmokeFailure(1, "ensure_users", f"Сервер недоступен: {exc}", {})

    def s_pie_start_2():
        # Переиспользуем PIE если уже идёт 2 клиента; иначе stop + start заново.
        st = _send_raw("pie_status", {})
        if st.get("is_running") and st.get("num_clients") == 2:
            print(f"    INFO: PIE уже с 2 клиентами — переиспользую")
            return
        if st.get("is_running"):
            _send_raw("pie_stop", {})
            time.sleep(1.5)
        r = _send_raw("pie_start", {"num_clients": 2})
        if not r.get("started"):
            raise SmokeFailure(2, "pie_start_2", f"PIE start failed: {r}", r)
        _wait_pie_ready_multi(2, timeout_s=25.0)
        st = _send_raw("pie_status", {})
        print(f"    PIE: num_clients={st.get('num_clients')} clients={len(st.get('clients') or [])}")

    def s_login_both():
        _login_client(0, USER1[0], USER1[1])
        # Пауза даёт серверу обработать первый login + STOMP handshake до того,
        # как второй клиент начнёт свой flow. Без неё multi-PIE bridge может
        # затупить под concurrent нагрузкой.
        time.sleep(2.0)
        _login_client(1, USER2[0], USER2[1])

    def s_find_game_both():
        _send_raw("invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
        _send_raw("invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 1})

    def s_wait_match_found():
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            for ci in (0, 1):
                widget = _find_any_post_queue_widget(ci)
                if widget:
                    state["match_widget"] = widget
                    state["match_client"] = ci
                    print(f"    MATCH_FOUND on client {ci}: {widget}")
                    return
            time.sleep(0.5)
        # Diag: что сейчас на экранах
        for ci in (0, 1):
            st = _send_raw("pie_status", {})
            cw = (st.get("clients") or [{}])[ci].get("current_widget") if len(st.get("clients") or []) > ci else "?"
            print(f"    DIAG client{ci}: current_widget={cw}")
        raise SmokeFailure(-1, "wait_match_found",
                           f"Ни один из {POST_QUEUE_WIDGETS} не появился за 60s", {})

    def s_screenshot_both():
        _send_raw("pie_screenshot", {"filename": "match_2p.png", "controller_index": 0})
        _send_raw("pie_screenshot", {"filename": "match_2p.png", "controller_index": 1})

    steps = [
        ("ensure both users",   s_ensure_users),
        ("pie_start num=2",     s_pie_start_2),
        ("login both clients",  s_login_both),
        ("invoke FindGame x2",  s_find_game_both),
        ("wait MATCH_FOUND",    s_wait_match_found),
        ("screenshots",         s_screenshot_both),
    ]

    try:
        return run_steps("smoke_pie_2players", steps)
    finally:
        stop_pie_safe()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
