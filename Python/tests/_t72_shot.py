"""Визуальный прогон: довести 1 UE-клиент (PIE n=1) + STOMP-бот до боя,
снять скриншоты с танком t72-red на деплое и в бою. БЕЗ сдачи (PIE остаётся).

Запуск из D:\\WarCard\\unreal-mcp\\Python:
    uv run python tests/_t72_shot.py
Скриншоты: client/Saved/Screenshots/t72_{deploy,battle}{,_ui}.png
"""
from __future__ import annotations
import sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

import logging
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_pie_full_game import UNITS_TO_PICK, _deploy_client, _send, _has_uw_any
from tests.smoke_fullgame_solo import (
    _pie_restart_n1, _ue_login_findgame, _ue_draft, _wait_pie_widget, _battle_units,
)
from unreal_mcp_server import UnrealConnection


def shot(conn, name, ui):
    r = _send(conn, "pie_screenshot", {"filename": name, "show_ui": ui}) or {}
    print(f"  SHOT {name} -> {r.get('assetPath') or r.get('status')} (source={r.get('source')})")


def _ensure_pie_nw(conn) -> bool:
    """PIE n=1 в режиме new_window (даёт реальный кадр для скриншота;
    selected_viewport в фоновом редакторе не презентит → чёрный/не стартует)."""
    st = _send(conn, "pie_status", {}) or {}
    cs = st.get("clients") or []
    if st.get("is_running") and cs and "Login" in (cs[0].get("current_widget") or ""):
        print("  reuse running new_window PIE (login)")
        return True
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 30
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(2)
    for attempt in range(3):
        _send(conn, "pie_start", {"num_clients": 1, "mode": "new_window",
                                  "window_width": 1280, "window_height": 720})
        dl = time.monotonic() + 60
        while time.monotonic() < dl:
            st = _send(conn, "pie_status", {}) or {}
            cs = st.get("clients") or []
            if st.get("is_running") and cs and cs[0].get("current_widget"):
                return True
            time.sleep(1.5)
        print(f"  pie_start new_window не готов за 60s (попытка {attempt+1}/3)")
    return False


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = f"wf_ue_{ts}", f"wf_bot_{ts}"
    pwd = "Test1234"
    print("=== _t72_shot: визуальный прогон до боя ===")
    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("бот не подключился -> FAIL"); return 1
    time.sleep(2.0)

    conn = UnrealConnection(); conn.connect()
    try:
        print("--- PIE n=1 (new_window) ---")
        if not _ensure_pie_nw(conn):
            print("PIE не поднялся -> FAIL"); return 1
        print("--- login + FindGame -> DRAFT ---")
        if not _ue_login_findgame(conn, ue_user, pwd):
            print("DRAFT не достигнут -> FAIL"); return 1
        print("--- draft ---")
        _ue_draft(conn)

        print("--- wait Deployment + deploy ---")
        dl = time.monotonic() + 45
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_DeploymentScreen"):
                break
            time.sleep(1.5)
        n = _deploy_client(conn, 0)
        print(f"  deployed {n}/{UNITS_TO_PICK}")
        time.sleep(2.5)  # дать юнитам отрисоваться
        shot(conn, "t72_deploy.png", False)
        shot(conn, "t72_deploy_ui.png", True)
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        print("--- mulligan ---")
        if _wait_pie_widget(conn, ("Mulligan",), 30, "mulligan"):
            time.sleep(1.5)
            _send(conn, "invoke_button_click", {"widget_name": "AcceptHandButton", "controller_index": 0})

        print("--- wait BATTLE ---")
        if not _wait_pie_widget(conn, ("BattleHUD", "ActionCardHand"), 60, "battle"):
            print("бой не достигнут -> FAIL"); return 1
        time.sleep(3.0)  # камера боя устаканилась
        units = _battle_units(conn, 0)
        print(f"  юнитов на поле: {len(units)}; t72 присутствует: "
              + str(any('t72' in (u.get('unitId') or '') for u in units)))
        shot(conn, "t72_battle.png", False)
        shot(conn, "t72_battle_ui.png", True)
        print("=== DONE — PIE на бое, скриншоты сняты (без сдачи) ===")
        return 0
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
