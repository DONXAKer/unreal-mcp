"""
Test recipe for Niagara primitives: copy_niagara_system → set_niagara_parameters → delete.

Requires at least one existing NiagaraSystem in the project. Skipped (not failed)
if none exists.
"""

from typing import Any

from tools.primitives import asset_exists, delete_asset
from tools.result_format import fail, ok

BASE_NS = "/Game/VFX/NS_CardHitBase"
TEST_NS = "/Game/Dev/MCP005_Test_NS_CardHit"


def _call(command, **params):
    from unreal_mcp_server import get_unreal_connection
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("No UE connection")
    raw = conn.send_command(command, params) or {}
    if isinstance(raw, dict) and "result" in raw:
        return raw["result"]
    return raw


def run() -> dict[str, Any]:
    errors = []

    # Skip gracefully if base Niagara system doesn't exist
    exists_r = asset_exists(BASE_NS)
    if not exists_r.get("meta", {}).get("exists"):
        return ok(
            "skipped",
            "test-report",
            name="test_niagara",
            duration_ms=0,
            reason=f"Base Niagara system not found: {BASE_NS}",
        )

    try:
        # 1. Copy NS
        r = _call("copy_niagara_system", sourcePath=BASE_NS, destPath=TEST_NS, ifExists="overwrite")
        if not r.get("ok"):
            # Could be NIAGARA_UNAVAILABLE if module not loaded — treat as skip
            if r.get("error", {}).get("code") == "NIAGARA_UNAVAILABLE":
                return ok("skipped", "test-report", name="test_niagara", duration_ms=0,
                          reason="Niagara module unavailable in this build")
            return fail("test", "NS_COPY_FAILED", str(r), test="test_niagara")

        # 2. Set parameters
        set_r = _call("set_niagara_parameters",
                      assetPath=TEST_NS,
                      params={"User.Scale": 1.5, "User.Color": [1.0, 0.5, 0.0, 1.0]})
        if not set_r.get("ok"):
            errors.append(f"set_niagara_parameters failed: {set_r}")

        # 3. Verify asset exists
        exists_after = asset_exists(TEST_NS)
        if not exists_after.get("meta", {}).get("exists"):
            errors.append("Niagara copy not found after copy_niagara_system")

        # 3b. niagara_get_info must describe the copy (v3.22.0)
        info = _call("niagara_get_info", assetPath=TEST_NS, includeModuleInputs=True)
        if not info.get("ok"):
            errors.append(f"niagara_get_info failed: {info}")
        else:
            meta = info.get("meta", {})
            for key in ("emitterCount", "isEmpty", "emitters", "warnings"):
                if key not in meta:
                    errors.append(f"niagara_get_info meta missing '{key}'")

        # 3c. A bogus module input must FAIL loudly with alternatives,
        #     never report a silent success.
        miss = _call("niagara_set_module_input",
                     systemPath=TEST_NS,
                     emitterName="__nope__",
                     moduleName="__nope__",
                     inputName="__nope__",
                     value=1.0,
                     compile=False)
        if miss.get("ok"):
            errors.append("niagara_set_module_input reported ok for a nonexistent emitter/input")
        else:
            code = miss.get("error", {}).get("code")
            if code not in ("NIAGARA_EMITTER_HANDLE_NOT_FOUND", "NIAGARA_MODULE_INPUT_NOT_FOUND"):
                errors.append(f"unexpected error code for bogus module input: {code}")

        # 4. Skip idempotency
        r2 = _call("copy_niagara_system", sourcePath=BASE_NS, destPath=TEST_NS, ifExists="skip")
        if not r2.get("ok"):
            errors.append(f"skip retry failed: {r2}")

    finally:
        delete_asset(TEST_NS, ifMissing="skip")

    if errors:
        return fail("test", "ASSERTIONS_FAILED", "; ".join(errors), test="test_niagara")
    return ok("created", "test-report", name="test_niagara", duration_ms=0)
