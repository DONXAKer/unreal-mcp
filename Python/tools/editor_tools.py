"""
Editor Tools for Unreal MCP.

This module provides tools for controlling the Unreal Editor viewport and other editor functionality.
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_editor_tools(mcp: FastMCP) -> None:
    """Register editor tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def get_actors_in_level(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """Get a list of all actors in the current level.

        Returns a dict envelope carrying meta.actors (list) and meta.count. The
        @mcp.tool is wrapped by wrap_with_envelope, whose _normalize_outbound
        ONLY accepts dict returns — a bare list is rejected as
        INVALID_RESPONSE_TYPE ("Tool returned non-dict (list)"), которое к тому
        же ломает output-схему (annotation list). Поэтому список актёров
        ВСЕГДА несётся внутри dict, не возвращается голым списком.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.warning("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_actors_in_level", {})

            if not response:
                logger.warning("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            # Log the complete response for debugging
            logger.info(f"Complete response from Unreal: {response}")

            # Check response format
            if "result" in response and "actors" in response["result"]:
                actors: list[dict[str, Any]] = response["result"]["actors"]
            elif "actors" in response:
                actors = response["actors"]
            else:
                logger.warning(f"Unexpected response format: {response}")
                return {"success": False, "message": "Unexpected response format from Unreal", "actors": [], "count": 0}

            logger.info(f"Found {len(actors)} actors in level")
            return {"success": True, "actors": actors, "count": len(actors)}

        except Exception as e:
            logger.error(f"Error getting actors: {e}")
            return {"success": False, "message": f"Error getting actors: {e}"}

    @mcp.tool()
    def find_actors_by_name(ctx: Context[Any, Any, Any], pattern: str) -> dict[str, Any]:
        """Find actors by name pattern. Returns meta.actors (list of names).

        Несёт результат внутри dict — голый список ломает wrap_with_envelope
        (см. get_actors_in_level).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.warning("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("find_actors_by_name", {
                "pattern": pattern
            })

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            result: list[str] = response.get("actors", [])
            if not result and isinstance(response.get("result"), dict):
                result = response["result"].get("actors", [])
            return {"success": True, "actors": result, "count": len(result)}

        except Exception as e:
            logger.error(f"Error finding actors: {e}")
            return {"success": False, "message": f"Error finding actors: {e}"}
    
    @mcp.tool()
    def spawn_actor(
        ctx: Context[Any, Any, Any],
        name: str,
        type: str,
        location: list[float] = None,
        rotation: list[float] = None,
    ) -> dict[str, Any]:
        """Create a new actor in the current level.
        
        Args:
            ctx: The MCP context
            name: The name to give the new actor (must be unique)
            type: The type of actor to create (e.g. StaticMeshActor, PointLight)
            location: The [x, y, z] world location to spawn at
            rotation: The [pitch, yaw, roll] rotation in degrees
            
        Returns:
            Dict containing the created actor's properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Ensure all parameters are properly formatted
            params = {
                "name": name,
                "type": type.upper(),  # Make sure type is uppercase
                "location": location if location is not None else [0.0, 0.0, 0.0],
                "rotation": rotation if rotation is not None else [0.0, 0.0, 0.0],
            }

            # Validate location and rotation formats
            for param_name in ["location", "rotation"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            logger.info(f"Creating actor '{name}' of type '{type}' with params: {params}")
            response = unreal.send_command("spawn_actor", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            # Log the complete response for debugging
            logger.info(f"Actor creation response: {response}")
            
            # Handle error responses correctly
            if response.get("status") == "error":
                # FIX-MCP-001: response["error"] can be the bridge's structured
                # {category, code, message, details} object now, not just a
                # string — extract .message so "message" below stays a string
                # instead of silently becoming a dict that str()-collapses
                # wherever a caller later interpolates it.
                error_field = response.get("error", "Unknown error")
                error_message = (
                    error_field.get("message", str(error_field))
                    if isinstance(error_field, dict)
                    else error_field
                )
                logger.error(f"Error creating actor: {error_message}")
                return {"success": False, "message": error_message}
            
            return response
            
        except Exception as e:
            error_msg = f"Error creating actor: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def delete_actor(ctx: Context[Any, Any, Any], name: str) -> dict[str, Any]:
        """Delete an actor by name."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("delete_actor", {
                "name": name
            })
            return response or {}
            
        except Exception as e:
            logger.error(f"Error deleting actor: {e}")
            return {}
    
    @mcp.tool()
    def set_actor_transform(
        ctx: Context[Any, Any, Any],
        name: str,
        location: list[float]  = None,
        rotation: list[float]  = None,
        scale: list[float] = None
    ) -> dict[str, Any]:
        """Set the transform of an actor."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            params: dict[str, Any] = {"name": name}
            if location is not None:
                params["location"] = location
            if rotation is not None:
                params["rotation"] = rotation
            if scale is not None:
                params["scale"] = scale

            response = unreal.send_command("set_actor_transform", params)
            return response or {}
            
        except Exception as e:
            logger.error(f"Error setting transform: {e}")
            return {}
    
    @mcp.tool()
    def get_actor_properties(ctx: Context[Any, Any, Any], name: str) -> dict[str, Any]:
        """Get all properties of an actor."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("get_actor_properties", {
                "name": name
            })
            return response or {}
            
        except Exception as e:
            logger.error(f"Error getting properties: {e}")
            return {}

    @mcp.tool()
    def set_actor_property(
        ctx: Context[Any, Any, Any],
        name: str,
        property_name: str,
        property_value: Any,
    ) -> dict[str, Any]:
        """
        Set a property on an actor.
        
        Args:
            name: Name of the actor
            property_name: Name of the property to set
            property_value: Value to set the property to
            
        Returns:
            Dict containing response from Unreal with operation status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("set_actor_property", {
                "name": name,
                "property_name": property_name,
                "property_value": property_value
            })
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set actor property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting actor property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # @mcp.tool() commented out because it's buggy
    def focus_viewport(
        ctx: Context[Any, Any, Any],
        target: str = None,
        location: list[float] = None,
        distance: float = 1000.0,
        orientation: list[float] = None
    ) -> dict[str, Any]:
        """
        Focus the viewport on a specific actor or location.
        
        Args:
            target: Name of the actor to focus on (if provided, location is ignored)
            location: [X, Y, Z] coordinates to focus on (used if target is None)
            distance: Distance from the target/location
            orientation: Optional [Pitch, Yaw, Roll] for the viewport camera
            
        Returns:
            Response from Unreal Engine
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            params: dict[str, Any] = {}
            if target:
                params["target"] = target
            elif location:
                params["location"] = location

            if distance:
                params["distance"] = distance

            if orientation:
                params["orientation"] = orientation

            response = unreal.send_command("focus_viewport", params)
            return response or {}
            
        except Exception as e:
            logger.error(f"Error focusing viewport: {e}")
            return {"status": "error", "message": str(e)}

    @mcp.tool()
    def take_screenshot(
        ctx: Context[Any, Any, Any],
        filename: str = "MCPScreenshot.png",
        show_ui: bool = False,
        resolution_x: int = 0,
        resolution_y: int = 0,
    ) -> dict[str, Any]:
        """Queue a viewport screenshot via FScreenshotRequest.

        Args:
            filename: relative PNG name; the engine writes to
                <ProjectSaved>/Screenshots/<filename>. ".png" is appended
                automatically when missing.
            show_ui: include UI overlays in the capture (default False).
            resolution_x: optional hi-res capture width. When both
                resolution_x and resolution_y are >0 the engine takes a
                hi-res shot via FHighResScreenshotConfig.
            resolution_y: optional hi-res capture height.

        The C++ side queues the screenshot asynchronously; the response
        returns the planned filesystem path before the file actually
        appears on disk (usually within 1-2 ticks).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "filename": filename,
                "show_ui": show_ui,
            }
            if resolution_x > 0 and resolution_y > 0:
                params["resolution_x"] = resolution_x
                params["resolution_y"] = resolution_y
            response = unreal.send_command("take_screenshot", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error taking screenshot: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def spawn_blueprint_actor(
        ctx: Context[Any, Any, Any],
        blueprint_name: str,
        actor_name: str,
        location: list[float] = None,
        rotation: list[float] = None,
    ) -> dict[str, Any]:
        """Spawn an actor from a Blueprint.
        
        Args:
            ctx: The MCP context
            blueprint_name: Name of the Blueprint to spawn from
            actor_name: Name to give the spawned actor
            location: The [x, y, z] world location to spawn at
            rotation: The [pitch, yaw, roll] rotation in degrees
            
        Returns:
            Dict containing the spawned actor's properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Ensure all parameters are properly formatted
            params = {
                "blueprint_name": blueprint_name,
                "actor_name": actor_name,
                "location": location or [0.0, 0.0, 0.0],
                "rotation": rotation or [0.0, 0.0, 0.0]
            }
            
            # Validate location and rotation formats
            for param_name in ["location", "rotation"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            logger.info(f"Spawning blueprint actor with params: {params}")
            response = unreal.send_command("spawn_blueprint_actor", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Spawn blueprint actor response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error spawning blueprint actor: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Connectivity probe (v1.17.0)
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def ping(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """
        Lightweight connectivity probe — round-trips a `ping` command to the
        Unreal Editor bridge. Returns the raw {"status": "ok", ...} response.
        Use this to verify the editor is listening on port 55557 before issuing
        heavier commands.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("ping", {})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error pinging Unreal Engine: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Lookdev / deterministic framing (v3.14.0)
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def viewport_set_camera(
        ctx: Context[Any, Any, Any],
        location: list[float] = None,
        rotation: list[float] = None,
        fov: float = 0.0,
    ) -> dict[str, Any]:
        """Set the perspective level viewport camera to an exact pose.

        Unlike `focus_viewport` (which frames a target and is therefore not
        reproducible run-to-run), this sets absolute camera state — the
        precondition for comparing two frames of the same shot.

        Args:
            location: world position [x, y, z] in UU. Omit to keep current.
            rotation: [pitch, yaw, roll] in degrees. Omit to keep current.
            fov: horizontal field of view in degrees. <=0 keeps current.

        Example:
            viewport_set_camera(location=[0, -2500, 2100], rotation=[-48, 90, 0], fov=60)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {}
            if location is not None:
                params["location"] = location
            if rotation is not None:
                params["rotation"] = rotation
            if fov and fov > 0:
                params["fov"] = fov
            response = unreal.send_command("viewport_set_camera", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting viewport camera: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def viewport_get_camera(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """Read the perspective level viewport camera pose.

        Returns {"location": [x,y,z], "rotation": [pitch,yaw,roll], "fov": float}.
        Use it to capture a pose you framed by hand and paste it into a shot
        definition.

        Example:
            viewport_get_camera()
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("viewport_get_camera", {})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading viewport camera: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def viewport_set_view_mode(
        ctx: Context[Any, Any, Any],
        mode: str = "lit",
    ) -> dict[str, Any]:
        """Set the level viewport view mode, including the Path Tracer.

        Args:
            mode: one of "lit", "unlit", "wireframe", "detaillighting",
                "lightingonly", "shadercomplexity", "pathtracing".

        Path tracing requires ray tracing enabled in project settings and needs
        a few seconds to accumulate samples before a clean capture. Capture the
        resulting frame with `take_screenshot` (it already supports hi-res via
        resolution_x/resolution_y).

        Example:
            viewport_set_view_mode(mode="pathtracing")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("viewport_set_view_mode", {"mode": mode})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting view mode: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def viewport_capture(
        ctx: Context[Any, Any, Any],
        filename: str = "MCPViewport.png",
        game_view: bool = False,
    ) -> dict[str, Any]:
        """Capture the level viewport synchronously to a PNG.

        Use this instead of `take_screenshot` when the editor is NOT in PIE.
        `take_screenshot` goes through FScreenshotRequest, which is serviced by
        the game viewport — in a plain editor session it never fires and no file
        is ever written. This command draws the level viewport and reads its
        pixels immediately, so the file exists by the time the call returns and
        the caller does not have to poll.

        Args:
            filename: PNG name written under <ProjectSaved>/Screenshots/.
            game_view: If True, temporarily enable FLevelEditorViewportClient
                game view (hides editor gizmos/actor icons) for the capture,
                then restore the previous mode. Needed for TOOL-SHOT-001 lookdev.

        The captured size follows the viewport, so it is not settable here — for
        an exact output size use PIE plus `take_screenshot` with
        resolution_x/resolution_y.

        Example:
            viewport_set_camera(location=[0, -2500, 2100], rotation=[-48, 90, 0])
            viewport_capture(filename="board_overview.png", game_view=True)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "viewport_capture",
                {"filename": filename, "game_view": game_view},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error capturing viewport: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def postprocess_get_params(
        ctx: Context[Any, Any, Any],
        actor_name: str = "",
    ) -> dict[str, Any]:
        """Read the overridden FPostProcessSettings of a PostProcessVolume.

        Only OVERRIDDEN parameters are returned — a full dump of
        FPostProcessSettings is hundreds of fields, of which a handful actually
        affect the frame.

        Args:
            actor_name: name of the PostProcessVolume actor in the level.

        Example:
            postprocess_get_params(actor_name="PP_GameMap_Military")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("postprocess_get_params", {"actor_name": actor_name})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading post process params: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def postprocess_set_params(
        ctx: Context[Any, Any, Any],
        actor_name: str = "",
        settings: dict = None,
        unbound: bool = None,
        priority: float = None,
        blend_weight: float = None,
    ) -> dict[str, Any]:
        """Write FPostProcessSettings on a PostProcessVolume.

        For every key written, the paired `bOverride_<Key>` flag is set to true.
        Without that flag the engine silently ignores the value — which is why
        generic `set_actor_property` appears to succeed while the frame does not
        change.

        Args:
            actor_name: name of the PostProcessVolume actor in the level.
            settings: {PropertyName: value}. Numbers for scalars; arrays of 3-4
                numbers for FVector / FVector4 / FLinearColor properties.
            unbound: optional — set the volume to infinite extent.
            priority: optional volume priority.
            blend_weight: optional blend weight.

        Returns applied[] and failed[] so a partial write is visible.
        The level is left dirty — call `save_level` to persist.

        Example:
            postprocess_set_params(
                actor_name="PP_GameMap_Military",
                settings={"ColorSaturation": [0.8, 0.8, 0.8, 1.0], "BloomIntensity": 0.35},
            )
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "actor_name": actor_name,
                "settings": settings if settings is not None else {},
            }
            if unbound is not None:
                params["unbound"] = unbound
            if priority is not None:
                params["priority"] = priority
            if blend_weight is not None:
                params["blend_weight"] = blend_weight
            response = unreal.send_command("postprocess_set_params", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error writing post process params: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def actor_get_component_properties(
        ctx: Context[Any, Any, Any],
        actor_name: str,
        component_name: str = "",
        filter: str = "",
    ) -> dict[str, Any]:
        """Read properties of a component on a level actor.

        Without `component_name` the components are ENUMERATED instead (name +
        class), so the caller can discover the right name rather than guess it —
        e.g. a DirectionalLight actor keeps its settings on "LightComponent0",
        not on the actor itself.

        Args:
            actor_name: name of the actor in the level.
            component_name: component to inspect. Empty = list components.
            filter: optional substring filter on property names. Strongly
                recommended — UDirectionalLightComponent has hundreds of fields.

        Example:
            actor_get_component_properties(actor_name="SunLight")
            actor_get_component_properties(
                actor_name="SunLight", component_name="LightComponent0",
                filter="Intensity")
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {"actor_name": actor_name}
            if component_name:
                params["component_name"] = component_name
            if filter:
                params["filter"] = filter
            response = unreal.send_command("actor_get_component_properties", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error reading component properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def actor_set_component_property(
        ctx: Context[Any, Any, Any],
        actor_name: str,
        component_name: str,
        property_name: str,
        property_value: Any,
    ) -> dict[str, Any]:
        """Write a property on a component of a level actor.

        Use this — not `set_actor_property` — for lighting, fog and atmosphere:
        those settings live on the COMPONENT, and the command issues
        PostEditChangeProperty + MarkRenderStateDirty so the frame actually
        updates instead of waiting for a random invalidation.

        Args:
            actor_name: name of the actor in the level.
            component_name: component that owns the property (empty = root).
            property_name: reflected property name (exact case).
            property_value: number, bool, string, or array of numbers for
                FVector / FRotator / FLinearColor properties.

        Example:
            actor_set_component_property(
                actor_name="SunLight", component_name="LightComponent0",
                property_name="Intensity", property_value=8.0)
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: dict[str, Any] = {
                "actor_name": actor_name,
                "component_name": component_name,
                "property_name": property_name,
                "property_value": property_value,
            }
            response = unreal.send_command("actor_set_component_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error writing component property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Editor tools registered successfully")
