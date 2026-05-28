"""E2E (Вариант B, snake draft): login 2 клиента → match → start-game → dice1 →
DRAFT (рендер каталога + snake-пики) → DEPLOYMENT (авто select-units) → dice2 →
mulligan → BATTLE. Проверяет, что цепочка фаз доходит до боя.

Запуск: из D:\\WarCard\\unreal-mcp\\Python:
    uv run python -m tests.smoke_pie_draft_flow
Требует: запущенный UnrealEditor (порт 55557) и сервер на :8081.
"""
from __future__ import annotations
import logging, sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)
from unreal_mcp_server import UnrealConnection
from tests._fixtures import ensure_test_user


def _send(conn, cmd, params, retries=4):
    last = None
    for _ in range(retries):
        try:
            r = conn.send_command(cmd, params)
            if isinstance(r, dict):
                return r.get("result", r) if r.get("status") == "success" else {"error": r.get("error"), "_raw": r}
            return {}
        except Exception as e:
            last = e
            time.sleep(0.5)
            try: conn.disconnect()
            except Exception: pass
            try: conn.connect()
            except Exception: pass
    return {"error": str(last)}


def _find_node(node, predicate):
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


# Плагин 2.17.3: get_widget_tree / invoke_button_click БЕЗ controller_index ищут
# во ВСЕХ PIE-мирах. Для драфта это надёжнее, чем резолв мира по индексу (в
# listen-server multi-world он периодически нестабилен). Имена CatalogEntryButton
# глобально уникальны, поэтому клик по имени без controller_index попадает в нужную
# кнопку (включённую — у текущего пикера). Логин остаётся per-controller (имена
# логин-виджетов одинаковы в обоих мирах).

def _tree(conn, ctrl):
    return _send(conn, "get_widget_tree", {"controller_index": ctrl})


def _uw_classes(conn, ctrl):
    tree = _tree(conn, ctrl)
    return [uw.get("class", "") for uw in (tree.get("user_widgets") or []) if uw.get("is_in_viewport")]


def _has_uw_any(conn, substr):
    """True если виджет с substr в class есть у любого из 2 клиентов."""
    for ctrl in (0, 1):
        if any(substr in c for c in _uw_classes(conn, ctrl)):
            return True
    return False


def _draft_buttons(conn):
    """Пары (controller_index, имя) для CatalogEntryButton в Scroll_AvailableUnits
    draft-виджета каждого клиента. get_widget_tree(controller_index) теперь надёжно
    резолвит мир клиента через PC->GetWorld() (плагин 2.17.4)."""
    pairs = []
    for ctrl in (0, 1):
        tree = _tree(conn, ctrl)
        for uw in (tree.get("user_widgets") or []):
            if "Draft" not in uw.get("class", ""):
                continue
            scroll = _find_node(uw.get("root"), lambda n: n.get("name") == "Scroll_AvailableUnits")
            if scroll:
                for ch in (scroll.get("children") or []):
                    if "Button" in ch.get("class", ""):
                        pairs.append((ctrl, ch.get("name")))
    return pairs


