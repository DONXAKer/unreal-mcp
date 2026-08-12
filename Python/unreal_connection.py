"""
Shared TCP client for the UnrealMCP C++ bridge (`EpicUnrealMCPBridge.cpp`,
port 55557 by default).

FIX-MCP-003: `UnrealConnection` (connect/send_command/receive_full_response)
and the two timeout constants used to be maintained as two byte-identical
copies in `unreal_mcp_server.py` (stdio) and `unreal_mcp_server_http.py`
(HTTP) — every receive-loop fix (UnicodeDecodeError, framing, ...) had to be
applied twice by hand and regularly wasn't. Both servers now import this
module and re-export `UnrealConnection` / `RECV_TIMEOUT` / `CONNECT_TIMEOUT`
from their own namespace for backward compatibility (many `tests/*.py`
scripts do `from unreal_mcp_server import UnrealConnection`).

Host/port are intentionally NOT module constants here: the two servers
default to different hosts when `UNREAL_HOST` is unset (`127.0.0.1` for
stdio, `host.docker.internal` for the HTTP/Docker variant) — each server
still owns its own `UNREAL_HOST`/`UNREAL_PORT` resolution and passes them
into `UnrealConnection(host, port)` explicitly. `UnrealConnection()` with no
args falls back to THIS module's own `UNREAL_HOST`/`UNREAL_PORT` (127.0.0.1
default) — that is what the many bare `UnrealConnection()` callers under
`tests/` have always relied on via `unreal_mcp_server.UnrealConnection`.

Deadline note for callers polling with their own budget (e.g.
`tools/pie_tools.py`'s `wait_for_widget`/`wait_for_condition`, which check
`time.monotonic() >= deadline` only AFTER `send_command()` returns): a single
`send_command()` call is bounded by `RECV_TIMEOUT` (see below), not by the
caller's own smaller `timeout_ms`. If the bridge's game thread is genuinely
hung, one `send_command()` can still take up to `RECV_TIMEOUT` to return —
this module only guarantees that it will NOT take longer than that (the
pre-FIX-MCP-003 receive loop could exceed its own configured timeout if data
trickled in slowly, since each individual `recv()` call got its own fresh
`RECV_TIMEOUT` budget instead of sharing one overall deadline). Shortening a
single poll iteration below `RECV_TIMEOUT` would require threading a
per-command timeout through `send_command` and every `@mcp.tool()` that polls
— out of scope here (see FIX-MCP-003 task, "Открытые вопросы").
"""

from __future__ import annotations

import json
import logging
import os
import socket
import time
from typing import Any

logger = logging.getLogger("UnrealMCP")


def _parse_positive_timeout(env_name: str, default: str) -> float:
    """Parse a timeout env var; refuse empty/non-positive rather than
    silently misbehaving downstream.

    FIX-MCP-003: `UNREAL_MCP_RECV_TIMEOUT=""` used to crash `float("")` at
    import time (server never starts, opaque traceback). `UNREAL_MCP_RECV_TIMEOUT=0`
    used to silently pass through to `sock.settimeout(0)`, which switches the
    socket to NON-BLOCKING mode — the first `recv()` then raises
    `BlockingIOError`, which none of the receive loop's except clauses caught,
    so EVERY command failed instantly with a message indistinguishable from
    "the Editor isn't responding" while the Editor was alive and idle. Both
    are refused here with an explicit, actionable message at import time
    instead of failing confusingly downstream. There is no "wait forever"
    mode via this env var — unset it (or don't set it) for the default.
    """
    raw = os.environ.get(env_name, default)
    try:
        value = float(raw)
    except ValueError as exc:
        raise ValueError(
            f"{env_name}={raw!r} is not a valid number of seconds. "
            f"Unset the variable to use the default ({default}s) or set a positive number."
        ) from exc
    if value <= 0:
        raise ValueError(
            f"{env_name}={raw!r} must be a positive number of seconds. "
            f"0 or a negative value silently turns the socket non-blocking, which makes every "
            f"command fail instantly and look like a dead Editor — there is no 'wait forever' mode "
            f"via this variable. Unset it to use the default ({default}s)."
        )
    return value


