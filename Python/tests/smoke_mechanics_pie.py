# -*- coding: utf-8 -*-
"""TEST-MECH-003: ПОЛНОЦЕННАЯ игра в PIE с проверкой механик.

Цель (goal): в PIE отыгрывается полная партия, в которой:
  * участвует КАЖДЫЙ юнит (все 5 совершают действие);
  * учитываются AP (расход по типу юнита, сброс по ходу);
  * учитываются ограничения ландшафта (HEAVY не входит в лес);
  * НЕЛЬЗЯ ходить несколько раз (один мув на юнита, один юнит за ход);
  * отыгрываются карты: BASIC/SPECIAL атаки, AIR_STRIKE, HEAL (на раненого),
    REPAIR, ARTILLERY_STRIKE.

Схема: 1 UE-клиент в PIE (wc_* команды) + минимальный STOMP-бот-оппонент
(smoke_fullgame_solo-стек) + STOMP-шпион на /state и /result для числовых
ассертов (AP, HP, позиции, серверные строки отказов).

Запуск: cd D:\\WarCard\\unreal-mcp\\Python && uv run python tests/smoke_mechanics_pie.py
Требования: Editor с PIE-мостом :55557, сервер :8081.
"""
from __future__ import annotations

import json
import queue
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0].rsplit("/tests", 1)[0]))

import logging

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot, StompWs
from tests.smoke_fullgame_solo import (
    _battle_units,
    _deploy_client,
    _pie_restart_n1,
    _send,
    _suffix,
    _ue_draft,
    _ue_login_findgame,
    _wait_pie_widget,
)
from unreal_mcp_server import UnrealConnection

RESULTS: list[tuple[str, str, str, bool]] = []

HEAVY_IMPASSABLE = {"FOREST", "SWAMP", "TRENCH"}


def check(op: str, expected: str, actual: str, ok: bool) -> None:
    RESULTS.append((op, expected, actual, ok))
    print(f"  [{'PASS' if ok else 'FAIL'}] {op}: ожидание [{expected}] факт [{actual}]", flush=True)


class Spy:
    """STOMP-шпион: снапшоты /state и /result игры (jwt бота, отдельный сокет)."""

    def __init__(self, jwt: str):
        self.ws = StompWs("127.0.0.1", 8081)
        self.state_q: queue.Queue = queue.Queue()
        self.result_q: queue.Queue = queue.Queue()
        self.last_state: dict | None = None
        self.bot_hand: list | None = None
        self.ws.on_message = self._route
        if not self.ws.connect(jwt):
            raise RuntimeError("Spy: connect failed")

    def _route(self, dest: str, body: dict) -> None:
        if "handCards" in body:
            self.bot_hand = body.get("handCards") or []
        elif body.get("gamePhase") is not None:
            self.last_state = body
            self.state_q.put(body)
        elif body.get("commandType") is not None:
            self.result_q.put(body)

    def sub(self, game_id: str) -> None:
        self.ws.subscribe(f"/topic/game/{game_id}/state")
        self.ws.subscribe(f"/topic/game/{game_id}/result")
        # рука БОТА (для bot_wound: бот по-настоящему атакует наш юнит)
        self.ws.subscribe(f"/user/topic/game/{game_id}/cards-update")

    def fresh_state(self, timeout: float = 8.0) -> dict | None:
        """Последний снапшот, дождавшись хотя бы одного нового."""
        try:
            s = self.state_q.get(timeout=timeout)
        except queue.Empty:
            return self.last_state
        while True:
            try:
                s = self.state_q.get_nowait()
            except queue.Empty:
                return s

    def drain(self) -> None:
        for q in (self.state_q, self.result_q):
            while True:
                try:
                    q.get_nowait()
                except queue.Empty:
                    break

    def wait_result_containing(self, substr: str, timeout: float = 8.0) -> dict | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                r = self.result_q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                return None
            if substr in str(r.get("message", "")):
                return r
        return None

    def wait_result_type(self, command_type: str, timeout: float = 8.0) -> dict | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                r = self.result_q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                return None
            if r.get("commandType") == command_type:
                return r
        return None

    def wait_state_until(self, pred, timeout: float = 10.0) -> dict | None:
        """Читает снапшоты, пока pred(state) не станет истинным (защита от
        чтения «предыдущего» снапшота: select и execute шлют по broadcast'у,
        и нужный приходит вторым). Возвращает подошедший снапшот или None."""
        if self.last_state and pred(self.last_state):
            return self.last_state
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                s = self.state_q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                return None
            if pred(s):
                return s
        return None


