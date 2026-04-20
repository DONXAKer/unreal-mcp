"""
Bridge-layer test for create_level + level load/save (MCP-CONTENT-003b).

Covers:
  - create_level("Empty", overwrite) → asset_exists → delete_asset → gone.
  - Idempotency: rerun with ifExists="skip" → status="skipped".
  - load_level / save_level round-trip.

Requires a running UE Editor with UnrealMCP 1.6.0+ rebuilt.

Run:  uv run python -m tests.test_create_level
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

MAP_PATH = "/Game/Dev/MCPContent/Map_CreateTmp"


def _check(label: str, fn: Callable[[], None]) -> Result:
    try:
        fn()
        return True, label
    except AssertionError as e:
        return False, f"{label}: {e}"
    except Exception:  # noqa: BLE001
        return False, f"{label}: unexpected\n{traceback.format_exc()}"


def _call(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    from unreal_mcp_server import get_unreal_connection  # type: ignore
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("bridge unreachable")
    raw = conn.send_command(command, params) or {}
    return raw.get("result", raw)


def _assert_ok(resp: Dict[str, Any], *statuses: str) -> None:
    assert resp.get("ok") is True, f"expected ok=true, got {resp}"
    if statuses:
        assert resp.get("status") in statuses, \
            f"expected status in {statuses}, got {resp.get('status')} ({resp})"


def _exists(path: str) -> bool:
    resp = _call("asset_exists", {"assetPath": path})
    return resp.get("meta", {}).get("exists", False)


# --- lifecycle --------------------------------------------------------------

def test_level_created() -> None:
    _call("delete_asset", {"assetPath": MAP_PATH, "ifMissing": "skip"})
    assert not _exists(MAP_PATH)

    resp = _call("create_level", {
        "destMapPath": MAP_PATH,
        "template": "Empty",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(MAP_PATH), "asset not in registry after create"


def test_level_skip_idempotent() -> None:
    resp = _call("create_level", {
        "destMapPath": MAP_PATH,
        "template": "Empty",
        "ifExists": "skip",
    })
    _assert_ok(resp, "skipped")


def test_level_load() -> None:
    resp = _call("load_level", {"mapPath": MAP_PATH})
    _assert_ok(resp)
    assert resp.get("meta", {}).get("handle") or resp.get("assetPath"), \
        f"expected level handle, got {resp}"


def test_level_save() -> None:
    resp = _call("save_level", {"mapPath": MAP_PATH})
    _assert_ok(resp)


def test_level_deleted() -> None:
    # Avoid deleting the active level — open a throwaway before deletion.
    fallback = "/Engine/Maps/Templates/OpenWorld"
    _call("load_level", {"mapPath": fallback})

    resp = _call("delete_asset", {"assetPath": MAP_PATH})
    _assert_ok(resp, "updated", "skipped")
    assert not _exists(MAP_PATH), "level still present after delete"


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_level_created,
    test_level_skip_idempotent,
    test_level_load,
    test_level_save,
    test_level_deleted,
]


def run() -> Dict[str, Any]:
    try:
        probe = _call("asset_exists", {"assetPath": "/Game/__probe__"})
    except Exception as e:  # noqa: BLE001
        return {
            "ok": True, "passed": 0, "failed": 0, "bridge_available": False,
            "results": [{"pass": True, "message": f"{t.__name__}: SKIPPED (bridge: {e})"}
                        for t in TESTS],
        }
    if not isinstance(probe, dict) or probe.get("ok") is None:
        return {
            "ok": True, "passed": 0, "failed": 0, "bridge_available": False,
            "results": [{"pass": True, "message": f"{t.__name__}: SKIPPED (plugin not rebuilt)"}
                        for t in TESTS],
        }

    results = [_check(t.__name__, t) for t in TESTS]
    passed = sum(1 for ok_, _ in results if ok_)
    failed = len(results) - passed
    return {
        "ok": failed == 0, "passed": passed, "failed": failed,
        "bridge_available": True,
        "results": [{"pass": ok_, "message": msg} for ok_, msg in results],
    }


if __name__ == "__main__":
    summary = run()
    for r in summary["results"]:
        prefix = "PASS" if r["pass"] else "FAIL"
        print(f"[{prefix}] {r['message']}")
    tag = "" if summary["bridge_available"] else " (bridge skipped)"
    print(f"\n{summary['passed']}/{summary['passed'] + summary['failed']} tests passed{tag}")
    sys.exit(0 if summary["ok"] else 1)
