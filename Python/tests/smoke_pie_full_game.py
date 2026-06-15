"""Полный e2e smoke ПОЛНОЙ игры: 2 клиента проходят
login → matchmaking → DRAFT (snake) → Deployment → Battle → surrender → GameResult.

Победа достигается детерминированно через капитуляцию (wc_surrender) одного
клиента — естественный бой до 0 юнитов занял бы десятки ходов и флакал бы.
После surrender сервер шлёт bGameEnded → оба клиента переходят в GameResult
(победитель/проигравший) → проверяем WBP_GameResult на ОБОИХ.

Поток (актуальный, FEAT-BATTLE + FIX-UI-008):
1. PIE n=2, оба клиента на WBP_Login.
2. Sequential login + FindGame (с ретраями) → оба сматчиваются → WBP_Draft.
3. Snake-draft: на каждом пике кликаем включённую CatalogEntryButton текущего
   пикера; ЗАПОМИНАЕМ имя выбранного юнита per-controller (нужно для деплоя —
   роадж драфт-состав в DeploymentSubsystem недоступен через MCP).
4. Deployment: размещаем именно выбранные на драфте юниты (имя→тип, пробуем оба
   суффикса -blue/-red и обе зоны) через wc_deploy_unit + wc_confirm_deployment.
5. Battle: wc_get_battle_state / wc_end_turn (упражняем боевой HUD).
6. wc_surrender(c0) → детерминированный game-over → WBP_GameResult на обоих.

Использует battle-команды плагина v2.18.0+
(wc_surrender / wc_end_turn / wc_get_battle_state).

Запуск из D:\\WarCard\\unreal-mcp\\Python:
    uv run python tests/smoke_pie_full_game.py

Требования:
- UnrealMCP плагин v2.18.0+ (battle-команды).
- WarCard сервер на :8081.
- BP_GamePhaseManager с заданными DraftWidgetClass/DeploymentScreenWidgetClass/
  BattleHUDWidgetClass/GameResultWidgetClass/ActionCardHandWidgetClass;
  ACardEffectManager на Game_Map (для VFX, не обязателен).

ВНИМАНИЕ: multi-client PIE нестабилен (см. memory project_multiclient_pie_limitation).
Тест устойчив к гонкам через ретраи, но «зелёный» прогон не гарантирован с первого раза.
"""

from __future__ import annotations

import json
import logging
import sys
import time
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

# Подавляем шумный INFO-логгер unreal_mcp_server (печатает полный JSON-ответ,
# крашится на Windows cp1251 при Unicode-символах вроде '✕' в widget tree).
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user
from unreal_mcp_server import UnrealConnection

UNITS_TO_PICK = 5

# Полный каталог id юнитов (сервер, обе стороны, по 9). Деплой перебирает все:
# 5 в ростере игрока разместятся, остальные сервер отвергнет (не в составе или
# клетка вне зоны). Не зависит от чтения имён из дерева виджета — устойчивее.
ALL_UNIT_IDS = [
    "sniper-blue", "medic-blue", "assault-blue", "apache-blue", "abrams-blue",
    "bradley-blue", "patriot-blue", "mlrs-blue", "paladin-blue",
    "sniper-red", "medic-red", "assault-red", "mi24-red", "t72-red",
    "btr80-red", "s300-red", "grad-red", "msta-red",
]

# Координаты зон: FIX-DEPLOY-ZONE-001 — только крайний столбец (Player1 x=0,
# Player2 x=7), по 5 РАЗНЫХ клеток в столбце (занятость проверяется). Сторона клиента
# неизвестна — пробуем обе зоны, невалидная отвергнется сервером.
COORDS_BY_ZONE = {
    "p1": [(0, 0), (0, 1), (0, 2), (0, 3), (0, 4)],
    "p2": [(7, 0), (7, 1), (7, 2), (7, 3), (7, 4)],
}


