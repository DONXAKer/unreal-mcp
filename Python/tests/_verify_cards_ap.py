"""PROD-UI-003 claim 5: карты в руке затемняются, когда AP кончился.

Отличие от smoke_fullgame_solo: тот в конце сдаётся и убивает бой, а здесь бой
нужен живым — снимаем руку ДО и ПОСЛЕ траты AP и сравниваем кадры.

Поток целиком переиспользован из smoke_fullgame_solo (логин, драфт, деплой,
мульлиган), чтобы проверка не разъехалась с каноническим харнессом.

Запуск из D:\\WarCard\\unreal-mcp\\Python:
    uv run python tests/_verify_cards_ap.py

Требования: UE Editor с плагином, сервер :8081.
"""

from __future__ import annotations

import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

import logging

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_fullgame_solo import (
    _battle_units,
    _pie_restart_n1,
    _ue_draft,
    _ue_login_findgame,
    _wait_pie_widget,
    _wait_uw,
)
from tests.smoke_pie_full_game import (
    UNITS_TO_PICK,
    _deploy_client,
    _has_uw_any,
    _send,
    _suffix,
)
from unreal_mcp_server import UnrealConnection

SHOTS = []


def _shot(conn, name: str) -> None:
    r = _send(conn, "pie_screenshot", {"filename": name, "show_ui": True,
                                       "controller_index": 0})
    path = (r or {}).get("assetPath") or (r or {}).get("filename") or name
    SHOTS.append((name, path))
    print("    кадр: %s -> %s" % (name, path))
    time.sleep(1.5)  # захват асинхронный, отдаём тик-другой на запись файла


def _ap(conn) -> int:
    st = _send(conn, "wc_get_battle_state", {"controller_index": 0}) or {}
    # Имя поля не угадываем: печатаем весь стейт при первом чтении.
    for key in ("ap_remaining", "apRemaining", "ap", "currentAP", "current_ap"):
        if key in st:
            return int(st[key])
    print("    ВНИМАНИЕ: поле AP не найдено, стейт = %s" % st)
    return -1


def _my_turn(conn) -> bool:
    st = _send(conn, "wc_get_battle_state", {"controller_index": 0}) or {}
    return bool(st.get("my_turn"))


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = "vc_ue_%d" % ts, "vc_bot_%d" % ts
    pwd = "Test1234"

    print("=== verify_cards_ap (AP=0 -> карты затемнены) ===")
    ensure_test_user(ue_user, pwd, "%s@x.com" % ue_user)
    ensure_test_user(bot_user, pwd, "%s@x.com" % bot_user)

    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("=== FAIL: бот не подключился ===")
        return 1
    time.sleep(2.0)

    conn = UnrealConnection()
    conn.connect()

    try:
        print("\n--- 1 PIE ---")
        if not _pie_restart_n1(conn):
            print("=== FAIL: PIE ===")
            return 1

        print("\n--- 2 login + FindGame ---")
        if not _ue_login_findgame(conn, ue_user, pwd):
            print("=== FAIL: DRAFT не достигнут ===")
            return 1

        print("\n--- 3 draft ---")
        _ue_draft(conn)

        print("\n--- 4 deployment ---")
        dl = time.monotonic() + 45
        while time.monotonic() < dl and not _has_uw_any(conn, "WBP_DeploymentScreen"):
            time.sleep(1.5)
        n = _deploy_client(conn, 0)
        print("  расставлено: %d/%d" % (n, UNITS_TO_PICK))
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        print("\n--- 4b mulligan ---")
        if _wait_pie_widget(conn, ("Mulligan",), 40, "mulligan"):
            _send(conn, "invoke_button_click",
                  {"widget_name": "AcceptHandButton", "controller_index": 0})

        print("\n--- 5 ждём BATTLE ---")
        if not (_wait_pie_widget(conn, ("BattleHUD",), 90, "battle")
                or _wait_uw(conn, "WBP_BattleHUD", 30, "battle-tree")):
            print("=== FAIL: бой не начался ===")
            return 1

        print("\n--- 6 ждём свой ход ---")
        dl = time.monotonic() + 60
        while time.monotonic() < dl and not _my_turn(conn):
            time.sleep(1.5)
        if not _my_turn(conn):
            print("=== FAIL: ход так и не настал ===")
            return 1

        ap0 = _ap(conn)
        print("  AP в начале хода: %s" % ap0)
        _shot(conn, "cards_ap_full.png")

        print("\n--- 7 тратим AP до нуля ---")
        # Успех НЕ определяем по ответу команды: `ok` означает «доставлено в
        # клиент», а не «сервер принял». Первый заход так и провалился — 12 атак
        # вернули ok, а сервер все отклонил по OUT_OF_RANGE, и AP не двинулся.
        # Единственный надёжный признак — AP реально уменьшился.
        #
        # Тратим движением, а не атакой: атака требует цели в радиусе, движение
        # стоит 1 AP всегда (бюджет хода — 3 клетки, ровно 3 AP).
        spent = 0
        for _ in range(20):
            # Ход «мигает»: бот пасует, и между нашими действиями ход успевает
            # уйти и вернуться. Прошлый заход выходил по первой же False —
            # и завершался с «мой ход: True» в итоге. Поэтому ЖДЁМ возврата,
            # а не сдаёмся.
            if not _my_turn(conn):
                wait_dl = time.monotonic() + 40
                while time.monotonic() < wait_dl and not _my_turn(conn):
                    time.sleep(1.5)
                if not _my_turn(conn):
                    print("  ход не вернулся за 40s — выходим")
                    break
            ap_before = _ap(conn)
            if ap_before <= 0:
                break

            units = [u for u in _battle_units(conn, 0) if u.get("alive")]
            mine = [u for u in units if _suffix(u.get("unitId", "")) == "red"]
            occupied = {(u["gridX"], u["gridY"]) for u in units}

            moved = False
            for u in mine:
                for dx, dy in ((1, 0), (0, 1), (0, -1), (-1, 0)):
                    nx, ny = u["gridX"] + dx, u["gridY"] + dy
                    if not (0 <= nx <= 7 and 0 <= ny <= 7) or (nx, ny) in occupied:
                        continue
                    _send(conn, "wc_free_move",
                          {"controller_index": 0, "unit_id": u["unitId"],
                           "x": nx, "y": ny})
                    time.sleep(1.2)
                    if _ap(conn) < ap_before:
                        spent += 1
                        moved = True
                        print("    %s -> (%d,%d), AP %d -> %d"
                              % (u["unitId"], nx, ny, ap_before, _ap(conn)))
                        break
                if moved:
                    break
            if not moved:
                print("  ни одно движение не сняло AP — выходим")
                break

        ap1 = _ap(conn)
        print("  действий выполнено: %d, AP стало: %s, мой ход: %s"
              % (spent, ap1, _my_turn(conn)))
        _shot(conn, "cards_ap_spent.png")

        print("\n=== ИТОГ: AP %s -> %s; кадры: %s ==="
              % (ap0, ap1, ", ".join(n for n, _ in SHOTS)))
        return 0
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
