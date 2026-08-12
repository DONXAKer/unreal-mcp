"""
Unified result/error format for MCP Content Pipeline.

All content primitives (C++ via bridge) and Python recipes return
the same JSON shape. See MCP-CONTENT-001 task for design decisions.
"""

from typing import Any, Literal, cast

Status = Literal["created", "skipped", "overwritten", "updated"]
ErrorCategory = Literal["user", "io", "ue_internal", "validation", "config", "test"]


def ok(status: Status, asset_path: str, **meta: Any) -> dict[str, Any]:
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
) -> dict[str, Any]:
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


def is_ok(response: dict[str, Any] | None) -> bool:
    """Return True iff `response` is a non-None unified-format success."""
    return response is not None and response.get("ok") is True


_KNOWN_ERROR_CATEGORIES: frozenset[str] = frozenset(
    {"user", "io", "ue_internal", "validation", "config", "test"}
)


def _coerce_error_category(value: Any) -> ErrorCategory:
    """Validate a bridge-supplied category string against the known Literal set.

    The bridge is a separate C++ codebase — nothing statically guarantees its
    `category` string matches this Python Literal. An unrecognized value falls
    back to "ue_internal" rather than being passed through unchecked.
    """
    text = str(value) if value else "ue_internal"
    if text in _KNOWN_ERROR_CATEGORIES:
        return cast(ErrorCategory, text)
    return "ue_internal"


def _fail_from_legacy_error(
    primary: Any, secondary: Any, raw_response: dict[str, Any]
) -> dict[str, Any]:
    """Build a fail() from a legacy {status:"error"}/{success:False} response.

    FIX-MCP-001: the bridge's `error` field can now be either a plain string
    (old shape) or a structured {category, code, message, details} object
    (new asset-pipeline shape). The old code here did `str(response.get("error")
    or ...)` unconditionally — on a structured object that collapses the whole
    dict into its Python repr as the "message" string, discarding category/code
    and making every asset-pipeline failure surface as an opaque LEGACY_ERROR
    string blob instead of a real code a caller could branch on.
    """
    if isinstance(primary, dict):
        details = primary.get("details")
        return fail(
            _coerce_error_category(primary.get("category")),
            str(primary.get("code") or "LEGACY_ERROR"),
            str(primary.get("message") or "Unknown error"),
            raw=raw_response,
            **(details if isinstance(details, dict) else {}),
        )
    if isinstance(secondary, dict):
        details = secondary.get("details")
        return fail(
            _coerce_error_category(secondary.get("category")),
            str(secondary.get("code") or "LEGACY_ERROR"),
            str(secondary.get("message") or "Unknown error"),
            raw=raw_response,
            **(details if isinstance(details, dict) else {}),
        )
    return fail(
        "ue_internal",
        "LEGACY_ERROR",
        str(primary or secondary or "Unknown error"),
        raw=raw_response,
    )


def normalize_legacy_response(response: dict[str, Any] | None) -> dict[str, Any]:
    """Best-effort convert legacy {success,message}/{status:"error"} shapes
    into the unified format. Used while existing (non-content) commands
    still return the old shape.
    """
    if response is None:
        return fail("ue_internal", "NO_RESPONSE", "No response from Unreal Engine")

    if "ok" in response:
        return response

    if response.get("status") == "error":
        return _fail_from_legacy_error(response.get("error"), response.get("message"), response)
    if response.get("success") is False:
        return _fail_from_legacy_error(response.get("message"), response.get("error"), response)

    if response.get("success") is True or response.get("status") == "success":
        return ok(
            "created",
            str(response.get("assetPath") or response.get("path") or ""),
            raw=response,
        )

    return response
