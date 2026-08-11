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

SHOTS: list[tuple[str, str]] = []


def _shot(conn, name: str) -> bool:
    """Снять кадр PIE. False — кадра нет, и это провал проверки, а не мелочь.

    Прежний вариант при отказе захвата подставлял само имя файла как «путь» и
    печатал правдоподобное «кадр: X -> X»: тест завершался бодрым ИТОГом с
    кодом 0, не сняв ни одного кадра, — тот самый молчаливый успех, против
    которого написан комментарий в цикле траты AP.
    """
    r = _send(conn, "pie_screenshot", {"filename": name, "show_ui": True,
                                       "controller_index": 0})
    path = (r or {}).get("assetPath")
    if not path:
        print(f"    ВНИМАНИЕ: кадр {name} НЕ снят, ответ={r}")
        return False
    SHOTS.append((name, path))
    print(f"    кадр: {name} -> {path}")
    time.sleep(1.5)  # захват асинхронный, отдаём тик-другой на запись файла
    return True


# Порядок ЗНАЧИМ: _ap берёт первое совпавшее поле. Достоверные имена идут
# первыми, а ненадёжное `ap` — последним: сырой мост кладёт его даже когда
# рефлексивный GetCurrentAP не удался (см. предупреждение в _ap), поэтому при
# наличии обоих полей верить надо ActionPoints, а не заглушке.
# Список сокращён до имён, которые ДЕЙСТВИТЕЛЬНО встречаются в дереве:
# ap_remaining / currentAP / current_ap не отдаёт ни мост, ни офиц. toolset, а
# лишнее имя опасно — под ним может однажды приехать чужая величина.
AP_KEYS = ("ActionPoints", "actionPoints", "apRemaining", "ap")

# То же для признака хода: сырой мост отдаёт `my_turn`, официальный toolset
# UE 5.8 — `bMyTurn`. Асимметрия с _ap уже стоила ложного «ход не вернулся»:
# _ap продолжал работать, а _my_turn вечно возвращал False.
TURN_KEYS = ("bMyTurn", "myTurn", "my_turn")

_UNKNOWN = -1


def _battle_state(conn) -> dict[str, object]:
    """Состояние боя или {} при неудачном чтении, с ретраями по таймауту моста.

    Ретраи ЗДЕСЬ, а не в _ap: `_my_turn` читает то же самое состояние, и без
    общей защиты фикс выходил асимметричным — таймаут превращался в «сейчас не
    мой ход», и цикл ожидания честно досиживал до дедлайна при живом ходе.

    Ретраим и на ПУСТОМ ответе: `_send` отдаёт {} когда редактор не принял
    коннект (send_command вернул None). Ключа `error` там нет, поэтому прежняя
    проверка считала пустой словарь успехом и уходила без единой повторной
    попытки — самый частый режим отказа не ретраился вовсе, а диагностика
    молчала.
    """
    for attempt in range(3):
        st = _send(conn, "wc_get_battle_state", {"controller_index": 0}) or {}
        if st and not st.get("error"):
            return st
        time.sleep(1.0 + attempt)
    print("    ВНИМАНИЕ: состояние боя не прочитано за 3 попытки")
    return {}


def _ap(conn) -> int:
    """Текущий AP или -1 («неизвестно»), но НЕ 0 — это разные вещи.

    Имя поля не одно: сырой мост отдаёт `ap`, официальный toolset UE 5.8 —
    `ActionPoints`; жёсткая привязка к `ap` молча вернула бы «неизвестно» после
    перевода харнесса на unreal-ue58.

    ВАЖНО: 0 в ответе НЕ доказывает, что AP исчерпан. `HandleGetBattleState`
    инициализирует `int32 AP = 0` и кладёт поле даже когда рефлексивный вызов
    `GetCurrentAP` не удался (WarCardGameCommands.cpp:516-522), поэтому сбой
    подсистемы выглядит как честный ноль. Отличить их отсюда нельзя — вызывающий
    обязан подтверждать «AP кончился» вторым признаком, а не одним этим числом.
    """
    st = _battle_state(conn)
    for key in AP_KEYS:
        if key not in st:
            continue
        value = st[key]
        # bool исключаем явно: в Python bool — наследник int, и булев флаг,
        # приехавший под именем AP, прошёл бы как «настоящие 0 или 1».
        # Строку, наоборот, принимаем: прежний `int(st["ap"])` её терпел, а
        # переход на isinstance(int, float) начал отбраковывать — обёртка
        # официального toolset вполне может отдать "3".
        if isinstance(value, bool):
            continue
        if isinstance(value, (int, float)):
            return int(value)
        if isinstance(value, str):
            try:
                return int(value.strip())
            except ValueError:
                continue
    if st:
        print(f"    ВНИМАНИЕ: в состоянии боя нет ни одного из полей AP: {st}")
    return _UNKNOWN


