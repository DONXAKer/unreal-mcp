"""Диагностика FIX-BATTLE-001: довести 2 клиента до боя и осмотреть PIE-мир
editor #1 (55557) — сколько GridCell/юнит-актёров, где камера, скриншот.
НЕ сдаётся в конце — оставляет PIE в бою для дальнейшего осмотра.

Запуск из D:\\WarCard\\unreal-mcp\\Python: uv run python tests/_diag_battle.py
Требует: сервер :8081, два редактора (55557/55558).
"""
from __future__ import annotations

import json
import logging
import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user
from tests.playtest_visual import (
    PORTS, _deploy_client, _send, _snake_draft, _start_pie, _wait_uw_all,
)
from unreal_mcp_server import UnrealConnection


def main() -> int:
    ts = str(int(time.time()))
    short = ts[-6:]
    users = {"p1": f"dg_a_{short}", "p2": f"dg_b_{short}"}
    pwd = "Test1234"
    for side in PORTS:
        ensure_test_user(users[side], pwd, f"{users[side]}@x.com")
    conns = {s: UnrealConnection(port=p) for s, p in PORTS.items()}
    for s, conn in conns.items():
        if not conn.connect():
            print(f"FATAL: нет соединения {s}")
            return 2

    for side, conn in conns.items():
        if not _start_pie(side, conn):
            print(f"FATAL: PIE не поднялся {side}")
            return 1
    time.sleep(3)

    # login + findgame (retry) -> draft
    for side, conn in conns.items():
        _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": users[side], "controller_index": 0})
        _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})
    _wait_uw_all(conns, "WBP_MainMenu", timeout=30)
    draft_ok = False
    for _ in range(3):
        for conn in conns.values():
            _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
        if _wait_uw_all(conns, "WBP_Draft", timeout=30):
            draft_ok = True
            break
    print("draft:", draft_ok)
    if not draft_ok:
        return 1

    _snake_draft(conns)
    _wait_uw_all(conns, "WBP_DeploymentScreen", timeout=45)
    for conn in conns.values():
        _deploy_client(conn, 0)
    for conn in conns.values():
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})
    print("battle:", _wait_uw_all(conns, "BattleHUD", timeout=60))
    time.sleep(3.0)  # дать /state заспавнить юниты

    # --- Инспекция PIE-мира editor #1 ---
    c = conns["p1"]

    def s(cmd, p=None):
        r = c.send_command(cmd, p or {})
        return r.get("result", r) if isinstance(r, dict) else r

    def count(pattern):
        r = s("find_actors_by_name", {"pattern": pattern})
        acts = r.get("actors") if isinstance(r, dict) else r
        return acts if isinstance(acts, list) else []

    print("\n=== PIE WORLD INSPECTION (editor #1 / p1) ===")
    for pat in ("Cell", "BP_Cell", "GridCell", "Unit", "BP_UnitActor", "Battlefield", "Camera", "CameraActor", "Light"):
        acts = count(pat)
        sample = [(a.get("name"), a.get("class"), a.get("location")) for a in acts[:4]]
        print(f"  {pat:14s}: {len(acts):3d}  {sample}")

    bm = count("Battlefield")
    if bm:
        props = s("get_actor_properties", {"name": bm[0]["name"]})
        p = (props or {}).get("properties", {})
        print("\n  BattlefieldManager (PIE):",
              "AllCells=", p.get("AllCells"), "SpawnedUnits=", p.get("SpawnedUnits"),
              "bHidden=", p.get("bHidden"), "loc=", bm[0].get("location"))

    s("pie_screenshot", {"filename": "diag_battle_p1.png", "show_ui": False})
    time.sleep(2)
    print("\n  screenshot -> Saved/Screenshots/diag_battle_p1.png (show_ui=false)")
    print("\nPIE оставлен в бою для осмотра.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
