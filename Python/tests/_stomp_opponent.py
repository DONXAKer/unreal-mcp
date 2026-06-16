"""Headless STOMP-over-WebSocket opponent bot for WarCard full-game e2e.

Зачем: single-process multi-client PIE нестабилен на widget-enumeration
(см. memory project_multiclient_pie_limitation). Поэтому полный e2e гоняем как
1 реальный UE-клиент (PIE n=1, без multi-client бага) + этот headless-оппонент
во втором «процессе» (отдельный WS-коннект к серверу).

Два режима (флаг full_play конструктора OpponentBot):

МИНИМАЛЬНЫЙ (full_play=False, дефолт) — бот делает МИНИМУМ, чтобы серверная игра
прогрессировала, а реальный UE-клиент прошёл весь поток
login → draft → deployment → battle → result (используется smoke_fullgame_solo.py):
  * matchmaking/start;
  * на своём ходу драфта (snake) — пик валидного юнита своей фракции;
  * в DEPLOYMENT — расстановка своих юнитов в своей зоне (place-unit);
  * в DICE_ROLL_1/2 и MULLIGAN — phase-ready (сервер авто-проброс);
  * в BATTLE — idle/pass (UE-клиент завершает партию капитуляцией).

ПОЛНЫЙ (full_play=True, TEST-E2E-001) — бот сам ведёт весь матч до GameResult,
без UE; используется smoke_headless_twobot.py (два таких бота). Доп. фазы:
  * MULLIGAN — явная команда /app/game/mulligan (keep-all), не только phase-ready;
  * BATTLE   — на своём ходу: /app/game/free-move (свободное движение к врагу),
               затем /app/game/select-action-card + /app/game/execute-action (атака
               картой по матрице урона из /state snapshot), иначе /app/game/end-turn;
  * финиш    — бот-«финишер» шлёт /app/game/surrender после того, как обе механики
               (≥1 движение и ≥1 атака) продемонстрированы — детерминированный конец
               с зафиксированным победителем. Координация — через BattleCoordinator.

Зависимостей нет: минимальный WebSocket-клиент (RFC6455) на stdlib + STOMP 1.2
поверх. PyPI в окружении недоступен, ставить websocket-client нечем.

Протокол (источник истины — сервер):
  WS endpoint:  ws://host:8081/ws   (raw STOMP, subprotocol v12.stomp)
  app prefix:   /app
  auth:         STOMP CONNECT header  Authorization: Bearer <jwt>
                (StompAuthChannelInterceptor → Principal = playerId = jwt.sub)
"""

from __future__ import annotations

import base64
import json
import os
import re
import socket
import struct
import threading
import time
from collections.abc import Callable
from typing import Any

# ----------------------------------------------------------------------------
# Минимальный WebSocket-клиент (RFC 6455), только то, что нужно для STOMP-текста.
# ----------------------------------------------------------------------------


