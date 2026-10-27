"""
Asset Tools for Unreal MCP.

Wrappers for generic asset registry primitives: asset_exists, delete_asset,
get_asset_references, rename_asset.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
Extended: v3.28.0 — references + rename for FIX-CLEANUP-003 / asset hygiene.
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_asset_tools(mcp: FastMCP) -> None:
    """Register Asset tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def asset_exists(
        ctx: Context[Any, Any, Any],
        assetPath: str,
    ) -> dict[str, Any]:
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
        ctx: Context[Any, Any, Any],
        assetPath: str,
        ifMissing: str = "skip",
    ) -> dict[str, Any]:
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

    @mcp.tool()
    def get_asset_references(
        ctx: Context[Any, Any, Any],
        assetPath: str,
    ) -> dict[str, Any]:
        """
        List package paths that reference the given asset (Reference Viewer).

        Args:
            assetPath: Full /Game/... path of the asset to inspect.

        Returns:
            meta.references (list of package paths), meta.count (int).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"assetPath": assetPath}
            logger.info(f"Getting asset references: {assetPath}")
            response = unreal.send_command("get_asset_references", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error getting asset references: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def rename_asset(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        newAssetPath: str,
    ) -> dict[str, Any]:
        """
        Rename or move an asset via EditorAssetLibrary (updates referencers).

        Do NOT use filesystem mv — UE referencers break. Use this instead.

        Args:
            assetPath: Current /Game/... path.
            newAssetPath: Destination /Game/... path (must not already exist).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"assetPath": assetPath, "newAssetPath": newAssetPath}
            logger.info(f"Renaming asset: {assetPath} → {newAssetPath}")
            response = unreal.send_command("rename_asset", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error renaming asset: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Asset tools registered successfully")
