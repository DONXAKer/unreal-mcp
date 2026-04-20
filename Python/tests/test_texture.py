"""
Bridge-layer tests for TextureCommands (MCP-CONTENT-002).

Covers:
  - generate_placeholder_texture create → overwrite → skip → delete
  - import_texture using a tiny bundled PNG fixture (tests/fixtures/tiny.png)

All tests require a running UE Editor with UnrealMCP 1.4.0+ rebuilt.
Skipped automatically if the bridge is unreachable.

Run:  uv run python -m tests.test_texture
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

# Unique paths used only by these tests; cleaned up at the end. The /Game/Dev
# prefix intentionally avoids clashing with real WarCard content.
T_PATH_PLACEHOLDER = "/Game/Dev/MCPContent/T_MCP_Placeholder_Test"
T_PATH_IMPORTED = "/Game/Dev/MCPContent/T_MCP_Imported_Test"

FIXTURE_PATH = Path(__file__).parent / "fixtures" / "tiny.png"


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
    # send_command returns {status, result} when success; we want the inner.
    return raw.get("result", raw)


def _assert_ok(resp: Dict[str, Any], expected_status: str) -> None:
    assert resp.get("ok") is True, f"expected ok=true, got {resp}"
    assert resp.get("status") == expected_status, \
        f"expected status={expected_status}, got {resp.get('status')} ({resp})"


def _exists(path: str) -> bool:
    resp = _call("asset_exists", {"assetPath": path})
    return resp.get("meta", {}).get("exists", False)


# --- placeholder texture lifecycle ------------------------------------------

def test_placeholder_created() -> None:
    # Start clean so we get "created" instead of "skipped".
    _call("delete_asset", {"assetPath": T_PATH_PLACEHOLDER, "ifMissing": "skip"})
    assert not _exists(T_PATH_PLACEHOLDER), "precondition: asset should be absent"

    resp = _call("generate_placeholder_texture", {
        "assetPath": T_PATH_PLACEHOLDER,
        "size": 128,
        "color": [0.8, 0.2, 0.3, 1.0],
        "label": "TEST 99",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(T_PATH_PLACEHOLDER), "asset not in registry after create"


def test_placeholder_skip_idempotent() -> None:
    resp = _call("generate_placeholder_texture", {
        "assetPath": T_PATH_PLACEHOLDER,
        "size": 128,
        "color": [0.8, 0.2, 0.3, 1.0],
        "label": "TEST 99",
        "ifExists": "skip",
    })
    _assert_ok(resp, "skipped")


def test_placeholder_overwrite() -> None:
    resp = _call("generate_placeholder_texture", {
        "assetPath": T_PATH_PLACEHOLDER,
        "size": 64,
        "color": [0.1, 0.1, 0.1, 1.0],
        "label": "OVR",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "overwritten")


def test_placeholder_deleted() -> None:
    resp = _call("delete_asset", {"assetPath": T_PATH_PLACEHOLDER})
    _assert_ok(resp, "updated")
    assert not _exists(T_PATH_PLACEHOLDER), "asset still present after delete"


# --- import_texture using bundled fixture ------------------------------------

def test_import_png_fixture() -> None:
    if not FIXTURE_PATH.is_file():
        # Fixture is optional — when missing the test self-skips so we don't
        # force anyone to check binaries into git until they're needed.
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    _call("delete_asset", {"assetPath": T_PATH_IMPORTED, "ifMissing": "skip"})
    resp = _call("import_texture", {
        "sourcePath": str(FIXTURE_PATH),
        "assetPath": T_PATH_IMPORTED,
        "sRGB": True,
        "compression": "Default",
        "mipGen": "NoMipmaps",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(T_PATH_IMPORTED)

    # Cleanup.
    _call("delete_asset", {"assetPath": T_PATH_IMPORTED})


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_placeholder_created,
    test_placeholder_skip_idempotent,
    test_placeholder_overwrite,
    test_placeholder_deleted,
    test_import_png_fixture,
]


def run() -> Dict[str, Any]:
    # Probe the bridge; skip all tests if not reachable / command not present.
    try:
        probe = _call("asset_exists", {"assetPath": "/Game/__probe__"})
    except Exception as e:  # noqa: BLE001
        return {
            "ok": True,
            "passed": 0,
            "failed": 0,
            "bridge_available": False,
            "results": [{"pass": True, "message": f"{t.__name__}: SKIPPED (bridge: {e})"}
                        for t in TESTS],
        }
    if not isinstance(probe, dict) or probe.get("ok") is None:
        return {
            "ok": True,
            "passed": 0,
            "failed": 0,
            "bridge_available": False,
            "results": [{"pass": True, "message": f"{t.__name__}: SKIPPED (plugin not rebuilt)"}
                        for t in TESTS],
        }

    results = [_check(t.__name__, t) for t in TESTS]
    passed = sum(1 for ok_, _ in results if ok_)
    failed = len(results) - passed
    return {
        "ok": failed == 0,
        "passed": passed,
        "failed": failed,
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
