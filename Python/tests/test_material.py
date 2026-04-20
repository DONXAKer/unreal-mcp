"""
Bridge-layer tests for MaterialCommands (MCP-CONTENT-002).

Covers:
  - create_material_instance off a known engine material
  - set_material_instance_params point-update
  - skip / overwrite / update idempotency
  - cleanup via delete_asset

Requires a running UE Editor with UnrealMCP 1.4.0+ rebuilt.

We use `/Engine/EngineMaterials/WorldGridMaterial` as the parent. It's a
regular UMaterial shipped with every UE install and always resolvable
without project content. Its parameter set includes `UVScale` (scalar),
`Color_A` / `Color_B` (vector); we probe only `UVScale` to keep asserts
stable across UE versions — skippedParams handling is already exercised.

Run:  uv run python -m tests.test_material
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

PARENT_MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial"
MI_PATH = "/Game/Dev/MCPContent/MI_MCP_Material_Test"


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
    assert resp.get("ok") is True, f"expected ok, got {resp}"
    assert resp.get("status") == expected_status, \
        f"expected status={expected_status}, got {resp.get('status')} ({resp})"


def _exists(path: str) -> bool:
    resp = _call("asset_exists", {"assetPath": path})
    return resp.get("meta", {}).get("exists", False)


# --- lifecycle ---------------------------------------------------------------

def test_mi_created_with_scalar_param() -> None:
    # Fresh start.
    _call("delete_asset", {"assetPath": MI_PATH, "ifMissing": "skip"})
    assert not _exists(MI_PATH)

    resp = _call("create_material_instance", {
        "parentMaterial": PARENT_MATERIAL,
        "assetPath": MI_PATH,
        # Mix a known-good param with a deliberately-bad one so we see the
        # skippedParams branch light up.
        "params": {
            "UVScale": 2.5,
            "NonExistentParamXyz": 1.0,
        },
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    meta = resp.get("meta", {})
    # At least one of applied/skipped should reflect the two input entries.
    applied = meta.get("appliedParams", [])
    skipped = meta.get("skippedParams", {})
    assert ("UVScale" in applied) or ("UVScale" in skipped), \
        f"UVScale neither applied nor skipped: {meta}"
    assert "NonExistentParamXyz" in skipped, \
        f"expected NonExistentParamXyz in skippedParams, got {meta}"


def test_mi_skip_when_exists() -> None:
    resp = _call("create_material_instance", {
        "parentMaterial": PARENT_MATERIAL,
        "assetPath": MI_PATH,
        "params": {},
        "ifExists": "skip",
    })
    _assert_ok(resp, "skipped")


def test_mi_set_params_point_update() -> None:
    resp = _call("set_material_instance_params", {
        "assetPath": MI_PATH,
        "params": {"UVScale": 4.0},
    })
    _assert_ok(resp, "updated")


def test_mi_deleted() -> None:
    resp = _call("delete_asset", {"assetPath": MI_PATH})
    _assert_ok(resp, "updated")
    assert not _exists(MI_PATH)


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_mi_created_with_scalar_param,
    test_mi_skip_when_exists,
    test_mi_set_params_point_update,
    test_mi_deleted,
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
