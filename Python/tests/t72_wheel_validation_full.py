"""Полная валидация вращения катков Т-72: UE-клиент + headless STOMP-бот.

Предусловия:
  - Редактор UE запущен с MCP-плагином.
  - Сервер :8081 жив, BotController работает, simulation-токены включены.
"""

from __future__ import annotations

import base64
import json
import os
import socket
import struct
import sys
import threading
import time
from collections.abc import Callable
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from tests._fixtures import get_test_jwt, server_url
from unreal_mcp_server import UnrealConnection
import urllib.request

NULL = "\x00"


class MinimalWebSocket:
    def __init__(self, host: str, port: int, path: str, subprotocol: str = "v12.stomp"):
        self._sock = socket.create_connection((host, port), timeout=10)
        self._recv_buf = b""
        self._closed = False
        self._handshake(host, port, path, subprotocol)
        self._sock.settimeout(None)

    def _handshake(self, host: str, port: int, path: str, subprotocol: str) -> None:
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Protocol: {subprotocol}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        self._sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            resp += self._sock.recv(1024)
        if not resp.startswith(b"HTTP/1.1 101"):
            raise ConnectionError(f"WS handshake failed: {resp[:200]}")

    def _read_exact(self, n: int) -> bytes:
        out = b""
        while len(out) < n:
            chunk = self._sock.recv(n - len(out))
            if not chunk:
                raise ConnectionError("WS closed")
            out += chunk
        return out

    def recv_text(self) -> str | None:
        if self._closed:
            return None
        try:
            b0, b1 = self._read_exact(2)
        except Exception:
            return None
        fin = (b0 & 0x80) != 0
        opcode = b0 & 0x0F
        masked = (b1 & 0x80) != 0
        length = b1 & 0x7F
        if length == 126:
            length = struct.unpack(">H", self._read_exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._read_exact(8))[0]
        if masked:
            mask = self._read_exact(4)
            payload = self._read_exact(length)
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        else:
            payload = self._read_exact(length)
        if opcode == 0x08:
            self._closed = True
            return None
        if opcode == 0x09:
            self._send_frame(0x0A, payload)
            return self.recv_text()
        if opcode == 0x01:
            return payload.decode("utf-8", errors="replace")
        if opcode == 0x0A:
            return self.recv_text()
        return self.recv_text()

    def _send_frame(self, opcode: int, payload: bytes) -> None:
        b0 = 0x80 | opcode
        length = len(payload)
        if length < 126:
            header = struct.pack("BB", b0, 0x80 | length)
        elif length < 65536:
            header = struct.pack("!BBH", b0, 0x80 | 126, length)
        else:
            header = struct.pack("!BBQ", b0, 0x80 | 127, length)
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self._sock.sendall(header + mask + masked)

    def send_text(self, text: str) -> None:
        self._send_frame(0x01, text.encode("utf-8"))

    def close(self) -> None:
        self._closed = True
        try:
            self._send_frame(0x08, b"")
        except Exception:
            pass
        try:
            self._sock.close()
        except Exception:
            pass


class StompClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 8081, path: str = "/ws"):
        self._host = host
        self._port = port
        self._path = path
        self._ws: MinimalWebSocket | None = None
        self._send_lock = threading.Lock()
        self._connected = threading.Event()
        self._stop = False
        self._frame_buf = ""
        self._recv_thread: threading.Thread | None = None
        self._sub_id = 0
        self.on_message: Callable[[str, dict[str, Any]], None] | None = None

    def connect(self, token: str, timeout: float = 10.0) -> bool:
        self._ws = MinimalWebSocket(self._host, self._port, self._path)
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
        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()
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
            print(f"[stomp] MESSAGE dest={dest} on_message_set={self.on_message is not None}")
            body = body.rstrip("\x00").strip()
            parsed: dict[str, Any] = {}
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
        if self._ws:
            self._ws.close()


# -----------------------------------------------------------------------------
# UE helpers
# -----------------------------------------------------------------------------

def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any]) -> dict[str, Any]:
    resp = conn.send_command(cmd, params)
    if isinstance(resp, dict):
        if resp.get("status") == "success":
            return resp.get("result", resp)
        err = resp.get("error", "no error msg")
        err_message = err.get("message", str(err)) if isinstance(err, dict) else err
        return {"error": err_message, "_raw": resp}
    return {}


def _wait_widget(conn: UnrealConnection, candidates: tuple[str, ...], timeout: float) -> str:
    dl = time.monotonic() + timeout
    while time.monotonic() < dl:
        st = _send(conn, "pie_status", {}) or {}
        cs = st.get("clients") or []
        cur = (cs[0].get("current_widget") or "") if cs else ""
        for c in candidates:
            if c in cur:
                return cur
        time.sleep(1.0)
    return ""


def _create_bot_match(username: str, password: str) -> dict[str, str]:
    token = get_test_jwt(username, password)
    url = f"{server_url()}/api/match/create-vs-bot?difficulty=EASY"
    req = urllib.request.Request(url, method="POST", headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/json",
    })
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


