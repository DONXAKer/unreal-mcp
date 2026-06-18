"""
Landscape Tools for Unreal MCP.

Python wrappers for the C++ FLandscapeCommands handler — creates real UE5
Landscape actors from 16-bit grayscale PNG heightmaps.

Introduced: v3.10.1 (patch — Python wrapper was missing from v3.10.0 C++ release).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_landscape_tools(mcp: FastMCP) -> None:
    """Register Landscape tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_landscape(
        ctx: Context[Any, Any, Any],
        heightmap_path: str,
        section_size: int = 63,
        sections_per_component: int = 1,
        components_x: int = 32,
        components_y: int = 32,
        scale_x: float = 100.0,
        scale_y: float = 100.0,
        scale_z: float = 100.0,
        material_path: str = None,
        actor_label: str = "Landscape",
    ) -> dict[str, Any]:
        """
        Create a real UE5 Landscape actor from a 16-bit grayscale PNG heightmap.

        Uses the native C++ ALandscape::Import path — the only way in UE 5.8 to
        produce a fully-functional Landscape with heightmap components from within
        the Editor's MCP bridge. Python UE API only produces a placeholder.

        The PNG dimensions must match exactly:
            width  = components_x * sections_per_component * section_size + 1
            height = components_y * sections_per_component * section_size + 1

        For default params (32 x 32 components, 1 section each, size 63):
            expected PNG size = 2017 x 2017 pixels

        Pixel values are big-endian 16-bit grayscale (0 = lowest, 65535 = highest).
        Convert to 16-bit with: `convert input.png -depth 16 -colorspace Gray output.png`

        After import the current level is automatically saved.

        Args:
            heightmap_path: Absolute OS path to the 16-bit grayscale PNG.
                Example: "D:/WarCard/client/ArtSource/Terrain/landscape_heightmap.png"
            section_size: Quads per landscape section. Must be one of:
                7, 15, 31, 63, 127, 255. Default: 63.
            sections_per_component: Sections per component. Must be 1 or 2.
                Default: 1.
            components_x: Number of components along X axis. Default: 32.
            components_y: Number of components along Y axis. Default: 32.
            scale_x: Actor scale X in cm per quad. Default: 100.0.
            scale_y: Actor scale Y in cm per quad. Default: 100.0.
            scale_z: Actor scale Z in cm (real Z range = scale_z * 512 cm).
                Default: 100.0.
            material_path: Optional /Game/... path of a UMaterialInterface to
                assign to the Landscape. Example: "/Game/Maps/Materials/M_Landscape"
            actor_label: Label for the actor in the editor world outliner.
                Default: "Landscape".

        Returns:
            On success: {ok: true, status: "created", meta: {actor_label, width,
                height, components, section_size, scale, location}}
            On failure: {ok: false, error: {category, code, message, details}}

        Example (default 2017x2017 terrain):
            create_landscape(
                heightmap_path="D:/WarCard/client/ArtSource/Terrain/hmap.png",
                components_x=32, components_y=32,
                scale_x=100.0, scale_y=100.0, scale_z=64.0,
                actor_label="Landscape_Main"
            )

        Example (small 513x513 test terrain):
            create_landscape(
                heightmap_path="D:/tmp/test_hmap.png",
                section_size=63, sections_per_component=1,
                components_x=8, components_y=8,
                scale_x=100.0, scale_y=100.0, scale_z=100.0,
                actor_label="Landscape_Test"
            )
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: dict[str, Any] = {
                "heightmap_path": heightmap_path,
                "section_size": section_size,
                "sections_per_component": sections_per_component,
                "components_x": components_x,
                "components_y": components_y,
                "scale_x": scale_x,
                "scale_y": scale_y,
                "scale_z": scale_z,
                "actor_label": actor_label,
            }
            if material_path is not None:
                params["material_path"] = material_path

            logger.info(
                "create_landscape: path=%s size=%dx%d components=%dx%d "
                "section_size=%d scale=(%.1f,%.1f,%.1f) label=%s",
                heightmap_path,
                components_x * sections_per_component * section_size + 1,
                components_y * sections_per_component * section_size + 1,
                components_x, components_y,
                section_size,
                scale_x, scale_y, scale_z,
                actor_label,
            )

            response = unreal.send_command("create_landscape", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error creating landscape: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Landscape tools registered successfully")
