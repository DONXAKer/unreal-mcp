"""Headless STOMP-over-WebSocket opponent bot for WarCard full-game e2e.

Зачем: single-process multi-client PIE нестабилен на widget-enumeration
(см. memory project_multiclient_pie_limitation). Поэтому полный e2e гоняем как
1 реальный UE-клиент (PIE n=1, без multi-client бага) + этот headless-оппонент
во втором «процессе» (отдельный WS-коннект к серверу).

Бот делает МИНИМУМ, чтобы серверная игра прогрессировала, а реальный UE-клиент
прошёл весь поток login → draft → deployment → battle → result:
  * matchmaking/start;
  * на своём ходу драфта (snake) — пик валидного юнита своей фракции;
  * в DEPLOYMENT — расстановка своих 5 юнитов в своей зоне (place-unit);
  * в DICE_ROLL_1/2 и MULLIGAN — phase-ready (сервер авто-проброс);
  * в BATTLE — idle (UE-клиент завершает партию капитуляцией).

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
import hashlib
import json
import os
import socket
import struct
import threading
import time
from typing import Any, Callable

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
                except Exception as exc:  # noqa: BLE001
                    print(f"[bot] on_message error: {exc}")

    def close(self) -> None:
        self._stop = True
        self._ws.close()


# ----------------------------------------------------------------------------
# Бот-оппонент: минимальная игровая логика.
# ----------------------------------------------------------------------------

# Зоны деплоя: red (player1) x∈{0,1,2}, blue (player2) x∈{5,6,7}.
_ZONE_CELLS = {
    "red": [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1)],
    "blue": [(5, 0), (6, 0), (7, 0), (5, 1), (6, 1)],
}
# Транзитивные фазы, где шлём phase-ready (сервер авто-прокидывает дальше).
_READY_PHASES = {"WAITING_FOR_START", "DICE_ROLL_1", "DICE_ROLL_2", "MULLIGAN"}


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
                 verbose: bool = True):
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
        self._reached_battle = threading.Event()
        self._verbose = verbose
        self._lock = threading.Lock()

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

    def close(self) -> None:
        self.stomp.close()

    # --- обработка входящих ---
    def _on_turn_changed(self, body: dict[str, Any]) -> None:
        """В BATTLE — пасуем ход бота: посылаем end-turn с задержкой (даём UE
        увидеть «Ход оппонента»). Без гейта `_reached_battle`: turn-changed
        эмитится сервером только в BATTLE — сам факт его прихода означает бой,
        а гейт ловил race с broadcast'ом фазы BATTLE."""
        active = body.get("activePlayerId", "")
        if active != self.my_id:
            return
        gid = self.game_id
        if not gid:
            return
        # Подстраховка: считать, что бой начался (для wait_battle).
        self._reached_battle.set()
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
        # state / result — источник текущей фазы
        phase = self._extract_phase(body)
        if phase:
            self._on_phase(phase)

    @staticmethod
    def _extract_phase(body: dict[str, Any]) -> str | None:
        if isinstance(body.get("phase"), str):
            return body["phase"]
        data = body.get("data")
        if isinstance(data, dict) and isinstance(data.get("phase"), str):
            return data["phase"]
        return None

    def _on_draft_update(self, body: dict[str, Any]) -> None:
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
            if phase in _READY_PHASES and phase not in self._readied_phases:
                self._readied_phases.add(phase)
                self.stomp.send_json("/app/game/phase-ready",
                                     {"gameId": self.game_id, "phase": phase})
                self._log(f"phase-ready: {phase}")
            elif phase == "DEPLOYMENT" and not self._deployed:
                self._deployed = True
                self._deploy_units()
            elif phase == "BATTLE":
                if not self._reached_battle.is_set():
                    self._log("BATTLE достигнут — оппонент в режиме ожидания")
                    # «Kick» end-turn: сервер не шлёт turn-changed на старте боя
                    # (turn-changed эмитится в ответ на end-turn). Если первый ход
                    # бота — этот end-turn пройдёт, дальше цикл turn-changed работает.
                    # Если не его — сервер отвергнет (не твой ход), no-op.
                    gid = self.game_id
                    if gid:
                        def _kick():
                            if self.game_id == gid:
                                self.stomp.send_json("/app/game/end-turn", {"gameId": gid})
                                self._log("end-turn (battle kick)")
                        threading.Timer(2.0, _kick).start()
                self._reached_battle.set()

    def _deploy_units(self) -> None:
        if not self.my_picks:
            self._log("деплой: нет своих пиков — пропуск")
            return
        faction = "red" if self.my_picks[0].endswith("-red") else "blue"
        cells = _ZONE_CELLS[faction]
        self._log(f"деплой {len(self.my_picks)} юнитов ({faction}) в свою зону")
        for unit_id, (x, y) in zip(self.my_picks, cells):
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
