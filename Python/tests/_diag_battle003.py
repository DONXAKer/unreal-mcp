"""FIX-BATTLE-003 diag: дойти до боя и выяснить, почему красная сторона не видна.

Камера vs спавн: выгружаем (а) клиентские юниты (wc_get_battle_units: сколько и
gridX), (б) актёров в мире (есть ли красные тела), (в) камеру (transform/view target).
PIE НЕ сдаём — оставляем в бою для MCP-инспекции. Запуск из unreal-mcp/Python
(редактор на 55557 уже поднят): uv run python tests/_diag_battle003.py
"""
from __future__ import annotations

import json
import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_fullgame_solo import (
    _battle_units, _ue_login_findgame, _ue_draft, _wait_uw,
)
from tests.smoke_pie_full_game import _deploy_client, _send
from unreal_mcp_server import UnrealConnection


def main() -> int:
    suffix = str(int(time.time()) % 100000)
    ue_user, pwd = f"d3_ue_{suffix}", "Test1234"
    bot_user = f"d3_bot_{suffix}"
    ensure_test_user(ue_user, pwd, f"{ue_user}@ex.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@ex.com")

    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("FAIL: бот не подключился")
        return 1

    conn = UnrealConnection("127.0.0.1", 55557)
    conn.connect()

    # PIE: переиспользуем если бежит, иначе стартуем new_window.
    st = _send(conn, "pie_status", {}) or {}
    cs = st.get("clients") or []
    if not (st.get("is_running") and cs and cs[0].get("current_widget")):
        _send(conn, "pie_stop", {})
        time.sleep(2)
        _send(conn, "pie_start", {"num_clients": 1, "mode": "new_window"})
        dl = time.monotonic() + 60
        while time.monotonic() < dl:
            st = _send(conn, "pie_status", {}) or {}
            cs = st.get("clients") or []
            if st.get("is_running") and cs and cs[0].get("current_widget"):
                break
            time.sleep(1.5)
    print(f"PIE widget={cs[0].get('current_widget') if cs else '?'}")

    if not _ue_login_findgame(conn, ue_user, pwd):
        print("FAIL: login/findgame"); return 1
    print(f"draft picks: {_ue_draft(conn)}")
    print(f"deployed: {_deploy_client(conn, 0)}")

    if not _wait_uw(conn, "ActionCardHand", 60, "battle"):
        print("FAIL: бой не достигнут"); return 1
    time.sleep(2)

    # === ДИАГНОСТИКА ===
    units = _battle_units(conn, 0)
    print(f"\n=== wc_get_battle_units: {len(units)} юнитов (клиентский BattlefieldManager) ===")
    by_col: dict = {}
    for u in units:
        gx = u.get("gridX")
        by_col.setdefault(gx, []).append(u.get("unitId"))
    for gx in sorted(by_col, key=lambda v: (v is None, v)):
        print(f"  gridX={gx}: {len(by_col[gx])} -> {by_col[gx]}")

    print("\n=== actors in world (BasicUnit/Unit) ===")
    for pat in ("BasicUnit", "Unit", "BP_Unit"):
        r = _send(conn, "find_actors_by_name", {"pattern": pat}) or {}
        acts = r.get("actors") or r.get("meta", {}).get("actors") or []
        print(f"  pattern '{pat}': {len(acts)} -> {acts[:12]}")

    print("\n=== camera actors ===")
    r = _send(conn, "find_actors_by_name", {"pattern": "Camera"}) or {}
    cams = r.get("actors") or r.get("meta", {}).get("actors") or []
    print(f"  cameras: {cams[:8]}")

    print("\n=== ИНТЕРПРЕТАЦИЯ ===")
    red = sum(len(v) for k, v in by_col.items() if k == 0)
    blue = sum(len(v) for k, v in by_col.items() if k == 7)
    print(f"  red(gridX=0)={red}, blue(gridX=7)={blue} в клиентском state")
    print("  PIE ОСТАВЛЕН В БОЮ — инспектируй через MCP (скриншоты/акторы).")
    bot.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
