"""
Unit tests for the take_screenshot tool wiring (PR8).

Verifies that the Python wrapper assembles params correctly and passes the
bridge response through the envelope unchanged. The real C++ side is
exercised separately by a bridge-marked integration test.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any
from unittest.mock import patch

from mcp.server.fastmcp import FastMCP

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.editor_tools import register_editor_tools


class _FakeConn:
    """Captures the (command, params) pair and replays a canned response."""

    def __init__(self, response: dict[str, Any]):
        self._response = response
        self.last_command: str | None = None
        self.last_params: dict[str, Any] | None = None

    def send_command(self, command: str, params: dict[str, Any]) -> dict[str, Any]:
        self.last_command = command
        self.last_params = params
        return self._response


def _make_take_screenshot(conn: _FakeConn) -> Any:
    """Register editor tools onto a fresh FastMCP and return the take_screenshot fn."""
    server = FastMCP("test")
    register_editor_tools(server)
    with patch("unreal_mcp_server.get_unreal_connection", return_value=conn):
        tool = server._tool_manager._tools["take_screenshot"]
    return tool.fn, server


def test_take_screenshot_defaults_send_basic_params() -> None:
    """Default call → command='take_screenshot', params={filename, show_ui}."""
    conn = _FakeConn(
        {
            "status": "success",
            "result": {
                "status": "created",
                "assetPath": "/Saved/Screenshots/MCPScreenshot.png",
                "filename": "MCPScreenshot.png",
            },
        }
    )
    fn, _ = _make_take_screenshot(conn)
    with patch("unreal_mcp_server.get_unreal_connection", return_value=conn):
        out = fn(ctx=None)
    assert conn.last_command == "take_screenshot"
    assert conn.last_params == {"filename": "MCPScreenshot.png", "show_ui": False}
    # envelope made it a dual-key result
    assert out["ok"] is True
    assert out["success"] is True
    assert out["status"] == "created"
    assert out["assetPath"].endswith("MCPScreenshot.png")


def test_take_screenshot_with_hires_resolution() -> None:
    """resolution_x/y > 0 → params include both keys."""
    conn = _FakeConn(
        {
            "status": "success",
            "result": {
                "status": "created",
                "assetPath": "/Saved/Screenshots/hi.png",
                "resolution_x": 3840,
                "resolution_y": 2160,
            },
        }
    )
    fn, _ = _make_take_screenshot(conn)
    with patch("unreal_mcp_server.get_unreal_connection", return_value=conn):
        out = fn(ctx=None, filename="hi.png", show_ui=True, resolution_x=3840, resolution_y=2160)
    assert conn.last_params == {
        "filename": "hi.png",
        "show_ui": True,
        "resolution_x": 3840,
        "resolution_y": 2160,
    }
    assert out["ok"] is True
    assert out["meta"]["resolution_x"] == 3840


def test_take_screenshot_partial_resolution_dropped() -> None:
    """Only one of resolution_x/y → both omitted (engine fallback to viewport size)."""
    conn = _FakeConn(
        {
            "status": "success",
            "result": {
                "status": "created",
                "assetPath": "/Saved/Screenshots/partial.png",
            },
        }
    )
    fn, _ = _make_take_screenshot(conn)
    with patch("unreal_mcp_server.get_unreal_connection", return_value=conn):
        fn(ctx=None, filename="partial.png", resolution_x=1920, resolution_y=0)
    assert "resolution_x" not in (conn.last_params or {})
    assert "resolution_y" not in (conn.last_params or {})


def test_take_screenshot_bridge_unreachable() -> None:
    """No connection → returns a fail envelope without raising."""
    fn, _ = _make_take_screenshot(_FakeConn({}))
    with patch("unreal_mcp_server.get_unreal_connection", return_value=None):
        out = fn(ctx=None)
    assert out["ok"] is False
    assert out["success"] is False
    assert "Unreal" in out["error"]["message"]
