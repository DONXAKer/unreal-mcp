"""
UMG Tools for Unreal MCP.

This module provides tools for creating and manipulating UMG Widget Blueprints in Unreal Engine.
"""

import logging
from typing import Dict, List, Any
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_umg_tools(mcp: FastMCP):
    """Register UMG tools with the MCP server."""

    @mcp.tool()
    def create_umg_widget_blueprint(
        ctx: Context,
        widget_name: str,
        parent_class: str = "UserWidget",
        path: str = "/Game/UI"
    ) -> Dict[str, Any]:
        """
        Create a new UMG Widget Blueprint.
        
        Args:
            widget_name: Name of the widget blueprint to create
            parent_class: Parent class for the widget (default: UserWidget)
            path: Content browser path where the widget should be created
            
        Returns:
            Dict containing success status and widget path
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "widget_name": widget_name,
                "parent_class": parent_class,
                "path": path
            }
            
            logger.info(f"Creating UMG Widget Blueprint with params: {params}")
            response = unreal.send_command("create_umg_widget_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Create UMG Widget Blueprint response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating UMG Widget Blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_text_block_to_widget(
        ctx: Context,
        widget_name: str,
        text_block_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0]
    ) -> Dict[str, Any]:
        """
        Add a Text Block widget to a UMG Widget Blueprint.
        
        Args:
            widget_name: Name of the target Widget Blueprint
            text_block_name: Name to give the new Text Block
            text: Initial text content
            position: [X, Y] position in the canvas panel
            size: [Width, Height] of the text block
            font_size: Font size in points
            color: [R, G, B, A] color values (0.0 to 1.0)
            
        Returns:
            Dict containing success status and text block properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "widget_name": widget_name,
                "text_block_name": text_block_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color
            }
            
            logger.info(f"Adding Text Block to widget with params: {params}")
            response = unreal.send_command("add_text_block_to_widget", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add Text Block response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding Text Block to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_button_to_widget(
        ctx: Context,
        widget_name: str,
        button_name: str,
        text: str = "",
        position: List[float] = [0.0, 0.0],
        size: List[float] = [200.0, 50.0],
        font_size: int = 12,
        color: List[float] = [1.0, 1.0, 1.0, 1.0],
        background_color: List[float] = [0.1, 0.1, 0.1, 1.0]
    ) -> Dict[str, Any]:
        """
        Add a Button widget to a UMG Widget Blueprint.
        
        Args:
            widget_name: Name of the target Widget Blueprint
            button_name: Name to give the new Button
            text: Text to display on the button
            position: [X, Y] position in the canvas panel
            size: [Width, Height] of the button
            font_size: Font size for button text
            color: [R, G, B, A] text color values (0.0 to 1.0)
            background_color: [R, G, B, A] button background color values (0.0 to 1.0)
            
        Returns:
            Dict containing success status and button properties
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "widget_name": widget_name,
                "button_name": button_name,
                "text": text,
                "position": position,
                "size": size,
                "font_size": font_size,
                "color": color,
                "background_color": background_color
            }
            
            logger.info(f"Adding Button to widget with params: {params}")
            response = unreal.send_command("add_button_to_widget", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add Button response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding Button to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_panel_widget_to_widget(
        ctx: Context,
        widget_name: str,
        panel_name: str,
        panel_type: str,
        position: List[float] = [0.0, 0.0],
        size: List[float] = [400.0, 100.0]
    ) -> Dict[str, Any]:
        """
        Add a container/panel widget (HorizontalBox, VerticalBox, UniformGridPanel,
        CanvasPanel, ScrollBox, WrapBox, Overlay) to a Widget Blueprint's root canvas.

        Args:
            widget_name: Name of the target Widget Blueprint (short or full path)
            panel_name: Name for the new panel (matches UPROPERTY(meta=(BindWidget)))
            panel_type: One of "HorizontalBox", "VerticalBox", "UniformGridPanel",
                "CanvasPanel", "ScrollBox", "WrapBox", "Overlay"
            position: [X, Y] position in the root canvas panel
            size: [Width, Height] of the panel

        Returns:
            Dict containing success status and panel properties
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "widget_name": widget_name,
                "panel_name": panel_name,
                "panel_type": panel_type,
                "position": position,
                "size": size
            }

            logger.info(f"Adding panel widget to widget with params: {params}")
            response = unreal.send_command("add_panel_widget_to_widget", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Add panel widget response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error adding panel widget to widget: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def bind_widget_event(
        ctx: Context,
        widget_name: str,
        widget_component_name: str,
        event_name: str,
        function_name: str = ""
    ) -> Dict[str, Any]:
        """
        Bind an event on a widget component to a function.
        
        Args:
            widget_name: Name of the target Widget Blueprint
            widget_component_name: Name of the widget component (button, etc.)
            event_name: Name of the event to bind (OnClicked, etc.)
            function_name: Name of the function to create/bind to (defaults to f"{widget_component_name}_{event_name}")
            
        Returns:
            Dict containing success status and binding information
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # If no function name provided, create one from component and event names
            if not function_name:
                function_name = f"{widget_component_name}_{event_name}"
            
            params = {
                "widget_name": widget_name,
                "widget_component_name": widget_component_name,
                "event_name": event_name,
                "function_name": function_name
            }
            
            logger.info(f"Binding widget event with params: {params}")
            response = unreal.send_command("bind_widget_event", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Bind widget event response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error binding widget event: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_widget_to_viewport(
        ctx: Context,
        widget_name: str,
        z_order: int = 0
    ) -> Dict[str, Any]:
        """
        Add a Widget Blueprint instance to the viewport.
        
        Args:
            widget_name: Name of the Widget Blueprint to add
            z_order: Z-order for the widget (higher numbers appear on top)
            
        Returns:
            Dict containing success status and widget instance information
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "widget_name": widget_name,
                "z_order": z_order
            }
            
            logger.info(f"Adding widget to viewport with params: {params}")
            response = unreal.send_command("add_widget_to_viewport", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Add widget to viewport response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding widget to viewport: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_text_block_binding(
        ctx: Context,
        widget_name: str,
        text_block_name: str,
        binding_property: str,
        binding_type: str = "Text"
    ) -> Dict[str, Any]:
        """
        Set up a property binding for a Text Block widget.
        
        Args:
            widget_name: Name of the target Widget Blueprint
            text_block_name: Name of the Text Block to bind
            binding_property: Name of the property to bind to
            binding_type: Type of binding (Text, Visibility, etc.)
            
        Returns:
            Dict containing success status and binding information
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "widget_name": widget_name,
                "text_block_name": text_block_name,
                "binding_property": binding_property,
                "binding_type": binding_type
            }
            
            logger.info(f"Setting text block binding with params: {params}")
            response = unreal.send_command("set_text_block_binding", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set text block binding response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting text block binding: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Phase 5 (v1.17.0) — Generic UMG widget/inspection wrappers
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def add_widget_to_umg(
        ctx: Context,
        blueprint_path: str,
        widget_type: str,
        widget_name: str,
        parent_name: str = None,
        is_variable: bool = True,
    ) -> Dict[str, Any]:
        """
        Generic widget constructor: add any supported widget type to a Widget
        Blueprint hierarchy.

        Args:
            blueprint_path: Full /Game/... path of the Widget Blueprint.
            widget_type: Class name. Supported: CanvasPanel, TextBlock, Button,
                VerticalBox, HorizontalBox, ScrollBox, SizeBox, Spacer, SpinBox,
                Image, Overlay, Border, ProgressBar.
            widget_name: Unique name for the new widget within the blueprint.
            parent_name: Optional existing widget name to nest under. When empty
                the widget becomes the root (or is added to the existing root panel).
            is_variable: Whether the widget is exposed as a class variable.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_path": blueprint_path,
                "widget_type": widget_type,
                "widget_name": widget_name,
                "is_variable": is_variable,
            }
            if parent_name is not None:
                params["parent_name"] = parent_name
            logger.info(f"Adding widget to UMG: {params}")
            response = unreal.send_command("add_widget_to_umg", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error adding widget to UMG: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # delete_widget_from_umg — inverse of add_widget_to_umg
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def delete_widget_from_umg(
        ctx: Context,
        blueprint_path: str,
        widget_name: str,
    ) -> Dict[str, Any]:
        """
        Remove a widget from a Widget Blueprint's WidgetTree.

        If the widget is a panel, all of its descendants are removed along with
        it (Unreal garbage-collects them once the subtree is detached). Use this
        to clean up unused leftover widgets — e.g. dropping HBox_CardContainer
        from WBP_ActionCardHand where only CardsBox is actually wired to C++.

        Args:
            blueprint_path: Full /Game/... path of the Widget Blueprint.
            widget_name: Name of the widget instance to delete. Returns
                {"success": False, "error": "Widget not found: ..."} if missing.

        Returns:
            {success, removed: bool, widget_name, blueprint_path, was_root: bool}.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_path": blueprint_path,
                "widget_name": widget_name,
            }
            logger.info(f"Deleting widget from UMG: {params}")
            response = unreal.send_command("delete_widget_from_umg", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error deleting widget from UMG: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_widget_property(
        ctx: Context,
        blueprint_path: str,
        widget_name: str,
        property_name: str,
        property_value: str,
    ) -> Dict[str, Any]:
        """
        Set a property on a widget inside a Widget Blueprint. Supports widget
        properties and slot properties (prefix "Slot.", e.g. "Slot.Position").

        Args:
            blueprint_path: Full /Game/... path of the Widget Blueprint.
            widget_name: Widget instance name (or "Root" / "" for the root widget).
            property_name: Property to set (e.g. "Text", "Slot.Position",
                "Slot.HorizontalAlignment").
            property_value: String value (vectors as "X,Y", colors as "R,G,B,A",
                booleans as "true"/"false", enums by short name).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_path": blueprint_path,
                "widget_name": widget_name,
                "property_name": property_name,
                "property_value": str(property_value),
            }
            logger.info(f"Setting widget property: {params}")
            response = unreal.send_command("set_widget_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting widget property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_umg_hierarchy(
        ctx: Context,
        blueprint_path: str,
    ) -> Dict[str, Any]:
        """
        Read-only recursive dump of a Widget Blueprint's widget tree.

        Returns:
            Dict with root widget object {name, type, is_variable, children[]}
            or {"empty": true} when the WidgetTree has no root.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"blueprint_path": blueprint_path}
            logger.info(f"Reading UMG hierarchy: {blueprint_path}")
            response = unreal.send_command("get_umg_hierarchy", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error getting UMG hierarchy: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("UMG tools registered successfully") 