# Сколько ждать ОТВЕТ на уже отправленную команду. Держать НЕ МЕНЬШЕ, чем
# `mcp.CommandTimeoutSeconds` в мосту (300 с, см. EpicUnrealMCPBridge.cpp —
# TODO там же: цифра эмпирически не проверена на боевой карте) плюс запас на
# сериализацию: мост гарантирует ответ в пределах своего потолка, и уход
# клиента раньше означает, что внятную ошибку COMMAND_TIMEOUT читать уже
# некому, а команда при этом продолжает выполняться. FIX-MCP-001
# (2026-08-12): 130 -> 330 вместе с потолком моста 120 -> 300.
RECV_TIMEOUT = _parse_positive_timeout("UNREAL_MCP_RECV_TIMEOUT", "330")
# Установка соединения — операция быстрая; долго ждать её незачем.
CONNECT_TIMEOUT = _parse_positive_timeout("UNREAL_MCP_CONNECT_TIMEOUT", "5")

# Дефолтный host/port для голого `UnrealConnection()` — используется
# исторически десятками tests/_diag_*.py и tests/test_*.py через
# `from unreal_mcp_server import UnrealConnection`. Совпадает с тем, что
# unreal_mcp_server.py всегда использовал по умолчанию (127.0.0.1); HTTP-сервер
# передаёт свои host/port ЯВНО (host.docker.internal по умолчанию) и на этот
# модульный дефолт не полагается.
UNREAL_HOST = os.environ.get("UNREAL_HOST", "127.0.0.1")
UNREAL_PORT = int(os.environ.get("UNREAL_PORT", "55557"))

# Короткий слайс для одного recv() внутри цикла приёма (см.
# receive_full_response). НЕ настраивается через env — это внутренняя деталь
# реализации ожидания общего дедлайна, а не публичный таймаут протокола.
_RECV_SLICE_SECONDS = 1.0


class _JsonObjectBoundaryScanner:
    """Incrementally detects the end of one top-level JSON value in a byte
    stream, without re-parsing the whole accumulated buffer on every chunk.

    FIX-MCP-003: the previous receive loop ran
    `json.loads(b''.join(chunks).decode())` on the ENTIRE accumulated buffer
    after every ~4 KB `recv()` — O(n^2) total work on a large response
    (`material_get_graph`: hundreds of KB, 100+ chunks, tens of MB of
    redundant re-decoding). Scanning raw BYTES for structural JSON characters
    is safe even with embedded multi-byte UTF-8 (Cyrillic node names, HLSL
    code in material props): every UTF-8 continuation/lead byte is >= 0x80,
    while the ASCII structural bytes this scanner looks for ('"', '\\', '{',
    '}', '[', ']') are all < 0x80 — none of them can appear as part of a
    multi-byte sequence, so there is no need to decode text to scan it
    correctly, and no risk of a false match inside a Cyrillic string.
    """

    __slots__ = ("_depth", "_escape", "_in_string", "_started")

    def __init__(self) -> None:
        self._depth = 0
        self._in_string = False
        self._escape = False
        self._started = False

    def feed(self, chunk: bytes) -> bool:
        """Process newly received bytes.

        Returns True once a complete top-level JSON object/array has been
        observed (i.e. the accumulated buffer, decoded and parsed, should now
        succeed as exactly one JSON value — which is what the bridge always
        sends, one value per response, connection closed after).
        """
        for byte in chunk:
            if self._in_string:
                if self._escape:
                    self._escape = False
                elif byte == 0x5C:  # backslash
                    self._escape = True
                elif byte == 0x22:  # double quote
                    self._in_string = False
                continue
            if byte == 0x22:  # double quote
                self._in_string = True
            elif byte == 0x7B or byte == 0x5B:  # '{' or '['
                self._depth += 1
                self._started = True
            elif byte == 0x7D or byte == 0x5D:  # '}' or ']'
                self._depth -= 1
                if self._started and self._depth <= 0:
                    return True
        return False


