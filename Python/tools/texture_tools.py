"""
Texture Tools for Unreal MCP.

Wrappers for texture primitives: import_texture, generate_placeholder_texture.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_texture_tools(mcp: FastMCP) -> None:
    """Register Texture tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def import_texture(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        sourcePath: str,
        sRGB: bool = True,
        compression: str = "BC7",
        mipGen: str = "FromTexture",
        ifExists: str = None,
    ) -> dict[str, Any]:
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
            params: dict[str, Any] = {
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
        ctx: Context[Any, Any, Any],
        assetPath: str,
        size: int = 512,
        color: list[float] = None,
        label: str = None,
        ifExists: str = None,
    ) -> dict[str, Any]:
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
            params: dict[str, Any] = {
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

    @mcp.tool()
    def texture_set_properties(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        compression: str = None,
        sRGB: bool = None,
        flipGreenChannel: bool = None,
    ) -> dict[str, Any]:
        """
        Change properties of an ALREADY imported UTexture2D.

        Why this exists separately from import_texture: passing
        compression="Normalmap" AT IMPORT TIME hangs the UE 5.8 importer for the
        rest of the editor session (ImportAssetsAutomated stops returning assets).
        Import normal / mask textures with the default compression, then fix the
        type here — this path recompresses through a normal PostEditChange.

        Setting compression to "Normalmap" or "Grayscale" clears sRGB
        automatically (linear data).

        Args:
            assetPath: Full /Game/... path of an existing UTexture2D.
            compression: TextureCompressionSettings short code, e.g. "BC7",
                "Normalmap", "Masks", "Grayscale", "Default".
            sRGB: explicit sRGB flag (applied after the compression auto-rule).
            flipGreenChannel: flip the green channel (DirectX vs OpenGL normals).

        Example:
            texture_set_properties(
                assetPath="/Game/Maps/Terrain/T_Splat_1",
                compression="Masks", sRGB=False)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {"assetPath": assetPath}
            if compression is not None:
                params["compression"] = compression
            if sRGB is not None:
                params["sRGB"] = bool(sRGB)
            if flipGreenChannel is not None:
                params["flipGreenChannel"] = bool(flipGreenChannel)
            logger.info(f"Setting texture properties: {params}")
            response = unreal.send_command("texture_set_properties", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting texture properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Texture tools registered successfully")
