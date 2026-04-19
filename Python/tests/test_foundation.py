"""
Foundation smoke tests for MCP Content Pipeline (MCP-CONTENT-001).

Two layers:
  1. Python-only unit checks — config loader, result_format helpers, @recipe
     decorator signature wiring. No UE required.
  2. Bridge integration checks — call `asset_exists` / `delete_asset` via the
     MCP TCP bridge. Require UE Editor with the rebuilt UnrealMCP 1.3.0 plugin
     running. Skipped automatically if the bridge is unreachable.

Run via: `uv run python -m tests.test_foundation` from unreal-mcp/Python/.
Will be integrated into `wc.run_tests` in MCP-CONTENT-005.
"""

from __future__ import annotations

import inspect
import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import project_config, recipe_framework, result_format  # noqa: E402


Result = Tuple[bool, str]


def _check(label: str, fn: Callable[[], None]) -> Result:
    try:
        fn()
        return True, label
    except AssertionError as e:
        return False, f"{label}: {e}"
    except Exception:  # noqa: BLE001
        return False, f"{label}: unexpected\n{traceback.format_exc()}"


# --- Layer 1: Python-only checks ---------------------------------------------

def test_result_format_shapes() -> None:
    r = result_format.ok("created", "/Game/X", extra=1)
    assert r == {"ok": True, "status": "created", "assetPath": "/Game/X",
                 "meta": {"extra": 1}}, r
    f = result_format.fail("user", "BAD", "nope", where="path")
    assert f["ok"] is False
    assert f["error"]["category"] == "user"
    assert f["error"]["code"] == "BAD"
    assert f["error"]["details"] == {"where": "path"}


def test_normalize_legacy_success() -> None:
    legacy = {"success": True, "name": "Foo"}
    r = result_format.normalize_legacy_response(legacy)
    assert r["ok"] is True


def test_normalize_legacy_error() -> None:
    legacy = {"status": "error", "error": "boom"}
    r = result_format.normalize_legacy_response(legacy)
    assert r["ok"] is False
    assert r["error"]["code"] == "LEGACY_ERROR"


def test_namespace_derivation() -> None:
    assert project_config.ProjectConfig(projectName="WarCard").namespace == "wc"
    assert project_config.ProjectConfig(projectName="Project Name").namespace == "pn"
    assert project_config.ProjectConfig(projectName="MyGameProject").namespace == "mgp"


def test_recipe_decorator_collects_args_and_produces() -> None:
    @recipe_framework.recipe(name="demo", desc="d", produces=["/Game/T_{id}"])
    @recipe_framework.arg("id", int, required=True)
    @recipe_framework.arg("opt", str, required=False, default="x")
    def demo(id: int, opt: str) -> Dict[str, Any]:
        return result_format.ok("created", f"/Game/T_{id}_{opt}")

    spec = getattr(demo, recipe_framework._RECIPE_SPEC_ATTR)
    assert spec.name == "demo"
    assert spec.description == "d"
    assert spec.produces == ["/Game/T_{id}"]
    assert [a.name for a in spec.args] == ["id", "opt"]
    assert spec.args[0].required is True
    assert spec.args[1].required is False
    assert spec.args[1].default == "x"
    # Direct call still works
    r = demo(id=5, opt="q")
    assert r["assetPath"] == "/Game/T_5_q"


