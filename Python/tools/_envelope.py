"""
Outbound envelope normalizer for MCP tool responses.

Wraps `FastMCP.tool` so every @mcp.tool() in `tools/*_tools.py` returns the
same dual-key shape regardless of what the tool body produced internally:

  - unified keys:  ok, status, assetPath, meta, error
  - legacy keys:   success, message

Existing tool internals stay untouched — the wrapping happens at registration
time inside `register_X_tools(mcp)` by calling `mcp = wrap_with_envelope(mcp)`
on its first line.

Design rationale and the 7-branch detection algorithm live in
`_envelope_design.md` (right next to this file).
"""

from __future__ import annotations

import functools
import logging
import traceback
from collections.abc import Callable
from typing import Any

from mcp.server.fastmcp import FastMCP

logger = logging.getLogger("UnrealMCP")


def _classify_legacy_failure(message: str) -> str:
    """Heuristic ErrorCategory for legacy {success: False, message: ...}.

    Crude string match — better than blanket `ue_internal` but not a contract.
    Matches reflect typical wording in tools/*.py legacy returns.
    """
    msg = (message or "").lower()
    if any(tok in msg for tok in ("connect", "bridge", "unreal", "no response")):
        return "ue_internal"
    if any(tok in msg for tok in ("missing required", "invalid", "must be", "must have")):
        return "user"
    return "ue_internal"


def _build_success(
    status: str,
    asset_path: str,
    meta: dict[str, Any],
    message: str | None = None,
) -> dict[str, Any]:
    """Construct the final dual-key success envelope."""
    msg = message or status
    return {
        "ok": True,
        "status": status,
        "assetPath": asset_path,
        "meta": meta,
        "success": True,
        "message": msg,
    }


