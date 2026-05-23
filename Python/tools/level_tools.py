"""
Level Tools for Unreal MCP.

Wrappers for /Script/UnrealMCP level (.umap) commands: create, load, save,
spawn/remove/transform/list actors in arbitrary levels.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_level_tools(mcp: FastMCP):
    """Register Level tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_level(
        ctx: Context,
        destMapPath: str,
        template: str = "Empty",
        ifExists: str = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {
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
        ctx: Context,
        mapPath: str,
    ) -> Dict[str, Any]:
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
        ctx: Context,
        mapPath: str = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {}
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
        ctx: Context,
        actorClass: str,
        mapPath: str = None,
        name: str = None,
        ifExists: str = None,
        transform: Dict[str, List[float]] = None,
        properties: Dict[str, Any] = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {"actorClass": actorClass}
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
        ctx: Context,
        actorName: str,
        mapPath: str = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {"actorName": actorName}
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
        ctx: Context,
        actorName: str,
        transform: Dict[str, List[float]],
        mapPath: str = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {
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
        ctx: Context,
        mapPath: str = None,
        classFilter: str = None,
    ) -> Dict[str, Any]:
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
            params: Dict[str, Any] = {}
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
