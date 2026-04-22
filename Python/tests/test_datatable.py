"""Test recipe for DataTable primitives: import CSV → set_row → get_rows → delete."""

import tempfile
import os
from tools.result_format import ok, fail

TEST_ASSET = "/Game/Dev/MCP005_Test_DT_CardStats"


def _call(command, **params):
    from unreal_mcp_server import get_unreal_connection
    conn = get_unreal_connection()
    if conn is None:
        raise RuntimeError("No UE connection")
    raw = conn.send_command(command, params) or {}
    if isinstance(raw, dict) and "result" in raw:
        return raw["result"]
    return raw


def run() -> dict:
    errors = []

    # Create a minimal CSV for import
    csv_content = "Name,Damage,HP\nCard_1,10,100\nCard_2,20,80\n"
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        csv_path = f.name
        f.write(csv_content)

    try:
        # 1. Import DataTable
        r = _call("import_datatable_from_csv",
                  csvPath=csv_path, assetPath=TEST_ASSET, ifExists="overwrite")
        if not r.get("ok"):
            return fail("test", "DT_IMPORT_FAILED", str(r), test="test_datatable")

        # 2. get_rows → verify 2 rows
        rows_r = _call("get_datatable_rows", assetPath=TEST_ASSET)
        row_count = rows_r.get("meta", {}).get("rowCount", -1)
        if row_count < 2:
            errors.append(f"Expected >=2 rows, got {row_count}")

        # 3. set_row — update Card_1 damage
        set_r = _call("set_datatable_row",
                      assetPath=TEST_ASSET, rowName="Card_1",
                      rowJson={"Damage": 15})
        if not set_r.get("ok"):
            errors.append(f"set_datatable_row failed: {set_r}")

        # 4. Verify the row changed
        rows_r2 = _call("get_datatable_rows", assetPath=TEST_ASSET)
        rows = rows_r2.get("meta", {}).get("rows", [])
        card1 = next((r for r in rows if r.get("Name") == "Card_1"), None)
        if card1 and card1.get("Damage") != 15:
            errors.append(f"Row update not reflected: {card1}")

        # 5. skip import (idempotency)
        r2 = _call("import_datatable_from_csv",
                   csvPath=csv_path, assetPath=TEST_ASSET, ifExists="skip")
        if not r2.get("ok"):
            errors.append(f"skip retry failed: {r2}")

    finally:
        os.unlink(csv_path)
        _call("delete_asset", assetPath=TEST_ASSET, ifMissing="skip")

    if errors:
        return fail("test", "ASSERTIONS_FAILED", "; ".join(errors), test="test_datatable")
    return ok("created", "test-report", name="test_datatable", duration_ms=0)