class UESpy:
    """Второй STOMP-сокет от имени UE-игрока: видит его руку (cards-update
    рассылается всем сессиям юзера) и может сбрасывать карты (discard-card
    завершает ход и добирает новую — прокрутка руки до нужного типа)."""

    def __init__(self, jwt: str):
        self.jwt = jwt
        self.hand: list | None = None
        self.last_update = time.time()
        self.game_id = ""
        self.ws = self._connect()

    def _connect(self) -> StompWs:
        ws = StompWs("127.0.0.1", 8081)
        ws.on_message = self._route
        if not ws.connect(self.jwt):
            raise RuntimeError("UESpy: connect failed")
        return ws

    def _route(self, dest: str, body: dict) -> None:
        if "handCards" in body:
            self.hand = body.get("handCards") or []
            self.last_update = time.time()

    def sub(self, game_id: str) -> None:
        self.game_id = game_id
        self.ws.subscribe(f"/user/topic/game/{game_id}/cards-update")

    def ensure_fresh(self, max_age: float = 25.0) -> None:
        """Watchdog: cards-update идёт на каждый end-turn; застой = сокет тихо
        умер — переподключаемся (рука придёт со следующим событием)."""
        if time.time() - self.last_update <= max_age:
            return
        print("  [UESPY] рука устарела — reconnect", flush=True)
        try:
            self.ws.close()
        except Exception:
            pass
        self.ws = self._connect()
        if self.game_id:
            self.ws.subscribe(f"/user/topic/game/{self.game_id}/cards-update")
        self.last_update = time.time()

    def has_type(self, card_type: str) -> bool:
        return any(c.get("actionType") == card_type for c in (self.hand or []))

    def discard_junk(self, reserved: set) -> str | None:
        """Сбросить первую карту, чей тип не в reserved (завершает ход!)."""
        for c in self.hand or []:
            if c.get("actionType") not in reserved:
                self.ws.send_json("/app/game/discard-card",
                                  {"gameId": self.game_id, "actionCardId": c.get("id")})
                return str(c.get("actionType"))
        return None


def units_map(state: dict) -> dict[str, dict]:
    return {u["unitId"]: u for u in (state.get("units") or [])}


def upos(u: dict) -> tuple[int, int]:
    return int(u.get("x", u.get("gridX", -1))), int(u.get("y", u.get("gridY", -1)))


def uhp(u: dict) -> int:
    return int(u.get("currentHp", u.get("health", -1)))


def terrain_at(state: dict, x: int, y: int) -> str:
    terrain = state.get("terrain") or []
    width = int(state.get("fieldWidth", 8) or 8)
    idx = y * width + x
    return str(terrain[idx]) if 0 <= idx < len(terrain) else "?"


def wait_my_turn(conn, timeout: float = 30.0) -> bool:
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        st = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        if st.get("my_turn"):
            return True
        time.sleep(1.0)
    return False


