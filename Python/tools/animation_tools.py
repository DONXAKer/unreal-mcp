"""
Animation Blueprint Tools for Unreal MCP (Phase 3B — v1.16.0; MCP-ANIM-001 +
FIX-ANIM-MCP-001 — v3.26.0).

9 primitives for authoring UAnimBlueprint assets, state machines, and AnimGraph
asset-player nodes (includes anim_sequence_import, the subject-first alias for
import_anim_sequence — same C++ handler, both names live forever). All
wrappers delegate to the C++ FAnimationBPCommands dispatcher; responses follow
the unified MCP Content Pipeline contract
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
        state_machine_name: str = None,
        state_name: str = None,
    ) -> dict[str, Any]:
        """
        Drop a SequencePlayer node into an AnimGraph and configure its
        UAnimSequence / loop / play-rate properties.

        With state_name set, the node goes INSIDE that state's BoundGraph and is
        connected to "Output Animation Pose" — that is the only way to make a
        state actually return a pose. Without it, the node lands in the root
        AnimGraph unconnected (composition is up to the caller).

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            animation_asset_path: /Game/... path to a UAnimSequence.
            loop: Loop the animation. Default true.
            play_rate: Multiplier. Default 1.0.
            node_position: Optional [x, y] coordinates.
            state_machine_name: State machine holding the state. Required with state_name.
            state_name: Target state; node is written into its BoundGraph and wired.

        Example:
            add_play_anim_node("ABP_Infantry", "/Game/Anim/A_Walk",
                               state_machine_name="Locomotion", state_name="Walk")
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
            if state_machine_name:
                payload["state_machine_name"] = state_machine_name
            if state_name:
                payload["state_name"] = state_name
            response = unreal.send_command("add_play_anim_node", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_play_anim_node failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def import_anim_sequence(
        ctx: Context[Any, Any, Any],
        asset_path: str,
        source_path: str,
        skeleton_path: str,
        if_exists: str = "skip",
    ) -> dict[str, Any]:
        """
        Import a UAnimSequence from an FBX onto an existing skeleton.

        Goes through the legacy UFbxFactory with Interchange disabled: in UE 5.8
        Interchange re-enters the GameThread TaskGraph and crashes the editor.
        The skeleton is bound before import, so the animation never spawns a
        stray skeleton of its own.

        Args:
            asset_path: /Game/... destination, e.g. "/Game/WarCard/Units/Anim/A_Idle".
            source_path: Absolute path to a .fbx containing an animation stack.
            skeleton_path: /Game/... path to the USkeleton to bind to.
            if_exists: "skip" (default) | "overwrite" | "update" | "fail".

        Example:
            import_anim_sequence("/Game/WarCard/Units/Anim/A_Idle",
                                 r"D:\\Anim\\idle.fbx",
                                 "/Game/WarCard/Units/Skeletal/SK_Rifleman_Skeleton")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "assetPath": asset_path,
                "sourcePath": source_path,
                "skeleton_path": skeleton_path,
                "ifExists": if_exists,
            }
            response = unreal.send_command("import_anim_sequence", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("import_anim_sequence failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def anim_sequence_import(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        sourcePath: str,
        skeletonPath: str,
        ifExists: str = "skip",
    ) -> dict[str, Any]:
        """
        Import a UAnimSequence from an FBX onto an existing skeleton.

        Subject-first alias for import_anim_sequence (unreal-mcp/CLAUDE.md naming
        convention, v1.17.0+: new commands use <domain>_<verb>). Same C++ handler,
        same behavior — only the tool name and Python parameter casing differ
        (camelCase here, matching the sibling import tools in mesh_tools.py).
        import_anim_sequence is NOT going away; both names are supported forever.

        Goes through the legacy UFbxFactory with Interchange disabled: in UE 5.8
        Interchange re-enters the GameThread TaskGraph and crashes the editor.
        The skeleton is bound before import, so the animation never spawns a
        stray skeleton of its own. A failed rename or an animation with the wrong
        skeleton / zero sampled keys now returns a real failure code instead of
        ok:true with a meta flag nobody is required to check (FIX-ANIM-MCP-001).

        Args:
            assetPath: /Game/... destination, e.g. "/Game/WarCard/Units/Anim/A_Idle".
            sourcePath: Absolute path to a .fbx containing an animation stack.
            skeletonPath: /Game/... path to the USkeleton to bind to.
            ifExists: "skip" (default) | "overwrite" | "update" | "fail".

        Example:
            anim_sequence_import("/Game/WarCard/Units/Anim/A_Idle",
                                 r"D:\\Anim\\idle.fbx",
                                 "/Game/WarCard/Units/Skeletal/SK_Rifleman_Skeleton")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"ok": False, "error": {"message": "Failed to connect to Unreal Engine"}}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "sourcePath": sourcePath,
                # C++ still reads "skeleton_path" — only the Python-side parameter
                # name is camelCase here, the wire key stays as the handler expects.
                "skeleton_path": skeletonPath,
                "ifExists": ifExists,
            }
            response = unreal.send_command("anim_sequence_import", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("anim_sequence_import failed")
            return {"ok": False, "error": {"message": str(e)}}

    @mcp.tool()
    def add_blend_space_player_node(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        blend_space_path: str,
        node_position: list[float] | None = None,
        state_machine_name: str = None,
        state_name: str = None,
    ) -> dict[str, Any]:
        """
        Drop a BlendSpacePlayer node into an AnimGraph and bind it to a UBlendSpace.

        With state_name set, the node goes INSIDE that state's BoundGraph and is
        connected to "Output Animation Pose" — mirrors add_play_anim_node's
        state-targeting behavior. Without it, the node lands in the root AnimGraph
        unconnected (composition is up to the caller).

        Args:
            blueprint_name: AnimBP short name or /Game/... path.
            blend_space_path: /Game/... path to a UBlendSpace.
            node_position: Optional [x, y] coordinates.
            state_machine_name: State machine holding the state. Required with state_name.
            state_name: Target state; node is written into its BoundGraph and wired.

        Example:
            add_blend_space_player_node("ABP_Infantry", "/Game/Anim/BS_Locomotion",
                                         state_machine_name="Locomotion", state_name="Walk")
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
            if state_machine_name:
                payload["state_machine_name"] = state_machine_name
            if state_name:
                payload["state_name"] = state_name
            response = unreal.send_command("add_blend_space_player_node", payload)
            return response or {"ok": False, "error": {"message": "No response from Unreal"}}
        except Exception as e:
            logger.exception("add_blend_space_player_node failed")
            return {"ok": False, "error": {"message": str(e)}}
