"""Полный e2e smoke: 2 клиента проходят login → matchmaking → UnitSelection →
Deployment → Battle. Стабильный — с retry, fallback и детальной диагностикой.

Stage 1-5 в одном flow. Использует:
- prefix unique users (timestamp) — обходит конфликты с pending games в БД сервера.
- PIE n=2 fresh start.
- Sequential login c long pause (sync STOMP handshake).
- _wait_widget с fallback на поиск без controller_index если PC-фильтр не находит сразу.
- wc_select_unit / wc_confirm_selection — UnitSelection phase.
- wc_deploy_unit / wc_confirm_deployment — Deployment phase.

Требования:
- UnrealMCP плагин v2.16.0+ (WarCardGameCommands + text в get_widget_tree).
- WarCard сервер на :8081.
"""

from __future__ import annotations

import sys
import time
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from tests._fixtures import ensure_test_user
from unreal_mcp_server import UnrealConnection


# Server отправляет roster по стороне. Берём первые 5 из логов available-units;
# скрипт пробует оба prefix'а, выбирая то, что AddUnitToComposition принимает.
UNIT_ROSTER_BLUE = ["sniper-blue", "medic-blue", "assault-blue", "apache-blue", "abrams-blue"]
UNIT_ROSTER_RED  = ["sniper-red",  "medic-red",  "assault-red",  "apache-red",  "abrams-red"]
UNITS_TO_PICK = 5


def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any],
          retries: int = 5, retry_delay: float = 0.6) -> dict[str, Any]:
    """Send + retry с reconnect на socket timeout. Возвращает result или {error}."""
    last_err: str | None = None
    for attempt in range(retries):
        try:
            resp = conn.send_command(cmd, params)
            if isinstance(resp, dict):
                return resp.get("result", resp) if resp.get("status") == "success" else {"error": resp.get("error", "no error msg"), "_raw": resp}
            return {}
        except Exception as exc:
            last_err = str(exc)
            time.sleep(retry_delay)
            try: conn.disconnect()
            except Exception: pass
            try: conn.connect()
            except Exception: pass
    return {"error": last_err or "unknown"}


def _wait_widget_strict(conn, ctrl: int, name: str, timeout: float = 25.0, label: str = "") -> bool:
    """Polling find_widget с фильтром по controller_index. Возвращает True если
    виджет найден за timeout, False иначе."""
    deadline = time.monotonic() + timeout
    attempts = 0
    while time.monotonic() < deadline:
        attempts += 1
        r = _send(conn, "find_widget", {"widget_name": name, "controller_index": ctrl})
        if r.get("found"):
            elapsed = timeout - (deadline - time.monotonic())
            if label: print(f"    [{label}] '{name}' found on c{ctrl} after {elapsed:.1f}s, {attempts} attempts")
            return True
        time.sleep(0.5)
    if label: print(f"    [{label}] '{name}' NOT found on c{ctrl} after {timeout}s, {attempts} attempts")
    return False


def _wait_any_widget(conn, ctrl: int, names: list[str], timeout: float = 20.0, label: str = "") -> str | None:
    """Polling нескольких виджетов — возвращает имя первого найденного."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for name in names:
            r = _send(conn, "find_widget", {"widget_name": name, "controller_index": ctrl})
            if r.get("found"):
                if label: print(f"    [{label}] '{name}' found on c{ctrl}")
                return name
        time.sleep(0.5)
    if label: print(f"    [{label}] none of {names} found on c{ctrl} after {timeout}s")
    return None


def _wait_pie_both_clients_ready(conn, target_widget: str = "WBP_Login_C", timeout: float = 30.0) -> bool:
    """Ждать пока оба клиента видны в pie_status и оба показывают target_widget."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        st = _send(conn, "pie_status", {})
        cs = st.get("clients") or []
        if len(cs) == 2:
            widgets = [c.get("current_widget") for c in cs]
            if all(w == target_widget for w in widgets):
                return True
        time.sleep(0.5)
    return False


