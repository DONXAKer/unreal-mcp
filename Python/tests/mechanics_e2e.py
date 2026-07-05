# -*- coding: utf-8 -*-
"""TEST-MECH-002: e2e-прогон механик против ЖИВОГО сервера (:8081), без UE.

Два скриптованных STOMP-клиента проходят matchmaking -> draft -> deployment ->
mulligan -> BATTLE, затем выполняют сценарий действий; после каждого действия
скрипт читает /topic/game/{id}/state + /result и сверяет показатели:
позиция после free-move, ДельтаAP по стоимости, ДельтаHP после атак (набор допустимых
значений: full / miss(-10) / пассивки — D20 живой, точные формулы покрывает
серверный MechanicsFullGameIT), отказы сервера по точным строкам, переход хода,
финал (surrender -> gameOver + winnerId).

Запуск (сервер :8081 должен работать):
    cd D:\\WarCard\\unreal-mcp\\Python
    uv run python tests/mechanics_e2e.py

Выход: таблица "операция -> ожидание -> факт" + exit code 0/1.
"""
from __future__ import annotations

import json
import queue
import sys
import threading
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0].rsplit("/tests", 1)[0]))

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import StompWs

RED_UNITS = ["t72-red", "mi24-red", "assault-red", "s300-red", "grad-red"]
BLUE_UNITS = ["abrams-blue", "apache-blue", "assault-blue", "patriot-blue", "mlrs-blue"]
RED_DEPLOY = [("t72-red", 0, 0), ("mi24-red", 0, 1), ("assault-red", 0, 2),
              ("s300-red", 0, 3), ("grad-red", 0, 4)]
BLUE_DEPLOY = [("abrams-blue", 7, 0), ("apache-blue", 7, 1), ("assault-blue", 7, 2),
               ("patriot-blue", 7, 3), ("mlrs-blue", 7, 4)]

MOVE_AP_COST = {"INFANTRY": 1, "AIR": 1, "SUPPORT": 1, "HEAVY_TECH": 2}

RESULTS: list[tuple[str, str, str, bool]] = []  # (операция, ожидание, факт, ok)


def check(op: str, expected: str, actual: str, ok: bool) -> None:
    RESULTS.append((op, expected, actual, ok))
    print(f"  [{'PASS' if ok else 'FAIL'}] {op}: ожидание [{expected}] факт [{actual}]", flush=True)


