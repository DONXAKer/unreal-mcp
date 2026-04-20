"""
Bridge-layer test for import_static_mesh (MCP-CONTENT-003a).

Covers:
  - Import a bundled FBX fixture → create → skip rerun → overwrite rerun → delete.
  - Self-skips without fixtures/cube.fbx so the suite passes on a clean install
    (matches the pattern of test_texture.py::test_import_png_fixture).

Requires a running UE Editor with UnrealMCP 1.5.0+ rebuilt.

Run:  uv run python -m tests.test_import_static_mesh
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

MESH_PATH = "/Game/Dev/MCPContent/SM_MCP_ImportTest"
FIXTURE_PATH = Path(__file__).parent / "fixtures" / "cube.fbx"


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


def _assert_ok(resp: Dict[str, Any], expected_status: str) -> None:
    assert resp.get("ok") is True, f"expected ok=true, got {resp}"
    assert resp.get("status") == expected_status, \
        f"expected status={expected_status}, got {resp.get('status')} ({resp})"


def _exists(path: str) -> bool:
    resp = _call("asset_exists", {"assetPath": path})
    return resp.get("meta", {}).get("exists", False)


# --- fixture-dependent lifecycle ---------------------------------------------

def test_mesh_created() -> None:
    if not FIXTURE_PATH.is_file():
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    _call("delete_asset", {"assetPath": MESH_PATH, "ifMissing": "skip"})
    assert not _exists(MESH_PATH), "precondition: asset should be absent"

    resp = _call("import_static_mesh", {
        "sourcePath": str(FIXTURE_PATH),
        "assetPath": MESH_PATH,
        "generateCollision": "Simple",
        "materialOverrides": {},
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(MESH_PATH), "asset not in registry after import"

    meta = resp.get("meta", {})
    assert meta.get("collision") == "Simple", f"collision in meta: {meta}"
    assert "trianglesCount" in meta, f"missing trianglesCount: {meta}"


def test_mesh_skip_idempotent() -> None:
    if not FIXTURE_PATH.is_file():
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    resp = _call("import_static_mesh", {
        "sourcePath": str(FIXTURE_PATH),
        "assetPath": MESH_PATH,
        "generateCollision": "Simple",
        "materialOverrides": {},
        "ifExists": "skip",
    })
    _assert_ok(resp, "skipped")


def test_mesh_overwrite() -> None:
    if not FIXTURE_PATH.is_file():
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    resp = _call("import_static_mesh", {
        "sourcePath": str(FIXTURE_PATH),
        "assetPath": MESH_PATH,
        "generateCollision": "None",
        "materialOverrides": {},
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "overwritten")


def test_unknown_override_is_skipped() -> None:
    if not FIXTURE_PATH.is_file():
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    # Passing an obviously-bogus slot name should succeed + report it in
    # meta.skippedOverrides rather than failing the whole import.
    resp = _call("import_static_mesh", {
        "sourcePath": str(FIXTURE_PATH),
        "assetPath": MESH_PATH,
        "generateCollision": "None",
        "materialOverrides": {"ThisSlotDoesNotExist_xyz": "/Engine/EngineMaterials/WorldGridMaterial"},
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "overwritten")
    skipped = resp.get("meta", {}).get("skippedOverrides", [])
    assert "ThisSlotDoesNotExist_xyz" in skipped, \
        f"expected ThisSlotDoesNotExist_xyz in skippedOverrides, got {skipped}"


def test_mesh_deleted() -> None:
    if not FIXTURE_PATH.is_file():
        print(f"  [skip] missing fixture {FIXTURE_PATH}")
        return

    resp = _call("delete_asset", {"assetPath": MESH_PATH})
    _assert_ok(resp, "updated")
    assert not _exists(MESH_PATH), "asset still present after delete"


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_mesh_created,
    test_mesh_skip_idempotent,
    test_mesh_overwrite,
    test_unknown_override_is_skipped,
    test_mesh_deleted,
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