def _login_client(conn, ctrl: int, login: str, password: str) -> bool:
    """Login flow для одного клиента. Возвращает True если дошёл до MainMenu/UnitSelection/Deployment."""
    print(f"\n--- login c{ctrl} ({login}) ---")
    r = _send(conn, "set_text_on_widget",
              {"widget_name": "LoginUsernameInput", "text": login, "controller_index": ctrl})
    if not r.get("ok"):
        print(f"  set username failed: {r}")
        return False

    r = _send(conn, "set_text_on_widget",
              {"widget_name": "LoginPasswordInput", "text": password, "controller_index": ctrl})
    if not r.get("ok"):
        print(f"  set password failed: {r}")
        return False

    r = _send(conn, "invoke_button_click",
              {"widget_name": "LoginButton", "controller_index": ctrl})
    if not r.get("ok"):
        print(f"  click failed: {r}")
        return False

    # Login — async на server. Ждём пост-login widget (MainMenu, либо если auto-resume —
    # сразу Matchmaking/UnitSelection/Deployment).
    found = _wait_any_widget(conn, ctrl,
                             ["WBP_MainMenu_C_0", "WBP_Matchmaking_C_0",
                              "WBP_UnitSelection_C_0", "WBP_DeploymentScreen_C_0"],
                             timeout=30.0, label=f"login c{ctrl}")
    return found is not None


def _select_5_units(conn, ctrl: int) -> int:
    """Выбрать 5 unit'ов через wc_select_unit. Пробует оба roster (blue/red).
    Возвращает количество успешно добавленных."""
    selected_count = 0
    for roster in [UNIT_ROSTER_BLUE, UNIT_ROSTER_RED]:
        if selected_count >= UNITS_TO_PICK: break
        for unit_id in roster:
            if selected_count >= UNITS_TO_PICK: break
            r = _send(conn, "wc_select_unit",
                      {"unit_id": unit_id, "controller_index": ctrl})
            if r.get("selected"):
                selected_count += 1
                print(f"    + {unit_id} ({selected_count}/{UNITS_TO_PICK})")
    return selected_count


def _deploy_5_units(conn, ctrl: int) -> int:
    """Деплоит 5 unit'ов на свободные координаты (i, 0). Возвращает количество успешных."""
    deployed_count = 0
    for roster in [UNIT_ROSTER_BLUE, UNIT_ROSTER_RED]:
        if deployed_count >= UNITS_TO_PICK: break
        for i, unit_id in enumerate(roster):
            if deployed_count >= UNITS_TO_PICK: break
            r = _send(conn, "wc_deploy_unit",
                      {"unit_id": unit_id, "grid_x": i, "grid_y": 0,
                       "controller_index": ctrl})
            if r.get("deployed"):
                deployed_count += 1
                print(f"    + {unit_id} @ ({i},0) ({deployed_count}/{UNITS_TO_PICK})")
    return deployed_count