class MinimalWebSocket:
    """Клиентский WebSocket поверх голого сокета. Текстовые фреймы, маскирование
    клиент→сервер, обработка ping/close. Не потокобезопасен на запись — слать из
    одного потока (наш случай: пишем из bot-логики, читаем в recv-треде)."""

    def __init__(self, host: str, port: int, path: str, subprotocol: str = "v12.stomp"):
        self._sock = socket.create_connection((host, port), timeout=10)
        self._recv_buf = b""
        self._closed = False
        self._handshake(host, port, path, subprotocol)
        # КРИТИЧНО: убрать таймаут после хендшейка. create_connection(timeout=10)
        # ставит 10s и на recv — recv-тред умер бы через 10s простоя (а матч в
        # solo-сценарии наступает через ~50s ожидания в очереди). Блокирующий
        # recv ждёт фреймы сколько нужно; close() рвёт его закрытием сокета.
        self._sock.settimeout(None)

    def _handshake(self, host: str, port: int, path: str, subprotocol: str) -> None:
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"Sec-WebSocket-Protocol: {subprotocol}\r\n"
            "\r\n"
        )
        self._sock.sendall(req.encode())
        # Читаем заголовки ответа до \r\n\r\n.
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self._sock.recv(4096)
            if not chunk:
                raise ConnectionError("WS handshake: соединение закрыто сервером")
            resp += chunk
        head, _, rest = resp.partition(b"\r\n\r\n")
        if b"101" not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(f"WS handshake провален: {head[:120]!r}")
        self._recv_buf = rest  # данные после хедшейка (если уже пришли фреймы)

    # --- низкоуровневое чтение N байт из сокета с учётом буфера ---
    def _read_exact(self, n: int) -> bytes:
        while len(self._recv_buf) < n:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ConnectionError("WS: соединение закрыто во время чтения")
            self._recv_buf += chunk
        out, self._recv_buf = self._recv_buf[:n], self._recv_buf[n:]
        return out

    def send_text(self, text: str) -> None:
        """Отправить один текстовый фрейм (FIN=1, opcode=0x1, mask=1)."""
        payload = text.encode("utf-8")
        header = bytearray([0x81])  # FIN + text
        length = len(payload)
        mask_bit = 0x80
        if length < 126:
            header.append(mask_bit | length)
        elif length < 65536:
            header.append(mask_bit | 126)
            header += struct.pack(">H", length)
        else:
            header.append(mask_bit | 127)
            header += struct.pack(">Q", length)
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self._sock.sendall(bytes(header) + mask + masked)

    def recv_text(self) -> str | None:
        """Прочитать один фрейм. Возвращает текст (для 0x1/0x2), либо None при
        close. Ping автоматически отвечается pong'ом и чтение продолжается."""
        while True:
            b0, b1 = self._read_exact(2)
            opcode = b0 & 0x0F
            masked = (b1 & 0x80) != 0
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._read_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._read_exact(8))[0]
            mask = self._read_exact(4) if masked else b""
            data = self._read_exact(length) if length else b""
            if masked and data:
                data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))

            if opcode == 0x8:  # close
                self._closed = True
                return None
            if opcode == 0x9:  # ping → pong
                self._send_control(0xA, data)
                continue
            if opcode == 0xA:  # pong
                continue
            # 0x1 text / 0x2 binary / 0x0 continuation — отдаём как текст
            return data.decode("utf-8", errors="replace")

    def _send_control(self, opcode: int, payload: bytes) -> None:
        header = bytearray([0x80 | opcode, 0x80 | len(payload)])
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self._sock.sendall(bytes(header) + mask + masked)

    def close(self) -> None:
        if self._closed:
            return
        try:
            self._send_control(0x8, b"")
        except Exception:
            pass
        try:
            self._sock.close()
        except Exception:
            pass
        self._closed = True


# ----------------------------------------------------------------------------
# STOMP 1.2 поверх MinimalWebSocket.
# ----------------------------------------------------------------------------

NULL = "\x00"


class StompWs:
    """STOMP 1.2 клиент. Фоновый recv-тред парсит входящие MESSAGE-фреймы и
    дёргает on_message(destination, body_dict)."""

    def __init__(self, host: str, port: int, path: str = "/ws"):
        self._ws = MinimalWebSocket(host, port, path)
        self._host = host
        self._sub_id = 0
        self._connected = threading.Event()
        self._stop = False
        self._frame_buf = ""
        # Сериализует запись в сокет: send_json/subscribe могут вызываться из
        # recv-треда (внутри on_message) И из Timer-тредов (отложенные end-turn,
        # боевой play). MinimalWebSocket.send_text не потокобезопасен на запись —
        # без лока два фрейма перемешаются и STOMP-парсер сервера упадёт.
        self._send_lock = threading.Lock()
        self.on_message: Callable[[str, dict[str, Any]], None] | None = None
        self._reader = threading.Thread(target=self._recv_loop, daemon=True)
        self._reader.start()

    def connect(self, token: str, timeout: float = 10.0) -> bool:
        frame = (
            "CONNECT\n"
            "accept-version:1.2\n"
            f"host:{self._host}\n"
            f"Authorization:Bearer {token}\n"
            "heart-beat:0,0\n"
            "\n" + NULL
        )
        with self._send_lock:
            self._ws.send_text(frame)
        return self._connected.wait(timeout)

    def subscribe(self, destination: str) -> None:
        self._sub_id += 1
        frame = (
            "SUBSCRIBE\n"
            f"id:sub-{self._sub_id}\n"
            f"destination:{destination}\n"
            "\n" + NULL
        )
        with self._send_lock:
            self._ws.send_text(frame)

    def send_json(self, destination: str, body: dict[str, Any]) -> None:
        payload = json.dumps(body)
        frame = (
            "SEND\n"
            f"destination:{destination}\n"
            "content-type:application/json\n"
            f"content-length:{len(payload.encode('utf-8'))}\n"
            "\n" + payload + NULL
        )
        with self._send_lock:
            self._ws.send_text(frame)

    def _recv_loop(self) -> None:
        while not self._stop:
            try:
                text = self._ws.recv_text()
            except Exception:
                break
            if text is None:
                break
            self._frame_buf += text
            # STOMP-фреймы разделены NULL-байтом; в одном WS-фрейме их может быть
            # несколько (или фрейм может прийти частями — тогда NULL ещё нет).
            while NULL in self._frame_buf:
                raw, self._frame_buf = self._frame_buf.split(NULL, 1)
                self._handle_frame(raw)

    def _handle_frame(self, raw: str) -> None:
        raw = raw.lstrip("\n")
        if not raw:
            return
        head, _, body = raw.partition("\n\n")
        lines = head.split("\n")
        command = lines[0].strip()
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                k, _, v = line.partition(":")
                headers[k] = v
        if command == "CONNECTED":
            self._connected.set()
            return
        if command == "MESSAGE":
            dest = headers.get("destination", "")
            parsed: dict[str, Any] = {}
            body = body.rstrip("\x00").strip()
            if body:
                try:
                    parsed = json.loads(body)
                except Exception:
                    parsed = {"_raw": body}
            if self.on_message:
                try:
                    self.on_message(dest, parsed)
                except Exception as exc:
                    print(f"[bot] on_message error: {exc}")

    def close(self) -> None:
        self._stop = True
        self._ws.close()