def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any],
          retries: int = 4, retry_delay: float = 0.6) -> dict[str, Any]:
    """Send + retry с reconnect на socket timeout. Возвращает result или {error}."""
    last_err: str | None = None
    for _ in range(retries):
        try:
            resp = conn.send_command(cmd, params)
            if isinstance(resp, dict):
                return resp.get("result", resp) if resp.get("status") == "success" \
                    else {"error": resp.get("error", "no error msg"), "_raw": resp}
            return {}
        except Exception as exc:
            last_err = str(exc)
            time.sleep(retry_delay)
            try: conn.disconnect()
            except Exception: pass
            try: conn.connect()
            except Exception: pass
    return {"error": last_err or "unknown"}


def _find_node(node: Any, predicate) -> Any:
    """DFS по дереву виджета (get_widget_tree root)."""
    if not isinstance(node, dict):
        return None
    if predicate(node):
        return node
    for child in node.get("children", []) or []:
        hit = _find_node(child, predicate)
        if hit:
            return hit
    return None


def _node_first_text(node: Any) -> str:
    """Первый непустой TextBlock-текст в поддереве (имя юнита в карточке драфта)."""
    found = _find_node(node, lambda n: "TextBlock" in n.get("class", "") and n.get("text"))
    return (found or {}).get("text", "") if found else ""


def _tree(conn, ctrl: int) -> dict[str, Any]:
    return _send(conn, "get_widget_tree", {"controller_index": ctrl})


def _uw_classes(conn, ctrl: int) -> list[str]:
    tree = _tree(conn, ctrl)
    return [uw.get("class", "") for uw in (tree.get("user_widgets") or []) if uw.get("is_in_viewport")]


def _has_uw_any(conn, substr: str) -> bool:
    """True если виджет с substr в class есть у любого из 2 клиентов."""
    for ctrl in (0, 1):
        if any(substr in c for c in _uw_classes(conn, ctrl)):
            return True
    return False


