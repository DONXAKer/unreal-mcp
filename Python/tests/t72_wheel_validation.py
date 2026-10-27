"""Быстрая валидация вращения катков Т-72.

Предусловия:
  - PIE n=1 запущен, игрок залогинен (current_widget = WBP_MainMenu_C).
  - Сервер :8081 жив, BotController работает (/api/match/create-vs-bot).
  - После запуска скрипт создаёт бот-матч, проходит draft/deployment,
    дожидается боя, делает ход танком Т-72 и делает pie_screenshot.
"""

from __future__ import annotations

import json
import sys
import time
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from tests._fixtures import get_test_jwt, server_url
from unreal_mcp_server import UnrealConnection
import urllib.request


def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any]) -> dict[str, Any]:
    resp = conn.send_command(cmd, params)
    if isinstance(resp, dict):
        if resp.get("status") == "success":
            return resp.get("result", resp)
        err = resp.get("error", "no error msg")
        err_message = err.get("message", str(err)) if isinstance(err, dict) else err
        return {"error": err_message, "_raw": resp}
    return {}


def _wait_widget(conn: UnrealConnection, candidates: tuple[str, ...], timeout: float) -> str:
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {}) or {}
        cs = st.get("clients") or []
        cur = (cs[0].get("current_widget") or "") if cs else ""
        for c in candidates:
            if c in cur:
                return cur
        time.sleep(1.0)
    return ""


def _create_bot_match(username: str, password: str) -> str:
    token = get_test_jwt(username, password)
    url = f"{server_url()}/api/match/create-vs-bot?difficulty=EASY"
    req = urllib.request.Request(url, method="POST", headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
    })
    with urllib.request.urlopen(req, timeout=10) as resp:
        body = json.loads(resp.read().decode("utf-8"))
    return body["gameId"]


def main() -> int:
    username, password = "wf_ue_644892", "Test1234"
    conn = UnrealConnection()
    conn.connect()

    # 1) Создать бот-матч (клиент уже в главном меню и подключен по WS).
    print("Creating bot match...")
    game_id = _create_bot_match(username, password)
    print(f"  gameId={game_id}")

    # 2) Дождаться draft.
    cur = _wait_widget(conn, ("WBP_Draft_C",), 30)
    if not cur:
        print("FAIL: draft not reached")
        return 1
    print(f"Draft reached: {cur}")

    # 3) Выбрать 5 red-юнитов, включая Т-72.
    picks = ["t72-red", "mi24-red", "assault-red", "sniper-red", "medic-red"]
    for uid in picks:
        r = _send(conn, "wc_select_unit", {"unit_id": uid, "controller_index": 0})
        print(f"  select {uid}: {r.get('selected', False)}")
        time.sleep(0.3)
    r = _send(conn, "wc_confirm_selection", {"controller_index": 0})
    print(f"  confirm selection: {r}")

    # 4) Дождаться deployment.
    cur = _wait_widget(conn, ("WBP_DeploymentScreen",), 45)
    if not cur:
        print("FAIL: deployment not reached")
        return 1
    print(f"Deployment reached: {cur}")

    # 5) Разместить юнитов.
    for i, uid in enumerate(picks):
        r = _send(conn, "wc_deploy_unit", {"unit_id": uid, "grid_x": 0, "grid_y": i, "controller_index": 0})
        print(f"  deploy {uid} -> (0,{i}): {r.get('deployed', False)}")
    r = _send(conn, "wc_confirm_deployment", {"controller_index": 0})
    print(f"  confirm deployment: {r}")

    # 6) Дождаться боя.
    cur = _wait_widget(conn, ("WBP_BattleHUD_C", "WBP_ActionCardHand_C", "CombatLog"), 60)
    if not cur:
        # fallback: проверить battle state
        bs = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        if not bs.get("ok"):
            print("FAIL: battle not reached")
            return 1
    print(f"Battle reached: {cur}")

    # 7) Найти Т-72 и сделать ход вперёд.
    time.sleep(2.0)
    units_json = _send(conn, "wc_get_battle_units", {"controller_index": 0})
    units = json.loads(units_json.get("units_json") or "{}").get("units", [])
    t72 = next((u for u in units if u.get("unitId", "").startswith("t72")), None)
    if not t72:
        print("WARNING: t72 not found in battle units, taking screenshot anyway")
    else:
        print(f"T-72 at ({t72['gridX']},{t72['gridY']}), HP={t72.get('currentHp')}")
        # Дождаться своего хода.
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            bs = _send(conn, "wc_get_battle_state", {"controller_index": 0})
            if bs.get("my_turn"):
                break
            time.sleep(1.0)
        # Движение вперёд (+x) на 1 клетку.
        nx, ny = t72["gridX"] + 1, t72["gridY"]
        r = _send(conn, "wc_free_move", {"unit_id": t72["unitId"], "x": nx, "y": ny, "controller_index": 0})
        print(f"  free_move -> ({nx},{ny}): {r}")
        time.sleep(2.0)

    # 8) Скриншот.
    shot = _send(conn, "pie_screenshot", {"filename": "t72_wheels_in_motion.png", "show_ui": False})
    print(f"Screenshot: {shot}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