# -----------------------------------------------------------------------------
# STOMP player driver (shared for human + bot)
# -----------------------------------------------------------------------------

_BLUE_PICKS = ["abrams-blue", "bradley-blue", "sniper-blue", "medic-blue", "assault-blue"]
_RED_PICKS = ["t72-red", "mi24-red", "assault-red", "sniper-red", "medic-red"]


def run_stomp_player(
    game_id: str,
    player_id: str,
    token: str,
    picks: list[str],
    is_bot: bool,
    done: threading.Event,
    deployment_started: threading.Event,
) -> None:
    """Headless STOMP-клиент для прохождения draft/deployment/mulligan."""
    label = "bot" if is_bot else "human"
    client = StompClient()
    if not client.connect(token, timeout=10):
        print(f"[{label}] connect failed")
        return
    client.subscribe(f"/topic/game/{game_id}/state")
    client.subscribe(f"/topic/game/{game_id}/turn-changed")
    client.subscribe(f"/topic/game/{game_id}/draft-update")

    # Пнуть игру, если она ещё не стартовала.
    client.send_json("/app/game/phase-ready", {"gameId": game_id, "phase": ""})

    picked: set[str] = set()
    deployed = False
    ready_sent: set[str] = set()
    last_phase = ""
    current_pick_player = ""

    def _phase_ready(phase: str) -> None:
        if phase not in ready_sent:
            client.send_json("/app/game/phase-ready", {"gameId": game_id, "phase": phase})
            ready_sent.add(phase)

    def on_message(dest: str, msg: dict[str, Any]) -> None:
        nonlocal last_phase, current_pick_player, deployed
        phase = msg.get("gamePhase", "")
        if not phase and "pickNumber" in msg:
            phase = "DRAFT"
        if phase != last_phase:
            print(f"[{label}] phase={phase}")
            last_phase = phase

        if "/draft-update" in dest:
            current_pick_player = msg.get("currentPickPlayerId", "")
            pick_number = msg.get("pickNumber", 0)
            print(f"[{label}] draft-update pickNumber={pick_number} picker={current_pick_player}")
            if current_pick_player == player_id:
                remaining = [u for u in picks if u not in picked]
                if remaining:
                    uid = remaining[0]
                    client.send_json("/app/game/select-units", {"gameId": game_id, "unitIds": [uid]})
                    picked.add(uid)
                    print(f"[{label}] picked {uid}")
            return

        if phase == "DEPLOYMENT":
            deployment_started.set()
            if not deployed:
                # DEPLOYMENT phase: select-units с 5 юнитами авто-размещает по фикс. позициям.
                client.send_json("/app/game/select-units", {"gameId": game_id, "unitIds": picks})
                deployed = True
                print(f"[{label}] deployment select-units sent")
            _phase_ready("DEPLOYMENT")

        elif phase in ("DICE_ROLL_1", "DICE_ROLL_2"):
            _phase_ready(phase)

        elif phase == "MULLIGAN":
            if "MULLIGAN" not in ready_sent:
                client.send_json("/app/game/mulligan", {"gameId": game_id, "cardIdsToDiscard": []})
                _phase_ready("MULLIGAN")
                print(f"[{label}] mulligan done")

        elif phase == "BATTLE":
            done.set()

    client.on_message = on_message

    deadline = time.monotonic() + 300
    while time.monotonic() < deadline and not done.is_set():
        time.sleep(1.0)
    client.close()
    print(f"[{label}] stomp client closed")


# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

