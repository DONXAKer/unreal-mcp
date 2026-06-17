"""
Material Tools for Unreal MCP.

Wrappers for material primitive commands: create_material_instance,
set_material_instance_params, set_mesh_material_color, get_available_materials,
apply_material_to_{actor,blueprint}, get_{actor,blueprint}_material_info.

Introduced: v1.17.0 (Phase 5 — close the bridge↔FastMCP wrapper gap).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP) -> None:
    """Register Material tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_material_instance(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        parentMaterial: str,
        ifExists: str = None,
        params: dict[str, Any] = None,
    ) -> dict[str, Any]:
        """
        Create a UMaterialInstanceConstant from a parent UMaterial(Interface).

        Args:
            assetPath: Full /Game/... path for the new MI asset.
            parentMaterial: /Game/... or /Engine/... path of the parent material.
            ifExists: Idempotency mode ("skip" | "fail" | "overwrite" | "update").
                In "update" mode, params are applied to an existing MI without
                recreating it.
            params: Optional dict of parameter name → value. Scalars/floats,
                booleans (StaticSwitch), vectors ([R,G,B,A] or "R,G,B,A"), and
                texture paths are auto-detected by the C++ side.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "parentMaterial": parentMaterial,
            }
            if ifExists is not None:
                payload["ifExists"] = ifExists
            if params is not None:
                payload["params"] = params
            logger.info(f"Creating material instance: {payload}")
            response = unreal.send_command("create_material_instance", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error creating material instance: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_material_instance_params(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        params: dict[str, Any],
        ifMissing: str = "fail",
    ) -> dict[str, Any]:
        """
        Apply parameter overrides to an existing UMaterialInstanceConstant.

        Args:
            assetPath: Full /Game/... path of the MI asset.
            params: Dict of parameter name → value.
            ifMissing: "fail" (default) or "skip" when the MI doesn't exist.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload = {
                "assetPath": assetPath,
                "params": params,
                "ifMissing": ifMissing,
            }
            logger.info(f"Setting material instance params: assetPath={assetPath}")
            response = unreal.send_command("set_material_instance_params", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting material instance params: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_mesh_material_color(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        component_name: str,
        color: list[float],
        material_slot: int = 0,
        parameter_name: str = "BaseColor",
        material_path: str = None,
    ) -> dict[str, Any]:
        """
        Create a dynamic material instance on a primitive component and set a
        vector parameter (default "BaseColor") to `color`.

        Args:
            blueprint_name: Target Blueprint containing the primitive component.
            component_name: SCS component variable name (UStaticMesh-/UPrimitive-).
            color: [R, G, B, A], each in [0..1].
            material_slot: Material slot index (default 0).
            parameter_name: Material vector parameter (default "BaseColor").
            material_path: Optional base material asset path. When omitted, reuses
                the component's currently assigned material or BasicShapeMaterial.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            if not isinstance(color, list) or len(color) != 4:
                return {"success": False, "message": "color must be a 4-element list [R, G, B, A]"}
            params: dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "color": [float(v) for v in color],
                "material_slot": int(material_slot),
                "parameter_name": parameter_name,
            }
            if material_path is not None:
                params["material_path"] = material_path
            logger.info(f"Setting mesh material color: {params}")
            response = unreal.send_command("set_mesh_material_color", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting mesh material color: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_available_materials(
        ctx: Context[Any, Any, Any],
        search_path: str = "",
        include_engine_materials: bool = True,
    ) -> dict[str, Any]:
        """
        Asset-registry query for materials.

        Args:
            search_path: /Game/... subpath to constrain search; empty means whole /Game/.
            include_engine_materials: Also include /Engine/ materials.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "search_path": search_path,
                "include_engine_materials": include_engine_materials,
            }
            logger.info(f"Listing materials: {params}")
            response = unreal.send_command("get_available_materials", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error listing materials: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def apply_material_to_actor(
        ctx: Context[Any, Any, Any],
        actor_name: str,
        material_path: str,
        material_slot: int = 0,
    ) -> dict[str, Any]:
        """
        Set a material on every UStaticMeshComponent of an actor in the editor world.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "actor_name": actor_name,
                "material_path": material_path,
                "material_slot": int(material_slot),
            }
            logger.info(f"Applying material to actor: {params}")
            response = unreal.send_command("apply_material_to_actor", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error applying material to actor: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def apply_material_to_blueprint(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        component_name: str,
        material_path: str,
        material_slot: int = 0,
    ) -> dict[str, Any]:
        """
        Assign a material to a primitive component template inside a Blueprint
        (so spawned instances inherit it).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "material_path": material_path,
                "material_slot": int(material_slot),
            }
            logger.info(f"Applying material to blueprint: {params}")
            response = unreal.send_command("apply_material_to_blueprint", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error applying material to blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_actor_material_info(
        ctx: Context[Any, Any, Any],
        actor_name: str,
    ) -> dict[str, Any]:
        """
        Read-only: enumerate material slots across all UStaticMeshComponents
        on an actor in the editor world.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"actor_name": actor_name}
            logger.info(f"Reading actor material info: {actor_name}")
            response = unreal.send_command("get_actor_material_info", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading actor material info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_material_info(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        component_name: str,
    ) -> dict[str, Any]:
        """
        Read-only: enumerate material slots on a static-mesh component template
        inside a Blueprint.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
            }
            logger.info(f"Reading blueprint material info: {params}")
            response = unreal.send_command("get_blueprint_material_info", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading blueprint material info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ── Master Material graph commands (FEAT-MAT-001 / v3.8.0) ──────────────

    @mcp.tool()
    def material_create(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        domain: str = "Surface",
        ifExists: str = "fail",
    ) -> dict[str, Any]:
        """
        Create a new master UMaterial asset.

        Args:
            assetPath: Full /Game/... path for the new UMaterial asset.
            domain: Material domain — "Surface" (default) or "UserInterface".
            ifExists: Idempotency mode — "skip" | "overwrite" | "fail" (default).

        Example:
            material_create(assetPath="/Game/Materials/M_Panel", domain="Surface")
            material_create(assetPath="/Game/UI/M_HUD", domain="UserInterface", ifExists="skip")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "domain": domain,
                "ifExists": ifExists,
            }
            logger.info(f"Creating master material: {payload}")
            response = unreal.send_command("material_create", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error creating master material: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def material_add_node(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        nodeType: str,
        nodeId: str,
        posX: int = 0,
        posY: int = 0,
    ) -> dict[str, Any]:
        """
        Add an expression node to a master material graph.

        Args:
            assetPath: Full /Game/... path to the UMaterial asset.
            nodeType: Node type — one of: Constant | Constant3Vector | ScalarParameter |
                VectorParameter | TextureSample | TextureSampleParameter2D |
                Lerp | Multiply | Add | Panner | Noise.
            nodeId: Unique string ID stored in the node Desc field for later reference.
            posX: X position in the graph editor (default 0).
            posY: Y position in the graph editor (default 0).

        Example:
            material_add_node(assetPath="/Game/Materials/M_Panel", nodeType="VectorParameter",
                              nodeId="color_param", posX=-200, posY=0)
            material_add_node(assetPath="/Game/Materials/M_Panel", nodeType="Lerp",
                              nodeId="lerp_blend", posX=0, posY=100)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "nodeType": nodeType,
                "nodeId": nodeId,
                "posX": posX,
                "posY": posY,
            }
            logger.info(f"Adding material node: {payload}")
            response = unreal.send_command("material_add_node", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error adding material node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def material_connect(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        fromNodeId: str,
        fromOutput: str = "",
        toNodeId: str = None,
        toInput: str = "BaseColor",
    ) -> dict[str, Any]:
        """
        Connect a material expression output to another expression input or root material property.

        Args:
            assetPath: Full /Game/... path to the UMaterial asset.
            fromNodeId: Desc (nodeId) of the source expression.
            fromOutput: Output pin name on the source node; empty string = first output.
            toNodeId: Desc (nodeId) of the target expression. If None, connects to the
                root material property specified by toInput.
            toInput: When toNodeId is None — root material property name:
                BaseColor | EmissiveColor | Opacity | Roughness | Metallic | Normal.
                When toNodeId is set — input pin name on the target expression.

        Example:
            material_connect(assetPath="/Game/Materials/M_Panel",
                             fromNodeId="color_param", toInput="BaseColor")
            material_connect(assetPath="/Game/Materials/M_Panel",
                             fromNodeId="color_param", toNodeId="lerp_blend", toInput="A")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "fromNodeId": fromNodeId,
                "fromOutput": fromOutput,
                "toInput": toInput,
            }
            if toNodeId is not None:
                payload["toNodeId"] = toNodeId
            logger.info(f"Connecting material nodes: {payload}")
            response = unreal.send_command("material_connect", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error connecting material nodes: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def material_set_node_param(
        ctx: Context[Any, Any, Any],
        assetPath: str,
        nodeId: str,
        params: dict[str, Any],
    ) -> dict[str, Any]:
        """
        Set parameters on a material expression node identified by its nodeId (Desc).

        Supported params by node type:
          - Constant:                R (float)
          - Constant3Vector:         Constant ([r, g, b, a])
          - ScalarParameter:         ParameterName (str), DefaultValue (float)
          - VectorParameter:         ParameterName (str), DefaultValue ([r, g, b, a])
          - TextureSample:           Texture (str, /Game/... asset path)
          - TextureSampleParameter2D: Texture (str), ParameterName (str)
          - Noise:                   Scale (float), Levels (int)

        Args:
            assetPath: Full /Game/... path to the UMaterial asset.
            nodeId: Desc string of the target expression node.
            params: Dict of parameter name → value.

        Example:
            material_set_node_param(
                assetPath="/Game/Materials/M_Panel",
                nodeId="color_param",
                params={"ParameterName": "PanelColor", "DefaultValue": [0.1, 0.1, 0.15, 1.0]})
            material_set_node_param(
                assetPath="/Game/Materials/M_Panel",
                nodeId="base_const",
                params={"R": 0.5})
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            payload: dict[str, Any] = {
                "assetPath": assetPath,
                "nodeId": nodeId,
                "params": params,
            }
            logger.info(f"Setting material node params: assetPath={assetPath}, nodeId={nodeId}")
            response = unreal.send_command("material_set_node_param", payload)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting material node params: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Material tools registered successfully")
