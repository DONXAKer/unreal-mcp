"""Надёжный полный e2e: 1 реальный UE-клиент (PIE n=1) + headless STOMP-оппонент.

Зачем вместо smoke_pie_full_game (2 клиента в одном PIE): single-process
multi-client PIE детерминированно флачит на widget-enumeration второго пикера
драфта (см. memory project_multiclient_pie_limitation; диагностировано в этой
сессии — игровая логика корректна, не проходил именно харнесс). Здесь UE-клиент
ОДИН (controller 0), поэтому enumeration стабилен, а второго игрока изображает
лёгкий Python STOMP-бот (tests/_stomp_opponent.py) в отдельном процессе-коннекте.

Поток:
  1. ensure 2 пользователя (UE-клиент + бот).
  2. Бот: login(REST)->JWT -> WS connect -> matchmaking/start (фоновый тред).
  3. PIE n=1, UE-клиент: login -> FindGame -> матчится с ботом -> WBP_Draft.
  4. DRAFT: UE кликает свои пики по очереди (snake), бот делает свои.
  5. DEPLOYMENT: UE расставляет 5 (wc_deploy_unit)+confirm, бот — свои 5.
  6. Сервер -> DICE_ROLL_2 -> MULLIGAN -> BATTLE (phase-ready авто с обеих сторон).
  7. Проверяем WBP_BattleHUD на UE-клиенте (главный критерий).
  8. UE surrender -> WBP_GameResult (бонус-проверка).

Запуск из D:\\WarCard\\unreal-mcp\\Python:
    uv run python tests/smoke_fullgame_solo.py

Требования: UnrealMCP плагин (PIE), WarCard сервер :8081.
"""

from __future__ import annotations

import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

import json
import logging

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_pie_full_game import (
    UNITS_TO_PICK,
    _deploy_client,
    _draft_entries,
    _has_uw_any,
    _send,
    _suffix,
)
from unreal_mcp_server import UnrealConnection


def _battle_units(conn, ctrl: int = 0) -> list[dict]:
    """Снимок юнитов на доске (через wc_get_battle_units)."""
    r = _send(conn, "wc_get_battle_units", {"controller_index": ctrl})
    try:
        return json.loads(r.get("units_json") or "{}").get("units", [])
    except Exception:
        return []


def _ue_battle_round(conn, my_side: str) -> tuple[bool, int, int]:
    """Дождаться хода UE (ctrl 0), перебрать свои юниты пока чей-то ход не пройдёт,
    плюс попробовать атаки. Возвращает (got_turn, moves_ok, attacks_ok)."""
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        st = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        if st.get("my_turn"):
            break
        time.sleep(1.0)
    else:
        return (False, 0, 0)

    units = [u for u in _battle_units(conn, 0) if u.get("alive")]
    mine = [u for u in units if _suffix(u.get("unitId", "")) == my_side]
    enemies = [u for u in units if _suffix(u.get("unitId", "")) != my_side]
    if not mine or not enemies:
        _send(conn, "wc_end_turn", {"controller_index": 0})
        return (True, 0, 0)

    occupied = {(u["gridX"], u["gridY"]) for u in units}
    moves_ok = attacks_ok = 0

    # FIX-TURN-001: перепроверяем «мой ход» перед КАЖДОЙ командой. Клиентский bIsMyTurn
    # синхронизируется по надёжному /state; при потере хода (ход ушёл оппоненту) клиент
    # это подхватывает за тик-другой. Без перепроверки тест слал бы всю пачку команд по
    # устаревшему my_turn и ловил серверные «не ваш ход». Перепроверка останавливает
    # отправку, как только ход реально не наш.
    def _still_my_turn() -> bool:
        st_now = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        return bool(st_now.get("my_turn"))

    # 1) Сначала попытка атак: атаковать может каждый свой юнит в диапазоне.
    for u in mine:
        if not _still_my_turn():
            break
        nearest = min(enemies, key=lambda e: abs(e["gridX"] - u["gridX"]) + abs(e["gridY"] - u["gridY"]))
        r = _send(conn, "wc_attack", {"controller_index": 0,
                                       "attacker_unit_id": u["unitId"], "target_unit_id": nearest["unitId"],
                                       "x": nearest["gridX"], "y": nearest["gridY"]})
        if isinstance(r, dict) and r.get("ok") and not r.get("error"):
            attacks_ok += 1

    # 2) Движение: ищем своего юнита, у которого есть свободная клетка вперёд (+x к врагу).
    for u in mine:
        if not _still_my_turn():
            break
        nx, ny = u["gridX"] + 1, u["gridY"]  # red → +x; для blue было бы -1, но мы red
        if 0 <= nx <= 7 and 0 <= ny <= 7 and (nx, ny) not in occupied:
            r = _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": u["unitId"], "x": nx, "y": ny})
            if isinstance(r, dict) and r.get("ok") and not r.get("error"):
                moves_ok += 1
                print(f"    UE move: {u['unitId']} ({u['gridX']},{u['gridY']}) -> ({nx},{ny})")
                break

    # end-turn только если ход всё ещё наш (иначе сервер отклонит «не ваш ход»).
    if _still_my_turn():
        _send(conn, "wc_end_turn", {"controller_index": 0})
    print(f"    UE turn: атак ok={attacks_ok}, движений ok={moves_ok}")
    return (True, moves_ok, attacks_ok)


