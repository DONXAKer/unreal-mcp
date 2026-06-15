"""2-player ВИЗУАЛЬНЫЙ playtest: два отдельных UnrealEditor (порты 55557/55558),
каждый PIE n=1, controller 0 — проходят все фазы игры; на каждой фазе с ОБОИХ
клиентов снимается скриншот + JSON-снимок состояния. Для каждого кадра гоняется
pixel-diff против baseline (_screenshot_diff), а всё вместе складывается в
Saved/PlaytestRuns/<ts>/ с manifest.json — его читает vision-агент
`visual-playtest-reviewer`, который «смотрит» PNG и ловит ГРАФИЧЕСКИЕ баги
(съехавший layout, пропавшие портреты/юниты, наложения, чёрный экран), а сам
harness ловит ЛОГИЧЕСКИЕ (не та фаза, юнит не на той клетке, доска не меняется).

Почему два Editor-процесса, а не PIE n=2: multi-client PIE флачит на
widget-enumeration (memory project_multiclient_pie_limitation). Здесь каждый
игрок — отдельный Editor (controller 0), enumeration стабилен; сервер матчит
двух реальных клиентов сам (бот не нужен). Требует UnrealMCP 3.2.0+
(конфигурируемый порт: Editor #2 запущен с `-MCPPort=55558`).

Запуск из D:\\WarCard\\unreal-mcp\\Python (оба редактора уже подняты skill'ом):
    uv run python tests/playtest_visual.py [<ts>]
    # первый прогон для сидирования эталонов:
    $env:WC_UPDATE_BASELINES="1"; uv run python tests/playtest_visual.py

Требования: WarCard сервер :8081; два Editor-инстанса с MCP на 55557 и 55558.
"""

from __future__ import annotations

import json
import logging
import shutil
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

# Подавляем шумный INFO unreal_mcp_server (печатает полный JSON, крашится на
# Windows cp1251 на Unicode-символах в widget tree).
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user
from tests._screenshot_diff import (
    SCREENSHOTS_DIR,
    BaselineMissing,
    ScreenshotMismatch,
    assert_screenshot_matches,
)
from tests.smoke_pie_full_game import (
    UNITS_TO_PICK,
    _battle_units,
    _deploy_client,
    _find_node,
    _node_first_text,
    _send,
    _suffix,
)
from unreal_mcp_server import UnrealConnection

# Порт на сторону. Editor #1 — дефолт 55557, Editor #2 — `-MCPPort=55558`.
PORTS = {"p1": 55557, "p2": 55558}

RUNS_ROOT = Path("D:/WarCard/client/Saved/PlaytestRuns")

# Пороги pixel-diff: статичные экраны строже, боевые мягче (анимации/таймеры).
THRESHOLDS = {
    "login": 0.97,
    "main_menu": 0.97,
    "matchmaking": 0.93,
    "dice_roll_1": 0.93,
    "draft": 0.95,
    "deployment": 0.95,
    "dice_roll_2": 0.93,
    "mulligan": 0.95,
    "battle": 0.90,
    "battle_field": 0.88,
    "battle_after": 0.88,
    "game_result": 0.95,
}

# Ожидаемый виджет-маркер фазы — агент сверяет картинку с этим контекстом.
EXPECTED_WIDGET = {
    "login": "WBP_Login",
    "main_menu": "WBP_MainMenu",
    "matchmaking": "WBP_Matchmaking",
    "dice_roll_1": "WBP_DiceRoll",
    "draft": "WBP_Draft",
    "deployment": "WBP_DeploymentScreen",
    "dice_roll_2": "WBP_DiceRoll",
    "mulligan": "WBP_Mulligan",
    "battle": "WBP_BattleHUD",
    "battle_field": "WBP_BattleHUD",
    "battle_after": "WBP_BattleHUD",
    "game_result": "WBP_GameResult",
}


# ────────────────────────────────────────────────────────────────────────
# Single-connection (controller 0) хелперы — в каждом редакторе один клиент.
# ────────────────────────────────────────────────────────────────────────

def _uw_classes(conn: UnrealConnection) -> list[str]:
    tree = _send(conn, "get_widget_tree", {"controller_index": 0})
    return [uw.get("class", "") for uw in (tree.get("user_widgets") or [])
            if uw.get("is_in_viewport")]


def _has_uw(conn: UnrealConnection, substr: str) -> bool:
    return any(substr in c for c in _uw_classes(conn))


