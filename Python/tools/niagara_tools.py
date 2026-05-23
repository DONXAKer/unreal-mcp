"""
Niagara Tools for Unreal MCP.

Wrappers for Niagara system primitives: copy_niagara_system, set_niagara_parameters.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_niagara_tools(mcp: FastMCP) -> None:
    """Register Niagara tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def copy_niagara_system(
        ctx: Context[Any, Any, Any],
        sourcePath: str,
        destPath: str,
        ifExists: str = None,
    ) -> dict[str, Any]:
        """
        Duplicate a UNiagaraSystem to a new /Game/... path.

        Args:
            sourcePath: Full /Game/... path to an existing UNiagaraSystem.
            destPath: Full /Game/... path for the duplicated asset.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "sourcePath": sourcePath,
                "destPath": destPath,
            }
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Copying Niagara system: {params}")
            response = unreal.send_command("copy_niagara_system", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error copying Niagara system: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_niagara_parameters(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        params: dict[str, Any],
    ) -> dict[str, Any]:
        """
        Set exposed user parameters on a UNiagaraSystem. Each key is the
        parameter name (with or without "User." prefix; the C++ side normalizes).

        Args:
            assetPath: Full /Game/... path of an existing UNiagaraSystem.
            params: Dict of parameter name → value. Supported value types:
                bool, int/float (number), and vectors as [x,y,z] or [x,y,z,w].
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload = {
                "assetPath": assetPath,
                "params": params,
            }
            logger.info(f"Setting Niagara parameters: assetPath={assetPath}")
            response = unreal.send_command("set_niagara_parameters", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting Niagara parameters: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Niagara tools registered successfully")