def _build_failure(
    category: str,
    code: str,
    message: str,
    details: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Construct the final dual-key failure envelope."""
    return {
        "ok": False,
        "error": {
            "category": category,
            "code": code,
            "message": message,
            "details": details or {},
        },
        "success": False,
        "message": message,
    }


def _extract_payload(d: dict[str, Any], drop_keys: tuple[str, ...]) -> tuple[str, dict[str, Any]]:
    """Pull assetPath out of a dict, return (asset_path, remaining_dict_without_drop_keys)."""
    asset_path = str(d.get("assetPath") or d.get("path") or "")
    leftover = {k: v for k, v in d.items() if k not in drop_keys}
    leftover.pop("assetPath", None)
    leftover.pop("path", None)
    return asset_path, leftover


def _normalize_outbound(raw: Any) -> dict[str, Any]:
    """Normalize whatever a tool returned into the dual-key envelope.

    See _envelope_design.md for the detection algorithm. Order matters:
    unified is checked before bridge/legacy so recipe responses are not
    double-wrapped.
    """

    if raw is None:
        return _build_failure("ue_internal", "NO_RESPONSE", "Tool returned None")

    if not isinstance(raw, dict):
        return _build_failure(
            "ue_internal",
            "INVALID_RESPONSE_TYPE",
            f"Tool returned non-dict ({type(raw).__name__})",
            details={"value": repr(raw)[:200]},
        )

    if not raw:
        return _build_failure("ue_internal", "NO_RESPONSE", "Tool returned empty dict")

    # Type 5 / Type 6 — already unified (from a recipe or explicit ok()/fail() call)
    if "ok" in raw:
        if raw["ok"] is True:
            status = str(raw.get("status") or "ok")
            return {
                "ok": True,
                "status": status,
                "assetPath": str(raw.get("assetPath") or ""),
                "meta": raw.get("meta") or {},
                "success": True,
                "message": status,
            }
        if raw["ok"] is False:
            err = raw.get("error") or {}
            if isinstance(err, dict):
                msg = str(err.get("message") or "Unknown error")
                return {
                    "ok": False,
                    "error": {
                        "category": str(err.get("category") or "ue_internal"),
                        "code": str(err.get("code") or "UNKNOWN"),
                        "message": msg,
                        "details": err.get("details") or {},
                    },
                    "success": False,
                    "message": msg,
                }
            msg = str(err or "Unknown error")
            return _build_failure("ue_internal", "UNKNOWN", msg)

    # Type 1 — bridge wrap success: {status: success, result: {...}}
    if raw.get("status") == "success":
        result = raw.get("result")
        if not isinstance(result, dict):
            return _build_success("created", "", {"result": result})
        asset_path, leftover = _extract_payload(result, drop_keys=("status",))
        status = str(result.get("status") or "created")
        return _build_success(status, asset_path, leftover)

    # Type 2 — bridge wrap error: {status: error, error: ...}
    if raw.get("status") == "error":
        msg = str(raw.get("error") or raw.get("message") or "Unknown UE error")
        return _build_failure(
            "ue_internal",
            "BRIDGE_ERROR",
            msg,
            details={"raw": {k: v for k, v in raw.items() if k != "status"}},
        )

    # Type 3 — legacy success=True
    if raw.get("success") is True:
        asset_path, leftover = _extract_payload(raw, drop_keys=("success", "message", "status"))
        status = str(raw.get("status") or "created")
        message = str(raw.get("message") or status)
        return _build_success(status, asset_path, leftover, message=message)

    # Type 4 — legacy success=False
    if raw.get("success") is False:
        msg = str(raw.get("message") or raw.get("error") or "Unknown failure")
        return _build_failure(_classify_legacy_failure(msg), "TOOL_FAILURE", msg)

    # Fallback — unknown shape, treat as failure so callers don't silently get garbage
    return _build_failure(
        "ue_internal",
        "UNKNOWN_SHAPE",
        "Tool returned a dict with no recognizable status/ok/success key",
        details={"keys": sorted(raw.keys())},
    )


def _wrap_callable(fn: Callable[..., Any]) -> Callable[..., Any]:
    """Wrap a tool function so its return passes through _normalize_outbound.

    Exceptions inside the tool are caught and converted to a Type-7 envelope.
    Signature is preserved via functools.wraps so FastMCP's pydantic
    signature-introspection still sees the original parameters.
    """

    @functools.wraps(fn)
    def wrapper(*args: Any, **kwargs: Any) -> dict[str, Any]:
        try:
            return _normalize_outbound(fn(*args, **kwargs))
        except Exception as exc:
            logger.exception("Tool %s raised", getattr(fn, "__name__", "?"))
            return _build_failure(
                "ue_internal",
                "TOOL_EXCEPTION",
                f"{type(exc).__name__}: {exc}",
                details={"traceback": traceback.format_exc()},
            )

    return wrapper


class _EnvelopeProxy:
    """Thin proxy around FastMCP that intercepts `.tool()` to wrap registered
    functions. All other attributes pass through unchanged so the proxy is a
    drop-in replacement for the original `mcp` instance inside a single
    `register_X_tools(mcp)` call.
    """

    def __init__(self, inner: FastMCP):
        self._inner = inner

    def tool(self, *tool_args: Any, **tool_kwargs: Any) -> Callable[..., Any]:
        original = self._inner.tool(*tool_args, **tool_kwargs)

        def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
            wrapped = _wrap_callable(fn)
            return original(wrapped)

        return decorator

    def __getattr__(self, name: str) -> Any:
        return getattr(self._inner, name)


def wrap_with_envelope(mcp: FastMCP) -> FastMCP:
    """Return a proxy around `mcp` whose `.tool()` decorator post-processes
    every registered function with `_normalize_outbound`.

    Call as the first line of each `register_X_tools(mcp)`:

        def register_blueprint_tools(mcp: FastMCP) -> None:
            mcp = wrap_with_envelope(mcp)
            @mcp.tool()
            def create_blueprint(...): ...

    Note: the runtime object is `_EnvelopeProxy`, not `FastMCP` — but it
    duck-types as one (every non-.tool attribute is delegated). Typing it
    as `FastMCP` lets callers reassign `mcp = wrap_with_envelope(mcp)`
    without an [assignment] error and keeps `.tool()` typed.
    """
    return _EnvelopeProxy(mcp)  # type: ignore[return-value]
