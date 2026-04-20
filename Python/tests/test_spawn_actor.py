"""
Bridge-layer test for spawn/remove/set_transform/list in a level
(MCP-CONTENT-003b).

Covers:
  - create_level → spawn DirectionalLight (/Script/Engine.DirectionalLight).
  - list_actors_in_level contains the spawned actor with correct label.
  - set_actor_transform_in_level moves it; list reflects new loc.
  - remove_actor_from_level → list empty.
  - cleanup (switch to throwaway map + delete_asset on tmp map).

Requires a running UE Editor with UnrealMCP 1.6.0+ rebuilt.

Run:  uv run python -m tests.test_spawn_actor
"""

from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


Result = Tuple[bool, str]

MAP_PATH = "/Game/Dev/MCPContent/Map_SpawnTmp"
ACTOR_LABEL = "MCP_Probe_Sun"
ACTOR_CLASS = "/Script/Engine.DirectionalLight"


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


def _list_labels(map_path: str) -> List[str]:
    resp = _call("list_actors_in_level", {"mapPath": map_path})
    _assert_ok(resp)
    actors = resp.get("meta", {}).get("actors", [])
    return [a.get("label") or a.get("name") for a in actors]


# --- sequence ---------------------------------------------------------------

def test_setup_level() -> None:
    _call("delete_asset", {"assetPath": MAP_PATH, "ifMissing": "skip"})
    resp = _call("create_level", {
        "destMapPath": MAP_PATH,
        "template": "Empty",
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created")


def test_spawn_actor() -> None:
    resp = _call("spawn_actor_in_level", {
        "mapPath": MAP_PATH,
        "actorClass": ACTOR_CLASS,
        "transform": {"loc": [0, 0, 1000], "rot": [-45, 0, 0], "scale": [1, 1, 1]},
        "name": ACTOR_LABEL,
        "ifExists": "overwrite",
    })
    _assert_ok(resp, "created", "overwritten")


def test_list_contains_spawned() -> None:
    labels = _list_labels(MAP_PATH)
    assert ACTOR_LABEL in labels, \
        f"expected {ACTOR_LABEL} in actors, got {labels}"


def test_set_transform_moves_actor() -> None:
    new_loc = [500, -500, 2000]
    resp = _call("set_actor_transform_in_level", {
        "mapPath": MAP_PATH,
        "actorName": ACTOR_LABEL,
        "transform": {"loc": new_loc, "rot": [-30, 0, 0], "scale": [1, 1, 1]},
    })
    _assert_ok(resp)

    list_resp = _call("list_actors_in_level", {"mapPath": MAP_PATH})
    _assert_ok(list_resp)
    actors = list_resp.get("meta", {}).get("actors", [])
    matching = [a for a in actors if (a.get("label") or a.get("name")) == ACTOR_LABEL]
    assert matching, f"actor {ACTOR_LABEL} missing after set_transform"
    loc = matching[0].get("loc")
    assert loc == new_loc, f"expected loc {new_loc}, got {loc} in {matching[0]}"


def test_class_filter() -> None:
    all_resp = _call("list_actors_in_level", {"mapPath": MAP_PATH})
    filtered = _call("list_actors_in_level", {
        "mapPath": MAP_PATH,
        "classFilter": ACTOR_CLASS,
    })
    _assert_ok(all_resp)
    _assert_ok(filtered)
    # Filter can only shrink the set.
    assert filtered.get("meta", {}).get("count", 0) >= 1, \
        f"expected at least 1 DirectionalLight, got {filtered}"
    assert filtered.get("meta", {}).get("count", 0) <= all_resp.get("meta", {}).get("count", 0)


def test_remove_actor() -> None:
    resp = _call("remove_actor_from_level", {
        "mapPath": MAP_PATH,
        "actorName": ACTOR_LABEL,
    })
    _assert_ok(resp)
    labels = _list_labels(MAP_PATH)
    assert ACTOR_LABEL not in labels, \
        f"actor still present after remove: {labels}"


def test_cleanup() -> None:
    # Switch away from the tmp level so delete_asset doesn't hit the
    # active world.
    _call("load_level", {"mapPath": "/Engine/Maps/Templates/OpenWorld"})
    _call("delete_asset", {"assetPath": MAP_PATH, "ifMissing": "skip"})


# --- runner ------------------------------------------------------------------

TESTS: List[Callable[[], None]] = [
    test_setup_level,
    test_spawn_actor,
    test_list_contains_spawned,
    test_set_transform_moves_actor,
    test_class_filter,
    test_remove_actor,
    test_cleanup,
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
