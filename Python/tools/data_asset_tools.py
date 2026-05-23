"""
Data Asset Tools for Unreal MCP.

Wrappers for DataTable / SoundWave primitives: import_datatable_from_csv,
set_datatable_row, get_datatable_rows, import_sound_wave.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_data_asset_tools(mcp: FastMCP):
    """Register DataAsset tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def import_datatable_from_csv(
        ctx: Context,
        assetPath: str,
        csvPath: str,
        rowStruct: str = None,
        ifExists: str = None,
    ) -> Dict[str, Any]:
        """
        Import a CSV into a UDataTable.

        Args:
            assetPath: Full /Game/... path for the resulting UDataTable.
            csvPath: Absolute on-disk path to the source .csv file.
            rowStruct: Optional UScriptStruct path overriding the factory default
                (e.g. "/Script/Client.MyRowStruct").
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "assetPath": assetPath,
                "csvPath": csvPath,
            }
            if rowStruct is not None:
                params["rowStruct"] = rowStruct
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Importing DataTable from CSV: {params}")
            response = unreal.send_command("import_datatable_from_csv", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error importing DataTable from CSV: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_datatable_row(
        ctx: Context,
        assetPath: str,
        rowName: str,
        rowJson: Dict[str, Any],
    ) -> Dict[str, Any]:
        """
        Insert or update a single row in a UDataTable. Field names not present
        on the row struct are silently skipped (returned in meta).

        Args:
            assetPath: Full /Game/... path of an existing UDataTable.
            rowName: Row key (FName).
            rowJson: Dict of struct field name → value. Supported field types:
                string, name, text, int, float, double, bool.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "assetPath": assetPath,
                "rowName": rowName,
                "rowJson": rowJson,
            }
            logger.info(f"Setting DataTable row: assetPath={assetPath} rowName={rowName}")
            response = unreal.send_command("set_datatable_row", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting DataTable row: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_datatable_rows(
        ctx: Context,
        assetPath: str,
    ) -> Dict[str, Any]:
        """
        Read-only: enumerate all rows of a UDataTable with their reflected field
        values.

        Returns:
            Dict with meta.rowCount and meta.rows (list of {rowName, fields{}}).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"assetPath": assetPath}
            logger.info(f"Reading DataTable rows: {assetPath}")
            response = unreal.send_command("get_datatable_rows", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading DataTable rows: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def import_sound_wave(
        ctx: Context,
        assetPath: str,
        wavPath: str,
        ifExists: str = None,
    ) -> Dict[str, Any]:
        """
        Import a .wav file as a USoundWave asset.

        Args:
            assetPath: Full /Game/... path for the resulting USoundWave.
            wavPath: Absolute on-disk path to the source .wav file.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "assetPath": assetPath,
                "wavPath": wavPath,
            }
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Importing SoundWave: {params}")
            response = unreal.send_command("import_sound_wave", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error importing SoundWave: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("DataAsset tools registered successfully")