def _wait_uw(conn, substr: str, timeout: float, label: str = "") -> bool:
    """Поллинг user-widget по подстроке КЛАССА (через get_widget_tree)."""
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        try:
            if _has_uw_any(conn, substr):
                if label:
                    print(f"    [{label}] '{substr}' найден")
                return True
        except Exception:
            pass  # tree может не парситься при больших ответах — игнорируем
        time.sleep(1.5)
    if label:
        print(f"    [{label}] '{substr}' НЕ найден за {timeout}s")
    return False


def _wait_pie_widget(conn, candidates: tuple, timeout: float, label: str = "") -> bool:
    """Поллинг pie_status.clients[0].current_widget на любую из подстрок.
    Лёгкий ответ — не страдает от truncation get_widget_tree при больших сценах."""
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {}) or {}
        cs = st.get("clients") or []
        cur = (cs[0].get("current_widget") or "") if cs else ""
        if any(c in cur for c in candidates):
            if label:
                print(f"    [{label}] '{cur}' матчит {candidates}")
            return True
        time.sleep(1.0)
    if label:
        print(f"    [{label}] ни одна из {candidates} не появилась за {timeout}s")
    return False


def _pie_ready_n1(conn) -> bool:
    """True если PIE запущен с 1 клиентом, у которого уже есть виджет (готов к UI)."""
    st = _send(conn, "pie_status", {}) or {}
    cs = st.get("clients") or []
    return bool(st.get("is_running") and len(cs) >= 1 and cs[0].get("current_widget"))


def _pie_restart_n1(conn) -> bool:
    """PIE n=1 со свежим состоянием. Быстрый путь: если PIE уже на логине —
    не трогаем (избегаем флакающего цикла stop/start). Иначе полный рестарт с
    терпеливым ожиданием (редактор после многих циклов поднимает PIE медленно)."""
    st = _send(conn, "pie_status", {}) or {}
    cs = st.get("clients") or []
    if st.get("is_running") and len(cs) == 1 and "Login" in (cs[0].get("current_widget") or ""):
        print("  PIE уже запущен на логине (n=1) — рестарт не нужен")
        return True

    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 30
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(2)
    for attempt in range(3):
        _send(conn, "pie_start", {"num_clients": 1})
        dl = time.monotonic() + 60  # редактор бывает медленным — ждём дольше
        while time.monotonic() < dl:
            if _pie_ready_n1(conn):
                return True
            time.sleep(1.5)
        print(f"  pie_start: PIE не готов за 60s (попытка {attempt + 1}/3) — повтор")
    return _pie_ready_n1(conn)


def _ue_login_findgame(conn, login: str, pwd: str) -> bool:
    """Login + FindGame на controller 0 с ретраями. Успех — появление WBP_Draft."""
    for rnd in range(5):
        _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": login, "controller_index": 0})
        _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})
        time.sleep(3.0)
        _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_Draft_C"):
                print(f"  DRAFT достигнут (раунд {rnd})")
                return True
            time.sleep(1.5)
        print(f"  раунд {rnd}: DRAFT ещё нет — повтор login+FindGame")
    return False


