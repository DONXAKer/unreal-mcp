"""
Unreal Engine MCP Server

A simple MCP server for interacting with Unreal Engine.
"""

import json
import logging
import os
import socket
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from typing import Any

from mcp.server.fastmcp import FastMCP

# Configure logging with more detailed format
logging.basicConfig(
    level=logging.DEBUG,  # Change to DEBUG level for more details
    format='%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s',
    handlers=[
        logging.FileHandler('unreal_mcp.log'),
        # logging.StreamHandler(sys.stdout) # Remove this handler to unexpected non-whitespace characters in JSON
    ]
)
logger = logging.getLogger("UnrealMCP")

# Configuration — honour UNREAL_HOST/UNREAL_PORT env (set by the Docker image to
# host.docker.internal so the containerised server reaches the Editor on the host).
# Falls back to 127.0.0.1 for non-container/local use.
UNREAL_HOST = os.environ.get("UNREAL_HOST", "127.0.0.1")
UNREAL_PORT = int(os.environ.get("UNREAL_PORT", "55557"))

class UnrealConnection:
    """Connection to an Unreal Engine instance."""
    
    def __init__(self) -> None:
        """Initialize the connection."""
        self.socket: socket.socket | None = None
        self.connected = False
    
    def connect(self) -> bool:
        """Connect to the Unreal Engine instance."""
        try:
            # Close any existing socket
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass
                self.socket = None
            
            logger.info(f"Connecting to Unreal at {UNREAL_HOST}:{UNREAL_PORT}...")
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5)  # 5 second timeout
            
            # Set socket options for better stability
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            
            # Set larger buffer sizes
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 65536)
            
            self.socket.connect((UNREAL_HOST, UNREAL_PORT))
            self.connected = True
            logger.info("Connected to Unreal Engine")
            return True
            
        except Exception as e:
            logger.error(f"Failed to connect to Unreal: {e}")
            self.connected = False
            return False
    
    def disconnect(self) -> None:
        """Disconnect from the Unreal Engine instance."""
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        self.socket = None
        self.connected = False

    def receive_full_response(self, sock: socket.socket, buffer_size: int = 4096) -> bytes:
        """Receive a complete response from Unreal, handling chunked data."""
        chunks: list[bytes] = []
        sock.settimeout(5)  # 5 second timeout
        try:
            while True:
                chunk = sock.recv(buffer_size)
                if not chunk:
                    if not chunks:
                        raise Exception("Connection closed before receiving data")
                    break
                chunks.append(chunk)
                
                # Process the data received so far
                data = b''.join(chunks)
                decoded_data = data.decode('utf-8')
                
                # Try to parse as JSON to check if complete
                try:
                    json.loads(decoded_data)
                    logger.info(f"Received complete response ({len(data)} bytes)")
                    return data
                except json.JSONDecodeError:
                    # Not complete JSON yet, continue reading
                    logger.debug("Received partial response, waiting for more data...")
                    continue
                except Exception as e:
                    logger.warning(f"Error processing response chunk: {e!s}")
                    continue
        except TimeoutError:
            logger.warning("Socket timeout during receive")
            if chunks:
                # If we have some data already, try to use it
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    logger.info(f"Using partial response after timeout ({len(data)} bytes)")
                    return data
                except Exception:
                    pass
            raise RuntimeError("Timeout receiving Unreal response") from None
        except Exception as e:
            logger.error(f"Error during receive: {e!s}")
            raise

        # Unreachable: loop exits via break only when EOF + partial chunks, which
        # already happened above. Kept to satisfy strict mypy "missing return".
        return b"".join(chunks)


    def send_command(self, command: str, params: dict[str, Any] = None) -> dict[str, Any] | None:
        """Send a command to Unreal Engine and get the response."""
        # Always reconnect for each command, since Unreal closes the connection after each command
        # This is different from Unity which keeps connections alive
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            self.connected = False

        if not self.connect():
            logger.error("Failed to connect to Unreal Engine for command")
            return None
        assert self.socket is not None  # connect() set it on success

        try:
            # Match Unity's command format exactly
            command_obj = {
                "type": command,  # Use "type" instead of "command"
                "params": params or {}  # Use Unity's params or {} pattern
            }

            # Send without newline, exactly like Unity
            command_json = json.dumps(command_obj)
            logger.info(f"Sending command: {command_json}")
            self.socket.sendall(command_json.encode('utf-8'))

            # Read response using improved handler
            response_data = self.receive_full_response(self.socket)
            response: dict[str, Any] = json.loads(response_data.decode('utf-8'))
            
            # Log complete response for debugging
            logger.info(f"Complete response from Unreal: {response}")
            
            # Check for both error formats: {"status": "error", ...} and {"success": false, ...}
            if response.get("status") == "error":
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (status=error): {error_message}")
                # We want to preserve the original error structure but ensure error is accessible
                if "error" not in response:
                    response["error"] = error_message
            elif response.get("success") is False:
                # This format uses {"success": false, "error": "message"} or {"success": false, "message": "message"}
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                logger.error(f"Unreal error (success=false): {error_message}")
                # Convert to the standard format expected by higher layers
                response = {
                    "status": "error",
                    "error": error_message
                }
            
            # Always close the connection after command is complete
            # since Unreal will close it on its side anyway
            if self.socket is not None:
                try:
                    self.socket.close()
                except Exception:
                    pass
            self.socket = None
            self.connected = False

            return response

        except Exception as e:
            logger.error(f"Error sending command: {e}")
            # Always reset connection state on any error
            self.connected = False
            if self.socket is not None:
                try:
                    self.socket.close()
                except Exception:
                    pass
            self.socket = None
            return {
                "status": "error",
                "error": str(e)
            }

