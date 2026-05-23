"""
Unit tests for tools/_envelope.py.

Covers the 7 input shapes documented in _envelope_design.md plus edge cases:
unified preserved meta, inner status wins, exception caught by wrapper.

No live Unreal Editor required — pure Python.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import pytest

# tests/unit/ sits two levels under Python/, where tools/ lives.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools._envelope import (
    _classify_legacy_failure,
    _normalize_outbound,
    _wrap_callable,
    wrap_with_envelope,
)


@pytest.mark.parametrize(
    "raw, expected_ok, expected_status, expected_asset",
    [
        # Type 1 — bridge wrap success
        (
            {"status": "success", "result": {"assetPath": "/Game/X", "size": 256}},
            True,
            "created",
            "/Game/X",
        ),
        # Type 1 — bridge wrap success with inner status
        (
            {"status": "success", "result": {"status": "skipped", "assetPath": "/Game/Y"}},
            True,
            "skipped",
            "/Game/Y",
        ),
        # Type 2 — bridge wrap error
        (
            {"status": "error", "error": "boom"},
            False,
            None,
            None,
        ),
        # Type 3 — legacy success=True
        (
            {"success": True, "name": "BP_X", "assetPath": "/Game/X"},
            True,
            "created",
            "/Game/X",
        ),
        # Type 3 — legacy with path (not assetPath)
        (
            {"success": True, "path": "/Game/Z"},
            True,
            "created",
            "/Game/Z",
        ),
        # Type 4 — legacy success=False
        (
            {"success": False, "message": "no connect"},
            False,
            None,
            None,
        ),
        # Type 5 — already unified ok (recipe)
        (
            {"ok": True, "status": "skipped", "assetPath": "/Game/Y", "meta": {"reason": "exists"}},
            True,
            "skipped",
            "/Game/Y",
        ),
        # Type 6 — already unified fail (recipe)
        (
            {
                "ok": False,
                "error": {
                    "category": "user",
                    "code": "MISSING_ARG",
                    "message": "bad",
                    "details": {},
                },
            },
            False,
            None,
            None,
        ),
        # Type 7 — None
        (None, False, None, None),
        # Type 7 — empty dict
        ({}, False, None, None),
        # Edge — non-dict (string)
        ("just a string", False, None, None),
        # Edge — non-dict (list)
        ([1, 2, 3], False, None, None),
        # Edge — dict with no recognizable keys
        ({"random_key": "random_value"}, False, None, None),
    ],
)
def test_normalize_outbound(raw, expected_ok, expected_status, expected_asset):
    result = _normalize_outbound(raw)
    # Dual-key invariant
    assert result["ok"] is expected_ok
    assert result["success"] is expected_ok
    assert result["ok"] == result["success"]
    if expected_ok:
        assert result["status"] == expected_status
        assert result["assetPath"] == expected_asset
        assert result["message"]  # non-empty
        assert "meta" in result
    else:
        assert "error" in result
        assert isinstance(result["error"], dict)
        assert result["error"]["category"]
        assert result["error"]["code"]
        assert result["message"] == result["error"]["message"]


def test_unified_ok_preserved_meta():
    """Recipe meta dict must not be rewritten by the envelope."""
    raw = {
        "ok": True,
        "status": "created",
        "assetPath": "/Game/Card",
        "meta": {"card_id": 42, "produces": ["/Game/MI_X", "/Game/BP_X"]},
    }
    out = _normalize_outbound(raw)
    assert out["meta"] == {"card_id": 42, "produces": ["/Game/MI_X", "/Game/BP_X"]}
    assert out["status"] == "created"
    assert out["assetPath"] == "/Game/Card"


def test_bridge_wrap_inner_status_wins():
    """If C++ result contains status, it overrides the default 'created'."""
    raw = {"status": "success", "result": {"status": "overwritten", "assetPath": "/Game/Q"}}
    out = _normalize_outbound(raw)
    assert out["status"] == "overwritten"
    assert out["assetPath"] == "/Game/Q"


def test_bridge_wrap_result_non_dict():
    """result can be a non-dict (e.g. int, list) — store under meta.result."""
    raw = {"status": "success", "result": [1, 2, 3]}
    out = _normalize_outbound(raw)
    assert out["ok"] is True
    assert out["meta"] == {"result": [1, 2, 3]}


def test_legacy_success_extra_keys_go_to_meta():
    """Legacy payload keys other than success/message/status/assetPath/path land in meta."""
    raw = {"success": True, "name": "Foo", "rowCount": 12, "assetPath": "/Game/DT"}
    out = _normalize_outbound(raw)
    assert out["assetPath"] == "/Game/DT"
    assert out["meta"] == {"name": "Foo", "rowCount": 12}


def test_legacy_fail_user_category_heuristic():
    """Wording about missing args nudges category to 'user'."""
    raw = {"success": False, "message": "Missing required parameter 'name'"}
    out = _normalize_outbound(raw)
    assert out["error"]["category"] == "user"


def test_legacy_fail_default_category():
    """Wording about connect nudges to ue_internal."""
    raw = {"success": False, "message": "Failed to connect to Unreal Engine"}
    out = _normalize_outbound(raw)
    assert out["error"]["category"] == "ue_internal"


def test_classify_legacy_failure_unknown():
    """Empty / random message defaults to ue_internal."""
    assert _classify_legacy_failure("") == "ue_internal"
    assert _classify_legacy_failure("something happened") == "ue_internal"


def test_exception_caught_by_wrapper():
    """Tool raising must produce a TOOL_EXCEPTION envelope, not propagate."""
    def boom():
        raise RuntimeError("kaboom")

    wrapped = _wrap_callable(boom)
    out = wrapped()
    assert out["ok"] is False
    assert out["error"]["code"] == "TOOL_EXCEPTION"
    assert "RuntimeError" in out["error"]["message"]
    assert "kaboom" in out["error"]["message"]
    assert "traceback" in out["error"]["details"]


def test_wrapped_callable_preserves_normal_return():
    """Wrapper should pass through legitimate returns."""
    def fine():
        return {"success": True, "assetPath": "/Game/A"}

    wrapped = _wrap_callable(fine)
    out = wrapped()
    assert out["ok"] is True
    assert out["assetPath"] == "/Game/A"


def test_wrapped_callable_preserves_signature():
    """functools.wraps must keep the original __name__ so FastMCP introspection works."""
    def my_tool(x: int, y: str = "z") -> dict[str, Any]:
        return {"success": True, "x": x, "y": y}

    wrapped = _wrap_callable(my_tool)
    assert wrapped.__name__ == "my_tool"
    out = wrapped(x=5, y="hello")
    assert out["ok"] is True
    assert out["meta"] == {"x": 5, "y": "hello"}


def test_envelope_proxy_passes_through_non_tool_attrs():
    """Anything other than .tool() on the proxy must delegate to the wrapped FastMCP."""
    class FakeMCP:
        name = "fake"

        def tool(self):
            def decorator(fn):
                return fn
            return decorator

        def some_other_method(self):
            return "passthrough"

    proxy: Any = wrap_with_envelope(FakeMCP())  # type: ignore[arg-type]
    assert proxy.name == "fake"
    assert proxy.some_other_method() == "passthrough"


def test_unified_ok_missing_meta_defaults_to_empty():
    """ok=True without meta key — meta defaults to {}."""
    raw = {"ok": True, "status": "created", "assetPath": "/Game/X"}
    out = _normalize_outbound(raw)
    assert out["meta"] == {}


def test_unified_fail_string_error():
    """ok=False with a non-dict error string — wrap it gracefully."""
    raw = {"ok": False, "error": "just a string"}
    out = _normalize_outbound(raw)
    assert out["ok"] is False
    assert out["error"]["message"] == "just a string"