def main():
    # username сервером ограничен 3..20 символами — держим имя коротким.
    ts = int(time.time()) % 1000000
    u1, u2 = f"wd_a_{ts}", f"wd_b_{ts}"
    ensure_test_user(u1, "Test1234", f"{u1}@x.com")
    ensure_test_user(u2, "Test1234", f"{u2}@x.com")
    conn = UnrealConnection(); conn.connect()

    # Полный стоп предыдущего PIE. ListenServer поднимает 2 окна, teardown
    # асинхронный и небыстрый — если дёрнуть pie_start пока PlayWorld!=null, он
    # no-op'нет ("PIE already running"), и мир не поднимется. Ждём is_running=false.
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(2)

    # Старт с ретраями до фактического запуска PIE.
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

    # Ждём пока ПОДНИМУТСЯ ОБА PIE-мира (num_pie_world_contexts>=2). 2-й клиентский
    # мир (PIE_Client) грузится/коннектится к listen-server медленнее — на свежем
    # редакторе это десятки секунд. Без ожидания login для ctrl=1 уходит «в пустоту».
    dl = time.monotonic() + 75
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {})
        cs = st.get("clients") or []
        if st.get("num_pie_world_contexts", 0) >= 2 and len(cs) >= 2 and all(c.get("current_widget") for c in cs):
            break
        time.sleep(2.0)
    st = _send(conn, "pie_status", {})
    print(f"PIE worlds={st.get('num_pie_world_contexts')}, clients={len(st.get('clients') or [])}")
    time.sleep(4)  # дать UMG обоих миров полностью инициализироваться

    # Логин+FindGame с ретраями. Повторные клики идемпотентны: для клиента, уже
    # ушедшего с Login/MainMenu, set_text/invoke вернут "not found" (игнорируем),
    # повторный FindGame → сервер ответит "уже в поиске". Критерий успеха раунда —
    # появление WBP_Draft (значит оба залогинились и сматчились). Это устойчиво к
    # гонке загрузки 2-го мира: на «хорошем» раунде оба клиента проходят.
    draft_seen = False
    for rnd in range(5):
        for ctrl, login in [(0, u1), (1, u2)]:
            _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login, "controller_index": ctrl})
            _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": "Test1234", "controller_index": ctrl})
            _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": ctrl})
        time.sleep(3)
        for ctrl in (0, 1):
            _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": ctrl})
        # ждём DRAFT до 30с в этом раунде
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_Draft_C"):
                draft_seen = True
                break
            time.sleep(1.5)
        if draft_seen:
            print(f"DRAFT достигнут на раунде {rnd}")
            break
        print(f"  раунд {rnd}: DRAFT ещё нет — повтор login+FindGame")

    print(f"DRAFT widget виден: {draft_seen}")
    print(f"  кнопок юнитов (оба клиента): {len(_draft_buttons(conn))}")

    # Гоним snake-пики: на каждом пике перебираем (controller, имя кнопки) пока
    # один invoke_button_click не сработает. Кнопка enabled только у текущего
    # пикера (RebuildAvailablePanel: SetIsEnabled(bMyTurn)), у остальных —
    # disabled → invoke вернёт error и мы попробуем следующего. После пика сервер
    # шлёт draft-update → панель перестраивается с НОВЫМИ именами кнопок, поэтому
    # перечитываем дерево каждую попытку.
    print("\n--- snake picks ---")
    picks_done = 0
    for pick in range(10):
        done = False
        for attempt in range(30):
            # Кликаем кнопку её же controller_index. Включённая (у текущего пикера)
            # кликнется успешно; отключённые (не их ход) вернут "disabled".
            for ctrl, nm in _draft_buttons(conn):
                r = _send(conn, "invoke_button_click", {"widget_name": nm, "controller_index": ctrl})
                if isinstance(r, dict) and r.get("ok") and not r.get("error"):
                    print(f"  pick {pick}: c{ctrl} -> {nm}")
                    done = True
                    break
            if done:
                break
            time.sleep(0.8)
        if not done:
            print(f"  pick {pick}: не удалось — остановка")
            break
        picks_done += 1
        time.sleep(1.8)  # дать серверу broadcast draft-update + UI rebuild
    print(f"  пиков сделано: {picks_done}/10")

    # Poll downstream-фаз (по class-substring). После 10 пиков сервер: DEPLOYMENT
    # (клиент авто-шлёт select-units) → DICE_ROLL_2 → MULLIGAN → BATTLE.
    print("\n--- polling phase widgets (90s) ---")
    seen = set()
    dl = time.monotonic() + 90
    targets = {"Deployment": "WBP_DeploymentScreen", "Mulligan": "WBP_Mulligan", "Battle": "WBP_BattleHUD"}
    while time.monotonic() < dl:
        for label, sub in targets.items():
            if label not in seen and _has_uw_any(conn, sub):
                seen.add(label)
                print(f"  [{time.strftime('%H:%M:%S')}] {label} ({sub}) APPEARED")
        if "Battle" in seen:
            break
        time.sleep(1.5)

    print(f"\nИТОГ: draft={draft_seen}, пиков={picks_done}/10, downstream={sorted(seen)}")
    return 0 if "Battle" in seen else 1


if __name__ == "__main__":
    sys.exit(main())