class UnrealConnection:
    """Connection to an Unreal Engine instance (the UnrealMCP C++ bridge)."""

    def __init__(self, host: str | None = None, port: int | None = None) -> None:
        """Initialize the connection.

        host/port default to this module's UNREAL_HOST/UNREAL_PORT
        (env-driven, 127.0.0.1:55557 unless overridden) so existing callers
        keep working. Pass explicit values to target a specific Editor
        instance — used by the 2-player visual playtest harness, which talks
        to two editors on 55557 and 55558.
        """
        self.host = host if host is not None else UNREAL_HOST
        self.port = port if port is not None else UNREAL_PORT
        self.socket: socket.socket | None = None
        self.connected = False

    def connect(self) -> bool:
        """Connect to the Unreal Engine instance."""
        try:
            # Close any existing socket
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass
                self.socket = None

            logger.info(f"Connecting to Unreal at {self.host}:{self.port}...")
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(CONNECT_TIMEOUT)

            # Set socket options for better stability
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

            # Set larger buffer sizes
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)

            self.socket.connect((self.host, self.port))
            self.connected = True
            logger.info("Connected to Unreal Engine")
            return True

        except Exception as e:
            logger.error(f"Failed to connect to Unreal: {e}")
            self.connected = False
            return False

    def disconnect(self) -> None:
        """Disconnect from the Unreal Engine instance."""
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        self.socket = None
        self.connected = False

    def receive_full_response(self, sock: socket.socket, buffer_size: int = 65536) -> bytes:
        """Receive one complete JSON response from the bridge, handling
        chunked/fragmented TCP data.

        FIX-MCP-003 rewrite of this method:
          - ONE overall wall-clock deadline (`start + RECV_TIMEOUT`) governs
            the whole loop, enforced via short per-`recv()` slices
            (`_RECV_SLICE_SECONDS`). The old code called
            `sock.settimeout(RECV_TIMEOUT)` ONCE outside the loop, which
            budgets EACH individual `recv()` call its own full RECV_TIMEOUT —
            a response that trickled in slowly across many chunks could
            previously run the total wait well past RECV_TIMEOUT. Now the
            loop bails via `TimeoutError` the moment the overall deadline is
            exceeded, regardless of how many individual slices that took.
          - Message-boundary detection via `_JsonObjectBoundaryScanner`
            instead of re-parsing the whole accumulated buffer on every
            chunk (was O(n^2) on large responses — see the scanner's
            docstring).
          - `UnicodeDecodeError` is still caught around the one-shot final
            decode: defensive, in case the scanner's "structural bytes are
            never part of a multi-byte UTF-8 sequence" guarantee is ever
            violated by a protocol change upstream.
        """
        chunks: list[bytes] = []
        scanner = _JsonObjectBoundaryScanner()
        deadline = time.monotonic() + RECV_TIMEOUT
        try:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError
                sock.settimeout(min(_RECV_SLICE_SECONDS, remaining))
                try:
                    chunk = sock.recv(buffer_size)
                except TimeoutError:
                    # Just this slice ran dry. The overall deadline check at
                    # the top of the loop is what actually governs how long
                    # we wait in total — keep looping until IT trips.
                    continue

                if not chunk:
                    if not chunks:
                        raise ConnectionError("Connection closed before receiving data")
                    break  # EOF after some data — handled after the loop below.

                chunks.append(chunk)
                if scanner.feed(chunk):
                    data = b"".join(chunks)
                    try:
                        json.loads(data.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        # Extremely unlikely given the scanner's guarantee,
                        # but if it ever fires, trust the actual parser over
                        # the scanner and keep reading instead of returning
                        # data we can't parse.
                        logger.warning(
                            "Boundary scanner signalled completion but json.loads "
                            "disagreed — continuing to read"
                        )
                        continue
                    logger.info(f"Received complete response ({len(data)} bytes)")
                    return data
        except TimeoutError:
            data_len = sum(len(c) for c in chunks)
            if data_len:
                logger.warning(
                    f"Socket timeout during receive after {RECV_TIMEOUT:.0f}s with "
                    f"{data_len} incomplete bytes buffered — response never completed"
                )
            else:
                logger.warning(f"Socket timeout during receive after {RECV_TIMEOUT:.0f}s — no data received")
            raise RuntimeError("Timeout receiving Unreal response") from None
        except ConnectionError:
            raise
        except Exception as e:
            logger.error(f"Error during receive: {e!s}")
            raise

        # EOF reached with SOME data buffered, but the boundary scanner never
        # signalled completion — the bridge closed the socket mid-message (a
        # genuine protocol violation, not the ordinary "message ends exactly
        # on a chunk boundary" case, which the scanner already returns from
        # inside the loop above). Return what we have; the caller's own
        # `json.loads()` on this raises a clear parse error rather than us
        # guessing here.
        return b"".join(chunks)

    def send_command(self, command: str, params: dict[str, Any] = None) -> dict[str, Any] | None:
        """Send a command to Unreal Engine and get the response."""
        # Always reconnect for each command, since Unreal closes the connection after each command
        # This is different from Unity which keeps connections alive
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            self.connected = False

        if not self.connect():
            logger.error("Failed to connect to Unreal Engine for command")
            # FIX-MCP-003: consolidated on the HTTP server's prior behavior —
            # a structured `{status:"error", error:"..."}` dict (the shape
            # tools/_envelope.py's `_normalize_outbound` already recognizes)
            # instead of a bare `None`. Every tool wrapper forwards whatever
            # `send_command` returns via `return response` once its own
            # `if not response:` guard passes — nothing branches on the
            # return being `None` specifically, and this carries host:port,
            # which the old generic "No response from Unreal Engine" did not.
            return {"status": "error", "error": f"Cannot connect to Unreal at {self.host}:{self.port}"}
        assert self.socket is not None  # connect() set it on success

        try:
            # Match Unity's command format exactly
            command_obj = {
                "type": command,  # Use "type" instead of "command"
                "params": params or {}  # Use Unity's params or {} pattern
            }

            # Send without newline, exactly like Unity
            command_json = json.dumps(command_obj)
            logger.info(f"Sending command: {command_json}")
            self.socket.sendall(command_json.encode('utf-8'))

            # Read response using improved handler
            response_data = self.receive_full_response(self.socket)
            response: dict[str, Any] = json.loads(response_data.decode('utf-8'))

            # Log complete response for debugging
            logger.info(f"Complete response from Unreal: {response}")

            # Check for both error formats: {"status": "error", ...} and {"success": false, ...}
            if response.get("status") == "error":
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (status=error): {error_message}")
                # We want to preserve the original error structure but ensure error is accessible
                if "error" not in response:
                    response["error"] = error_message
            elif response.get("success") is False:
                # This format uses {"success": false, "error": "message"} or {"success": false, "message": "message"}
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (success=false): {error_message}")
                # Convert to the standard format expected by higher layers
                response = {
                    "status": "error",
                    "error": error_message
                }

            # Always close the connection after command is complete
            # since Unreal will close it on its side anyway
            if self.socket is not None:
                try:
                    self.socket.close()
                except Exception:
                    pass
            self.socket = None
            self.connected = False

            return response

        except Exception as e:
            logger.error(f"Error sending command: {e}")
            # Always reset connection state on any error
            self.connected = False
            if self.socket is not None:
                try:
                    self.socket.close()
                except Exception:
                    pass
            self.socket = None
            return {
                "status": "error",
                "error": str(e)
            }
