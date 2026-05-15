"""
Blueprint Node Tools for Unreal MCP.

This module provides tools for manipulating Blueprint graph nodes and connections.
"""

import logging
from typing import Dict, List, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_blueprint_node_tools(mcp: FastMCP):
    """Register Blueprint node manipulation tools with the MCP server."""
    
    @mcp.tool()
    def add_blueprint_event_node(
        ctx: Context,
        blueprint_name: str,
        event_name: str,
        node_position = None
    ) -> Dict[str, Any]:
        """
        Add an event node to a Blueprint's event graph.
        
        Args:
            blueprint_name: Name of the target Blueprint
            event_name: Name of the event. Use 'Receive' prefix for standard events:
                       - 'ReceiveBeginPlay' for Begin Play
                       - 'ReceiveTick' for Tick
                       - etc.
            node_position: Optional [X, Y] position in the graph
            
        Returns:
            Response containing the node ID and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Handle default value within the method body
            if node_position is None:
                node_position = [0, 0]
            
            params = {
                "blueprint_name": blueprint_name,
                "event_name": event_name,
                "node_position": node_position
            }
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            logger.info(f"Adding event node '{event_name}' to blueprint '{blueprint_name}'")
            response = unreal.send_command("add_event_node", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Event node creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding event node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_blueprint_input_action_node(
        ctx: Context,
        blueprint_name: str,
        action_name: str,
        node_position = None
    ) -> Dict[str, Any]:
        """
        Add an input action event node to a Blueprint's event graph.
        
        Args:
            blueprint_name: Name of the target Blueprint
            action_name: Name of the input action to respond to
            node_position: Optional [X, Y] position in the graph
            
        Returns:
            Response containing the node ID and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Handle default value within the method body
            if node_position is None:
                node_position = [0, 0]
            
            params = {
                "blueprint_name": blueprint_name,
                "action_name": action_name,
                "node_position": node_position
            }
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            logger.info(f"Adding input action node for '{action_name}' to blueprint '{blueprint_name}'")
            response = unreal.send_command("add_input_action_node", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Input action node creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding input action node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_blueprint_function_node(
        ctx: Context,
        blueprint_name: str,
        target: str,
        function_name: str,
        params = None,
        node_position = None
    ) -> Dict[str, Any]:
        """
        Add a function call node to a Blueprint's event graph.
        
        Args:
            blueprint_name: Name of the target Blueprint
            target: Target object for the function (component name or self)
            function_name: Name of the function to call
            params: Optional parameters to set on the function node
            node_position: Optional [X, Y] position in the graph
            
        Returns:
            Response containing the node ID and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Handle default values within the method body
            if params is None:
                params = {}
            if node_position is None:
                node_position = [0, 0]
            
            command_params = {
                "blueprint_name": blueprint_name,
                "node_type": "CallFunction",
                "target_class": target,
                "target_function": function_name,
                "params": params,
                "node_position": node_position
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Adding function node '{function_name}' to blueprint '{blueprint_name}'")
            response = unreal.send_command("add_blueprint_node", command_params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Function node creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding function node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
            
    @mcp.tool()
    def connect_blueprint_nodes(
        ctx: Context,
        blueprint_name: str,
        source_node_id: str,
        source_pin: str,
        target_node_id: str,
        target_pin: str
    ) -> Dict[str, Any]:
        """
        Connect two nodes in a Blueprint's event graph.
        
        Args:
            blueprint_name: Name of the target Blueprint
            source_node_id: ID of the source node
            source_pin: Name of the output pin on the source node
            target_node_id: ID of the target node
            target_pin: Name of the input pin on the target node
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            params = {
                "blueprint_name": blueprint_name,
                "source_node_id": source_node_id,
                "source_pin_name": source_pin,
                "target_node_id": target_node_id,
                "target_pin_name": target_pin
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Connecting nodes in blueprint '{blueprint_name}'")
            response = unreal.send_command("connect_nodes", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Node connection response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error connecting nodes: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_blueprint_variable(
        ctx: Context,
        blueprint_name: str,
        variable_name: str,
        variable_type: str,
        is_exposed: bool = False
    ) -> Dict[str, Any]:
        """
        Add a variable to a Blueprint.
        
        Args:
            blueprint_name: Name of the target Blueprint
            variable_name: Name of the variable
            variable_type: Type of the variable (Boolean, Integer, Float, Vector, etc.)
            is_exposed: Whether to expose the variable to the editor
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            params = {
                "blueprint_name": blueprint_name,
                "variable_name": variable_name,
                "variable_type": variable_type,
                "is_exposed": is_exposed
            }
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            logger.info(f"Adding variable '{variable_name}' to blueprint '{blueprint_name}'")
            response = unreal.send_command("create_variable", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Variable creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding variable: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_blueprint_get_self_component_reference(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        node_position = None
    ) -> Dict[str, Any]:
        """
        Add a node that gets a reference to a component owned by the current Blueprint.
        This creates a node similar to what you get when dragging a component from the Components panel.
        
        Args:
            blueprint_name: Name of the target Blueprint
            component_name: Name of the component to get a reference to
            node_position: Optional [X, Y] position in the graph
            
        Returns:
            Response containing the node ID and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Handle None case explicitly in the function
            if node_position is None:
                node_position = [0, 0]
            
            params = {
                "blueprint_name": blueprint_name,
                "node_type": "VariableGet",
                "variable_name": component_name,
                "node_position": node_position
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Adding self component reference node for '{component_name}' to blueprint '{blueprint_name}'")
            response = unreal.send_command("add_blueprint_node", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Self component reference node creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding self component reference node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_blueprint_self_reference(
        ctx: Context,
        blueprint_name: str,
        node_position = None
    ) -> Dict[str, Any]:
        """
        Add a 'Get Self' node to a Blueprint's event graph that returns a reference to this actor.
        
        Args:
            blueprint_name: Name of the target Blueprint
            node_position: Optional [X, Y] position in the graph
            
        Returns:
            Response containing the node ID and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            if node_position is None:
                node_position = [0, 0]
                
            params = {
                "blueprint_name": blueprint_name,
                "node_type": "Self",
                "node_position": node_position
            }

            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            logger.info(f"Adding self reference node to blueprint '{blueprint_name}'")
            response = unreal.send_command("add_blueprint_node", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Self reference node creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding self reference node: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def find_blueprint_nodes(
        ctx: Context,
        blueprint_name: str,
        node_type = None,
        event_type = None
    ) -> Dict[str, Any]:
        """
        Find nodes in a Blueprint's event graph.
        
        Args:
            blueprint_name: Name of the target Blueprint
            node_type: Optional type of node to find (Event, Function, Variable, etc.)
            event_type: Optional specific event type to find (BeginPlay, Tick, etc.)
            
        Returns:
            Response containing array of found node IDs and success status
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            params = {
                "blueprint_name": blueprint_name,
                "node_type": node_type,
                "event_type": event_type
            }
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            logger.info(f"Finding nodes in blueprint '{blueprint_name}'")
            response = unreal.send_command("find_blueprint_nodes", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Node find response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error finding nodes: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Phase 1B (v1.11.0) — Variable lifecycle
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def rename_blueprint_variable(
        ctx: Context,
        blueprint_name: str,
        old_name: str,
        new_name: str,
    ) -> Dict[str, Any]:
        """
        Rename a member variable of a Blueprint. Updates all Get/Set references in the graph.

        Args:
            blueprint_name: Target Blueprint.
            old_name: Current variable name.
            new_name: New name (must not collide with an existing variable).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "old_name": old_name,
                "new_name": new_name,
            }
            logger.info(f"Renaming blueprint variable: {params}")
            response = unreal.send_command("rename_blueprint_variable", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error renaming blueprint variable: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def delete_blueprint_variable(
        ctx: Context,
        blueprint_name: str,
        variable_name: str,
    ) -> Dict[str, Any]:
        """
        Delete a member variable from a Blueprint. Removes all Get/Set references too.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "variable_name": variable_name,
            }
            logger.info(f"Deleting blueprint variable: {params}")
            response = unreal.send_command("delete_blueprint_variable", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error deleting blueprint variable: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_variable_default_value(
        ctx: Context,
        blueprint_name: str,
        variable_name: str,
        default_value: str,
    ) -> Dict[str, Any]:
        """
        Set the default value of a Blueprint variable.

        Args:
            blueprint_name: Target Blueprint.
            variable_name: Variable to modify.
            default_value: String form of the value (e.g. "42", "true", "(X=1,Y=2,Z=3)").
                The textual form is parsed via PropertyValueFromString into the generated
                class's CDO and is also stored on FBPVariableDescription::DefaultValue.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "variable_name": variable_name,
                "default_value": default_value,
            }
            logger.info(f"Setting variable default value: {params}")
            response = unreal.send_command("set_variable_default_value", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting variable default value: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_blueprint_variables(
        ctx: Context,
        blueprint_name: str,
    ) -> Dict[str, Any]:
        """
        Read-only: list all NewVariables of a Blueprint.

        Returns:
            Dict with 'variables' (list of {name, type, is_instance_editable, expose_on_spawn,
            blueprint_read_only, category, default_value, replication}) and 'count'.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"blueprint_name": blueprint_name}
            logger.info(f"Listing variables for blueprint: {blueprint_name}")
            response = unreal.send_command("list_blueprint_variables", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error listing blueprint variables: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_blueprint_variable_flags(
        ctx: Context,
        blueprint_name: str,
        variable_name: str,
        instance_editable: bool = None,
        expose_on_spawn: bool = None,
        blueprint_read_only: bool = None,
        category: str = None,
        replication: str = None,
    ) -> Dict[str, Any]:
        """
        Update a focused subset of variable flags. Use this for clean instance_editable /
        expose_on_spawn / blueprint_read_only / category / replication changes; for the
        full legacy API (tooltips, slider range, bitmask, etc.) keep using
        set_blueprint_variable_properties.

        Args:
            blueprint_name: Target Blueprint.
            variable_name: Variable to modify.
            instance_editable: If set, toggles CPF_Edit (visible & editable in instances).
            expose_on_spawn: If set, toggles ExposeOnSpawn metadata (+ forces instance_editable=True).
            blueprint_read_only: If set, toggles CPF_BlueprintReadOnly.
            category: If set, updates the Category text.
            replication: One of "None" / "Replicated" / "RepNotify".
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "variable_name": variable_name,
            }
            if instance_editable is not None:
                params["instance_editable"] = bool(instance_editable)
            if expose_on_spawn is not None:
                params["expose_on_spawn"] = bool(expose_on_spawn)
            if blueprint_read_only is not None:
                params["blueprint_read_only"] = bool(blueprint_read_only)
            if category is not None:
                params["category"] = str(category)
            if replication is not None:
                params["replication"] = str(replication)

            logger.info(f"Setting variable flags: {params}")
            response = unreal.send_command("set_blueprint_variable_flags", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting blueprint variable flags: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Phase 1C (v1.12.0) — Function lifecycle
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def list_blueprint_functions(
        ctx: Context,
        blueprint_name: str,
    ) -> Dict[str, Any]:
        """
        Read-only: list all user-defined functions of a Blueprint with their I/O counts
        and flags.

        Returns:
            Dict with 'functions' (list of {name, num_inputs, num_outputs, is_pure,
            is_const, access_specifier, category}) and 'count'.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"blueprint_name": blueprint_name}
            logger.info(f"Listing functions for blueprint: {blueprint_name}")
            response = unreal.send_command("list_blueprint_functions", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error listing blueprint functions: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_function_local_variable(
        ctx: Context,
        blueprint_name: str,
        function_name: str,
        variable_name: str,
        variable_type: str,
        default_value: str = None,
    ) -> Dict[str, Any]:
        """
        Add a local variable to a Blueprint function. The variable is stored on the
        K2Node_FunctionEntry's LocalVariables array (function-scoped, NOT on
        Blueprint->NewVariables).

        Args:
            blueprint_name: Target Blueprint.
            function_name: Name of the function to extend.
            variable_name: New local variable name (must be a valid identifier).
            variable_type: Type string. Same vocabulary as create_variable:
                bool, int, float, string, text, name, vector, rotator,
                struct:<Name>, object:<Class>, array:<inner>.
            default_value: Optional string-form default value.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "function_name": function_name,
                "variable_name": variable_name,
                "variable_type": variable_type,
            }
            if default_value is not None:
                params["default_value"] = str(default_value)
            logger.info(f"Adding function local variable: {params}")
            response = unreal.send_command("add_function_local_variable", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error adding function local variable: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_function_flags(
        ctx: Context,
        blueprint_name: str,
        function_name: str,
        is_pure: bool = None,
        is_const: bool = None,
        access: str = None,
        category: str = None,
    ) -> Dict[str, Any]:
        """
        Update flags of a Blueprint function. Recompiles the Blueprint so changes
        propagate to the generated UFunction.

        Args:
            blueprint_name: Target Blueprint.
            function_name: Function to modify.
            is_pure: If set, toggles FUNC_BlueprintPure (pure functions have no exec pins).
            is_const: If set, toggles FUNC_Const.
            access: One of "Public" / "Protected" / "Private".
            category: If set, updates the Category text shown in the editor.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "function_name": function_name,
            }
            if is_pure is not None:
                params["is_pure"] = bool(is_pure)
            if is_const is not None:
                params["is_const"] = bool(is_const)
            if access is not None:
                params["access"] = str(access)
            if category is not None:
                params["category"] = str(category)
            logger.info(f"Setting function flags: {params}")
            response = unreal.send_command("set_function_flags", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting function flags: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Phase 1D (v1.12.0) — Custom events
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def create_custom_event(
        ctx: Context,
        blueprint_name: str,
        event_name: str,
        node_position = None,
    ) -> Dict[str, Any]:
        """
        Create a brand-new K2Node_CustomEvent in the Blueprint's Ubergraph. Unlike
        add_blueprint_event_node (which overrides ReceiveBeginPlay / ReceiveTick etc.),
        this creates a Blueprint-only event that can be called via Call Function or
        bound to a delegate.

        Args:
            blueprint_name: Target Blueprint.
            event_name: New event name (must be a valid identifier; must not collide
                with an existing custom event on the same Blueprint).
            node_position: Optional [X, Y] graph coordinates.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            if node_position is None:
                node_position = [0, 0]
            params = {
                "blueprint_name": blueprint_name,
                "event_name": event_name,
                "node_position": node_position,
            }
            logger.info(f"Creating custom event '{event_name}' in blueprint '{blueprint_name}'")
            response = unreal.send_command("create_custom_event", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error creating custom event: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def add_custom_event_input(
        ctx: Context,
        blueprint_name: str,
        event_name: str,
        parameter_name: str,
        parameter_type: str,
    ) -> Dict[str, Any]:
        """
        Add an input parameter pin to an existing custom event.

        Args:
            blueprint_name: Target Blueprint.
            event_name: Existing custom event (matched by CustomFunctionName).
            parameter_name: New pin name.
            parameter_type: Type string (bool, int, float, string, vector, rotator,
                struct:..., object:..., array:<inner>).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "event_name": event_name,
                "parameter_name": parameter_name,
                "parameter_type": parameter_type,
            }
            logger.info(f"Adding custom event input: {params}")
            response = unreal.send_command("add_custom_event_input", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error adding custom event input: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ─────────────────────────────────────────────────────────────────────
    # Phase 3C (v1.15.0) — Pin-level operations
    # ─────────────────────────────────────────────────────────────────────

    @mcp.tool()
    def split_struct_pin(
        ctx: Context,
        blueprint_name: str,
        node_id: str,
        pin_name: str,
    ) -> Dict[str, Any]:
        """
        Split a struct pin into per-field sub-pins (e.g. FVector → X/Y/Z).

        Args:
            blueprint_name: Target Blueprint.
            node_id: NodeGuid string of the node owning the pin.
            pin_name: Name of the struct pin to split.

        Returns:
            Dict with sub_pin_names[] and num_sub_pins.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "node_id": node_id,
                "pin_name": pin_name,
            }
            logger.info(f"Splitting struct pin: {params}")
            response = unreal.send_command("split_struct_pin", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error splitting struct pin: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def recombine_struct_pin(
        ctx: Context,
        blueprint_name: str,
        node_id: str,
        pin_name: str,
    ) -> Dict[str, Any]:
        """
        Inverse of split_struct_pin — collapse sub-pins back into one struct pin.

        Args:
            blueprint_name: Target Blueprint.
            node_id: NodeGuid string of the node.
            pin_name: The parent split pin OR any of its sub-pins.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "node_id": node_id,
                "pin_name": pin_name,
            }
            logger.info(f"Recombining struct pin: {params}")
            response = unreal.send_command("recombine_struct_pin", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error recombining struct pin: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_pin_default_value(
        ctx: Context,
        blueprint_name: str,
        node_id: str,
        pin_name: str,
        default_value: str,
    ) -> Dict[str, Any]:
        """
        Set the literal default value of a pin (unconnected pins use this value at runtime).

        For primitive pins (bool/int/float/string): UEdGraphSchema_K2::TrySetDefaultValue
        validates the string. For Object/Class/SoftObject/SoftClass pins: the value is
        treated as a full /Game/... path and loaded into Pin->DefaultObject.

        Args:
            blueprint_name: Target Blueprint.
            node_id: NodeGuid string.
            pin_name: Pin to mutate.
            default_value: String form ("true"/"false", "42", "1.5", "Hello", "(X=0,Y=0,Z=0)",
                "/Game/Path/Asset" for object refs, "None" to clear an object ref).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "node_id": node_id,
                "pin_name": pin_name,
                "default_value": str(default_value),
            }
            logger.info(f"Setting pin default value: {params}")
            response = unreal.send_command("set_pin_default_value", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error setting pin default value: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_pin_info(
        ctx: Context,
        blueprint_name: str,
        node_id: str,
        pin_name: str = None,
    ) -> Dict[str, Any]:
        """
        Read-only pin inspection. If pin_name is provided, returns info for a single
        pin under 'pin'. Otherwise returns info for all pins on the node under 'pins[]'.

        Per-pin fields:
            name, direction (input/output), pin_category, pin_sub_category,
            pin_sub_category_object?, default_value, default_object?, default_text?,
            is_split, is_orphaned, is_hidden, num_connections,
            connection_targets[{pin_name, node_id, node_title}].
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_name": blueprint_name,
                "node_id": node_id,
            }
            if pin_name is not None:
                params["pin_name"] = pin_name
            logger.info(f"Getting pin info: {params}")
            response = unreal.send_command("get_pin_info", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error getting pin info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def disconnect_pin(
        ctx: Context,
        blueprint_name: str,
        node_id: str,
        pin_name: str,
    ) -> Dict[str, Any]:
        """
        Break all links on a pin (UEdGraphSchema_K2::BreakPinLinks).

        Returns:
            Dict with num_links_broken (int) — how many connections existed before
            the call.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "blueprint_name": blueprint_name,
                "node_id": node_id,
                "pin_name": pin_name,
            }
            logger.info(f"Disconnecting pin: {params}")
            response = unreal.send_command("disconnect_pin", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response
        except Exception as e:
            error_msg = f"Error disconnecting pin: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Blueprint node tools registered successfully")