# Global connection state
_unreal_connection: UnrealConnection | None = None

def get_unreal_connection() -> UnrealConnection | None:
    """Return the cached UnrealConnection, lazily constructed on first call.

    No liveness probe — `send_command` already reopens the socket on every
    call (the bridge closes it server-side after each response), so any
    cached socket here is just a handle. A previous ping(\x00) check forced
    a double-reconnect and was dead code; removed.
    """
    global _unreal_connection
    try:
        if _unreal_connection is None:
            _unreal_connection = UnrealConnection()
            if not _unreal_connection.connect():
                logger.warning("Could not connect to Unreal Engine")
                _unreal_connection = None
        return _unreal_connection
    except Exception as e:
        logger.error(f"Error getting Unreal connection: {e}")
        return None

@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[dict[str, Any]]:
    """Handle server startup and shutdown."""
    global _unreal_connection
    logger.info("UnrealMCP server starting up")
    try:
        _unreal_connection = get_unreal_connection()
        if _unreal_connection:
            logger.info("Connected to Unreal Engine on startup")
        else:
            logger.warning("Could not connect to Unreal Engine on startup")
    except Exception as e:
        logger.error(f"Error connecting to Unreal Engine on startup: {e}")
        _unreal_connection = None
    
    try:
        yield {}
    finally:
        if _unreal_connection:
            _unreal_connection.disconnect()
            _unreal_connection = None
        logger.info("Unreal MCP server shut down")

# Initialize server
mcp = FastMCP(
    "UnrealMCP",
    lifespan=server_lifespan
)

# Import and register tools
from tools.animation_tools import register_animation_tools
from tools.asset_tools import register_asset_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.data_asset_tools import register_data_asset_tools
from tools.editor_tools import register_editor_tools

# v1.17.0 — Phase 5 wrappers (close the bridge↔FastMCP gap)
from tools.level_tools import register_level_tools
from tools.material_tools import register_material_tools
from tools.mesh_tools import register_mesh_tools
from tools.niagara_tools import register_niagara_tools
from tools.graph_builder import register_graph_builder_tool
from tools.node_tools import register_blueprint_node_tools
from tools.pie_tools import register_pie_tools
from tools.console_tools import register_console_tools
from tools.project_tools import register_project_tools
from tools.texture_tools import register_texture_tools
from tools.umg_tools import register_umg_tools
from tools.enhanced_input_tools import register_enhanced_input_tools

# Register tools
register_editor_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_graph_builder_tool(mcp)  # v2.1: declarative EventGraph builder
register_project_tools(mcp)
register_umg_tools(mcp)
register_animation_tools(mcp)
register_level_tools(mcp)
register_material_tools(mcp)
register_asset_tools(mcp)
register_texture_tools(mcp)
register_mesh_tools(mcp)
register_data_asset_tools(mcp)
register_niagara_tools(mcp)
register_pie_tools(mcp)  # v2.4.0 — Playwright-like e2e: PIE lifecycle + UMG automation
register_console_tools(mcp)  # v2.6.0 — arbitrary console-command execution (Automation RunTests, CVars, etc.)
register_enhanced_input_tools(mcp)  # v2.10.0 — UE5.7 Enhanced Input (MCP-PLUGIN-004)

# --- MCP Content Pipeline (MCP-CONTENT-001): config + recipe framework ---
from tools import project_config as _project_config
from tools import recipe_framework as _recipe_framework
from tools._envelope import wrap_with_envelope as _wrap_with_envelope

_recipe_framework.init_registry(mcp)

_server_mcp = _wrap_with_envelope(mcp)

@_server_mcp.tool()
def reload_config() -> dict[str, Any]:
    """Reload <ProjectRoot>/mcp-project.json from disk."""
    return _project_config.reload_config()

@_server_mcp.tool()
def reload_recipes() -> dict[str, Any]:
    """Rediscover and re-register all recipes under the configured recipesDir."""
    return _recipe_framework.reload_recipes_impl()

@_server_mcp.tool()
def list_recipes() -> dict[str, Any]:
    """Return metadata (name, description, args, produces) for every registered recipe."""
    return _recipe_framework.list_recipes_impl()