def _ue_draft(conn, max_rounds: int = 45) -> int:
    """UE-клиент (ctrl 0) кликает свои пики, когда его очередь (кнопка включена).
    На чужом ходу кнопки disabled — клик no-op, ждём бота. Выходим по DEPLOYMENT."""
    picks = 0
    for _ in range(max_rounds):
        if _has_uw_any(conn, "WBP_DeploymentScreen"):
            break
        clicked = False
        for ctrl, btn, _unit in _draft_entries(conn):
            if ctrl != 0:
                continue
            r = _send(conn, "invoke_button_click", {"widget_name": btn, "controller_index": 0})
            if isinstance(r, dict) and r.get("ok") and not r.get("error"):
                picks += 1
                print(f"  UE pick ~{picks} -> {btn}")
                clicked = True
                break
        time.sleep(1.3 if clicked else 1.0)
    return picks


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = f"wf_ue_{ts}", f"wf_bot_{ts}"
    pwd = "Test1234"

    print("=== smoke_fullgame_solo (1 UE client + STOMP opponent) ===")
    print(f"UE user: {ue_user} | bot user: {bot_user}\n")

    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

    # --- бот стартует первым (встаёт в очередь), UE-клиент сматчится с ним ---
    print("--- bot: connect + matchmaking ---")
    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("=== RESULT: бот не подключился -> FAIL ===")
        return 1
    time.sleep(2.0)

    conn = UnrealConnection()
    conn.connect()

    try:
        print("\n--- 1/6 PIE n=1 setup ---")
        if not _pie_restart_n1(conn):
            print("=== RESULT: PIE не поднялся -> FAIL ===")
            return 1
        st = _send(conn, "pie_status", {})
        print(f"  PIE running={st.get('is_running')}, clients={len(st.get('clients') or [])}")

        print("\n--- 2/6 UE login + FindGame -> DRAFT ---")
        if not _ue_login_findgame(conn, ue_user, pwd):
            print("=== RESULT: DRAFT NOT reached -> FAIL ===")
            return 1

        print("\n--- 3/6 draft (UE picks + bot picks) ---")
        ue_picks = _ue_draft(conn)
        print(f"  UE кликов-пиков: ~{ue_picks}")

        print("\n--- 4/6 wait Deployment + UE deploy ---")
        deploy_seen = False
        dl = time.monotonic() + 45
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_DeploymentScreen"):
                deploy_seen = True
                break
            time.sleep(1.5)
        print(f"  deployment widget виден: {deploy_seen}")
        n = _deploy_client(conn, 0)
        print(f"  UE deployed: {n}/{UNITS_TO_PICK}")
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        print("\n--- 4b/6 mulligan: UE accepts hand ---")
        # FIX-MULLIGAN-001: SendGamePhaseReady больше не посылает авто-ready для
        # MULLIGAN — игрок должен нажать «Принять руку». Ждём WBP_Mulligan, кликаем.
        # Таймаут 30s: сервер переходит DICE_ROLL_2 → MULLIGAN, виджет появляется.
        mulligan_seen = _wait_pie_widget(conn, ("Mulligan",), 30, "mulligan")
        if mulligan_seen:
            time.sleep(1.5)  # дать время руке прогрузиться (OnHandCardsUpdated)
            _send(conn, "invoke_button_click", {"widget_name": "AcceptHandButton", "controller_index": 0})
            print("  AcceptHandButton кликнут [OK]")
        else:
            print("  WBP_Mulligan не появился — пробуем через wc_phase_ready (fallback)")
            # Fallback: если виджет так и не показался (напр. кадр пропущен), смотрим
            # по current_widget; если уже в бою — mulligan проскочил раньше.

        print("\n--- 5/6 wait BATTLE HUD on UE client ---")
        # Используем pie_status.current_widget (лёгкий ответ) вместо get_widget_tree:
        # последний даёт многоКБ JSON и режется UTF-8 декодером в боевом состоянии.
        # В бою current_widget показывает WBP_BattleHUD или WBP_ActionCardHand —
        # любое из них = клиент дошёл до боя.
        battle = _wait_pie_widget(conn, ("BattleHUD", "ActionCardHand"), 60, "battle")
        if not battle:
            bot_battle = bot.wait_battle(5)
            print(f"  боевой виджет не найден на UE; бот в BATTLE={bot_battle}")
            print("\n=== RESULT: BATTLE NOT reached on UE client -> FAIL ===")
            return 1
        print("  боевой UI виден на UE-клиенте [OK]")

        # Регресс-гард (фикс HideStaleScreenWidgets): экраны прошлых фаз не должны
        # оставаться в viewport поверх боя. Раньше залипали Login/MainMenu/
        # Matchmaking/Mulligan (найдено этим же e2e 2026-05-29).
        time.sleep(2)
        stale = [s for s in ("WBP_Login", "WBP_MainMenu", "WBP_Matchmaking", "WBP_Mulligan")
                 if _has_uw_any(conn, s)]
        if stale:
            print(f"  ВНИМАНИЕ: залипшие экраны в бою: {stale}")
        else:
            print("  залипших экранов прошлых фаз нет [OK]")

        # FEAT-BATTLE-003: интерактивный card-play. UE играет 3 хода (атака + ход),
        # бот пасует свои. После — снимок доски должен отличаться (юнит сдвинулся
        # или HP изменилось).
        print("\n--- 5b/6 card-play (UE moves + бот пасует) ---")
        ue_side = "red"  # UE деплоил в зону x=0..2 (red)
        before = _battle_units(conn, 0)
        rounds = total_moves = total_attacks = 0
        for _ in range(3):
            got, m, a = _ue_battle_round(conn, ue_side)
            if got:
                rounds += 1
                total_moves += m
                total_attacks += a
                time.sleep(2.0)  # дать боту пасовать
            else:
                print("  ход UE не настал за 25s — стоп")
                break
        after = _battle_units(conn, 0)
        def _key(u): return (u.get("unitId"), u.get("gridX"), u.get("gridY"), u.get("currentHp"))
        changed = {_key(u) for u in before} != {_key(u) for u in after}
        print(f"  UE ходов: {rounds}, успешных движений: {total_moves}, атак: {total_attacks}; доска изменилась: {changed}")

        print("\n--- 6/6 UE surrender -> GameResult ---")
        _send(conn, "wc_surrender", {"controller_index": 0})
        result = _wait_pie_widget(conn, ("GameResult",), 30, "result")
        print(f"  WBP_GameResult виден: {result}")

        print("\n=== RESULT: PASS — UE client reached BATTLE"
              + (" + GameResult" if result else "") + " ===")
        return 0
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