def _my_turn(conn) -> bool:
    st = _battle_state(conn)
    for key in TURN_KEYS:
        if key in st:
            return bool(st[key])
    return False


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = f"vc_ue_{ts}", f"vc_bot_{ts}"
    pwd = "Test1234"

    print("=== verify_cards_ap (AP=0 -> карты затемнены) ===")
    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

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
        print(f"  расставлено: {n}/{UNITS_TO_PICK}")
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
        print(f"  AP в начале хода: {ap0}")
        if ap0 <= 0:
            print(f"=== FAIL: AP на старте {ap0} — сравнивать не с чем ===")
            return 1
        if not _shot(conn, "cards_ap_full.png"):
            print("=== FAIL: кадр 'AP полный' не снят ===")
            return 1

        print("\n--- 7 тратим AP до нуля ---")
        # Успех НЕ определяем по ответу команды: `ok` означает «доставлено в
        # клиент», а не «сервер принял». Первый заход так и провалился — 12 атак
        # вернули ok, а сервер все отклонил по OUT_OF_RANGE, и AP не двинулся.
        # Единственный надёжный признак — AP реально уменьшился.
        #
        # Тратим движением, а не атакой: атака требует цели в радиусе. Но НЕ
        # «движение стоит 1 AP всегда» — GameConstants.MOVE_AP_COST даёт
        # HEAVY_TECH = 2, поэтому на танковом ростере можно застрять на AP=1,
        # где рука ещё активна. Цикл поэтому крутится до нуля, а не до трёх
        # успешных ходов, и выход по «ничего не прошло» считается провалом.
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
            if ap_before == _UNKNOWN:
                print("  AP не читается — состояние неизвестно, прерываем")
                return 1
            if ap_before == 0:
                # Ноль ДО первой траты — не «дошли до нуля», а подозрительное
                # состояние: `HandleGetBattleState` кладёт AP = 0 и когда
                # рефлексивный GetCurrentAP не удался. Раньше этот выход
                # проскакивал финальный гард `ap1 != 0` и давал успех, не
                # потратив ни одного очка, — кадр «AP потрачен» снимался при
                # полной активной руке.
                if spent == 0:
                    print(f"=== FAIL: AP упал в 0 без единой траты (было {ap0}) — "
                          "похоже на сбой чтения AP, а не на исчерпание ===")
                    return 1
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
                    # Читаем ОДИН раз и сравниваем это же значение: прежде AP
                    # читался дважды (в условии и в print), и в лог уходило
                    # третье значение, которого не видел ни один из сравнений.
                    ap_after = _ap(conn)
                    # -1 («неизвестно») НЕ доказательство траты: раньше -1 < 3
                    # было истинно, и отклонённый сервером ход засчитывался.
                    if ap_after != _UNKNOWN and ap_after < ap_before:
                        spent += 1
                        moved = True
                        print(f"    {u['unitId']} -> ({nx},{ny}), "
                              f"AP {ap_before} -> {ap_after}")
                        break
                if moved:
                    break
            if not moved:
                print("  ни одно движение не сняло AP — выходим")
                break

        ap1 = _ap(conn)
        print(f"  действий выполнено: {spent}, AP стало: {ap1}, "
              f"мой ход: {_my_turn(conn)}")
        if not _shot(conn, "cards_ap_spent.png"):
            print("=== FAIL: кадр 'AP потрачен' не снят ===")
            return 1

        # Проверка обязана уметь провалиться. Раньше main() возвращал 0 на всех
        # путях после начала боя, и прогон с «AP 3 -> 3» рапортовал успех, хотя
        # проверяемое свойство (карты гаснут при AP=0) не подтверждалось ничем.
        #
        # Два разных провала — две разные причины, и различать их обязательно:
        # «ни одной траты» указывает на сломанный канал команд, а «застряли на
        # ненулевом AP» — на ростер, которым до нуля не дойти движением
        # (GameConstants.MOVE_AP_COST: HEAVY_TECH = 2 при бюджете 3). Второе —
        # ограничение харнесса, а не игры, и валить его молча под общим
        # сообщением значило бы каждый раз заново искать несуществующий баг.
        if spent == 0:
            print(f"\n=== FAIL: AP {ap0} -> {ap1}, ни одно действие не сняло AP — "
                  "проверять затемнение руки не на чем ===")
            return 1
        if ap1 != 0:
            print(f"\n=== FAIL: AP {ap0} -> {ap1} за {spent} действий, до нуля не дошли. "
                  "Кадр 'AP потрачен' снят при активной руке. Вероятная причина — "
                  "ростер из тяжёлой техники (ход стоит 2 AP при бюджете 3): "
                  "нужен пехотный ростер либо трата картой ===")
            return 1

        print("\n=== ИТОГ: AP {} -> {}; кадры: {} ===".format(ap0, ap1, ", ".join(n for n, _ in SHOTS)))
        return 0
    finally:
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
