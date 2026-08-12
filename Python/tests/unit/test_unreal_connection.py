"""
Unit tests for unreal_connection.py (FIX-MCP-003).

Covers: env-var timeout validation, the O(n) JSON boundary scanner (replaces
the old O(n^2) re-parse-the-whole-buffer-per-chunk receive loop), and
receive_full_response()'s bounded-wait behavior via a fake socket — no live
Unreal Editor required, pure Python.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Any

import pytest

# tests/unit/ sits two levels under Python/, where unreal_connection.py lives.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import unreal_connection
from unreal_connection import (
    UnrealConnection,
    _JsonObjectBoundaryScanner,
    _parse_positive_timeout,
)

# ─────────────────────────────────────────────────────────────────────────────
# _parse_positive_timeout — env validation
# ─────────────────────────────────────────────────────────────────────────────


def test_parse_positive_timeout_rejects_empty(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("_TEST_TIMEOUT", "")
    with pytest.raises(ValueError, match="not a valid number"):
        _parse_positive_timeout("_TEST_TIMEOUT", "330")


@pytest.mark.parametrize("bad_value", ["0", "-1", "-330"])
def test_parse_positive_timeout_rejects_non_positive(monkeypatch: pytest.MonkeyPatch, bad_value: str) -> None:
    monkeypatch.setenv("_TEST_TIMEOUT", bad_value)
    with pytest.raises(ValueError, match="positive number of seconds"):
        _parse_positive_timeout("_TEST_TIMEOUT", "330")


def test_parse_positive_timeout_rejects_non_numeric(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("_TEST_TIMEOUT", "not-a-number")
    with pytest.raises(ValueError, match="not a valid number"):
        _parse_positive_timeout("_TEST_TIMEOUT", "330")


def test_parse_positive_timeout_accepts_valid_override(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("_TEST_TIMEOUT", "42")
    assert _parse_positive_timeout("_TEST_TIMEOUT", "330") == 42.0


def test_parse_positive_timeout_uses_default_when_unset(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("_TEST_TIMEOUT", raising=False)
    assert _parse_positive_timeout("_TEST_TIMEOUT", "330") == 330.0


# ─────────────────────────────────────────────────────────────────────────────
# _JsonObjectBoundaryScanner
# ─────────────────────────────────────────────────────────────────────────────


def test_scanner_completes_on_simple_object() -> None:
    scanner = _JsonObjectBoundaryScanner()
    assert scanner.feed(b'{"a": 1}') is True


def test_scanner_incomplete_until_closing_brace() -> None:
    scanner = _JsonObjectBoundaryScanner()
    assert scanner.feed(b'{"a": ') is False
    assert scanner.feed(b'1') is False
    assert scanner.feed(b'}') is True


def test_scanner_ignores_braces_inside_strings() -> None:
    # The value itself contains '{' and '}' — must not trip the depth
    # counter early. Fed byte-by-byte to catch any single-byte state bug.
    payload = b'{"key": "{not a real brace}"}'
    scanner = _JsonObjectBoundaryScanner()
    result = False
    for i, byte in enumerate(payload):
        result = scanner.feed(bytes([byte]))
        if result:
            assert i == len(payload) - 1, "scanner fired before the real closing brace"
    assert result is True


def test_scanner_handles_escaped_quote_inside_string() -> None:
    # `"a\"b"` — the escaped quote must NOT end the string early, otherwise
    # the following `}` (still logically inside the string) would look like
    # a real structural byte.
    payload = b'{"key": "a\\"b"}'
    scanner = _JsonObjectBoundaryScanner()
    result = False
    for byte in payload:
        result = scanner.feed(bytes([byte]))
    assert result is True
    # Sanity: the payload really does parse as valid JSON with that value.
    import json
    assert json.loads(payload.decode("utf-8"))["key"] == 'a"b'


def test_scanner_multibyte_utf8_split_across_feed_calls() -> None:
    # Cyrillic text encodes to multi-byte UTF-8; split the payload at an
    # arbitrary byte offset that lands INSIDE a multi-byte sequence and
    # confirm the scanner still completes correctly (byte-level scanning
    # must never misinterpret a continuation byte as a structural char —
    # continuation bytes are >= 0x80, structural bytes are all ASCII).
    payload = json_dump_bytes({"nodeId": "Узел_текстура_панорамирование"})
    split_at = len(payload) // 2
    scanner = _JsonObjectBoundaryScanner()
    first = scanner.feed(payload[:split_at])
    second = scanner.feed(payload[split_at:])
    assert first is False
    assert second is True


def json_dump_bytes(obj: dict[str, Any]) -> bytes:
    import json
    return json.dumps(obj).encode("utf-8")


# ─────────────────────────────────────────────────────────────────────────────
# receive_full_response — fake socket harness
# ─────────────────────────────────────────────────────────────────────────────

_TIMEOUT_SLICE = object()  # sentinel: this recv() call raises TimeoutError


class _FakeSocket:
    """Minimal stand-in for socket.socket, scripted with a fixed sequence of
    recv() results. `_TIMEOUT_SLICE` entries simulate a per-slice timeout
    (the short-slice recv() in receive_full_response's loop timing out
    without data); anything after the scripted sequence also times out
    forever, simulating a permanently stalled connection.
    """

    def __init__(self, script: list[Any]) -> None:
        self._script = list(script)
        self._index = 0
        self.settimeout_calls: list[float] = []

    def settimeout(self, value: float) -> None:
        self.settimeout_calls.append(value)

    def recv(self, bufsize: int) -> bytes:
        if self._index >= len(self._script):
            raise TimeoutError
        item = self._script[self._index]
        self._index += 1
        if item is _TIMEOUT_SLICE:
            raise TimeoutError
        assert isinstance(item, bytes)
        return item


def test_receive_full_response_reassembles_chunks(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 5.0)
    payload = json_dump_bytes({"status": "success", "result": {"ok": True}})
    mid = len(payload) // 2
    sock = _FakeSocket([payload[:mid], payload[mid:]])
    conn = UnrealConnection()
    data = conn.receive_full_response(sock)  # type: ignore[arg-type]
    assert data == payload


def test_receive_full_response_survives_multibyte_split(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 5.0)
    payload = json_dump_bytes({"nodeId": "текстура_панорамирование_гусеницы"})
    # Split at an offset chosen to land inside a multi-byte UTF-8 sequence.
    split_at = 10
    sock = _FakeSocket([payload[:split_at], payload[split_at:]])
    conn = UnrealConnection()
    data = conn.receive_full_response(sock)  # type: ignore[arg-type]
    assert data == payload
    import json
    assert json.loads(data.decode("utf-8"))["nodeId"] == "текстура_панорамирование_гусеницы"


def test_receive_full_response_recovers_from_slice_timeouts(monkeypatch: pytest.MonkeyPatch) -> None:
    """A per-recv() slice timing out (no data yet) must NOT abort the whole
    receive — only the OVERALL deadline should."""
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 5.0)
    payload = json_dump_bytes({"status": "success", "result": {}})
    sock = _FakeSocket([_TIMEOUT_SLICE, _TIMEOUT_SLICE, payload])
    conn = UnrealConnection()
    data = conn.receive_full_response(sock)  # type: ignore[arg-type]
    assert data == payload


def test_receive_full_response_raises_on_eof_before_any_data(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 5.0)
    sock = _FakeSocket([b""])
    conn = UnrealConnection()
    with pytest.raises(ConnectionError):
        conn.receive_full_response(sock)  # type: ignore[arg-type]


def test_receive_full_response_overall_deadline_is_bounded(monkeypatch: pytest.MonkeyPatch) -> None:
    """FIX-MCP-003 core guarantee: total wait is bounded by RECV_TIMEOUT
    regardless of how many short per-slice timeouts happen in between — the
    old code gave every individual recv() its own fresh RECV_TIMEOUT budget,
    so a connection that trickled data slowly (or never sent anything but
    also never hit a single recv()'s own timeout) could run well past the
    configured limit.
    """
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 0.3)
    monkeypatch.setattr(unreal_connection, "_RECV_SLICE_SECONDS", 0.05)
    sock = _FakeSocket([_TIMEOUT_SLICE] * 3)  # never produces real data
    conn = UnrealConnection()
    start = time.monotonic()
    with pytest.raises(RuntimeError, match="Timeout receiving Unreal response"):
        conn.receive_full_response(sock)  # type: ignore[arg-type]
    elapsed = time.monotonic() - start
    # Generous slack for CI scheduling jitter — the point is "bounded near
    # RECV_TIMEOUT", not "exactly RECV_TIMEOUT to the millisecond".
    assert elapsed < 2.0, f"receive_full_response ran {elapsed:.2f}s past a 0.3s RECV_TIMEOUT"


def test_receive_full_response_timeout_with_partial_data_still_raises(monkeypatch: pytest.MonkeyPatch) -> None:
    """The old 'try to salvage a partial response after timeout' branch was
    provably dead code (chunks never changed between the last loop iteration
    and the TimeoutError). FIX-MCP-003 replaces it with an honest timeout —
    partial, incomplete data does not get silently accepted as a full
    response."""
    monkeypatch.setattr(unreal_connection, "RECV_TIMEOUT", 0.2)
    monkeypatch.setattr(unreal_connection, "_RECV_SLICE_SECONDS", 0.05)
    sock = _FakeSocket([b'{"status": "success"'] + [_TIMEOUT_SLICE] * 3)  # never closes the object
    conn = UnrealConnection()
    with pytest.raises(RuntimeError, match="Timeout receiving Unreal response"):
        conn.receive_full_response(sock)  # type: ignore[arg-type]
