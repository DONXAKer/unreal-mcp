"""
Mesh Tools for Unreal MCP.

Wrappers for static mesh primitives: import_static_mesh.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_mesh_tools(mcp: FastMCP):
    """Register Mesh tools with the MCP server."""

    @mcp.tool()
    def import_static_mesh(
        ctx: Context,
        assetPath: str,
        sourcePath: str,
        generateCollision: str = "Simple",
        materialOverrides: Dict[str, str] = None,
        ifExists: str = None,
    ) -> Dict[str, Any]:
        """
        Import an FBX or OBJ static mesh into Unreal as a UStaticMesh.

        Args:
            assetPath: Full /Game/... path for the resulting UStaticMesh.
            sourcePath: Absolute path to the FBX or OBJ file on disk.
            generateCollision: "None" | "Simple" (default) | "Complex".
            materialOverrides: Optional dict of mesh material slot name → /Game/...
                material path. Slots not in the dict are left at engine defaults.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite" | "update").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "assetPath": assetPath,
                "sourcePath": sourcePath,
                "generateCollision": generateCollision,
            }
            if materialOverrides is not None:
                params["materialOverrides"] = materialOverrides
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Importing static mesh: {params}")
            response = unreal.send_command("import_static_mesh", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error importing static mesh: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Mesh tools registered successfully")