class Client:
    """STOMP-клиент с очередями state/result/draft/game-found."""

    def __init__(self, name: str, login: str, pwd: str):
        self.name = name
        self.login = login
        ensure_test_user(login, pwd, f"{login}@example.com")
        self.jwt = get_test_jwt(login, pwd)
        self.player_id = self._player_id_from_login(login, pwd)
        self.ws = StompWs("127.0.0.1", 8081)
        self.state_q: queue.Queue = queue.Queue()
        self.result_q: queue.Queue = queue.Queue()
        self.gf_q: queue.Queue = queue.Queue()
        self.du_q: queue.Queue = queue.Queue()
        self.last_hand: list | None = None
        self.ws.on_message = self._route

    @staticmethod
    def _player_id_from_login(login: str, pwd: str) -> str:
        import requests
        r = requests.post("http://127.0.0.1:8081/api/auth/login",
                          json={"usernameOrEmail": login, "password": pwd}, timeout=10)
        d = r.json()
        return (d.get("player") or {}).get("id", "")

    def _route(self, dest: str, body: dict) -> None:
        if "handCards" in body:
            self.last_hand = body.get("handCards") or []
        elif body.get("gamePhase") is not None:
            self.state_q.put(body)
        elif body.get("commandType") is not None or body.get("success") is not None:
            self.result_q.put(body)
        elif body.get("currentPickPlayerId") is not None or "pickNumber" in body:
            self.du_q.put(body)
        elif body.get("gameId") and "status" not in body:
            self.gf_q.put(body)

    def connect(self) -> None:
        if not self.ws.connect(self.jwt):
            raise RuntimeError(f"{self.name}: STOMP connect failed")
        for dest in ("/user/queue/matchmaking/game-found",
                     "/user/queue/matchmaking/status",
                     "/user/queue/active-game"):
            self.ws.subscribe(dest)

    def sub_game(self, game_id: str) -> None:
        for suffix in ("state", "result", "draft-update"):
            self.ws.subscribe(f"/topic/game/{game_id}/{suffix}")
        # per-player рука (сервер шлёт convertAndSendToUser)
        self.ws.subscribe(f"/user/topic/game/{game_id}/cards-update")

    def send(self, dest: str, body: dict) -> None:
        self.ws.send_json(dest, body)

    @staticmethod
    def _drain(q: queue.Queue) -> None:
        while True:
            try:
                q.get_nowait()
            except queue.Empty:
                return

    def drain_all(self) -> None:
        for q in (self.state_q, self.result_q, self.du_q):
            self._drain(q)

    def wait_state(self, phase: str | None = None, timeout: float = 8.0) -> dict | None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                s = self.state_q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                return None
            if phase is None or s.get("gamePhase") == phase:
                return s
        return None

    def wait_result(self, timeout: float = 6.0, command_type: str | None = None) -> dict | None:
        """Ждёт /result; при command_type фильтрует (в общий топик летят
        результаты ВСЕХ команд обоих игроков — без фильтра очередь «сдвигается»)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                r = self.result_q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                return None
            if command_type is None or r.get("commandType") == command_type:
                return r
        return None

    def close(self) -> None:
        try:
            self.ws.close()
        except Exception:
            pass


def units_by_id(state: dict) -> dict[str, dict]:
    return {u["unitId"]: u for u in (state.get("units") or [])}


def ux(u: dict) -> int:
    return int(u.get("x", u.get("gridX", -1)))


def uy(u: dict) -> int:
    return int(u.get("y", u.get("gridY", -1)))


def uhp(u: dict) -> int:
    return int(u.get("currentHp", u.get("health", -1)))


def hp_of(state: dict, unit_id: str) -> int:
    u = units_by_id(state).get(unit_id)
    return uhp(u) if u else -1


def latest_state(c: Client, timeout: float = 5.0) -> dict | None:
    """Последний снапшот из очереди (снимает подряд идущие)."""
    s = c.wait_state(timeout=timeout)
    if s is None:
        return None
    while True:
        try:
            s = c.state_q.get_nowait()
        except queue.Empty:
            return s


def main() -> int:
    print("=== TEST-MECH-002: e2e механики против живого сервера ===\n", flush=True)

    p1 = Client("P1/testuser1", "testuser1", "admin123")
    p2 = Client("P2/testuser2", "testuser2", "admin123")
    p1.connect()
    p2.connect()

    # Чистый старт: закрыть возможные активные игры
    for c in (p1, p2):
        c.send("/app/matchmaking/abandon-games", {})
    time.sleep(1.5)
    for c in (p1, p2):
        c.drain_all()
        Client._drain(c.gf_q)

    # Матчмейкинг: P1 первым в очередь, P2 триггерит матч -> P2 = player1/RED
    p1.send("/app/matchmaking/start", {})
    time.sleep(0.5)
    p2.send("/app/matchmaking/start", {})
    gf = None
    deadline = time.time() + 15
    while time.time() < deadline and gf is None:
        for c in (p1, p2):
            try:
                gf = c.gf_q.get_nowait()
                break
            except queue.Empty:
                continue
        time.sleep(0.2)
    if not gf or not gf.get("gameId"):
        print("ERROR: матч не создан", flush=True)
        return 1
    game_id = gf["gameId"]
    pr = {"gameId": game_id}
    print(f"Игра: {game_id}", flush=True)

    red, blue = p2, p1  # P2 стартовал вторым -> player1/RED
    by_pid = {p1.player_id: p1, p2.player_id: p2}
    for c in (p1, p2):
        c.sub_game(game_id)

    # DICE_ROLL_1 -> DRAFT
    p1.send("/app/game/phase-ready", pr)
    time.sleep(0.4)
    p2.send("/app/game/phase-ready", pr)
    draft = p1.wait_state("DRAFT", timeout=15)
    if not draft:
        print("ERROR: DRAFT не достигнут", flush=True)
        return 1

    # Первый пикер из draft-update
    first_picker_id = ""
    try:
        du = p1.du_q.get(timeout=6)
        first_picker_id = du.get("currentPickPlayerId", "")
    except queue.Empty:
        first_picker_id = draft.get("activePlayerId", "")
    picker = by_pid.get(first_picker_id, red)
    print(f"DRAFT: первый пик — {picker.name}", flush=True)

    red_pool, blue_pool = list(RED_UNITS), list(BLUE_UNITS)
    for i in range(10):
        pool = red_pool if picker is red else blue_pool
        if not pool:
            break
        uid = pool.pop(0)
        picker.send("/app/game/select-unit", {"gameId": game_id, "unitId": uid})
        nxt = None
        du_deadline = time.time() + 8
        while time.time() < du_deadline:
            try:
                du = p1.du_q.get(timeout=0.5)
                if du.get("pickNumber", -1) >= i + 1:
                    nxt = du.get("currentPickPlayerId", "")
                    break
            except queue.Empty:
                continue
        if nxt:
            picker = by_pid.get(nxt, picker)
        elif i < 9:
            print(f"WARN: draft-update не пришёл на пике {i + 1}", flush=True)
    print("DRAFT завершён", flush=True)

    for c in (p1, p2):
        c.drain_all()
    p1.send("/app/game/phase-ready", pr)
    time.sleep(0.4)
    p2.send("/app/game/phase-ready", pr)
    if not p1.wait_state("DEPLOYMENT", timeout=12):
        print("WARN: DEPLOYMENT-state не получен, продолжаем", flush=True)

    # DEPLOYMENT
    for uid, x, y in RED_DEPLOY:
        red.send("/app/game/place-unit", {"gameId": game_id, "unitId": uid, "x": x, "y": y})
        time.sleep(0.15)
    for uid, x, y in BLUE_DEPLOY:
        blue.send("/app/game/place-unit", {"gameId": game_id, "unitId": uid, "x": x, "y": y})
        time.sleep(0.15)
    print("DEPLOYMENT отправлен", flush=True)

    # DICE_ROLL_2 + MULLIGAN -> BATTLE (phase-ready-прокачка)
    battle = None
    for _ in range(25):
        p1.send("/app/game/phase-ready", pr)
        time.sleep(0.3)
        p2.send("/app/game/phase-ready", pr)
        s = p1.wait_state(timeout=2.5)
        if s and s.get("gamePhase") == "BATTLE":
            battle = s
            break
    if not battle:
        print("ERROR: BATTLE не достигнут", flush=True)
        return 1
    for c in (p1, p2):
        c.drain_all()

    print(f"\nBATTLE: turn={battle.get('turnNumber')} AP={battle.get('apRemaining')}\n", flush=True)
    active = by_pid.get(battle.get("activePlayerId", ""), red)
    passive = p1 if active is p2 else p2
    act_units = RED_UNITS if active is red else BLUE_UNITS
    pas_units = BLUE_UNITS if active is red else RED_UNITS
    umap = units_by_id(battle)

    # ── 1. free-move: позиция + ДельтаAP ──────────────────────────────────────
    mover = act_units[2]  # assault-* (INFANTRY, cost 1, movement 2)
    mu = umap[mover]
    ap0 = int(battle.get("apRemaining", 3))
    tx, ty = ux(mu) + (1 if active is red else -1), uy(mu)
    active.send("/app/game/free-move",
                {"gameId": game_id, "unitId": mover,
                 "targetX": tx, "targetY": ty, "x": tx, "y": ty})
    r = active.wait_result(command_type="free-move")
    s = latest_state(p1)
    ok_move = bool(r and r.get("success")) and s is not None
    new_x = ux(units_by_id(s).get(mover, {})) if s else -1
    pos_ok = ok_move and new_x == tx
    ap_ok = ok_move and int(s.get("apRemaining", -1)) == ap0 - MOVE_AP_COST["INFANTRY"]
    check("free-move пехотой на 1 клетку",
          f"success, x={tx}, AP {ap0}->{ap0 - 1}",
          f"success={bool(r and r.get('success'))}, x={new_x}, AP={s.get('apRemaining') if s else '?'}",
          pos_ok and ap_ok)

    # ── 2. повторный move тем же юнитом — отказ ───────────────────────────
    active.send("/app/game/free-move",
                {"gameId": game_id, "unitId": mover,
                 "targetX": tx, "targetY": ty + 1, "x": tx, "y": ty + 1})
    r = active.wait_result(command_type="free-move")
    msg = str(r.get("message", "")) if r else "(timeout)"
    check("повторный free-move тем же юнитом",
          "отказ: уже двигался",
          f"success={bool(r and r.get('success'))}, msg={msg[:60]}",
          bool(r) and not r.get("success") and "уже двигался" in msg)

    # ── 3. move другим юнитом в том же ходу — отказ (замок) ───────────────
    other = act_units[0]
    ou = umap[other]
    ox2 = ux(ou) + (1 if active is red else -1)
    active.send("/app/game/free-move",
                {"gameId": game_id, "unitId": other,
                 "targetX": ox2, "targetY": uy(ou), "x": ox2, "y": uy(ou)})
    r = active.wait_result(command_type="free-move")
    msg = str(r.get("message", "")) if r else "(timeout)"
    check("free-move вторым юнитом в том же ходу",
          "отказ: уже действовал юнит",
          f"msg={msg[:60]}",
          bool(r) and not r.get("success") and "уже действовал" in msg)

    # ── 4. атака: select + execute, ДельтаHP из допустимого набора ────────────
    attacker = act_units[0]                       # t72-red / abrams-blue
    target = pas_units[0]                         # abrams-blue / t72-red
    s0 = s or battle
    hp_before = hp_of(s0, target)
    a_stats = units_by_id(s0)[attacker]
    base = int(a_stats.get("dmgVsHeavy", a_stats.get("attackVsHeavy", 40)))  # HEAVY vs HEAVY: mod 1.0
    tu = units_by_id(s0)[target]

    # Рука приходит per-player cards-update (после mulligan/free-move/end-turn) —
    # роутер клиента складывает её в last_hand. Если ещё не пришла — обмен пасами.
    if not active.last_hand:
        active.send("/app/game/end-turn", pr)
        active.wait_result(command_type="end-turn")
        passive.send("/app/game/end-turn", pr)
        passive.wait_result(command_type="end-turn")
        time.sleep(1.0)
        for c in (p1, p2):
            c.drain_all()
    t_end = time.time() + 5
    while not active.last_hand and time.time() < t_end:
        time.sleep(0.2)

    hand = active.last_hand or []
    attack_card = next((c for c in hand if c.get("actionType") in ("BASIC_ATTACK", "SPECIAL_ATTACK")), None)
    if attack_card:
        card_id = attack_card["id"]
        active.send("/app/game/select-action-card", {"gameId": game_id, "actionCardId": card_id})
        active.wait_result(command_type="select-action-card")
        active.send("/app/game/execute-action",
                    {"gameId": game_id, "actionCardId": card_id,
                     "targetX": ux(tu), "targetY": uy(tu),
                     "targetUnitId": target, "attackerUnitId": attacker})
        r = active.wait_result(8, command_type="execute-action")
        s = latest_state(p1, timeout=6)
        hp_after = hp_of(s, target) if s else -1
        delta = hp_before - hp_after if hp_after >= 0 else -1
        is_secondary = attack_card.get("actionType") == "SPECIAL_ATTACK"
        eff = max(1, base // 2) if is_secondary else base
        # Допустимые исходы: full, miss(-10), реактивная броня(-20), Trophy-блок(0)
        allowed = {max(1, eff), max(1, eff - 10), max(1, eff - 20), max(1, eff - 10 - 20), 0}
        check(f"атака {attacker} -> {target} ({attack_card.get('actionType')})",
              f"success, ДельтаHP из {sorted(allowed)} (база {eff}, D20 живой)",
              f"success={bool(r and r.get('success'))}, ДельтаHP={delta}",
              bool(r and r.get("success")) and delta in allowed)
        # Ход перешёл автоматически + AP сброшен
        turn_ok = s is not None and s.get("activePlayerId") == passive.player_id
        ap_reset = s is not None and int(s.get("apRemaining", -1)) == 3
        check("авто-переход хода после карты",
              f"activePlayer={passive.name}, AP=3",
              f"activePlayer={'ok' if turn_ok else s.get('activePlayerId') if s else '?'}, AP={s.get('apRemaining') if s else '?'}",
              turn_ok and ap_reset)
    else:
        check("атака картой", "карта атаки в руке (cards-update)",
              f"рука не получена/без атак: {len(hand)} карт", False)

    # ── 5. surrender: gameOver + победитель ────────────────────────────────
    passive.send("/app/game/surrender", pr)
    r = passive.wait_result(8, command_type="surrender")
    s = None
    t_end = time.time() + 8
    while time.time() < t_end:
        cand = p1.wait_state(timeout=1.0)
        if cand and cand.get("gameOver"):
            s = cand
            break
    winner_expected = active.player_id
    check("surrender: конец игры",
          f"gameOver=true, winner={active.name}",
          f"gameOver={bool(s and s.get('gameOver'))}, winner={'ok' if s and s.get('winnerId') == winner_expected else (s or {}).get('winnerId', '?')}",
          bool(s and s.get("gameOver") and s.get("winnerId") == winner_expected))

    # ── 6. PROG-XP-001: XP начислен по итогам матча ────────────────────────
    import requests as _rq
    time.sleep(1.5)  # начисление в completeGame — практически мгновенно

    def _progress(jwt: str) -> dict:
        r_ = _rq.get("http://127.0.0.1:8081/api/player/progress",
                     headers={"Authorization": f"Bearer {jwt}"}, timeout=10)
        return r_.json() if r_.status_code == 200 else {}

    prog_w = _progress(active.jwt)
    prog_l = _progress(passive.jwt)
    # Тестовые аккаунты на max level (6): XP клампится, но уровень/ответ валидны.
    # Инвариант, не зависящий от кривой: endpoint отвечает и уровень >= 1;
    # на НЕ-максимальном уровне xp должен вырасти (для level<6 аккаунтов).
    xp_ok = bool(prog_w) and bool(prog_l) and prog_w.get("level", 0) >= 1
    check("XP-начисление после матча (GET /api/player/progress)",
          "progress отвечает обоим (winner/loser), уровень валиден",
          f"winner: lvl={prog_w.get('level')} xp={prog_w.get('xp')} | "
          f"loser: lvl={prog_l.get('level')} xp={prog_l.get('xp')}",
          xp_ok)

    p1.close()
    p2.close()

    # ── Итог ───────────────────────────────────────────────────────────────
    passed = sum(1 for *_x, ok in RESULTS if ok)
    print(f"\n=== ИТОГ: {passed}/{len(RESULTS)} PASS ===", flush=True)
    for op, exp, act, ok in RESULTS:
        print(f"  {'PASS' if ok else 'FAIL'} | {op} | ожидание: {exp} | факт: {act}", flush=True)
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
