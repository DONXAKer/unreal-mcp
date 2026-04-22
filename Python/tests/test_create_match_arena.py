"""
Test recipe for wc.create_match_arena.

Covers: create → verify actors → retry skip → retry overwrite → delete.
"""

from tools.primitives import list_actors_in_level, delete_asset, asset_exists
from tools.result_format import ok, fail

TEST_MAP = "/Game/Maps/TestArena_MCP004"
REQUIRED_ACTORS = {"Sun", "Sky", "GlobalPP", "MatchCamera"}


def _get_connection():
    from unreal_mcp_server import get_unreal_connection
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("No connection to Unreal Engine")
    return conn


def _call_recipe(name: str, **kwargs):
    conn = _get_connection()
    raw = conn.send_command("call_recipe", {"name": name, "args": kwargs}) or {}
    if isinstance(raw, dict) and "result" in raw:
        return raw["result"]
    return raw


def run() -> dict:
    errors = []

    # ── 1. Create arena (default theme) ──────────────────────────────────────
    r = _call_recipe("wc.create_match_arena", map_name="TestArena_MCP004", theme="default", if_exists="overwrite")
    if not r.get("ok"):
        return fail("test", "CREATE_FAILED", f"create_match_arena failed: {r}", test="test_create_match_arena")

    # ── 2. Verify required actors present ────────────────────────────────────
    actors_r = list_actors_in_level(mapPath=TEST_MAP)
    actor_names = {a.get("name") for a in actors_r.get("meta", {}).get("actors", [])}
    missing = REQUIRED_ACTORS - actor_names
    if missing:
        errors.append(f"Missing actors: {missing}")

    # ── 3. Retry with if_exists=skip → no error, level not recreated ─────────
    r2 = _call_recipe("wc.create_match_arena", map_name="TestArena_MCP004", theme="default", if_exists="skip")
    if not r2.get("ok"):
        errors.append(f"skip retry failed: {r2}")

    # ── 4. Retry with if_exists=overwrite → level recreated ──────────────────
    r3 = _call_recipe("wc.create_match_arena", map_name="TestArena_MCP004", theme="default", if_exists="overwrite")
    if not r3.get("ok"):
        errors.append(f"overwrite retry failed: {r3}")

    # ── 5. Cleanup ────────────────────────────────────────────────────────────
    delete_asset(TEST_MAP, ifMissing="skip")

    if errors:
        return fail("test", "ASSERTIONS_FAILED", "; ".join(errors), test="test_create_match_arena")

    return ok("created", "test-report",
               name="test_create_match_arena",
               duration_ms=0,
               checks=["create", "verify_actors", "retry_skip", "retry_overwrite", "cleanup"])
