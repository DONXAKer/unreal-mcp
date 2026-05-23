"""Shared helpers for smoke_* scripts.

Smoke tests are *standalone* — они подключаются напрямую к плагину на
127.0.0.1:55557 и не используют FastMCP-обвязку из unreal_mcp_server.py.
Это позволяет запускать их без MCP-клиента (Claude/Cursor), просто
`python -m tests.smoke_blueprint`.

Wire format (см. EpicUnrealMCPBridge.cpp):
    request:   {"type": <command>, "params": {...}}  — без trailing newline
    response:  {"status": "success" | "error", "result": {...}, "error"?: str}
Сервер закрывает соединение после каждой команды, поэтому каждый вызов
открывает новый socket.
"""

from __future__ import annotations

import json
import socket
import traceback
from collections.abc import Callable
from typing import Any

UNREAL_HOST = "127.0.0.1"
UNREAL_PORT = 55557
RECV_TIMEOUT_S = 10.0


class SmokeFailure(RuntimeError):
    """Raised when a smoke-test step fails. Wraps the bridge response."""

    def __init__(self, step_index: int, step_name: str, detail: str, raw: dict[str, Any] | None = None):
        super().__init__(f"[step {step_index}] {step_name}: {detail}")
        self.step_index = step_index
        self.step_name = step_name
        self.raw = raw


def send_command(command: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    """Send one command to the UnrealMCP plugin and return the parsed response.

    Raises SmokeFailure on socket/JSON errors. Bridge-level errors
    (status="error") are returned as-is — callers decide whether to fail.
    """
    payload = json.dumps({"type": command, "params": params or {}})
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(RECV_TIMEOUT_S)
    try:
        sock.connect((UNREAL_HOST, UNREAL_PORT))
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(8192)
            if not chunk:
                break
            chunks.append(chunk)
            try:
                decoded = b"".join(chunks).decode("utf-8")
                return json.loads(decoded)
            except json.JSONDecodeError:
                continue
        raise SmokeFailure(-1, command, "connection closed before complete JSON received")
    except (TimeoutError, OSError) as exc:
        raise SmokeFailure(-1, command, f"socket error: {exc}") from exc
    finally:
        try:
            sock.close()
        except Exception:
            pass


def assert_success(response: dict[str, Any], step_index: int, step_name: str) -> dict[str, Any]:
    """Validate a bridge response is success-shaped, return its result payload.

    Bridge wraps successful results as {"status": "success", "result": {...}}.
    Some commands answer with {"success": true, ...} (older payloads) — we
    accept both. Anything else is treated as failure.
    """
    if not isinstance(response, dict):
        raise SmokeFailure(step_index, step_name, f"non-dict response: {response!r}", response)
    if response.get("status") == "success":
        result = response.get("result")
        if isinstance(result, dict):
            return result
        return {}
    if response.get("success") is True:
        return response
    err = response.get("error") or response.get("message") or "unknown error"
    raise SmokeFailure(step_index, step_name, str(err), response)


def run_steps(label: str, steps: list[tuple[str, Callable[[], dict[str, Any] | None]]]) -> int:
    """Execute a list of (name, callable) steps with progress output.

    Each callable returns the response dict (or None on internal handling).
    Exceptions terminate the test with exit code 1.
    """
    total = len(steps)
    print(f"--- {label} ({total} steps) ---")
    for idx, (name, func) in enumerate(steps, start=1):
        try:
            func()
        except SmokeFailure as exc:
            print(f"[{idx}/{total}] {name} FAIL: {exc}")
            if exc.raw:
                print(f"    raw response: {json.dumps(exc.raw, indent=2)[:800]}")
            traceback.print_exc()
            return 1
        except Exception:
            print(f"[{idx}/{total}] {name} CRASH:")
            traceback.print_exc()
            return 1
        print(f"[{idx}/{total}] {name} OK")
    print(f"--- {label} PASSED ---")
    return 0


def parse_no_cleanup(argv: list[str]) -> bool:
    return "--no-cleanup" in argv[1:]