def battle_ready(conn, timeout: float = 45.0) -> bool:
    """BATTLE достигнут: боевой стейт отвечает и на доске есть юниты.
    (current_widget ненадёжен — WBP_Mulligan может остаться верхним в стеке.)"""
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        st = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        if isinstance(st, dict) and "my_turn" in st:
            units = _battle_units(conn, 0)
            if units:
                return True
        time.sleep(1.5)
    return False


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = f"mech_ue_{ts}", f"mech_bot_{ts}"
    pwd = "Test1234"

    print("=== TEST-MECH-003: полноценная партия в PIE с проверкой механик ===", flush=True)
    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

    bot_jwt = get_test_jwt(bot_user, pwd)
    bot = OpponentBot(bot_jwt, name="opp")
    if not bot.start():
        print("FAIL: бот не подключился", flush=True)
        return 1
    time.sleep(2.0)

    conn = UnrealConnection()
    conn.connect()

    try:
        # ── Setup: PIE -> login -> draft -> deploy -> BATTLE ──────────────────
        if not _pie_restart_n1(conn):
            print("FAIL: PIE не поднялся", flush=True)
            return 1
        if not _ue_login_findgame(conn, ue_user, pwd):
            print("FAIL: login/FindGame", flush=True)
            return 1

        picks = _ue_draft(conn)
        print(f"DRAFT: UE сделал ~{picks} пиков", flush=True)

        if not _wait_pie_widget(conn, ("Deployment",), 20, "deploy"):
            print("WARN: deployment-виджет не пойман — продолжаем", flush=True)
        deployed = _deploy_client(conn, 0)
        print(f"DEPLOY: {deployed}/5", flush=True)
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        # Mulligan: дождаться и принять руку (FIX-MULLIGAN-001 — авто-ready нет).
        # Клик ретраится: рука может ещё грузиться (кнопка disabled — no-op).
        if _wait_pie_widget(conn, ("Mulligan",), 30, "mulligan"):
            for i in range(10):
                time.sleep(2.0)
                r = _send(conn, "invoke_button_click",
                          {"widget_name": "AcceptHandButton", "controller_index": 0})
                print(f"  AcceptHandButton click #{i}: ok={bool(r.get('ok'))}", flush=True)
                if battle_ready(conn, 6):
                    break
        if not battle_ready(conn, 45):
            print("FAIL: BATTLE не достигнут", flush=True)
            return 1
        print("BATTLE достигнут (боевой стейт отвечает)", flush=True)

        spy = Spy(bot_jwt)
        # game_id от бота
        gid_deadline = time.monotonic() + 15
        while not bot.game_id and time.monotonic() < gid_deadline:
            time.sleep(0.5)
        if not bot.game_id:
            print("FAIL: gameId не получен ботом", flush=True)
            return 1
        spy.sub(bot.game_id)
        ue_spy = UESpy(get_test_jwt(ue_user, pwd))
        ue_spy.sub(bot.game_id)
        print(f"Игра: {bot.game_id}", flush=True)

        # Серверное подтверждение фазы BATTLE (battle_ready опирается на
        # клиентский стейт и ложно-срабатывает, если UE застрял в MULLIGAN,
        # не успев отрисовать виджет — холодный старт редактора)
        def server_phase() -> str:
            spy.drain()
            spy.ws.send_json("/app/game/request-state", {"gameId": bot.game_id})
            s0 = spy.wait_state_until(lambda s_: True, 8)
            return str((s0 or {}).get("gamePhase", "?"))

        for _try in range(15):
            ph = server_phase()
            if ph == "BATTLE":
                break
            print(f"  [PHASE] сервер в {ph} — кликаем AcceptHandButton", flush=True)
            _send(conn, "invoke_button_click", {"widget_name": "AcceptHandButton", "controller_index": 0})
            time.sleep(2.5)
        else:
            print("FAIL: сервер так и не перешёл в BATTLE", flush=True)
            return 1

        if not wait_my_turn(conn, 40):
            print("FAIL: ход UE не наступил", flush=True)
            return 1

        # Стартовый снапшот: спровоцировать нечем — ждём с прошлых фаз
        state = spy.last_state or {}
        if not state:
            # bot пасует/фазы — снапшоты уже летали; форсим лёгким end-turn нельзя.
            state = spy.fresh_state(10) or {}
        units = _battle_units(conn, 0)
        mine = [u for u in units if u.get("alive") and _suffix(u["unitId"]) == "red"]
        enemies = [u for u in units if u.get("alive") and _suffix(u["unitId"]) != "red"]
        print(f"BATTLE: мои {sorted(u['unitId'] for u in mine)}", flush=True)
        print(f"        враги {sorted(u['unitId'] for u in enemies)}", flush=True)
        if len(mine) < 5 or not enemies:
            print("FAIL: неполный состав", flush=True)
            return 1

        participated: set[str] = set()
        my_ids = [u["unitId"] for u in mine]

        # ══ FIX-OWNERSHIP-001: выбор юнитов — только свои ══════════════════
        # Полный клиентский путь выбора (SelectActiveUnit): свой живой юнит
        # выделяется, вражеский — отклоняется (раньше «свой/чужой» определялся
        # Atoi(UUID) и работал случайно).
        r_own = _send(conn, "wc_select_battle_unit",
                      {"controller_index": 0, "unit_id": mine[0]["unitId"]})
        check("выбор СВОЕГО юнита",
              f"{mine[0]['unitId']} выделен",
              f"selected={r_own.get('selected')}",
              bool(r_own.get("selected")))
        r_foe = _send(conn, "wc_select_battle_unit",
                      {"controller_index": 0, "unit_id": enemies[0]["unitId"]})
        check("выбор ЧУЖОГО юнита отклонён",
              f"{enemies[0]['unitId']} НЕ выделен",
              f"selected={r_foe.get('selected')}",
              not r_foe.get("selected"))

        def refresh() -> tuple[dict, dict[str, dict]]:
            s = spy.fresh_state(10) or spy.last_state or {}
            return s, units_map(s)

        def occupied_cells(um: dict[str, dict]) -> set:
            return {upos(u) for u in um.values() if u.get("isAlive", u.get("alive", True))}

        def free_step(um: dict[str, dict], uid: str, dx: int = 1) -> tuple[int, int] | None:
            x, y = upos(um[uid])
            occ = occupied_cells(um)
            for cand in ((x + dx, y), (x, y + 1), (x, y - 1)):
                cx, cy = cand
                if 0 <= cx <= 7 and 0 <= cy <= 7 and cand not in occ:
                    return cand
            return None

        def nearest_enemy(um: dict[str, dict], uid: str) -> dict | None:
            x, y = upos(um[uid])
            foes = [u for i, u in um.items() if _suffix(i) != "red" and u.get("isAlive", u.get("alive", True))]
            return min(foes, key=lambda e: abs(upos(e)[0] - x) + abs(upos(e)[1] - y)) if foes else None

        # выбор юнитов по ролям (адаптивно к драфту)
        def pick(prefixes: list[str]) -> str | None:
            for p in prefixes:
                for uid in my_ids:
                    if uid.startswith(p) and uid not in participated:
                        return uid
            for uid in my_ids:
                if uid not in participated:
                    return uid
            return my_ids[0]

        state, um = (spy.last_state or {}), units_map(spy.last_state or {})
        if not um:
            # первый снапшот мог не прийти — форсим: лёгкий невалидный мув спровоцирует /result, не /state.
            # Надёжнее: state из wc_get_battle_units (позиции) + AP из последующих снапшотов.
            um = {u["unitId"]: {"unitId": u["unitId"], "gridX": u["gridX"], "gridY": u["gridY"],
                               "health": u.get("hp", -1), "alive": u.get("isAlive", u.get("alive", True))} for u in units}
            state = {"apRemaining": 3, "terrain": [], "fieldWidth": 8}

        # ══ Ход 1: пехота — мув (AP 3->2), повторный мув — отказ, второй юнит — отказ; BASIC-атака ══
        infantry = pick(["assault", "sniper", "medic"])
        step = free_step(um, infantry)
        ap0 = int(state.get("apRemaining", 3))
        if step:
            _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": infantry, "x": step[0], "y": step[1]})
            s1, um1 = refresh()
            moved_pos = upos(um1.get(infantry, {})) if um1.get(infantry) else (-1, -1)
            ap1 = int(s1.get("apRemaining", -1))
            check("мув пехоты + расход AP",
                  f"позиция {step}, AP {ap0}->{ap0 - 1}",
                  f"позиция {moved_pos}, AP {ap1}",
                  moved_pos == step and ap1 == ap0 - 1)
            participated.add(infantry)

            # Повторный мув тем же юнитом — сервер отказывает, позиция не меняется
            spy.drain()
            back = free_step(um1, infantry)
            if back:
                _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": infantry, "x": back[0], "y": back[1]})
                rej = spy.wait_result_containing("уже двигался", 8)
                s2 = spy.last_state or s1
                pos_after = upos(units_map(s2).get(infantry, {})) if units_map(s2).get(infantry) else moved_pos
                check("повторный мув того же юнита",
                      "отказ «уже двигался», позиция не изменилась",
                      f"отказ={'да' if rej else 'нет'}, позиция {pos_after}",
                      rej is not None and pos_after == step)

            # Мув ДРУГИМ юнитом в этом же ходу — отказ (замок активного юнита)
            other = next((u for u in my_ids if u != infantry), None)
            if other:
                spy.drain()
                um_cur = units_map(spy.last_state or s1) or um1
                ostep = free_step(um_cur, other)
                if ostep:
                    _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": other, "x": ostep[0], "y": ostep[1]})
                    rej2 = spy.wait_result_containing("уже действовал", 8)
                    check("мув вторым юнитом в том же ходу",
                          "отказ «уже действовал юнит» (AP на всех не хватит)",
                          f"отказ={'да' if rej2 else 'нет'}",
                          rej2 is not None)
        else:
            check("мув пехоты", "свободная клетка рядом", "нет свободной клетки", False)

        # BASIC-атака ближайшего врага стрелком с уроном
        attacker = pick(["t72", "btr80", "grad", "msta", "mi24", "assault", "sniper"])
        um_cur = units_map(spy.last_state or {}) or um
        foe = nearest_enemy(um_cur, attacker) if attacker in um_cur else None
        if foe:
            hp_before = uhp(foe)
            fx, fy = upos(foe)
            spy.drain()
            _send(conn, "wc_attack", {"controller_index": 0, "attacker_unit_id": attacker,
                                      "target_unit_id": foe["unitId"], "x": fx, "y": fy})
            # FIX-CARD-RACE-001: клиент шлёт execute после подтверждения select —
            # ждём именно execute-result, затем снапшот, где урон УЖЕ применён
            r_exec = spy.wait_result_type("execute-action", 12)
            foe_id = foe["unitId"]

            def hp_dropped(s_):
                u_ = units_map(s_).get(foe_id)
                return u_ is None or uhp(u_) < hp_before

            basic_done = False
            if r_exec and r_exec.get("success"):
                s3 = spy.wait_state_until(hp_dropped, 10)
                um3 = units_map(s3 or spy.last_state or {})
                hp_after = uhp(um3.get(foe_id, {})) if um3.get(foe_id) else -1
                msg_exec = str(r_exec.get("message", ""))
                # Валидные исходы: урон прошёл, цель убита, либо пассивка цели
                # (Trophy System) детерминированно перехватила атаку
                trophy_block = "перехватил" in msg_exec
                check("BASIC-атака (карта атаки)",
                      f"success + (HP {foe_id} < {hp_before} | Trophy-перехват)",
                      f"HP={hp_after} | {msg_exec[:60]}",
                      (0 <= hp_after < hp_before or hp_after == -1 or trophy_block))
                participated.add(attacker)
                basic_done = True
                # авто-переход хода: бот пасует -> ход снова наш, AP сброшен до 3
                s_ap = spy.wait_state_until(lambda s_: int(s_.get("apRemaining", -1)) == 3, 12)
                check("AP сброшен новым ходом", "apRemaining=3",
                      f"apRemaining={(s_ap or spy.last_state or {}).get('apRemaining')}",
                      s_ap is not None)
            else:
                print("  [BASIC] в стартовой руке нет карты атаки — отложено в оркестратор", flush=True)

        # ══ Хелперы серии ходов ══════════════════════════════════════════════
        def wait_our_turn(timeout: float = 30.0) -> bool:
            """Серверная истина хода: activePlayerId != бот. Клиентский
            wc_get_battle_state.my_turn отстаёт на гонках с пасом бота."""
            return spy.wait_state_until(
                lambda s_: not s_.get("gameOver")
                           and s_.get("activePlayerId")
                           and s_.get("activePlayerId") != bot.my_id, timeout) is not None

        def try_move(uid: str) -> bool:
            """Мув юнита с подтверждением по снапшоту; participated — только по факту."""
            um_ = units_map(spy.last_state or {})
            if uid not in um_ or uid in participated:
                return False
            st_ = free_step(um_, uid)
            if not st_:
                return False
            _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": uid, "x": st_[0], "y": st_[1]})
            _s2, um2 = refresh()
            if um2.get(uid) and upos(um2[uid]) == st_:
                participated.add(uid)
                return True
            return False

        def end_turn_ue() -> None:
            _send(conn, "wc_end_turn", {"controller_index": 0})
            time.sleep(1.5)

        RESERVED_TYPES = {"SPECIAL_ATTACK", "AIR_STRIKE", "HEAL", "ARTILLERY_STRIKE", "REPAIR"}

        def play_when_available(card_type: str, build, label: str, max_turns: int = 12):
            """Играет карту card_type, как только она есть в руке UE (руку видим
            через UESpy). Если типа нет — discard ненужной карты (завершает ход
            и добирает новую: прокрутка руки). build(state, units) -> params|None.
            Возвращает execute-result или None."""
            for _ in range(max_turns):
                if not wait_our_turn(30):
                    return None
                # бонус: двигаем ещё не участвовавшего юнита (цель «каждый юнит»)
                nxt = next((u for u in my_ids if u not in participated), None)
                if nxt:
                    try_move(nxt)
                if ue_spy.hand is not None and not ue_spy.has_type(card_type):
                    spy.drain()
                    junk = ue_spy.discard_junk(RESERVED_TYPES | {card_type})
                    if junk:
                        r_disc = spy.wait_result_type("discard-card", 8)
                        ok_disc = bool(r_disc and r_disc.get("success"))
                        print(f"  [{label}] {card_type} нет в руке — discard {junk}: "
                              f"{'ok' if ok_disc else str((r_disc or {}).get('message'))[:50]}", flush=True)
                    else:
                        print(f"  [{label}] {card_type} нет, сбрасывать нечего — пас", flush=True)
                        end_turn_ue()
                    continue
                um_ = units_map(spy.last_state or {})
                prm = build(spy.last_state or {}, um_)
                if prm is None:
                    end_turn_ue()
                    continue
                spy.drain()
                _send(conn, "wc_play_card", {"controller_index": 0, "card_type": card_type, **prm})
                r = spy.wait_result_type("execute-action", 10)
                if r and r.get("success"):
                    return r
                if r:
                    print(f"  [{label}] сервер отклонил: {str(r.get('message'))[:80]}", flush=True)
                else:
                    print(f"  [{label}] execute-result не пришёл — пас", flush=True)
                if wait_our_turn(3):
                    end_turn_ue()
            return None

        # ══ Ландшафт + стоимость HEAVY-мува (в СВЕЖЕМ ходу: если BASIC не
        # сыгралась, текущий ход уже занят мувом пехоты — сбрасываем пасом) ══
        if not basic_done and wait_our_turn(5):
            end_turn_ue()
        if wait_our_turn(30):
            s, um = spy.last_state or {}, units_map(spy.last_state or {})
            heavy = next((u for u in my_ids if u.startswith("t72")), None)
            if heavy and um.get(heavy):
                hx, hy = upos(um[heavy])
                blocked_cell = None
                for cand in ((hx + 1, hy), (hx - 1, hy), (hx, hy + 1), (hx, hy - 1)):
                    cx, cy = cand
                    if 0 <= cx <= 7 and 0 <= cy <= 7 and terrain_at(s, cx, cy) in HEAVY_IMPASSABLE \
                            and cand not in occupied_cells(um):
                        blocked_cell = cand
                        break
                if blocked_cell:
                    spy.drain()
                    _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": heavy,
                                                 "x": blocked_cell[0], "y": blocked_cell[1]})
                    rej = spy.wait_result_containing("Невозможно пройти", 8)
                    check("ландшафт: HEAVY в непроходимую клетку",
                          "отказ «Невозможно пройти»",
                          f"отказ={'да' if rej else 'нет'} ({terrain_at(s, *blocked_cell)})",
                          rej is not None)
                else:
                    print("  [SKIP] рядом с t72 нет непроходимого террейна", flush=True)
                # обычный мув HEAVY: стоимость 2 AP (клетка — только полностью
                # проходимая: RIVER/вода блокируют все наземные типы)
                hvx, hvy = upos(um[heavy])
                good = None
                occ_ = occupied_cells(um)
                for cand in ((hvx + 1, hvy), (hvx, hvy + 1), (hvx, hvy - 1), (hvx - 1, hvy)):
                    cx, cy = cand
                    if 0 <= cx <= 7 and 0 <= cy <= 7 and cand not in occ_ \
                            and terrain_at(s, cx, cy) in ("PLAIN", "ROAD", "HILL", "CITY", "RUINS", "BRIDGE", "?"):
                        good = cand
                        break
                if good:
                    ap_b = int(s.get("apRemaining", 3))
                    spy.drain()
                    _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": heavy,
                                                 "x": good[0], "y": good[1]})
                    r_mv = spy.wait_result_type("free-move", 10)
                    s4 = spy.wait_state_until(
                        lambda s_: units_map(s_).get(heavy) and upos(units_map(s_)[heavy]) == good, 8)
                    um4 = units_map(s4 or spy.last_state or {})
                    moved = um4.get(heavy) and upos(um4[heavy]) == good
                    check("мув HEAVY_TECH: стоимость 2 AP",
                          f"позиция {good}, AP {ap_b}->{ap_b - 2}",
                          f"позиция {upos(um4.get(heavy, {})) if um4.get(heavy) else '?'}, "
                          f"AP={(s4 or spy.last_state or {}).get('apRemaining')} | {str((r_mv or {}).get('message'))[:60]}",
                          bool(moved) and int((s4 or {}).get("apRemaining", -1)) == ap_b - 2)
                    if moved:
                        participated.add(heavy)
                else:
                    print("  [SKIP] у t72 нет полностью проходимой соседней клетки", flush=True)
            end_turn_ue()

        # ══ Карточные сценарии: единый оркестратор ══════════════════════════
        # Играем каждый тип, как только он в руке (иначе discard-прокрутка);
        # HEAL/REPAIR требуют раненого — его создаёт НАСТОЯЩАЯ атака бота
        # (bot_wound: команды от STOMP-сессии бота, обгоняя его авто-пас).
        pending = {"SPECIAL_ATTACK", "AIR_STRIKE", "HEAL", "ARTILLERY_STRIKE", "REPAIR"}
        if not basic_done:
            pending.add("BASIC_ATTACK")
        ap_check_pending = not basic_done
        heavy_id = next((u for u in my_ids if u.startswith(("t72", "btr80"))), None)
        base_hp = {i_: uhp(u_) for i_, u_ in units_map(spy.last_state or {}).items()}

        def is_wounded(um_, uid) -> bool:
            u_ = um_.get(uid)
            return bool(u_) and 0 < uhp(u_) < base_hp.get(uid, uhp(u_))

        def ground_foe(um_):
            foes_ = [u for i_, u in um_.items() if _suffix(i_) != "red" and u.get("isAlive", u.get("alive", True))
                     and not i_.startswith(("apache", "mi24"))]
            return foes_[0] if foes_ else None

        def bot_pass() -> None:
            """Пас за бота его же сокетом (авто-пас подавлен на время wound)."""
            spy.ws.send_json("/app/game/end-turn", {"gameId": bot.game_id})
            time.sleep(0.8)

        def bot_wound(target_uid: str, dmg_field: str) -> bool:
            """Бот по-настоящему атакует наш юнит (раненый для HEAL/REPAIR + покрытие
            вражеской атаки). Авто-пас бота подавляется на время сценария."""
            bot.suppress_pass = True
            try:
                spy.drain()
                end_turn_ue()
                s_ = spy.wait_state_until(lambda s2: s2.get("activePlayerId") == bot.my_id, 15)
                if not s_:
                    print("  [WOUND] ход бота не наступил", flush=True)
                    return False
                um_ = units_map(s_)
                if target_uid not in um_:
                    print(f"  [WOUND] цель {target_uid} не на доске", flush=True)
                    bot_pass()
                    return False
                t_dl = time.time() + 6
                while not spy.bot_hand and time.time() < t_dl:
                    time.sleep(0.2)
                card = next((c for c in spy.bot_hand or []
                             if c.get("actionType") in ("BASIC_ATTACK", "SPECIAL_ATTACK")), None)
                if card is None:
                    print("  [WOUND] у бота нет карты атаки — пас", flush=True)
                    bot_pass()
                    return False
                foes_ = [u for i_, u in um_.items() if _suffix(i_) != "red" and u.get("isAlive", u.get("alive", True))]
                cand = [u for u in foes_ if int(u.get(dmg_field, 0) or 0) > 0]
                if not cand:
                    print("  [WOUND] нет атакера с положительным уроном — пас", flush=True)
                    bot_pass()
                    return False
                att = min(cand, key=lambda u: int(u.get(dmg_field, 999)))
                tx_, ty_ = upos(um_[target_uid])
                spy.drain()
                spy.ws.send_json("/app/game/select-action-card",
                                 {"gameId": bot.game_id, "actionCardId": card["id"]})
                r_sel = spy.wait_result_type("select-action-card", 6)
                if not (r_sel and r_sel.get("success")):
                    print(f"  [WOUND] select fail: {str((r_sel or {}).get('message'))[:60]}", flush=True)
                    bot_pass()
                    return False
                spy.ws.send_json("/app/game/execute-action",
                                 {"gameId": bot.game_id, "actionCardId": card["id"],
                                  "targetX": tx_, "targetY": ty_,
                                  "targetUnitId": target_uid, "attackerUnitId": att["unitId"]})
                r_ex = spy.wait_result_type("execute-action", 10)
                print(f"  [WOUND] бот ({att['unitId']}) -> {target_uid}: "
                      f"{str((r_ex or {}).get('message'))[:70]}", flush=True)
                if not (r_ex and r_ex.get("success")):
                    bot_pass()
                    return False
                return True
            finally:
                bot.suppress_pass = False

        INF_PREFIXES = ("assault", "sniper", "medic")
        for _turn in range(40):
            if not pending:
                break
            if not wait_our_turn(30):
                print("  [ORCH] наш ход не наступил — стоп", flush=True)
                break
            nxt = next((u for u in my_ids if u not in participated), None)
            if nxt:
                try_move(nxt)
            ue_spy.ensure_fresh()
            um_now = units_map(spy.last_state or {})
            hand_types = {c.get("actionType") for c in (ue_spy.hand or [])}
            print(f"  [ORCH] круг {_turn}: рука={sorted(hand_types) if ue_spy.hand is not None else 'None'} "
                  f"pending={sorted(pending)}", flush=True)
            played = False
            for ct in ("BASIC_ATTACK", "SPECIAL_ATTACK", "AIR_STRIKE", "ARTILLERY_STRIKE", "HEAL", "REPAIR"):
                if ct not in pending or ct not in hand_types:
                    continue
                prm = meta = None
                if ct in ("SPECIAL_ATTACK", "BASIC_ATTACK"):
                    sp = next((u for u in my_ids if u in um_now and not u.startswith("medic")), None)
                    foe_ = nearest_enemy(um_now, sp) if sp else None
                    if foe_:
                        meta = (foe_["unitId"], uhp(foe_))
                        prm = {"attacker_unit_id": sp, "target_unit_id": foe_["unitId"],
                               "x": upos(foe_)[0], "y": upos(foe_)[1]}
                elif ct in ("AIR_STRIKE", "ARTILLERY_STRIKE"):
                    foe_ = ground_foe(um_now)
                    if foe_:
                        meta = (foe_["unitId"], uhp(foe_))
                        prm = {"target_unit_id": "", "attacker_unit_id": "",
                               "x": upos(foe_)[0], "y": upos(foe_)[1]}
                elif ct == "HEAL":
                    w = next((uid for uid in my_ids
                              if uid.startswith(INF_PREFIXES) and is_wounded(um_now, uid)), None)
                    if w:
                        meta = (w, uhp(um_now[w]))
                        prm = {"target_unit_id": w, "attacker_unit_id": "",
                               "x": upos(um_now[w])[0], "y": upos(um_now[w])[1]}
                elif ct == "REPAIR":
                    if heavy_id and is_wounded(um_now, heavy_id):
                        meta = (heavy_id, uhp(um_now[heavy_id]))
                        prm = {"target_unit_id": heavy_id, "attacker_unit_id": "",
                               "x": upos(um_now[heavy_id])[0], "y": upos(um_now[heavy_id])[1]}
                if prm is None:
                    continue
                uid_, hp_b = meta
                spy.drain()
                _send(conn, "wc_play_card", {"controller_index": 0, "card_type": ct, **prm})
                r = spy.wait_result_type("execute-action", 10)
                if not (r and r.get("success")):
                    print(f"  [{ct}] отклонено/нет ответа: {str((r or {}).get('message'))[:70]}", flush=True)
                    break  # ход мог уйти — новый круг оркестратора
                msg_r = str(r.get("message", ""))
                if ct in ("HEAL", "REPAIR"):
                    s_ok = spy.wait_state_until(
                        lambda s2, u2=uid_, h2=hp_b: units_map(s2).get(u2) and uhp(units_map(s2)[u2]) > h2, 10)
                    um_a = units_map(s_ok or spy.last_state or {})
                    hp_a = uhp(um_a.get(uid_, {})) if um_a.get(uid_) else -1
                    title = ("HEAL раненой пехоты (+15, кламп max)" if ct == "HEAL"
                             else "REPAIR раненой техники (+HP, +2 боезапас)")
                    check(title, f"HP {uid_} вырос с {hp_b}",
                          f"HP={hp_a} | {msg_r[:60]}", s_ok is not None and hp_a > hp_b)
                else:
                    s_ok = spy.wait_state_until(
                        lambda s2, u2=uid_, h2=hp_b: units_map(s2).get(u2) is None
                        or uhp(units_map(s2)[u2]) < h2, 10)
                    um_a = units_map(s_ok or spy.last_state or {})
                    hp_a = uhp(um_a.get(uid_, {})) if um_a.get(uid_) else -1
                    if ct in ("SPECIAL_ATTACK", "BASIC_ATTACK"):
                        ok_ = hp_a < hp_b or "перехватил" in msg_r
                        title_a = ("SPECIAL_ATTACK (доп. атака)" if ct == "SPECIAL_ATTACK"
                                   else "BASIC-атака (карта атаки)")
                        check(title_a,
                              f"HP {uid_} снизился с {hp_b} (или Trophy)",
                              f"HP={hp_a} | {msg_r[:60]}", ok_)
                    else:
                        ok_ = hp_a == hp_b - 15 or hp_a <= 0 or hp_a == -1
                        check(f"{ct} (зона 2×2, 15 урона)",
                              f"HP {uid_} {hp_b}->{hp_b - 15} (или убит)",
                              f"HP={hp_a} | {msg_r[:60]}", ok_)
                pending.discard(ct)
                played = True
                if ap_check_pending:
                    ap_check_pending = False
                    s_ap = spy.wait_state_until(lambda s2: int(s2.get("apRemaining", -1)) == 3, 12)
                    check("AP сброшен новым ходом", "apRemaining=3",
                          f"apRemaining={(s_ap or spy.last_state or {}).get('apRemaining')}",
                          s_ap is not None)
                break
            if played:
                continue
            um_now = units_map(spy.last_state or {})
            if "HEAL" in pending and "HEAL" in hand_types \
                    and not any(is_wounded(um_now, uid) for uid in my_ids if uid.startswith(INF_PREFIXES)):
                bot_wound(infantry, "damageVsInfantry")
                continue
            if "REPAIR" in pending and "REPAIR" in hand_types and heavy_id \
                    and not is_wounded(um_now, heavy_id):
                bot_wound(heavy_id, "damageVsHeavyTech")
                continue
            spy.drain()
            junk = ue_spy.discard_junk(pending)
            if junk:
                r_disc = spy.wait_result_type("discard-card", 8)
                ok_d = "ok" if r_disc and r_disc.get("success") else str((r_disc or {}).get("message"))[:50]
                print(f"  [ORCH] discard {junk}: {ok_d}", flush=True)
            else:
                end_turn_ue()

        for ct in sorted(pending):
            check(f"{ct}", "карта сыграна с эффектом", "не удалось за 40 ходов", False)

        # ══ Участие каждого юнита ══
        check("участвует каждый юнит",
              f"5/5 совершили действие",
              f"{len(participated)}/5: {sorted(participated)}",
              len(participated) >= 5)

        # ══ Финал: surrender -> gameOver в снапшоте (+ виджет best-effort) ══
        spy.drain()
        _send(conn, "wc_surrender", {"controller_index": 0})
        r_sur = spy.wait_result_type("surrender", 10)
        print(f"  [SURRENDER] {str((r_sur or {}).get('message'))[:80]}", flush=True)
        game_over = False
        dl_end = time.time() + 15
        while time.time() < dl_end and not game_over:
            s_ = spy.fresh_state(3)
            if s_ and s_.get("gameOver"):
                game_over = True
        ls = spy.last_state or {}
        print(f"  [FINAL] снапшот: phase={ls.get('gamePhase')} gameOver={ls.get('gameOver')} "
              f"winner={str(ls.get('winnerId'))[:8]}", flush=True)
        got_widget = _wait_pie_widget(conn, ("GameResult",), 12, "result")
        # gameOver из снапшота ЛИБО бот получил game-over событие (bot._game_over)
        bot_saw_end = bot.winner_id is not None
        check("финал партии (surrender)",
              "gameOver в снапшоте или game-over у оппонента",
              f"gameOver={game_over}, bot_game_over={bot_saw_end}, виджет={'да' if got_widget else 'нет'}",
              game_over or bot_saw_end)

    finally:
        bot.close()
        try:
            conn.disconnect()
        except Exception:
            pass

    passed = sum(1 for *_x, ok in RESULTS if ok)
    print(f"\n=== ИТОГ: {passed}/{len(RESULTS)} PASS ===", flush=True)
    for op, exp, act, ok in RESULTS:
        print(f"  {'PASS' if ok else 'FAIL'} | {op} | ожидание: {exp} | факт: {act}", flush=True)
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
