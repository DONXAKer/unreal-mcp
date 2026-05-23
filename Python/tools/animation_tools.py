"""
Animation Blueprint Tools for Unreal MCP (Phase 3B — v1.16.0).

7 primitives for authoring UAnimBlueprint assets, state machines, and AnimGraph
asset-player nodes. All wrappers delegate to the C++ FAnimationBPCommands
dispatcher; responses follow the unified MCP Content Pipeline contract
({ ok, status, assetPath, meta } / { ok:false, error:{...} }).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_animation_tools(mcp: FastMCP) -> None:
    """Register Animation Blueprint tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_animation_blueprint(
        ctx: Context[Any, Any, Any],
        name: str,
        skeleton_path: str,
        parent_class_path: str = "/Script/Engine.AnimInstance",
        package_path: str = "/Game/Animations",
        ifExists: str = "skip",
    ) -> dict[str, Any]:
        """
        Create a new Animation Blueprint asset bound to a Skeleton.

        Args:
            name: Asset name (no extension).
            skeleton_path: /Game/... path to a USkeleton (required).
            parent_class_path: Full class path; default /Script/Engine.AnimInstance.
            package_path: Destination /Game/... folder; default /Game/Animations.
            ifExists: "skip" (default) | "overwrite".
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload = {
                "name": name,
                "skeleton_path": skeleton_path,
                "parent_class_path": parent_class_path,
                "package_path": package_path,
                "ifExists": ifExists,
            }
            logger.info(f"create_animation_blueprint params: {payload}")
            response = unreal.send_command("create_animation_blueprint", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("create_animation_blueprint failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def set_anim_skeleton(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        skeleton_path: str,
    ) -> dict[str, Any]:
        """
        Retarget an existing Animation Blueprint to a different Skeleton.

        Args:
            blueprint_name: Short name or /Game/... path of the AnimBP.
            skeleton_path: /Game/... path to the new USkeleton.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload = {"blueprint_name": blueprint_name, "skeleton_path": skeleton_path}
            response = unreal.send_command("set_anim_skeleton", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("set_anim_skeleton failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_state_machine(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        state_machine_name: str,
        node_position: list[float] | None = None,
    ) -> dict[str, Any]:
        """
        Add a new State Machine node to the AnimBP's AnimGraph and rename its
        sub-graph to the requested name.

        Args:
            blueprint_name: Short name or /Game/... path of the AnimBP.
            state_machine_name: Name to assign to the SM sub-graph.
            node_position: Optional [x, y] graph coordinates for the SM node.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "state_machine_name": state_machine_name,
            }
            if node_position is not None:
                payload["node_position"] = node_position
            response = unreal.send_command("add_state_machine", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_state_machine failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_anim_state(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        state_machine_name: str,
        state_name: str,
        animation_asset_path: str | None = None,
        node_position: list[float] | None = None,
    ) -> dict[str, Any]:
        """
        Add a State node to an existing State Machine. If `animation_asset_path`
        is provided, a SequencePlayer for that UAnimSequence is dropped into
        the state's BoundGraph (not auto-wired to the state result pin).

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            state_machine_name: Name of the existing state machine sub-graph.
            state_name: Name of the new state.
            animation_asset_path: Optional /Game/... path to a UAnimSequence.
            node_position: Optional [x, y] graph coordinates inside the SM graph.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "state_machine_name": state_machine_name,
                "state_name": state_name,
            }
            if animation_asset_path:
                payload["animation_asset_path"] = animation_asset_path
            if node_position is not None:
                payload["node_position"] = node_position
            response = unreal.send_command("add_anim_state", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_anim_state failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_anim_transition(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        state_machine_name: str,
        from_state: str,
        to_state: str,
        priority_order: int = 1,
    ) -> dict[str, Any]:
        """
        Add a one-way transition between two existing states.

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            state_machine_name: Name of the existing state machine sub-graph.
            from_state: Source state name.
            to_state: Destination state name.
            priority_order: Smaller = higher priority when multiple transitions
                fire at the same frame. Default 1.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload = {
                "blueprint_name": blueprint_name,
                "state_machine_name": state_machine_name,
                "from_state": from_state,
                "to_state": to_state,
                "priority_order": priority_order,
            }
            response = unreal.send_command("add_anim_transition", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_anim_transition failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_play_anim_node(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        animation_asset_path: str,
        loop: bool = True,
        play_rate: float = 1.0,
        node_position: list[float] | None = None,
    ) -> dict[str, Any]:
        """
        Drop a free-standing SequencePlayer node into the AnimGraph and
        configure its UAnimSequence / loop / play-rate properties.

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            animation_asset_path: /Game/... path to a UAnimSequence.
            loop: Loop the animation. Default true.
            play_rate: Multiplier. Default 1.0.
            node_position: Optional [x, y] coordinates.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "animation_asset_path": animation_asset_path,
                "loop": loop,
                "play_rate": play_rate,
            }
            if node_position is not None:
                payload["node_position"] = node_position
            response = unreal.send_command("add_play_anim_node", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_play_anim_node failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_blend_space_player_node(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        blend_space_path: str,
        node_position: list[float] | None = None,
    ) -> dict[str, Any]:
        """
        Drop a BlendSpacePlayer node into the AnimGraph bound to a UBlendSpace.

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            blend_space_path: /Game/... path to a UBlendSpace.
            node_position: Optional [x, y] coordinates.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "blend_space_path": blend_space_path,
            }
            if node_position is not None:
                payload["node_position"] = node_position
            response = unreal.send_command("add_blend_space_player_node", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_blend_space_player_node failed")
            return {"ok": False, "error": {"message": str(e)}}
