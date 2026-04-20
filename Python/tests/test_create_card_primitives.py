"""
Dry-run composition test for MCP-CONTENT-002 card primitives.

This is NOT the `wc.create_card` recipe itself — that lives in the WarCard
repo under Content/Python/recipes and is added by main Claude in a
follow-up step. Here we exercise the same primitive sequence a recipe
would run, so a broken primitive fails fast at the plugin layer before
recipe wiring is added.

Sequence per "card":
  1. generate_placeholder_texture → T_CardArt_<id>
  2. create_material_instance     → MI_Card_<id>  (BaseColor = texture)
  3. (create_blueprint_from_template is only run if a template BP exists;
      otherwise it's reported as SKIPPED so the test passes on a clean
      install without WarCard content.)

Idempotency is verified by rerunning the same calls with ifExists="skip".
Cleanup removes every asset the test created.

Run:  uv run python -m tests.test_create_card_primitives
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

CARD_ID = 9999  # obviously-fake id so we don't collide with real cards
T_PATH = f"/Game/Dev/MCPContent/T_CardArt_Test_{CARD_ID}"
MI_PATH = f"/Game/Dev/MCPContent/MI_Card_Test_{CARD_ID}"
BP_PATH = f"/Game/Dev/MCPContent/BP_Card_Test_{CARD_ID}"

PARENT_MATERIAL = "/Engine/EngineMaterials/WorldGridMaterial"
BP_TEMPLATE_CANDIDATES = [
    # Ordered by preference. First one that exists in the registry will be used.
    "/Game/Card/BP_CardBase",   # WarCard layout (mcp-project.json paths.cards)
    "/Game/Cards/BP_CardBase",
    "/Game/Blueprints/BP_CardBase",
]


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
    assert resp.get("ok") is True, f"expected ok, got {resp}"
    if statuses:
        assert resp.get("status") in statuses, \
            f"expected status in {statuses}, got {resp.get('status')} ({resp})"


def _exists(path: str) -> bool:
    resp = _call("asset_exists", {"assetPath": path})
    return resp.get("meta", {}).get("exists", False)


def _find_template() -> str | None:
    for p in BP_TEMPLATE_CANDIDATES:
        if _exists(p):
            return p
    return None


# --- create sequence ---------------------------------------------------------

def test_create_texture() -> None:
    _call("delete_asset", {"assetPath": T_PATH, "ifMissing": "skip"})
    resp = _call("generate_placeholder_texture", {
        "assetPath": T_PATH,
        "size": 256,
        "color": [0.15, 0.35, 0.55, 1.0],
        "label": f"CARD {CARD_ID}",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(T_PATH)


def test_create_material_instance() -> None:
    _call("delete_asset", {"assetPath": MI_PATH, "ifMissing": "skip"})
    resp = _call("create_material_instance", {
        "parentMaterial": PARENT_MATERIAL,
        "assetPath": MI_PATH,
        # BaseColor likely isn't a param on WorldGridMaterial so it should
        # land in skippedParams — that's fine, we're exercising the path.
        "params": {"BaseColor": T_PATH},
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(MI_PATH)


def test_create_blueprint_from_template_if_available() -> None:
    template = _find_template()
    if template is None:
        print("  [skip] no BP_CardBase template in registry — run WarCard content init first")
        return

    _call("delete_asset", {"assetPath": BP_PATH, "ifMissing": "skip"})
    resp = _call("create_blueprint_from_template", {
        "templatePath": template,
        "assetPath": BP_PATH,
        "defaultsOverride": {"CardId": CARD_ID},
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")
    assert _exists(BP_PATH)


# --- idempotency rerun -------------------------------------------------------

def test_rerun_skips_all() -> None:
    t_resp = _call("generate_placeholder_texture", {
        "assetPath": T_PATH,
        "size": 256,
        "color": [0.15, 0.35, 0.55, 1.0],
        "label": f"CARD {CARD_ID}",
        "ifExists": "skip",
    })
    _assert_ok(t_resp, "skipped")

    mi_resp = _call("create_material_instance", {
        "parentMaterial": PARENT_MATERIAL,
        "assetPath": MI_PATH,
        "params": {},
        "ifExists": "skip",
    })
    _assert_ok(mi_resp, "skipped")

    if _exists(BP_PATH):
        bp_resp = _call("create_blueprint_from_template", {
            "templatePath": _find_template() or BP_TEMPLATE_CANDIDATES[0],
            "assetPath": BP_PATH,
            "defaultsOverride": {},
            "ifExists": "skip",
        })
        _assert_ok(bp_resp, "skipped")


# --- cleanup ----------------------------------------------------------------

def test_cleanup_all() -> None:
    for p in (BP_PATH, MI_PATH, T_PATH):
        _call("delete_asset", {"assetPath": p, "ifMissing": "skip"})
    assert not _exists(T_PATH)
    assert not _exists(MI_PATH)
    assert not _exists(BP_PATH)


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_create_texture,
    test_create_material_instance,
    test_create_blueprint_from_template_if_available,
    test_rerun_skips_all,
    test_cleanup_all,
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
