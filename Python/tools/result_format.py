"""
Unified result/error format for MCP Content Pipeline.

All content primitives (C++ via bridge) and Python recipes return
the same JSON shape. See MCP-CONTENT-001 task for design decisions.
"""

from typing import Any, Dict, Literal, Optional

Status = Literal["created", "skipped", "overwritten", "updated"]
ErrorCategory = Literal["user", "io", "ue_internal", "validation", "config"]


def ok(status: Status, asset_path: str, **meta: Any) -> Dict[str, Any]:
    """Build a success response.

    Args:
        status: How the operation ended — one of created|skipped|overwritten|updated.
        asset_path: Primary asset path this operation produced or touched.
        **meta: Extra per-primitive data surfaced in `meta`.

    Returns:
        { "ok": True, "status": ..., "assetPath": ..., "meta": {...} }
    """
    return {
        "ok": True,
        "status": status,
        "assetPath": asset_path,
        "meta": meta,
    }


def fail(
    category: ErrorCategory,
    code: str,
    message: str,
    **details: Any,
) -> Dict[str, Any]:
    """Build a failure response.

    Args:
        category: Error bucket used by callers to pick retry/abort strategy.
        code: Stable machine-readable error code (e.g. TEXTURE_IMPORT_FAILED).
        message: Human-readable one-liner.
        **details: Extra structured context for the failure.

    Returns:
        { "ok": False, "error": { "category", "code", "message", "details" } }
    """
    return {
        "ok": False,
        "error": {
            "category": category,
            "code": code,
            "message": message,
            "details": details,
        },
    }


def is_ok(response: Optional[Dict[str, Any]]) -> bool:
    """Return True iff `response` is a non-None unified-format success."""
    return bool(response) and response.get("ok") is True


def normalize_legacy_response(response: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    """Best-effort convert legacy {success,message}/{status:"error"} shapes
    into the unified format. Used while existing (non-content) commands
    still return the old shape.
    """
    if response is None:
        return fail("ue_internal", "NO_RESPONSE", "No response from Unreal Engine")

    if "ok" in response:
        return response

    if response.get("status") == "error":
        return fail(
            "ue_internal",
            "LEGACY_ERROR",
            str(response.get("error") or response.get("message") or "Unknown error"),
            raw=response,
        )
    if response.get("success") is False:
        return fail(
            "ue_internal",
            "LEGACY_ERROR",
            str(response.get("message") or response.get("error") or "Unknown error"),
            raw=response,
        )

    if response.get("success") is True or response.get("status") == "success":
        return ok(
            "created",
            str(response.get("assetPath") or response.get("path") or ""),
            raw=response,
        )

    return response
