"""
Asset Tools for Unreal MCP.

Wrappers for generic asset registry primitives: asset_exists, delete_asset.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_asset_tools(mcp: FastMCP):
    """Register Asset tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def asset_exists(
        ctx: Context,
        assetPath: str,
    ) -> Dict[str, Any]:
        """
        Read-only: check whether an asset exists in the AssetRegistry.

        Args:
            assetPath: Full /Game/... or /Engine/... package path.

        Returns:
            Dict with meta.exists (bool) and meta.class (UClass name, when present).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"assetPath": assetPath}
            logger.info(f"Checking asset existence: {assetPath}")
            response = unreal.send_command("asset_exists", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error checking asset existence: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def delete_asset(
        ctx: Context,
        assetPath: str,
        ifMissing: str = "skip",
    ) -> Dict[str, Any]:
        """
        Delete an asset by /Game/... path.

        Args:
            assetPath: Full /Game/... path of the asset to remove.
            ifMissing: "skip" (default — idempotent no-op) or "fail".
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "assetPath": assetPath,
                "ifMissing": ifMissing,
            }
            logger.info(f"Deleting asset: {params}")
            response = unreal.send_command("delete_asset", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error deleting asset: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Asset tools registered successfully")