try:
    if _project_config.load_config() is not None:
        _count, _names, _errors = _recipe_framework.register_all_recipes()
        logger.info(
            "MCP Content Pipeline startup: %d recipes registered (errors=%s)",
            _count,
            _errors or "none",
        )
    else:
        logger.info("MCP Content Pipeline: no mcp-project.json — recipes not loaded")
except Exception as _startup_err:
    logger.exception("MCP Content Pipeline startup failed: %s", _startup_err)

@mcp.prompt()
def info() -> str:
    """Information about available Unreal MCP tools and best practices."""
    return """
    # Unreal MCP Server Tools and Best Practices
    
    ## UMG (Widget Blueprint) Tools
    - `create_umg_widget_blueprint(widget_name, parent_class="UserWidget", path="/Game/UI")` 
      Create a new UMG Widget Blueprint
    - `add_text_block_to_widget(widget_name, text_block_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1])`
      Add a Text Block widget with customizable properties
    - `add_button_to_widget(widget_name, button_name, text="", position=[0,0], size=[200,50], font_size=12, color=[1,1,1,1], background_color=[0.1,0.1,0.1,1])`
      Add a Button widget with text and styling
    - `bind_widget_event(widget_name, widget_component_name, event_name, function_name="")`
      Bind events like OnClicked to functions
    - `add_widget_to_viewport(widget_name, z_order=0)`
      Add widget instance to game viewport
    - `set_text_block_binding(widget_name, text_block_name, binding_property, binding_type="Text")`
      Set up dynamic property binding for text blocks

    ## Editor Tools
    ### Viewport and Screenshots
    - `focus_viewport(target, location, distance, orientation)` - Focus viewport
    - `take_screenshot(filename, show_ui, resolution)` - Capture screenshots

    ### Actor Management
    - `get_actors_in_level()` - List all actors in current level
    - `find_actors_by_name(pattern)` - Find actors by name pattern
    - `spawn_actor(name, type, location=[0,0,0], rotation=[0,0,0], scale=[1,1,1])` - Create actors
    - `delete_actor(name)` - Remove actors
    - `set_actor_transform(name, location, rotation, scale)` - Modify actor transform
    - `get_actor_properties(name)` - Get actor properties
    
    ## Blueprint Management
    - `create_blueprint(name, parent_class)` - Create new Blueprint classes
    - `add_component_to_blueprint(blueprint_name, component_type, component_name)` - Add components
    - `set_static_mesh_properties(blueprint_name, component_name, static_mesh)` - Configure meshes
    - `set_physics_properties(blueprint_name, component_name)` - Configure physics
    - `compile_blueprint(blueprint_name)` - Compile Blueprint changes
    - `set_blueprint_property(blueprint_name, property_name, property_value)` - Set properties
    - `set_pawn_properties(blueprint_name)` - Configure Pawn settings
    - `spawn_blueprint_actor(blueprint_name, actor_name)` - Spawn Blueprint actors
    
    ## Blueprint Node Management
    - `add_blueprint_event_node(blueprint_name, event_type)` - Add event nodes
    - `add_blueprint_input_action_node(blueprint_name, action_name)` - Add input nodes
    - `add_blueprint_function_node(blueprint_name, target, function_name)` - Add function nodes
    - `connect_blueprint_nodes(blueprint_name, source_node_id, source_pin, target_node_id, target_pin)` - Connect nodes
    - `add_blueprint_variable(blueprint_name, variable_name, variable_type)` - Add variables
    - `add_blueprint_get_self_component_reference(blueprint_name, component_name)` - Add component refs
    - `add_blueprint_self_reference(blueprint_name)` - Add self references
    - `find_blueprint_nodes(blueprint_name, node_type, event_type)` - Find nodes
    
    ## Project Tools
    - `create_input_mapping(action_name, key, input_type)` - Create input mappings
    
    ## Best Practices
    
    ### UMG Widget Development
    - Create widgets with descriptive names that reflect their purpose
    - Use consistent naming conventions for widget components
    - Organize widget hierarchy logically
    - Set appropriate anchors and alignment for responsive layouts
    - Use property bindings for dynamic updates instead of direct setting
    - Handle widget events appropriately with meaningful function names
    - Clean up widgets when no longer needed
    - Test widget layouts at different resolutions
    
    ### Editor and Actor Management
    - Use unique names for actors to avoid conflicts
    - Clean up temporary actors
    - Validate transforms before applying
    - Check actor existence before modifications
    - Take regular viewport screenshots during development
    - Keep the viewport focused on relevant actors during operations
    
    ### Blueprint Development
    - Compile Blueprints after changes
    - Use meaningful names for variables and functions
    - Organize nodes logically
    - Test functionality in isolation
    - Consider performance implications
    - Document complex setups
    
    ### Error Handling
    - Check command responses for success
    - Handle errors gracefully
    - Log important operations
    - Validate parameters
    - Clean up resources on errors
    """

# Run the server
if __name__ == "__main__":
    logger.info("Starting MCP server with stdio transport")
    mcp.run(transport='stdio') 