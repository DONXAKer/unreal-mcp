"""
Texture Tools for Unreal MCP.

Wrappers for texture primitives: import_texture, generate_placeholder_texture.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_texture_tools(mcp: FastMCP):
    """Register Texture tools with the MCP server."""

    @mcp.tool()
    def import_texture(
        ctx: Context,
        assetPath: str,
        sourcePath: str,
        sRGB: bool = True,
        compression: str = "BC7",
        mipGen: str = "FromTexture",
        ifExists: str = None,
    ) -> Dict[str, Any]:
        """
        Import a texture from disk (PNG/TGA/JPG/etc) into Unreal as a UTexture2D.

        Args:
            assetPath: Full /Game/... path for the resulting UTexture2D.
            sourcePath: Absolute on-disk path to the source image (ASCII only).
            sRGB: sRGB flag (default True).
            compression: TextureCompressionSettings short code (default "BC7").
                Common: "BC7", "BC5", "BC4", "Default", "Masks", "HDR".
            mipGen: MipGenSettings short code (default "FromTexture").
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
                "sRGB": sRGB,
                "compression": compression,
                "mipGen": mipGen,
            }
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Importing texture: {params}")
            response = unreal.send_command("import_texture", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error importing texture: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def generate_placeholder_texture(
        ctx: Context,
        assetPath: str,
        size: int = 512,
        color: List[float] = None,
        label: str = None,
        ifExists: str = None,
    ) -> Dict[str, Any]:
        """
        Generate a synthetic placeholder UTexture2D (solid color, optionally with
        an inline label).

        Args:
            assetPath: Full /Game/... path for the new texture.
            size: Square texture dimension in pixels (1..4096; default 512).
            color: Optional [R, G, B, A] in [0..1]. Defaults to a muted indigo.
            label: Optional text rendered on the placeholder.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite" | "update").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "assetPath": assetPath,
                "size": int(size),
            }
            if color is not None:
                params["color"] = [float(v) for v in color]
            if label is not None:
                params["label"] = label
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Generating placeholder texture: {params}")
            response = unreal.send_command("generate_placeholder_texture", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error generating placeholder texture: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Texture tools registered successfully")