def _wait_uw(conn: UnrealConnection, substr: str, timeout: float = 30.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _has_uw(conn, substr):
            return True
        time.sleep(1.0)
    return False


def _wait_uw_all(conns: dict[str, UnrealConnection], substr: str, timeout: float = 40.0) -> bool:
    deadline = time.monotonic() + timeout
    pending = set(conns)
    while time.monotonic() < deadline and pending:
        for side in list(pending):
            if _has_uw(conns[side], substr):
                pending.discard(side)
        if not pending:
            return True
        time.sleep(1.0)
    return not pending


def _draft_entries(conn: UnrealConnection) -> list[tuple[str, str]]:
    """(имя кнопки, имя юнита) для CatalogEntryButton в Scroll_AvailableUnits."""
    out: list[tuple[str, str]] = []
    tree = _send(conn, "get_widget_tree", {"controller_index": 0})
    for uw in (tree.get("user_widgets") or []):
        if "Draft" not in uw.get("class", ""):
            continue
        scroll = _find_node(uw.get("root"), lambda n: n.get("name") == "Scroll_AvailableUnits")
        if not scroll:
            continue
        for ch in (scroll.get("children") or []):
            if "Button" in ch.get("class", ""):
                out.append((ch.get("name"), _node_first_text(ch)))
    return out


def _collect_state(conn: UnrealConnection) -> dict[str, Any]:
    """Богатый снимок состояния клиента (current_widget + что отдаст phase-subsystem)."""
    out: dict[str, Any] = {}
    status = _send(conn, "pie_status", {})
    clients = status.get("clients") or []
    out["current_widget"] = clients[0].get("current_widget") if clients else None
    out["pie_running"] = status.get("is_running")
    out["viewport_widgets"] = _uw_classes(conn)
    for cmd, key in (
        ("wc_get_selection_state", "selection"),
        ("wc_get_deployment_state", "deployment"),
        ("wc_get_battle_state", "battle"),
    ):
        r = _send(conn, cmd, {"controller_index": 0})
        if isinstance(r, dict) and not r.get("error"):
            out[key] = r
    bu = _battle_units(conn, 0)
    if bu:
        out["battle_units"] = bu
    return out


# ────────────────────────────────────────────────────────────────────────
# Захват кадра: скриншот + state + копия в run-папку + pixel-diff.
# ────────────────────────────────────────────────────────────────────────

def _capture(side: str, conn: UnrealConnection, phase: str, run_dir: Path,
             manifest: dict[str, Any], *, show_ui: bool = True,
             do_diff: bool = True) -> dict[str, Any]:
    fname = f"pt_{side}_{phase}.png"
    src = SCREENSHOTS_DIR / fname
    # Снимаем заново — удаляем прошлый файл, чтобы дождаться именно свежего.
    try:
        src.unlink()
    except FileNotFoundError:
        pass
    _send(conn, "pie_screenshot", {"filename": fname, "show_ui": show_ui})
    # Скрин пишется на redraw'е (плагин 3.2.1 флашит синхронно) — но ждём файл
    # с запасом на случай задержки ФС/тика.
    deadline = time.monotonic() + 6.0
    while not src.is_file() and time.monotonic() < deadline:
        time.sleep(0.5)

    state = _collect_state(conn)

    # Копия из общей Saved/Screenshots в архив прогона.
    side_dir = run_dir / side
    side_dir.mkdir(parents=True, exist_ok=True)
    dst = side_dir / f"{phase}.png"
    copied = False
    if src.is_file():
        try:
            shutil.copyfile(src, dst)
            copied = True
        except Exception as exc:
            print(f"    [{side}] copy fail {fname}: {exc}")

    diff_result: dict[str, Any] | None = None
    if do_diff and copied:
        threshold = THRESHOLDS.get(phase, 0.95)
        try:
            assert_screenshot_matches(fname, threshold)
            diff_result = {"status": "ok", "threshold": threshold}
        except BaselineMissing:
            diff_result = {"status": "baseline_missing", "threshold": threshold}
        except ScreenshotMismatch as exc:
            diff_result = {"status": "mismatch", "threshold": threshold, "detail": str(exc)}
        except Exception as exc:
            diff_result = {"status": "error", "detail": str(exc)}

    expected = EXPECTED_WIDGET.get(phase, "")
    widget_ok = bool(expected) and any(expected in c for c in state.get("viewport_widgets", []))

    entry = {
        "phase": phase,
        "side": side,
        "expected_widget": expected,
        "widget_present": widget_ok,
        "screenshot": str(dst) if copied else None,
        "screenshot_flat": fname,
        "screenshot_present": copied,
        "show_ui": show_ui,
        "state": state,
        "pixel_diff": diff_result,
    }
    manifest["phases"].append(entry)
    dstat = diff_result.get("status") if diff_result else "—"
    print(f"  [{side}] {phase}: widget={state.get('current_widget')} "
          f"expect={expected}({'ok' if widget_ok else 'MISS'}) "
          f"diff={dstat} png={'ok' if copied else 'MISSING'}")
    return entry


def _capture_both(conns: dict[str, UnrealConnection], phase: str, run_dir: Path,
                  manifest: dict[str, Any], *, show_ui: bool = True,
                  do_diff: bool = True) -> None:
    for side, conn in conns.items():
        _capture(side, conn, phase, run_dir, manifest, show_ui=show_ui, do_diff=do_diff)


def _warm_render(conns: dict[str, UnrealConnection], rounds: int = 4) -> None:
    """Прогрев рендера перед захватом 3D-сцены. Фоновое new_window-окно
    отрисовывает поле боя НЕ с первого кадра (троттлинг фона + стриминг сцены),
    поэтому ранний pie_screenshot выходит чёрным. Форсируем несколько Draw'ов
    throwaway-снимками, чтобы сцена успела сойтись до реального захвата."""
    for _ in range(rounds):
        for conn in conns.values():
            _send(conn, "pie_screenshot", {"filename": "_warmup.png", "show_ui": False})
        time.sleep(1.2)


def _add_check(manifest: dict[str, Any], name: str, passed: bool, detail: str = "") -> None:
    manifest["logic_checks"].append({"name": name, "passed": bool(passed), "detail": detail})
    print(f"  CHECK {'PASS' if passed else 'FAIL'}: {name} {('— ' + detail) if detail else ''}")


# ────────────────────────────────────────────────────────────────────────
# PIE lifecycle / драйверы фаз (через два соединения).
# ────────────────────────────────────────────────────────────────────────

def _start_pie(side: str, conn: UnrealConnection) -> bool:
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(1.5)
    for _ in range(3):
        # mode="new_window" (плагин 3.3.0+) — PIE в отдельном presented-окне;
        # Slate-readback (3.3.1) снимает с него реальный кадр (не чёрный).
        _send(conn, "pie_start", {"num_clients": 1, "mode": "new_window"})
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            st = _send(conn, "pie_status", {})
            cs = st.get("clients") or []
            if st.get("is_running") and cs and cs[0].get("current_widget"):
                print(f"  [{side}] PIE up, widget={cs[0].get('current_widget')}")
                return True
            time.sleep(1.5)
        print(f"  [{side}] pie_start не поднял PIE — повтор")
    return False


def _login_findgame(conns: dict[str, UnrealConnection], users: dict[str, str], pwd: str) -> None:
    for side, conn in conns.items():
        _send(conn, "set_text_on_widget",
              {"widget_name": "LoginUsernameInput", "text": users[side], "controller_index": 0})
        _send(conn, "set_text_on_widget",
              {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})


def _snake_draft(conns: dict[str, UnrealConnection]) -> dict[str, int]:
    counts = {side: 0 for side in conns}
    for pick in range(10):
        done = False
        for _ in range(30):
            for side, conn in conns.items():
                for btn, _unit in _draft_entries(conn):
                    r = _send(conn, "invoke_button_click",
                              {"widget_name": btn, "controller_index": 0})
                    if isinstance(r, dict) and r.get("ok") and not r.get("error"):
                        counts[side] += 1
                        print(f"  pick {pick}: {side} -> {btn}")
                        done = True
                        break
                if done:
                    break
            if done:
                break
            time.sleep(0.8)
        if not done:
            print(f"  pick {pick}: не удалось — стоп драфта")
            break
        time.sleep(1.8)  # сервер broadcast draft-update + UI rebuild
    return counts


def _active_side(conns: dict[str, UnrealConnection], wait: float = 0.0) -> str | None:
    """Сторона с my_turn=True. Опционально ждём wait секунд (бой/dice_roll_2
    могут назначить ход не сразу)."""
    deadline = time.monotonic() + wait
    while True:
        for side, conn in conns.items():
            st = _send(conn, "wc_get_battle_state", {"controller_index": 0})
            if st.get("my_turn"):
                return side
        if time.monotonic() >= deadline:
            return None
        time.sleep(1.0)


def _battle_moves(conns: dict[str, UnrealConnection], max_turns: int = 8) -> int:
    """Детерминированный бой без LLM: активный клиент двигает юниты к врагу + end-turn."""
    turns = 0
    for turn in range(max_turns):
        # Ждём, пока чей-то my_turn станет True, и действуем ИМЕННО за эту сторону.
        # (Не перечитываем my_turn повторно во вложенном цикле — reflection-чтение
        # IsMyTurn флакает, из-за чего ход мог теряться: turns=0 при активной стороне.)
        active_side = _active_side(conns, wait=20.0 if turn == 0 else 5.0)
        if active_side is None:
            break
        conn = conns[active_side]
        units = [u for u in _battle_units(conn, 0) if u.get("alive")]
        for u in units:
            enemies = [e for e in units
                       if _suffix(e.get("unitId", "")) != _suffix(u.get("unitId", ""))]
            if not enemies:
                continue
            tgt = min(enemies, key=lambda e: abs(e["gridX"] - u["gridX"]) + abs(e["gridY"] - u["gridY"]))
            _send(conn, "wc_attack", {"controller_index": 0, "attacker_unit_id": u["unitId"],
                                      "target_unit_id": tgt["unitId"], "x": tgt["gridX"], "y": tgt["gridY"]})
            nx = u["gridX"] + (1 if tgt["gridX"] > u["gridX"] else -1 if tgt["gridX"] < u["gridX"] else 0)
            ny = u["gridY"] + (1 if tgt["gridY"] > u["gridY"] else -1 if tgt["gridY"] < u["gridY"] else 0)
            _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": u["unitId"], "x": nx, "y": ny})
        _send(conn, "wc_end_turn", {"controller_index": 0})
        turns += 1
        print(f"  ход {turn}: {active_side} отыграл ({len(units)} юнитов)")
        time.sleep(1.2)
    return turns


# ────────────────────────────────────────────────────────────────────────

def main() -> int:
    ts = sys.argv[1] if len(sys.argv) > 1 else str(int(time.time()))
    pwd = "Test1234"
    short = ts[-6:]
    users = {"p1": f"pt_a_{short}", "p2": f"pt_b_{short}"}

    run_dir = RUNS_ROOT / ts
    run_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "ts": ts, "users": users, "ports": PORTS,
        "phases": [], "logic_checks": [],
    }

    print("=== playtest_visual (2 Editor-процесса) ===")
    print(f"ts={ts} users={users} run_dir={run_dir}\n")

    for side in PORTS:
        ensure_test_user(users[side], pwd, f"{users[side]}@x.com")

    # Подключение к обоим редакторам.
    conns: dict[str, UnrealConnection] = {}
    for side, port in PORTS.items():
        conn = UnrealConnection(port=port)
        if not conn.connect():
            print(f"FATAL: нет MCP-соединения с {side} на 127.0.0.1:{port}. "
                  f"Запущен ли второй Editor с -MCPPort={port}? Собран ли плагин 3.2.0+?")
            return 2
        conns[side] = conn
    print("оба MCP-соединения установлены\n")

    rc = 0
    try:
        # 1. PIE n=1 в каждом редакторе.
        print("--- PIE setup ---")
        for side, conn in conns.items():
            if not _start_pie(side, conn):
                print(f"FATAL: PIE не поднялся в {side}")
                return 1
        time.sleep(3)
        _capture_both(conns, "login", run_dir, manifest)

        # 2. Login → MainMenu.
        print("\n--- login ---")
        _login_findgame(conns, users, pwd)
        if not _wait_uw_all(conns, "WBP_MainMenu", timeout=30):
            print("  WARN: MainMenu не у всех — продолжаем (best-effort)")
        _capture_both(conns, "main_menu", run_dir, manifest)

        # 3. FindGame → Matchmaking (transient) → Draft. Матчмейкинг двух
        #    реальных клиентов иногда не сводится с первого клика — ретраим.
        print("\n--- FindGame -> matchmaking -> draft ---")
        draft_ok = False
        for attempt in range(3):
            for conn in conns.values():
                _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
            if attempt == 0:
                # matchmaking / dice_roll_1 — best-effort кадры (фазы краткие).
                if _wait_uw_all(conns, "WBP_Matchmaking", timeout=6):
                    _capture_both(conns, "matchmaking", run_dir, manifest)
                if _wait_uw_all(conns, "WBP_DiceRoll", timeout=8):
                    _capture_both(conns, "dice_roll_1", run_dir, manifest)
            if _wait_uw_all(conns, "WBP_Draft", timeout=30):
                draft_ok = True
                break
            print(f"  попытка {attempt}: draft ещё нет — повтор FindGame")
        _add_check(manifest, "draft_reached", draft_ok)
        if not draft_ok:
            print("=== draft не достигнут -> stop ===")
            rc = 1
            return rc
        _capture_both(conns, "draft", run_dir, manifest)

        # 4. Snake draft.
        print("\n--- snake draft ---")
        counts = _snake_draft(conns)
        total = sum(counts.values())
        _add_check(manifest, "draft_complete", total >= 10, f"picks={counts} total={total}/10")

        # 5. Deployment.
        print("\n--- deployment ---")
        deploy_ok = _wait_uw_all(conns, "WBP_DeploymentScreen", timeout=45)
        _add_check(manifest, "deployment_reached", deploy_ok)
        _warm_render(conns)  # дать сетке расстановки отрисоваться (иначе ложно-чёрный кадр)
        _capture_both(conns, "deployment", run_dir, manifest)
        for side, conn in conns.items():
            n = _deploy_client(conn, 0)
            stt = _send(conn, "wc_get_deployment_state", {"controller_index": 0})
            _add_check(manifest, f"deploy_{side}", stt.get("deployed_count") == UNITS_TO_PICK,
                       f"deployed={stt.get('deployed_count')} ready={stt.get('ready')} placed={n}")
        for conn in conns.values():
            _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        # mulligan — best-effort между deployment и battle.
        if _wait_uw_all(conns, "WBP_Mulligan", timeout=10):
            _capture_both(conns, "mulligan", run_dir, manifest)

        # 6. Battle HUD.
        print("\n--- battle ---")
        battle_ok = _wait_uw_all(conns, "BattleHUD", timeout=60)
        _add_check(manifest, "battle_reached", battle_ok)
        time.sleep(2.5)  # дать /state-броадкасту заспавнить юнит-актёры
        _warm_render(conns, rounds=5)  # прогрев: поле боя рендерится не с первого кадра
        _capture_both(conns, "battle", run_dir, manifest)
        _capture_both(conns, "battle_field", run_dir, manifest, show_ui=False)

        # 7. Несколько ходов + проверка, что доска меняется.
        before = {side: _battle_units(conn, 0) for side, conn in conns.items()}
        turns = _battle_moves(conns, max_turns=8)
        time.sleep(1.5)
        after = {side: _battle_units(conn, 0) for side, conn in conns.items()}

        def _key(units: list[dict[str, Any]]) -> set[tuple]:
            return {(u.get("unitId"), u.get("gridX"), u.get("gridY"), u.get("hp"), u.get("alive"))
                    for u in units}
        changed = any(_key(before[s]) != _key(after[s]) for s in conns)
        _add_check(manifest, "board_changed_after_moves", changed, f"turns={turns}")
        _capture_both(conns, "battle_after", run_dir, manifest, show_ui=False)

        # 8. Surrender p1 → GameResult на обоих.
        print("\n--- surrender p1 -> game_result ---")
        _send(conns["p1"], "wc_surrender", {"controller_index": 0})
        result_ok = _wait_uw_all(conns, "WBP_GameResult", timeout=30)
        _add_check(manifest, "game_result_reached_both", result_ok)
        _capture_both(conns, "game_result", run_dir, manifest)

        passed = all(c["passed"] for c in manifest["logic_checks"])
        manifest["summary"] = {
            "logic_passed": passed,
            "phases_captured": len(manifest["phases"]),
            "checks": len(manifest["logic_checks"]),
        }
        rc = 0 if passed else 1
    finally:
        (run_dir / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nmanifest -> {run_dir / 'manifest.json'}")
        print(f"кадров: {len(manifest['phases'])}, проверок: {len(manifest['logic_checks'])}")

    print(f"\n=== RESULT: {'PASS' if rc == 0 else 'FAIL'} (run {ts}) ===")
    return rc


if __name__ == "__main__":
    sys.exit(main())