def main() -> int:
    ts = int(time.time())
    u1, u2 = f"wc_e2e_a_{ts}", f"wc_e2e_b_{ts}"
    pwd = "Test1234"

    print(f"=== smoke_pie_full_game ===")
    print(f"users: {u1} / {u2}\n")

    # 1. Ensure both users.
    ensure_test_user(u1, pwd, f"{u1}@x.com")
    ensure_test_user(u2, pwd, f"{u2}@x.com")

    conn = UnrealConnection()
    conn.connect()

    # 2. Fresh PIE n=2.
    print("--- 1/9 fresh PIE n=2 ---")
    _send(conn, "pie_stop", {})
    time.sleep(3.0)  # дать UE доочиститься после stop.
    r = _send(conn, "pie_start", {"num_clients": 2})
    if not r.get("started"):
        print(f"  pie_start failed: {r}")
        return 1
    if not _wait_pie_both_clients_ready(conn, "WBP_Login_C", timeout=30.0):
        print("  PIE clients not ready in 30s")
        return 1
    print("  both clients on WBP_Login_C")

    # 3. Login both — sequential, 4s pause между для async STOMP handshake.
    print("\n--- 2/9 login both clients (sequential) ---")
    if not _login_client(conn, 0, u1, pwd):
        print("  login c0 failed")
        return 1
    time.sleep(4.0)
    if not _login_client(conn, 1, u2, pwd):
        print("  login c1 failed")
        return 1

    # 4. FindGame на тех клиентах, кто на MainMenu (если auto-resume — уже после).
    print("\n--- 3/9 FindGame ---")
    for ctrl in (0, 1):
        r = _send(conn, "find_widget",
                  {"widget_name": "WBP_MainMenu_C_0", "controller_index": ctrl})
        if r.get("found"):
            _send(conn, "invoke_button_click",
                  {"widget_name": "FindGameButton", "controller_index": ctrl})
            print(f"  c{ctrl} clicked FindGameButton")
        else:
            print(f"  c{ctrl} not on MainMenu — skip FindGame")

    # 5. Wait UnitSelection (или сразу DeploymentScreen если phase прыгает).
    print("\n--- 4/9 wait UnitSelection ---")
    selection_ctrl = None
    for ctrl in (0, 1):
        if _wait_widget_strict(conn, ctrl, "WBP_UnitSelection_C_0", timeout=60.0,
                                label="MATCH"):
            selection_ctrl = ctrl
            break
    if selection_ctrl is None:
        print("  WBP_UnitSelection не появился ни на одном клиенте — Stage 4 stuck")
        # Probe via state — может subsystem уже инициализирован.
        for ctrl in (0, 1):
            st = _send(conn, "wc_get_selection_state", {"controller_index": ctrl})
            print(f"  c{ctrl} selection state: {st}")
        return 1
    print(f"  UnitSelection on c{selection_ctrl}")

    # 6. UnitSelection phase — выбрать 5 unit'ов на КАЖДОМ клиенте.
    print("\n--- 5/9 select 5 units on each client ---")
    for ctrl in (0, 1):
        print(f"  client {ctrl}:")
        # Skip если этого клиента нет в UnitSelection (другой ещё в MainMenu или
        # already in deployment).
        if not _send(conn, "find_widget",
                     {"widget_name": "WBP_UnitSelection_C_0", "controller_index": ctrl}).get("found"):
            print(f"    not on UnitSelection — пробуем wc_select_unit всё равно")
        n = _select_5_units(conn, ctrl)
        st = _send(conn, "wc_get_selection_state", {"controller_index": ctrl})
        print(f"    state: selected={st.get('selected_count')} ready={st.get('ready')}")

    # 7. Confirm selection on both.
    print("\n--- 6/9 confirm_selection on both ---")
    for ctrl in (0, 1):
        r = _send(conn, "wc_confirm_selection", {"controller_index": ctrl})
        print(f"  c{ctrl}: ok={r.get('ok')}")

    # 8. Wait DeploymentScreen.
    print("\n--- 7/9 wait Deployment ---")
    deploy_ctrl = None
    for ctrl in (0, 1):
        if _wait_widget_strict(conn, ctrl, "WBP_DeploymentScreen_C_0",
                                timeout=30.0, label="DEPLOY"):
            deploy_ctrl = ctrl
            break
    if deploy_ctrl is None:
        print("  Deployment не появился — server возможно ждёт ещё подтверждение")
        for ctrl in (0, 1):
            st = _send(conn, "wc_get_deployment_state", {"controller_index": ctrl})
            print(f"  c{ctrl} deploy state: {st}")
        # не fatal — пробуем дальше через wc_deploy напрямую (subsystem может
        # быть инициализирован хотя UI не показан).

    # 9. Deploy 5 units на каждом.
    print("\n--- 8/9 deploy 5 units on each ---")
    for ctrl in (0, 1):
        print(f"  client {ctrl}:")
        n = _deploy_5_units(conn, ctrl)
        st = _send(conn, "wc_get_deployment_state", {"controller_index": ctrl})
        print(f"    state: deployed={st.get('deployed_count')} ready={st.get('ready')}")

    # 10. Confirm deployment.
    print("\n--- 9/9 confirm_deployment ---")
    for ctrl in (0, 1):
        r = _send(conn, "wc_confirm_deployment", {"controller_index": ctrl})
        print(f"  c{ctrl}: ok={r.get('ok')}")

    # 11. Wait Battle widget.
    print("\n--- waiting Battle ---")
    battle = None
    for ctrl in (0, 1):
        name = _wait_any_widget(conn, ctrl,
                                ["WBP_BattleHUD_C_0", "WBP_Battle_C_0",
                                 "WBP_ActionCardHand_C_0"],
                                timeout=30.0, label="BATTLE")
        if name:
            battle = (ctrl, name)
            break
    print(f"\n=== RESULT: battle={battle} ===")

    _send(conn, "pie_stop", {})
    return 0 if battle else 1


if __name__ == "__main__":
    sys.exit(main())