# ----------------------------------------------------------------------------
# Бот-оппонент: минимальная игровая логика.
# ----------------------------------------------------------------------------

# Зоны деплоя (FIX-DEPLOY-ZONE-001): только крайний столбец — red (player1) x=0,
# blue (player2) x=7; 5 разных клеток в столбце (занятость проверяется).
_ZONE_CELLS = {
    "red": [(0, 0), (0, 1), (0, 2), (0, 3), (0, 4)],
    "blue": [(7, 0), (7, 1), (7, 2), (7, 3), (7, 4)],
}
# Транзитивные фазы, где шлём phase-ready (сервер авто-прокидывает дальше).
_READY_PHASES = {"WAITING_FOR_START", "DICE_ROLL_1", "DICE_ROLL_2", "MULLIGAN"}

# Типы карт, годные для execute-action как «атака» (см. server ActionCard.ActionType).
_ATTACK_CARD_TYPES = ("BASIC_ATTACK", "SPECIAL_ATTACK")
# unitType цели → ключ матрицы урона в UnitSnapshotDto (/state snapshot).
_DMG_KEY_BY_TYPE = {
    "AIR": "damageVsAir",
    "INFANTRY": "damageVsInfantry",
    "SUPPORT": "damageVsSupport",
    "HEAVY_TECH": "damageVsHeavyTech",
}
_WINNER_RE = re.compile(r"[Пп]обедитель[:\s]+([0-9a-fA-F-]{36})")


class BattleCoordinator:
    """Разделяемый двумя ботами в одном процессе счётчик прогресса боя + правило
    финиша. Нужен, чтобы «финишер» сдался лишь ПОСЛЕ того, как в матче реально
    продемонстрированы обе механики (≥1 свободное движение И ≥1 атака) — иначе
    e2e не покрыл бы то, ради чего написан. hard_turn_cap — страховка от зависания:
    финишер сдаётся безусловно, если бой затянулся (тогда тест проверит счётчики и
    честно зафейлит отсутствие атаки/движения, а не повиснет)."""

    def __init__(self, min_finish_turn: int = 2, hard_turn_cap: int = 16):
        self._lock = threading.Lock()
        self.moves_done = 0
        self.attacks_done = 0
        self.min_finish_turn = min_finish_turn
        self.hard_turn_cap = hard_turn_cap

    def record_move(self) -> None:
        with self._lock:
            self.moves_done += 1

    def record_attack(self) -> None:
        with self._lock:
            self.attacks_done += 1

    def should_finish(self, turn_number: int) -> bool:
        with self._lock:
            if turn_number >= self.hard_turn_cap:
                return True
            return (self.moves_done >= 1 and self.attacks_done >= 1
                    and turn_number >= self.min_finish_turn)


def _jwt_subject(token: str) -> str:
    """Извлечь playerId (claim sub) из JWT без проверки подписи."""
    try:
        payload_b64 = token.split(".")[1]
        payload_b64 += "=" * (-len(payload_b64) % 4)
        claims = json.loads(base64.urlsafe_b64decode(payload_b64))
        return str(claims.get("sub", ""))
    except Exception:
        return ""


