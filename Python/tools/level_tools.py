"""
Level Tools for Unreal MCP.

Wrappers for /Script/UnrealMCP level (.umap) commands: create, load, save,
spawn/remove/transform/list actors in arbitrary levels.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_level_tools(mcp: FastMCP) -> None:
    """Register Level tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_level(
        ctx: Context[Any, Any, Any],
        destMapPath: str,
        template: str = "Empty",
        ifExists: str = None,
    ) -> dict[str, Any]:
        """
        Create a new .umap asset.

        Args:
            destMapPath: Full /Game/... path for the new map (must start /Game/).
            template: "Empty" (default) or "Default" (adds DirectionalLight,
                SkyLight, PlayerStart).
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite" | "update").
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "destMapPath": destMapPath,
                "template": template,
            }
            if ifExists is not None:
                params["ifExists"] = ifExists
            logger.info(f"Creating level: {params}")
            response = unreal.send_command("create_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error creating level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def load_level(
        ctx: Context[Any, Any, Any],
        mapPath: str,
    ) -> dict[str, Any]:
        """
        Load a .umap in the Editor world context.

        Args:
            mapPath: Full /Game/... path of the level to load.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"mapPath": mapPath}
            logger.info(f"Loading level: {mapPath}")
            response = unreal.send_command("load_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error loading level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def save_level(
        ctx: Context[Any, Any, Any],
        mapPath: str = None,
    ) -> dict[str, Any]:
        """
        Save a .umap.

        Args:
            mapPath: Optional /Game/... path. When omitted, saves the currently
                loaded editor map.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {}
            if mapPath is not None:
                params["mapPath"] = mapPath
            logger.info(f"Saving level: {params}")
            response = unreal.send_command("save_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error saving level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def spawn_actor_in_level(
        ctx: Context[Any, Any, Any],
        actorClass: str,
        mapPath: str = None,
        name: str = None,
        ifExists: str = None,
        transform: dict[str, list[float]] = None,
        properties: dict[str, Any] = None,
    ) -> dict[str, Any]:
        """
        Spawn an actor into a specific level (currently loaded, or one identified
        by mapPath).

        Args:
            actorClass: Full class path (e.g. "/Script/Engine.StaticMeshActor",
                "/Game/Blueprints/BP_MyActor.BP_MyActor_C").
            mapPath: Optional target level (loads/saves if not currently active).
            name: Optional desired actor label / object name.
            ifExists: Idempotency for the actor name within the level.
            transform: Optional {"loc": [x,y,z], "rot": [p,y,r], "scale": [sx,sy,sz]}.
            properties: Optional dict of property names → values applied to the
                newly spawned actor.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {"actorClass": actorClass}
            if mapPath is not None:
                params["mapPath"] = mapPath
            if name is not None:
                params["name"] = name
            if ifExists is not None:
                params["ifExists"] = ifExists
            if transform is not None:
                params["transform"] = transform
            if properties is not None:
                params["properties"] = properties
            logger.info(f"Spawning actor in level: {params}")
            response = unreal.send_command("spawn_actor_in_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error spawning actor in level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def remove_actor_from_level(
        ctx: Context[Any, Any, Any],
        actorName: str,
        mapPath: str = None,
    ) -> dict[str, Any]:
        """
        Destroy/remove an actor from a level by name.

        Args:
            actorName: Object/label name of the actor in the level.
            mapPath: Optional level identifier. When omitted, operates on the
                currently loaded editor map.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {"actorName": actorName}
            if mapPath is not None:
                params["mapPath"] = mapPath
            logger.info(f"Removing actor from level: {params}")
            response = unreal.send_command("remove_actor_from_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error removing actor from level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_actor_transform_in_level(
        ctx: Context[Any, Any, Any],
        actorName: str,
        transform: dict[str, list[float]],
        mapPath: str = None,
    ) -> dict[str, Any]:
        """
        Apply a relative/absolute transform to a level actor.

        Args:
            actorName: Target actor object name.
            transform: {"loc": [x,y,z], "rot": [p,y,r], "scale": [sx,sy,sz]} —
                missing keys leave that component of the transform unchanged.
            mapPath: Optional level identifier.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "actorName": actorName,
                "transform": transform,
            }
            if mapPath is not None:
                params["mapPath"] = mapPath
            logger.info(f"Setting actor transform in level: {params}")
            response = unreal.send_command("set_actor_transform_in_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting actor transform in level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_actors_in_level(
        ctx: Context[Any, Any, Any],
        mapPath: str = None,
        classFilter: str = None,
    ) -> dict[str, Any]:
        """
        Read-only listing of actors in a level.

        Args:
            mapPath: Optional /Game/... level path. When omitted, lists actors
                in the currently loaded editor map.
            classFilter: Optional class name to filter by (matches against
                actor's class GetName()).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {}
            if mapPath is not None:
                params["mapPath"] = mapPath
            if classFilter is not None:
                params["classFilter"] = classFilter
            logger.info(f"Listing actors in level: {params}")
            response = unreal.send_command("list_actors_in_level", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error listing actors in level: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Level tools registered successfully")
