"""Надёжный SOLO ВИЗУАЛЬНЫЙ playtest: 1 реальный UE-клиент (PIE n=1, new_window) +
headless STOMP-оппонент. Совмещает надёжность solo-потока (smoke_fullgame_solo:
2-редакторный драфт флакает на widget-enumeration — здесь UE-клиент ОДИН, бот
изображает второго игрока) с визуальным захватом (playtest_visual: PIE new_window
+ прогрев рендера + pie_screenshot + manifest для агента visual-playtest-reviewer).

Зачем: для визуальной проверки боевых элементов (жетоны юнитов, поле) нужен
не-чёрный 3D-кадр → PIE обязан быть в mode="new_window" (Slate-readback, плагин
3.3.1) + прогрев. А чтобы НАДЁЖНО дойти до боя — нужен solo-поток (не 2 редактора).
Этот harness даёт оба свойства. Рендерится один игрок (P1, red); экраны одинаковы
для обеих сторон, поэтому solo ловит большинство UI/графических багов.

Запуск из D:\\WarCard\\unreal-mcp\\Python (один редактор на 55557 уже поднят):
    uv run python tests/playtest_solo_visual.py [<ts>]

Артефакты: D:\\WarCard\\client\\Saved\\PlaytestRuns\\<ts>\\p1\\*.png + manifest.json.
Требования: WarCard сервер :8081; один Editor с MCP на 55557. Код возврата 0 = бой достигнут.
"""

from __future__ import annotations

import json
import logging
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_fullgame_solo import (
    _ue_login_findgame,
    _ue_draft,
    _ue_battle_round,
    _battle_units,
    _wait_pie_widget,
    _pie_ready_n1,
)
from tests.smoke_pie_full_game import _deploy_client, _has_uw_any, _send, UNITS_TO_PICK
# Переиспользуем визуальные хелперы (скриншот+state+копия в run-папку, прогрев).
from tests.playtest_visual import _capture, _warm_render, _add_check, RUNS_ROOT
from unreal_mcp_server import UnrealConnection

PORT = 55557


def _start_pie_new_window(conn: UnrealConnection) -> bool:
    """PIE n=1 в отдельном окне (new_window) — обязательно для не-чёрного 3D-кадра."""
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {}) or {}
        if not st.get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(1.5)
    for attempt in range(3):
        _send(conn, "pie_start", {"num_clients": 1, "mode": "new_window"})
        dl = time.monotonic() + 45
        while time.monotonic() < dl:
            st = _send(conn, "pie_status", {}) or {}
            cs = st.get("clients") or []
            if st.get("is_running") and cs and cs[0].get("current_widget"):
                print(f"  PIE up (new_window), widget={cs[0].get('current_widget')}")
                return True
            time.sleep(1.5)
        print(f"  pie_start не поднял PIE (попытка {attempt + 1}/3) — повтор")
    return False


def main() -> int:
    ts = sys.argv[1] if len(sys.argv) > 1 else str(int(time.time()))
    run_dir = RUNS_ROOT / ts
    run_dir.mkdir(parents=True, exist_ok=True)

    uid = int(time.time()) % 1000000
    ue_user, bot_user = f"sv_ue_{uid}", f"sv_bot_{uid}"
    pwd = "Test1234"

    print("=== playtest_solo_visual (1 UE client new_window + STOMP opponent) ===")
    print(f"ts={ts} UE={ue_user} bot={bot_user} run_dir={run_dir}\n")

    manifest: dict[str, Any] = {"ts": ts, "mode": "solo_visual", "phases": [], "logic_checks": []}

    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

    print("--- bot: connect + matchmaking ---")
    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("=== RESULT: бот не подключился -> FAIL ===")
        return 1
    time.sleep(2.0)

    conn = UnrealConnection()
    conn.connect()
    conns = {"p1": conn}  # _capture/_warm_render ждут dict side->conn

    try:
        print("\n--- 1/6 PIE n=1 (new_window) ---")
        if not _start_pie_new_window(conn):
            print("=== RESULT: PIE не поднялся -> FAIL ===")
            return 1

        print("\n--- 2/6 login + FindGame -> DRAFT ---")
        draft_ok = _ue_login_findgame(conn, ue_user, pwd)
        _add_check(manifest, "draft_reached", draft_ok)
        if not draft_ok:
            print("=== RESULT: DRAFT не достигнут -> FAIL ===")
            return 1

        print("\n--- 3/6 draft (UE picks + bot picks) ---")
        ue_picks = _ue_draft(conn)
        print(f"  UE кликов-пиков: ~{ue_picks}")

        print("\n--- 4/6 wait Deployment + deploy ---")
        deploy_seen = False
        dl = time.monotonic() + 45
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_DeploymentScreen"):
                deploy_seen = True
                break
            time.sleep(1.5)
        _add_check(manifest, "deployment_reached", deploy_seen)
        n = _deploy_client(conn, 0)
        print(f"  UE deployed: {n}/{UNITS_TO_PICK}")
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        print("\n--- 5/6 wait BATTLE ---")
        battle = _wait_pie_widget(conn, ("BattleHUD", "ActionCardHand"), 60, "battle")
        _add_check(manifest, "battle_reached", battle)
        if not battle:
            print("=== RESULT: BATTLE не достигнут -> FAIL ===")
            return 1

        # Card-play: UE двигает/атакует 3 хода, бот пасует — юниты смещаются,
        # жетоны должны ехать вместе с ними.
        print("\n--- 5b/6 card-play (UE moves) ---")
        before = _battle_units(conn, 0)
        rounds = 0
        for _ in range(3):
            got, _m, _a = _ue_battle_round(conn, "red")
            if got:
                rounds += 1
                time.sleep(2.0)
            else:
                break
        after = _battle_units(conn, 0)

        def _key(u: dict) -> tuple:
            return (u.get("unitId"), u.get("gridX"), u.get("gridY"), u.get("currentHp"))

        changed = {_key(u) for u in before} != {_key(u) for u in after}
        _add_check(manifest, "board_changed_after_moves", changed, f"rounds={rounds}")

        # Прогрев рендера (фоновое new_window-окно троттлит редрав и сходится не с
        # первого кадра) → захват. Прогрев обильный: иначе именованный скриншот
        # пишется позже файлового дедлайна _capture (png MISSING).
        print("\n--- 5c/6 capture battle frames ---")
        _warm_render(conns, rounds=10)
        _capture("p1", conn, "battle", run_dir, manifest, show_ui=True, do_diff=False)
        _warm_render(conns, rounds=4)
        _capture("p1", conn, "battle_field", run_dir, manifest, show_ui=False, do_diff=False)

        print("\n--- 6/6 surrender -> GameResult ---")
        _send(conn, "wc_surrender", {"controller_index": 0})
        result = _wait_pie_widget(conn, ("GameResult",), 30, "result")
        _add_check(manifest, "game_result_reached", result)
        if result:
            _capture("p1", conn, "game_result", run_dir, manifest, show_ui=True, do_diff=False)

        manifest["summary"] = {
            "captured": sum(1 for p in manifest["phases"] if p.get("screenshot_present")),
            "logic_passed": all(c["passed"] for c in manifest["logic_checks"]),
        }
        (run_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nmanifest -> {run_dir / 'manifest.json'}")
        print(f"=== RESULT: {'PASS' if manifest['summary']['logic_passed'] else 'PARTIAL'} (run {ts}) ===")
        return 0
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