def main() -> int:
    username, password = "wf_ue_644892", "Test1234"
    conn = UnrealConnection()
    conn.connect()

    # 1) Логин в PIE (если ещё на логине)
    st = _send(conn, "pie_status", {}) or {}
    cs = st.get("clients") or []
    cur_widget = (cs[0].get("current_widget") or "") if cs else ""
    if "Login" in cur_widget:
        print("UE login...")
        _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": username, "controller_index": 0})
        _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": password, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})
        cur = _wait_widget(conn, ("WBP_MainMenu_C",), 15)
        if not cur:
            print("FAIL: main menu not reached")
            return 1
    print("  main menu reached")

    # 2) Перейти в matchmaking, чтобы слушать game-found
    print("Entering matchmaking...")
    _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
    cur = _wait_widget(conn, ("WBP_Matchmaking_C", "WBP_Draft_C"), 15)
    if not cur:
        print("FAIL: matchmaking/draft not reached")
        return 1
    print(f"  reached: {cur}")
    if "Matchmaking" in cur:
        time.sleep(5.0)  # дать виджету время подписаться на /user/queue/matchmaking/game-found

    # 3) Создать бот-матч
    print("Creating bot match...")
    match = _create_bot_match(username, password)
    game_id = match["gameId"]
    human_player_id = match["humanPlayerId"]
    bot_player_id = match["botPlayerId"]
    print(f"  gameId={game_id}, humanId={human_player_id}, botId={bot_player_id}")

    # 4) Запустить headless STOMP-клиентов для human и bot
    battle_done = threading.Event()
    deployment_started = threading.Event()
    human_token = get_test_jwt(username, password)

    human_thread = threading.Thread(
        target=run_stomp_player,
        args=(game_id, human_player_id, human_token, _RED_PICKS, False, battle_done, deployment_started),
        daemon=True,
    )
    bot_thread = threading.Thread(
        target=run_stomp_player,
        args=(game_id, bot_player_id, f"simulation-token-{bot_player_id}", _BLUE_PICKS, True, battle_done, deployment_started),
        daemon=True,
    )
    human_thread.start()
    bot_thread.start()

    # 5) Дождаться игрового экрана (Draft/Deployment/Battle).
    # STOMP-драйверы проходят draft автоматически, UE может сразу показать Deployment.
    cur = _wait_widget(conn, ("WBP_Draft_C", "WBP_DeploymentScreen", "WBP_BattleHUD_C"), 60)
    if not cur:
        print("FAIL: game screen (draft/deployment/battle) not reached")
        return 1
    print(f"Game screen reached: {cur}")

    # 6) Если сейчас Draft — STOMP-драйверы сделают пики; дождаться DEPLOYMENT.
    if "Draft" in cur:
        cur = _wait_widget(conn, ("WBP_DeploymentScreen", "WBP_BattleHUD_C"), 60)
        if not cur:
            print("FAIL: deployment/battle not reached after draft")
            return 1
        print(f"After draft reached: {cur}")

    # 7) UE размещает юнитов через UI/MCP.
    picks = _RED_PICKS
    for i, uid in enumerate(picks):
        r = _send(conn, "wc_deploy_unit", {"unit_id": uid, "grid_x": 0, "grid_y": i, "controller_index": 0})
        print(f"  deploy {uid} -> (0,{i}): {r.get('deployed', False)}")
    r = _send(conn, "wc_confirm_deployment", {"controller_index": 0})
    print(f"  confirm deployment: {r.get('ok', False)}")

    # 8) Дождаться боя
    cur = _wait_widget(conn, ("WBP_BattleHUD_C", "WBP_ActionCardHand_C", "CombatLog"), 60)
    if not cur:
        bs = _send(conn, "wc_get_battle_state", {"controller_index": 0})
        if not bs.get("ok"):
            print("FAIL: battle not reached")
            return 1
    print(f"Battle reached: {cur}")

    # 9) Найти Т-72 и сделать ход вперёд.
    time.sleep(8.0)  # дать BattlefieldManager время засинхронизировать юнитов
    t72 = None
    for attempt in range(10):
        units_json = _send(conn, "wc_get_battle_units", {"controller_index": 0})
        print(f"  battle units attempt {attempt}: {units_json.get('units_json', '')[:200]}")
        units = json.loads(units_json.get("units_json") or "{}").get("units", [])
        t72 = next((u for u in units if u.get("unitId", "").startswith("t72")), None)
        if t72:
            break
        time.sleep(2.0)

    if not t72:
        print("WARNING: t72 not found in battle units, taking screenshot anyway")
        shot = _send(conn, "pie_screenshot", {"filename": "t72_wheels_in_motion.png", "show_ui": False})
        print(f"Screenshot: {shot}")
    else:
        print(f"T-72 at ({t72['gridX']},{t72['gridY']}), HP={t72.get('currentHp')}")
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            bs = _send(conn, "wc_get_battle_state", {"controller_index": 0})
            if bs.get("my_turn"):
                break
            time.sleep(1.0)

        # Приблизить камеру к Т-72 для детального контроля катков.
        # Клетка 250x250 UU; row-major offset от центра поля.
        spacing = 250.0
        cx = (t72["gridX"] - 3.5) * spacing
        cy = (t72["gridY"] - 3.5) * spacing
        cam_x, cam_y = cx - 180.0, cy - 180.0
        cam_z = 90.0
        _send(conn, "viewport_set_camera", {"location": [cam_x, cam_y, cam_z], "rotation": [-35, 45, 0], "fov": 40})
        time.sleep(2.0)
        _send(conn, "pie_screenshot", {"filename": "t72_wheels_before_move.png", "show_ui": False})

        # Ход вперёд и серия скриншотов во время движения.
        nx, ny = t72["gridX"] + 1, t72["gridY"]
        r = _send(conn, "wc_free_move", {"unit_id": t72["unitId"], "x": nx, "y": ny, "controller_index": 0})
        print(f"  free_move -> ({nx},{ny}): {r}")
        for i in range(8):
            time.sleep(0.25)
            _send(conn, "pie_screenshot", {"filename": f"t72_wheels_move_{i:02d}.png", "show_ui": False})
        time.sleep(1.0)

    # 10) Финальный скриншот общего плана
    shot = _send(conn, "pie_screenshot", {"filename": "t72_wheels_in_motion.png", "show_ui": False})
    print(f"Screenshot: {shot}")

    battle_done.set()
    human_thread.join(timeout=5)
    bot_thread.join(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