class OpponentBot:
    """Один headless-игрок. Запускать через .start() (неблокирующе) после
    логина (есть JWT). Сам матчится, играет драфт/деплой, фазы-проброс."""

    def __init__(self, token: str, name: str = "bot",
                 server_host: str = "127.0.0.1", server_port: int = 8081,
                 verbose: bool = True, full_play: bool = False,
                 coordinator: BattleCoordinator | None = None,
                 is_finisher: bool = False):
        self.token = token
        self.name = name
        self.my_id = _jwt_subject(token)
        self.stomp = StompWs(server_host, server_port)
        self.stomp.on_message = self._on_message
        self.game_id: str | None = None
        self.faction_ids: list[str] = []
        self.my_picks: list[str] = []
        self._readied_phases: set[str] = set()
        self._deployed = False
        self._pulse_started = False
        self._reached_battle = threading.Event()
        self._verbose = verbose
        self._lock = threading.Lock()
        # --- full-play (TEST-E2E-001): полноценный матч до GameResult ---
        # full_play=False  → старое минимальное поведение (matchmaking/draft/deploy
        #   + idle-pass в бою). Используется smoke_fullgame_solo.py (1 UE + 1 бот) —
        #   там бой ведёт реальный UE-клиент, бот лишь пасует. НЕ ломаем его.
        # full_play=True   → бот сам играет бой: явный mulligan, свободное движение,
        #   атака картой, и (если is_finisher) сдача после демонстрации механик.
        self.full_play = full_play
        self.coordinator = coordinator
        self.is_finisher = is_finisher
        self.battle: dict[str, Any] = {}        # последний /state snapshot (gamePhase=BATTLE)
        self.my_hand: list[dict[str, str]] = []  # [{id,type}] — рука из cards-update/result
        self._mulliganed = False
        self._last_acted_turn = -1
        self._game_over = threading.Event()
        self.winner_id: str | None = None
        self._battle_lock = threading.Lock()
        self._pending: dict[str, tuple[threading.Event, dict[str, Any]]] = {}

    def _log(self, msg: str) -> None:
        if self._verbose:
            print(f"[bot:{self.name}] {msg}")

    def start(self) -> bool:
        if not self.stomp.connect(self.token):
            self._log("CONNECT не подтверждён (нет CONNECTED)")
            return False
        self._log(f"connected, playerId={self.my_id[:8]}…")
        # user-destination подписки (Principal = playerId).
        self.stomp.subscribe("/user/queue/matchmaking/game-found")
        self.stomp.subscribe("/user/queue/matchmaking/available-units")
        self.stomp.subscribe("/user/queue/matchmaking/status")
        time.sleep(0.3)
        self.stomp.send_json("/app/matchmaking/start", {})
        self._log("matchmaking/start отправлен")
        return True

    def wait_battle(self, timeout: float) -> bool:
        return self._reached_battle.wait(timeout)

    def wait_game_over(self, timeout: float) -> bool:
        """Дождаться завершения партии (GameResult/победитель). True если игра
        закончилась в пределах timeout."""
        return self._game_over.wait(timeout)

    def close(self) -> None:
        self.stomp.close()

    # --- обработка входящих ---
    def _on_turn_changed(self, body: dict[str, Any]) -> None:
        """В BATTLE — пасуем ход бота: посылаем end-turn с задержкой (даём UE
        увидеть «Ход оппонента»). Без гейта `_reached_battle`: turn-changed
        эмитится сервером только в BATTLE — сам факт его прихода означает бой,
        а гейт ловил race с broadcast'ом фазы BATTLE."""
        active = body.get("activePlayerId", "")
        # Подстраховка: считать, что бой начался (для wait_battle) — turn-changed
        # эмитится сервером только в BATTLE.
        self._reached_battle.set()
        if active != self.my_id:
            return
        gid = self.game_id
        if not gid:
            return
        # full_play: бой ведём по /state snapshot (там свежие позиции/юниты),
        # turn-changed лишь будит. В минимальном режиме — старый blind-pass.
        if self.full_play:
            return
        def _pass():
            if self.game_id == gid:
                self.stomp.send_json("/app/game/end-turn", {"gameId": gid})
                self._log("end-turn (pass)")
        threading.Timer(1.5, _pass).start()

    def _on_message(self, dest: str, body: dict[str, Any]) -> None:
        if dest.endswith("/turn-changed"):
            self._on_turn_changed(body)
            return
        if dest.endswith("/game-found"):
            gid = body.get("gameId")
            if gid and not self.game_id:
                self.game_id = gid
                self._log(f"game-found: {gid}")
                for suffix in ("state", "result", "draft-update", "turn-changed"):
                    self.stomp.subscribe(f"/topic/game/{gid}/{suffix}")
                # cards-update — per-user (Principal=playerId), нужен боту в full_play
                # чтобы знать руку для execute-action. Безвреден в минимальном режиме.
                self.stomp.subscribe(f"/user/topic/game/{gid}/cards-update")
                # ОТЛОЖЕННЫЙ «пинок» (а не мгновенный): бот headless и иначе
                # обгоняет реального UE-клиента — пинает start-game за мс до того,
                # как UE успел подписаться на /topic/.../result, и UE пропускает
                # broadcast DICE_ROLL_1 → не бросает кубик → DICE_ROLL_1 (нужен
                # бросок ОБОИХ) залипает. Даём UE фору: он сам стартует игру
                # (как в multi-client smoke), а наш пинок через 8с — лишь
                # фоллбэк (start-game идемпотентен; если уже DICE_ROLL_1 —
                # phase-ready просто добавит наш бросок). Сервер игнорирует поле
                # phase, читает gameState.
                threading.Timer(8.0, self._delayed_kick, args=(gid,)).start()
            return
        if dest.endswith("/available-units"):
            units = body.get("units") or []
            self.faction_ids = [u.get("unitId") for u in units if u.get("unitId")]
            self._log(f"available-units: {len(self.faction_ids)} юнитов")
            return
        if dest.endswith("/draft-update"):
            self._on_draft_update(body)
            return
        if dest.endswith("/cards-update"):
            self._capture_hand_from_cards_update(body)
            return
        if dest.endswith("/state"):
            self._on_state_snapshot(body)
            return
        if dest.endswith("/result"):
            self._on_result(body)
            return
        # прочее — источник текущей фазы (на всякий случай)
        phase = self._extract_phase(body)
        if phase:
            self._on_phase(phase)

    # --- захват состояния/руки ---
    def _capture_hand_from_cards_update(self, body: dict[str, Any]) -> None:
        cards = body.get("handCards")
        if isinstance(cards, list):
            self.my_hand = [{"id": c.get("id", ""), "type": c.get("actionType", "")}
                            for c in cards if isinstance(c, dict)]

    def _capture_hand_from_state_data(self, data: dict[str, Any]) -> None:
        """Рука из enriched /result (GameState.toData(): actionCards.hand_<myId>)."""
        ac = data.get("actionCards")
        if isinstance(ac, dict):
            hand = ac.get("hand_" + self.my_id)
            if isinstance(hand, list):
                self.my_hand = [{"id": c.get("id", ""), "type": c.get("type", "")}
                                for c in hand if isinstance(c, dict)]

    def _on_result(self, body: dict[str, Any]) -> None:
        ct = body.get("commandType", "")
        success = bool(body.get("success"))
        data = body.get("data")
        if isinstance(data, dict):
            self._capture_hand_from_state_data(data)
        # разбудить ожидающего отправителя боевой команды (request/response)
        pend = self._pending.pop(ct, None)
        if pend is not None:
            ev, holder = pend
            holder["success"] = success
            holder["message"] = body.get("message", "")
            holder["data"] = data
            ev.set()
        # Завершение партии распознаём СТРОГО по типу команды — иначе ловим
        # ложняк на roll-dice: его result тоже содержит UUID + слово «Победитель»
        # (победитель БРОСКА, ходит первым), а не победителя ИГРЫ.
        is_end = False
        winner = None
        if ct == "surrender" and success:
            is_end = True
            m = _WINNER_RE.search(str(body.get("message", "")))
            if m:
                winner = m.group(1)
        elif (ct in ("execute-action", "end-game")
              and isinstance(data, dict) and data.get("winnerId")):
            is_end = True
            winner = str(data["winnerId"])
        if is_end:
            self.winner_id = winner
            if not self._game_over.is_set():
                self._log(f"game-over: winner={winner or '?'} (via {ct})")
            self._game_over.set()
        # фаза из toData (DEPLOYMENT/MULLIGAN/BATTLE и т.п.)
        phase = self._extract_phase(body)
        if phase:
            self._on_phase(phase)

    def _on_state_snapshot(self, body: dict[str, Any]) -> None:
        """/state = GameStateSnapshotDto: gamePhase, activePlayerId, turnNumber,
        apRemaining, units[] (с матрицей урона). Источник истины для боя."""
        phase = body.get("gamePhase")
        if isinstance(phase, str):
            self.battle = body
            self._on_phase(phase)
            if (self.full_play and phase == "BATTLE"
                    and body.get("activePlayerId") == self.my_id):
                self._maybe_play_battle_turn(body)

    @staticmethod
    def _extract_phase(body: dict[str, Any]) -> str | None:
        if isinstance(body.get("phase"), str):
            return body["phase"]
        if isinstance(body.get("gamePhase"), str):  # /state snapshot
            return body["gamePhase"]
        data = body.get("data")
        if isinstance(data, dict) and isinstance(data.get("phase"), str):
            return data["phase"]
        return None

    def _ensure_draft_pulse(self) -> None:
        """Запустить периодический phase-ready на время драфта (один раз). Сервер в
        DRAFT по phase-ready ре-броадкастит draft-update (FIX-DRAFT-SYNC) → оба клиента
        освежают чей ход, и пропустивший апдейт UE-клиент восстанавливается (топик
        /draft-update не реплеится). Стартуем по первому draft-update — игра уже в
        DRAFT, поэтому нет гонки со start-game (которой опасается _delayed_kick)."""
        if self._pulse_started or not self.game_id:
            return
        self._pulse_started = True
        self._draft_pulse(self.game_id)

    def _draft_pulse(self, gid: str) -> None:
        # Стоп, когда драфт закончился (наступил DEPLOYMENT) или игра сменилась.
        if self.game_id != gid or self._deployed:
            return
        self.stomp.send_json("/app/game/phase-ready", {"gameId": gid, "phase": "DRAFT"})
        threading.Timer(3.0, self._draft_pulse, args=(gid,)).start()

    def _on_draft_update(self, body: dict[str, Any]) -> None:
        self._ensure_draft_pulse()
        with self._lock:
            pick_number = body.get("pickNumber", 0)
            current = body.get("currentPickPlayerId", "")
            if pick_number >= 10 or current != self.my_id:
                return
            taken = set(body.get("picksA") or []) | set(body.get("picksB") or [])
            taken |= set(self.my_picks)
            candidate = next((u for u in self.faction_ids if u not in taken), None)
            if not candidate:
                self._log("драфт: нет доступного кандидата (?)")
                return
            self.my_picks.append(candidate)
            self.stomp.send_json("/app/game/select-unit",
                                 {"gameId": self.game_id, "unitId": candidate})
            self._log(f"draft pick #{pick_number} -> {candidate}")

    def _delayed_kick(self, gid: str) -> None:
        """Фоллбэк-пинок через паузу: к этому моменту UE-клиент уже подписан и,
        как правило, сам стартовал игру. Сервер читает фазу из gameState, поэтому
        phase-ready корректен независимо от поля phase."""
        if self.game_id != gid:
            return
        self._readied_phases.add("WAITING_FOR_START")
        self.stomp.send_json("/app/game/phase-ready",
                             {"gameId": gid, "phase": "WAITING_FOR_START"})
        self._log("phase-ready: WAITING_FOR_START (delayed kick)")

    def _on_phase(self, phase: str) -> None:
        with self._lock:
            # full_play: явный mulligan вместо авто-phase-ready (покрывает реальный
            # mulligan-путь сервера + логирует решение). keep-all = пустой сброс.
            if phase == "MULLIGAN" and self.full_play:
                if not self._mulliganed:
                    self._mulliganed = True
                    self._readied_phases.add("MULLIGAN")  # подавить phase-ready дубль
                    self.stomp.send_json("/app/game/mulligan",
                                         {"gameId": self.game_id, "cardIdsToDiscard": []})
                    self._log("mulligan: keep-all (0 карт сброшено)")
                return
            if phase in _READY_PHASES and phase not in self._readied_phases:
                self._readied_phases.add(phase)
                self.stomp.send_json("/app/game/phase-ready",
                                     {"gameId": self.game_id, "phase": phase})
                self._log(f"phase-ready: {phase}")
            elif phase == "DEPLOYMENT" and not self._deployed:
                self._deployed = True
                self._deploy_units()
            elif phase == "BATTLE":
                if not self.full_play and not self._reached_battle.is_set():
                    self._log("BATTLE достигнут — оппонент в режиме ожидания")
                    # «Kick» end-turn (минимальный режим): сервер не шлёт turn-changed
                    # на старте боя. Если первый ход бота — этот end-turn пройдёт,
                    # дальше цикл turn-changed работает. Если не его — сервер отвергнет.
                    gid = self.game_id
                    if gid:
                        def _kick():
                            if self.game_id == gid:
                                self.stomp.send_json("/app/game/end-turn", {"gameId": gid})
                                self._log("end-turn (battle kick)")
                        threading.Timer(2.0, _kick).start()
                self._reached_battle.set()

    # ------------------------------------------------------------------
    # Боевая логика (full_play): свободное движение + атака + сдача-финиш.
    # ------------------------------------------------------------------
    def _register_pending(self, command_type: str) -> tuple[threading.Event, dict[str, Any]]:
        ev = threading.Event()
        holder: dict[str, Any] = {}
        self._pending[command_type] = (ev, holder)
        return ev, holder

    def _maybe_play_battle_turn(self, snapshot: dict[str, Any]) -> None:
        """Запланировать один проход боевого хода для текущего turnNumber (ровно
        раз — free-move шлёт повторный /state с тем же turn, не должен ре-триггерить)."""
        turn = int(snapshot.get("turnNumber", 0))
        with self._battle_lock:
            if self._game_over.is_set() or turn == self._last_acted_turn:
                return
            self._last_acted_turn = turn
        threading.Timer(0.4, self._play_battle_turn, args=(turn,)).start()

    def _play_battle_turn(self, turn: int) -> None:
        if self._game_over.is_set() or not self.game_id:
            return
        snapshot = self.battle
        gid = self.game_id
        # Финишер: после демонстрации обеих механик (или по hard-cap) сдаёмся —
        # детерминированный конец партии с зафиксированным победителем.
        if (self.is_finisher and self.coordinator is not None
                and self.coordinator.should_finish(turn)):
            self.stomp.send_json("/app/game/surrender",
                                 {"gameId": gid, "reason": "e2e: механики продемонстрированы"})
            self._log(f"surrender (финиш на ходу {turn})")
            return

        moved = self._try_free_move(gid, snapshot)
        if moved and self.coordinator is not None:
            self.coordinator.record_move()

        attacked = self._try_attack(gid, snapshot)
        if attacked:
            if self.coordinator is not None:
                self.coordinator.record_attack()
            # успешная атака авто-завершает ход на сервере — end-turn не нужен.
            return

        # атака не удалась → явно завершаем ход, чтобы матч не завис.
        self.stomp.send_json("/app/game/end-turn", {"gameId": gid})
        self._log(f"end-turn (ход {turn}, без атаки)")

    def _my_units(self, snapshot: dict[str, Any]) -> list[dict[str, Any]]:
        return [u for u in snapshot.get("units", [])
                if u.get("ownerId") == self.my_id and u.get("isAlive", True)]

    def _enemy_units(self, snapshot: dict[str, Any]) -> list[dict[str, Any]]:
        return [u for u in snapshot.get("units", [])
                if u.get("ownerId") and u.get("ownerId") != self.my_id and u.get("isAlive", True)]

    @staticmethod
    def _dist(a: dict[str, Any], b: dict[str, Any]) -> int:
        return abs(int(a["gridX"]) - int(b["gridX"])) + abs(int(a["gridY"]) - int(b["gridY"]))

    def _try_free_move(self, gid: str, snapshot: dict[str, Any]) -> bool:
        if int(snapshot.get("apRemaining", 0)) < 1:
            return False
        mine = self._my_units(snapshot)
        foes = self._enemy_units(snapshot)
        if not mine or not foes:
            return False
        occupied = {(int(u["gridX"]), int(u["gridY"])) for u in snapshot.get("units", [])}
        width = int(snapshot.get("fieldWidth", 8))
        terrain = snapshot.get("terrain") or []
        # юнит, ближайший к врагу → двигаем его к ближайшему врагу на 1 клетку.
        mover = min(mine, key=lambda u: min(self._dist(u, f) for f in foes))
        target_foe = min(foes, key=lambda f: self._dist(mover, f))
        mx, my = int(mover["gridX"]), int(mover["gridY"])
        cur_dist = self._dist(mover, target_foe)
        best = None
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = mx + dx, my + dy
            if not (0 <= nx < width and 0 <= ny < 8):
                continue
            if (nx, ny) in occupied:
                continue
            # предпочитаем PLAIN (проходима всем) — избегаем непроходимого террейна.
            idx = ny * width + nx
            cell = terrain[idx] if 0 <= idx < len(terrain) else "PLAIN"
            if cell not in ("PLAIN", "ROAD", "CITY"):
                continue
            nd = abs(nx - int(target_foe["gridX"])) + abs(ny - int(target_foe["gridY"]))
            if nd < cur_dist and (best is None or nd < best[2]):
                best = (nx, ny, nd)
        if best is None:
            return False
        ev, holder = self._register_pending("free-move")
        self.stomp.send_json("/app/game/free-move",
                             {"gameId": gid, "unitId": mover["unitId"],
                              "targetX": best[0], "targetY": best[1]})
        if ev.wait(4.0) and holder.get("success"):
            self._log(f"free-move {mover['unitId']} -> ({best[0]},{best[1]})")
            return True
        return False

    def _try_attack(self, gid: str, snapshot: dict[str, Any]) -> bool:
        if int(snapshot.get("apRemaining", 0)) < 1:
            return False
        mine = self._my_units(snapshot)
        foes = self._enemy_units(snapshot)
        card = next((c for c in self.my_hand if c.get("type") in _ATTACK_CARD_TYPES), None)
        if not mine or not foes or not card:
            self._log(f"attack skip: mine={len(mine)} foes={len(foes)} "
                      f"hand={len(self.my_hand)} types={[c.get('type') for c in self.my_hand]}")
            return False
        # пара атакующий/цель с уроном > 0 по матрице из /state snapshot.
        pair = None
        for foe in sorted(foes, key=lambda f: min(self._dist(a, f) for a in mine)):
            dmg_key = _DMG_KEY_BY_TYPE.get(foe.get("unitType", ""), "")
            attacker = next((a for a in mine if dmg_key and int(a.get(dmg_key, 0)) > 0), None)
            if attacker is not None:
                pair = (attacker, foe)
                break
        if pair is None:
            self._log("attack skip: нет пары атакующий/цель с уроном>0")
            return False
        attacker, foe = pair
        # 1) выбрать карту
        ev_s, _ = self._register_pending("select-action-card")
        self.stomp.send_json("/app/game/select-action-card",
                             {"gameId": gid, "actionCardId": card["id"]})
        ev_s.wait(3.0)
        # 2) выполнить атаку выбранной картой
        ev_e, holder = self._register_pending("execute-action")
        self.stomp.send_json("/app/game/execute-action", {
            "gameId": gid,
            "actionCardId": card["id"],
            "targetX": int(foe["gridX"]),
            "targetY": int(foe["gridY"]),
            "targetUnitId": foe["unitId"],
            "attackerUnitId": attacker["unitId"],
        })
        if ev_e.wait(5.0) and holder.get("success"):
            self._log(f"attack {attacker['unitId']} -> {foe['unitId']} "
                      f"({card['type']}): {str(holder.get('message',''))[:60]}")
            return True
        self._log(f"attack отклонён: {str(holder.get('message',''))[:80]}")
        return False

    def _deploy_units(self) -> None:
        if not self.my_picks:
            self._log("деплой: нет своих пиков — пропуск")
            return
        faction = "red" if self.my_picks[0].endswith("-red") else "blue"
        cells = _ZONE_CELLS[faction]
        self._log(f"деплой {len(self.my_picks)} юнитов ({faction}) в свою зону")
        for unit_id, (x, y) in zip(self.my_picks, cells, strict=False):
            self.stomp.send_json("/app/game/place-unit",
                                 {"gameId": self.game_id, "unitId": unit_id, "x": x, "y": y})
            time.sleep(0.3)


# ----------------------------------------------------------------------------
# Standalone self-test: 2 бота матчатся и играют до BATTLE (UE не нужен).
# ----------------------------------------------------------------------------

def _selftest() -> int:
    import sys
    sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
    from tests._fixtures import ensure_test_user, get_test_jwt, is_server_alive

    if not is_server_alive():
        print("FAIL: сервер не отвечает")
        return 1

    suffix = str(int(time.time()) % 100000)
    creds = [(f"bot_a_{suffix}", "Test1234", f"bot_a_{suffix}@ex.com"),
             (f"bot_b_{suffix}", "Test1234", f"bot_b_{suffix}@ex.com")]
    bots = []
    for login, pwd, email in creds:
        ensure_test_user(login, pwd, email)
        jwt = get_test_jwt(login, pwd)
        bots.append(OpponentBot(jwt, name=login.split("_")[1]))

    for b in bots:
        if not b.start():
            print("FAIL: бот не подключился")
            return 1
        time.sleep(1.0)

    ok = all(b.wait_battle(90) for b in bots)
    for b in bots:
        b.close()
    print("=== SELFTEST:", "PASS — оба бота дошли до BATTLE" if ok else "FAIL — BATTLE не достигнут", "===")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(_selftest())