def test_recipe_wire_tool_builds_signature() -> None:
    from mcp.server.fastmcp import FastMCP
    server = FastMCP("test")
    registry = recipe_framework.RecipeRegistry(server)

    @recipe_framework.recipe(name="sig_demo", desc="s")
    @recipe_framework.arg("x", int, required=True)
    @recipe_framework.arg("y", str, required=False, default="")
    def sig_demo(x: int, y: str) -> Dict[str, Any]:
        return result_format.ok("created", f"/Game/{x}_{y}")

    spec = getattr(sig_demo, recipe_framework._RECIPE_SPEC_ATTR)
    qualified = registry.register(spec, namespace="wc")
    assert qualified == "wc.sig_demo"

    tool_manager = getattr(server, "_tool_manager", None)
    assert tool_manager is not None
    tools_map = getattr(tool_manager, "_tools", {})
    assert "wc.sig_demo" in tools_map, list(tools_map.keys())
    tool_obj = tools_map["wc.sig_demo"]
    wrapper = getattr(tool_obj, "fn", None) or getattr(tool_obj, "function", None)
    assert wrapper is not None, tool_obj
    sig = inspect.signature(wrapper)
    assert list(sig.parameters.keys()) == ["x", "y"]
    assert sig.parameters["x"].annotation is int
    assert sig.parameters["y"].default == ""


# --- Layer 2: bridge integration (skipped if no UE) --------------------------

def _call_bridge(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    from unreal_mcp_server import get_unreal_connection  # type: ignore
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("bridge unreachable")
    return conn.send_command(command, params) or {}


def test_asset_exists_bridge() -> None:
    # Expects WarCard editor running with rebuilt UnrealMCP 1.3.0.
    resp = _call_bridge("asset_exists", {"assetPath": "/Game/UI/WBP_GameResult"})
    inner = resp.get("result", resp)
    assert inner.get("ok") is True, resp
    assert "exists" in inner.get("meta", {}), inner


def test_asset_exists_negative_bridge() -> None:
    resp = _call_bridge("asset_exists", {"assetPath": "/Game/__Definitely_Not_Here__"})
    inner = resp.get("result", resp)
    assert inner.get("ok") is True, resp
    assert inner["meta"]["exists"] is False, inner


# --- Runner ------------------------------------------------------------------

UNIT_TESTS: List[Callable[[], None]] = [
    test_result_format_shapes,
    test_normalize_legacy_success,
    test_normalize_legacy_error,
    test_namespace_derivation,
    test_recipe_decorator_collects_args_and_produces,
    test_recipe_wire_tool_builds_signature,
]

BRIDGE_TESTS: List[Callable[[], None]] = [
    test_asset_exists_bridge,
    test_asset_exists_negative_bridge,
]


def run() -> Dict[str, Any]:
    unit_results = [_check(t.__name__, t) for t in UNIT_TESTS]
    bridge_available = True
    skip_reason = ""
    try:
        probe = _call_bridge("asset_exists", {"assetPath": "/Game/__probe__"})
        if isinstance(probe, dict):
            err = str(probe.get("error", "")) if probe.get("status") == "error" else ""
            if "Unknown command" in err:
                bridge_available = False
                skip_reason = "plugin not rebuilt (UnrealMCP 1.3.0 required)"
    except Exception:  # noqa: BLE001
        bridge_available = False
        skip_reason = "bridge unreachable"

    bridge_results: List[Result]
    if bridge_available:
        bridge_results = [_check(t.__name__, t) for t in BRIDGE_TESTS]
    else:
        bridge_results = [(True, f"{t.__name__}: SKIPPED ({skip_reason})")
                          for t in BRIDGE_TESTS]

    all_results = unit_results + bridge_results
    passed = sum(1 for ok_, _ in all_results if ok_)
    failed = len(all_results) - passed
    return {
        "ok": failed == 0,
        "passed": passed,
        "failed": failed,
        "bridge_available": bridge_available,
        "results": [{"pass": ok_, "message": msg} for ok_, msg in all_results],
    }


if __name__ == "__main__":
    summary = run()
    for r in summary["results"]:
        prefix = "PASS" if r["pass"] else "FAIL"
        print(f"[{prefix}] {r['message']}")
    print(f"\n{summary['passed']}/{summary['passed'] + summary['failed']} tests passed"
          + (" (bridge skipped)" if not summary["bridge_available"] else ""))
    sys.exit(0 if summary["ok"] else 1)