def _wait_widget_strict(conn, ctrl: int, name: str, timeout: float = 25.0, label: str = "") -> bool:
    """Polling find_widget с фильтром по controller_index."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r = _send(conn, "find_widget", {"widget_name": name, "controller_index": ctrl})
        if r.get("found"):
            if label: print(f"    [{label}] '{name}' found on c{ctrl}")
            return True
        time.sleep(0.5)
    if label: print(f"    [{label}] '{name}' NOT found on c{ctrl} after {timeout}s")
    return False


def _draft_entries(conn) -> list[tuple[int, str, str]]:
    """Тройки (controller_index, имя кнопки, имя юнита) для CatalogEntryButton
    в Scroll_AvailableUnits draft-виджета каждого клиента."""
    out: list[tuple[int, str, str]] = []
    for ctrl in (0, 1):
        tree = _tree(conn, ctrl)
        for uw in (tree.get("user_widgets") or []):
            if "Draft" not in uw.get("class", ""):
                continue
            scroll = _find_node(uw.get("root"), lambda n: n.get("name") == "Scroll_AvailableUnits")
            if not scroll:
                continue
            for ch in (scroll.get("children") or []):
                if "Button" in ch.get("class", ""):
                    out.append((ctrl, ch.get("name"), _node_first_text(ch)))
    return out


def _login_round(conn, u1: str, u2: str, pwd: str) -> bool:
    """Login + FindGame обоих клиентов с ретраями. Критерий успеха — появление
    WBP_Draft (значит оба залогинились и сматчились)."""
    for rnd in range(5):
        for ctrl, login in [(0, u1), (1, u2)]:
            _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login, "controller_index": ctrl})
            _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": ctrl})
            _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": ctrl})
        time.sleep(3.0)
        for ctrl in (0, 1):
            _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": ctrl})
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if _has_uw_any(conn, "WBP_Draft_C"):
                print(f"  DRAFT достигнут на раунде {rnd}")
                return True
            time.sleep(1.5)
        print(f"  раунд {rnd}: DRAFT ещё нет — повтор login+FindGame")
    return False


def _run_snake_draft(conn) -> int:
    """10 snake-пиков. Возвращает число сделанных пиков. На каждом пике кликаем
    включённую кнопку текущего пикера (у остальных disabled → invoke вернёт
    error). Имена кнопок меняются после draft-update — перечитываем дерево."""
    counts = {0: 0, 1: 0}
    for pick in range(10):
        done = False
        for _ in range(30):
            for ctrl, btn_name, _unit in _draft_entries(conn):
                r = _send(conn, "invoke_button_click", {"widget_name": btn_name, "controller_index": ctrl})
                if isinstance(r, dict) and r.get("ok") and not r.get("error"):
                    counts[ctrl] += 1
                    print(f"  pick {pick}: c{ctrl} -> {btn_name}")
                    done = True
                    break
            if done:
                break
            time.sleep(0.8)
        if not done:
            print(f"  pick {pick}: не удалось — остановка драфта")
            break
        time.sleep(1.8)  # дать серверу broadcast draft-update + UI rebuild
    total = counts[0] + counts[1]
    print(f"  пики сделано: c0={counts[0]} c1={counts[1]} (всего {total}/10)")
    return total


def _deploy_client(conn, ctrl: int) -> int:
    """Разместить 5 юнитов ростера, перебирая весь каталог id по координатам зоны.
    Сторона/зона клиента неизвестны — пробуем обе зоны; невалидные комбинации
    (юнит не в составе ИЛИ клетка вне зоны) сервер отвергает (deployed=false).
    Имя из дерева читать не нужно — устойчиво к multi-world enumeration flake."""
    deployed = 0
    for coords in COORDS_BY_ZONE.values():
        if deployed >= UNITS_TO_PICK:
            break
        ci = 0
        for uid in ALL_UNIT_IDS:
            if deployed >= UNITS_TO_PICK or ci >= len(coords):
                break
            gx, gy = coords[ci]
            r = _send(conn, "wc_deploy_unit",
                      {"unit_id": uid, "grid_x": gx, "grid_y": gy, "controller_index": ctrl})
            if r.get("deployed"):
                deployed += 1
                ci += 1
                print(f"    c{ctrl} + {uid} @ ({gx},{gy}) ({deployed}/{UNITS_TO_PICK})")
    return deployed


def _battle_units(conn, ctrl: int) -> list[dict[str, Any]]:
    """Снимок юнитов на доске (через wc_get_battle_units → ActionCardSubsystem)."""
    r = _send(conn, "wc_get_battle_units", {"controller_index": ctrl})
    raw = r.get("units_json") or "{}"
    try:
        return json.loads(raw).get("units", [])
    except Exception:
        return []


def _suffix(unit_id: str) -> str:
    return "blue" if unit_id.endswith("-blue") else "red"


def _run_battle_moves(conn, max_turns: int = 8) -> int:
    """Детерминированный драйвер боя БЕЗ LLM: на ходу активного клиента двигаем
    каждый его юнит на 1 клетку к ближайшему вражескому (по суффиксу id) юниту,
    затем end-turn. Сервер применяет free-move только для юнитов активного игрока,
    поэтому стороны вычислять не нужно. Возвращает число сыгранных ходов."""
    turns_done = 0
    for turn in range(max_turns):
        active = None
        for c in (0, 1):
            st = _send(conn, "wc_get_battle_state", {"controller_index": c})
            if st.get("my_turn"):
                active = c
                break
        if active is None:
            print(f"  ход {turn}: ни у кого нет my_turn — стоп")
            break

        units = [u for u in _battle_units(conn, active) if u.get("alive")]
        if not units:
            print(f"  ход {turn}: юнитов нет (c{active}) — стоп")
            break

        moved = 0
        attacked = 0
        for u in units:
            enemies = [e for e in units if _suffix(e.get("unitId", "")) != _suffix(u.get("unitId", ""))]
            if not enemies:
                continue
            tgt = min(enemies, key=lambda e: abs(e["gridX"] - u["gridX"]) + abs(e["gridY"] - u["gridY"]))
            # 1) попытка атаки картой (сервер валидирует радиус/наличие карты);
            ra = _send(conn, "wc_attack", {"controller_index": active,
                                           "attacker_unit_id": u["unitId"], "target_unit_id": tgt["unitId"],
                                           "x": tgt["gridX"], "y": tgt["gridY"]})
            if isinstance(ra, dict) and ra.get("ok") and not ra.get("error"):
                attacked += 1
            # 2) сближение на 1 клетку к ближайшему врагу.
            nx = u["gridX"] + (1 if tgt["gridX"] > u["gridX"] else -1 if tgt["gridX"] < u["gridX"] else 0)
            ny = u["gridY"] + (1 if tgt["gridY"] > u["gridY"] else -1 if tgt["gridY"] < u["gridY"] else 0)
            rm = _send(conn, "wc_free_move",
                       {"controller_index": active, "unit_id": u["unitId"], "x": nx, "y": ny})
            if isinstance(rm, dict) and rm.get("ok") and not rm.get("error"):
                moved += 1
        _send(conn, "wc_end_turn", {"controller_index": active})
        turns_done += 1
        print(f"  ход {turn}: c{active} — атак {attacked}, движений {moved}")
        time.sleep(1.5)
    return turns_done


def main() -> int:
    ts = int(time.time()) % 1000000
    u1, u2 = f"wf_a_{ts}", f"wf_b_{ts}"
    pwd = "Test1234"

    print("=== smoke_pie_full_game ===")
    print(f"users: {u1} / {u2}\n")

    ensure_test_user(u1, pwd, f"{u1}@x.com")
    ensure_test_user(u2, pwd, f"{u2}@x.com")

    conn = UnrealConnection()
    conn.connect()

    # 1. Полный стоп предыдущего PIE → старт n=2 → ждём оба PIE-мира.
    print("--- 1/8 PIE n=2 setup ---")
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(2)
    for _ in range(3):
        _send(conn, "pie_start", {"num_clients": 2})
        dl = time.monotonic() + 25
        while time.monotonic() < dl:
            if (_send(conn, "pie_status", {}) or {}).get("is_running"):
                break
            time.sleep(1.0)
        if (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        print("  pie_start не поднял PIE — повтор")
    dl = time.monotonic() + 75
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {})
        cs = st.get("clients") or []
        if st.get("num_pie_world_contexts", 0) >= 2 and len(cs) >= 2 and all(c.get("current_widget") for c in cs):
            break
        time.sleep(2.0)
    st = _send(conn, "pie_status", {})
    print(f"  PIE worlds={st.get('num_pie_world_contexts')}, clients={len(st.get('clients') or [])}")
    time.sleep(4)

    # 2. Login + FindGame → DRAFT.
    print("\n--- 2/8 login + FindGame -> DRAFT ---")
    if not _login_round(conn, u1, u2, pwd):
        print("\n=== RESULT: DRAFT NOT reached -> FAIL ===")
        return 1

    # 3. Snake-draft (10 пиков).
    print("\n--- 3/8 snake draft ---")
    total_picks = _run_snake_draft(conn)
    if total_picks < 10:
        print(f"  ПРЕДУПРЕЖДЕНИЕ: неполный драфт ({total_picks}/10) — сервер может не перейти в Deployment")

    # 4. Wait Deployment.
    print("\n--- 4/8 wait Deployment ---")
    deploy_seen = False
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        if _has_uw_any(conn, "WBP_DeploymentScreen"):
            deploy_seen = True
            break
        time.sleep(1.5)
    print(f"  deployment widget виден: {deploy_seen}")

    # 5. Deploy выбранные юниты на каждом клиенте + confirm.
    print("\n--- 5/8 deploy drafted units ---")
    for ctrl in (0, 1):
        print(f"  client {ctrl}:")
        n = _deploy_client(conn, ctrl)
        stt = _send(conn, "wc_get_deployment_state", {"controller_index": ctrl})
        print(f"    deployed={stt.get('deployed_count')} ready={stt.get('ready')} (placed={n})")
    for ctrl in (0, 1):
        r = _send(conn, "wc_confirm_deployment", {"controller_index": ctrl})
        print(f"  c{ctrl} confirm_deployment: ok={r.get('ok')}")

    # 6. Wait Battle HUD на обоих клиентах.
    print("\n--- 6/8 wait Battle HUD ---")
    battle_ctrls: list[int] = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and len(battle_ctrls) < 2:
        for ctrl in (0, 1):
            if ctrl in battle_ctrls:
                continue
            if any("BattleHUD" in c or "ActionCardHand" in c for c in _uw_classes(conn, ctrl)):
                battle_ctrls.append(ctrl)
                print(f"  battle HUD on c{ctrl}")
        time.sleep(1.5)

    if not battle_ctrls:
        print("\n--- diag: all user_widgets per client ---")
        for ctrl in (0, 1):
            tree = _tree(conn, ctrl)
            uws = tree.get("user_widgets") or []
            print(f"  c{ctrl} ({len(uws)} widgets): {[uw.get('class') for uw in uws if uw.get('is_in_viewport')]}")
        print("\n=== RESULT: battle NOT reached -> FAIL ===")
        return 1

    # Скриншот поля боя БЕЗ UI — проверяем рендер сетки + юнитов на Game_Map.
    time.sleep(2.5)  # дать /state-броадкасту заспавнить юнит-актёры
    _send(conn, "pie_screenshot", {"filename": "e2e_battle_field.png", "show_ui": False})
    print("  battle field screenshot -> e2e_battle_field.png")

    # 7. Детерминированный драйвер боя БЕЗ LLM: юниты двигаются к врагу по ходам.
    print("\n--- 7/8 deterministic battle moves (no LLM) ---")
    for ctrl in (0, 1):
        stt = _send(conn, "wc_get_battle_state", {"controller_index": ctrl})
        print(f"  c{ctrl}: my_turn={stt.get('my_turn')} ap={stt.get('ap')}/{stt.get('max_ap')}")
    units0 = _battle_units(conn, 0)
    print(f"  юнитов на доске: {len(units0)}")
    turns = _run_battle_moves(conn, max_turns=8)
    print(f"  сыграно ходов: {turns}")
    # Скриншот после боя — видно, что юниты сошлись/подрались.
    time.sleep(1.5)
    _send(conn, "pie_screenshot", {"filename": "e2e_battle_after_moves.png", "show_ui": False})
    print("  after-battle screenshot -> e2e_battle_after_moves.png")
    # Итоговое состояние юнитов (HP/живость) — видно результат атак.
    units_after = _battle_units(conn, 0)
    alive_after = [u for u in units_after if u.get("alive")]
    print(f"  юнитов после боя: всего {len(units_after)}, живых {len(alive_after)}")
    for u in units_after:
        print(f"    {u.get('unitId')}: hp={u.get('hp')} alive={u.get('alive')} @({u.get('gridX')},{u.get('gridY')})")

    # 8. Капитуляция c0 → game-over → WBP_GameResult на ОБОИХ.
    print("\n--- 8/8 surrender c0 -> expect WBP_GameResult on both ---")
    r = _send(conn, "wc_surrender", {"controller_index": 0})
    print(f"  c0 surrender: ok={r.get('ok')} surrendered={r.get('surrendered')}")

    result_ctrls: list[int] = []
    for ctrl in (0, 1):
        if _wait_widget_strict(conn, ctrl, "WBP_GameResult_C_0", timeout=30.0, label="RESULT"):
            result_ctrls.append(ctrl)

    if len(result_ctrls) < 2:
        print("\n--- diag: all user_widgets per client ---")
        for ctrl in (0, 1):
            tree = _tree(conn, ctrl)
            uws = tree.get("user_widgets") or []
            print(f"  c{ctrl}: {[uw.get('class') for uw in uws if uw.get('is_in_viewport')]}")

    ok = len(result_ctrls) == 2
    print(f"\n=== RESULT: battle={battle_ctrls} result_screen={result_ctrls} -> {'PASS' if ok else 'FAIL'} ===")

    # Оставляем PIE running для дальнейшей диагностики.
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
