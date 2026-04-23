"""
Unreal Engine MCP Server — HTTP/SSE transport variant.

Runs the same MCP server as unreal_mcp_server.py but listens on an HTTP port
instead of stdio. This allows AI-Workflow pipeline agents to connect via URL.

Environment variables:
  UNREAL_HOST   - host where Unreal Engine is running (default: host.docker.internal)
  UNREAL_PORT   - TCP port of the Unreal MCP bridge (default: 55557)
  MCP_HTTP_PORT - port to listen on for HTTP/SSE (default: 3001)
"""

import logging
import os
import socket
import json
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional
from mcp.server.fastmcp import FastMCP

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler()],
)
logger = logging.getLogger("UnrealMCP-HTTP")

UNREAL_HOST = os.getenv("UNREAL_HOST", "host.docker.internal")
UNREAL_PORT = int(os.getenv("UNREAL_PORT", "55557"))
MCP_HTTP_PORT = int(os.getenv("MCP_HTTP_PORT", "3001"))

logger.info("UnrealMCP HTTP: will connect to Unreal at %s:%d", UNREAL_HOST, UNREAL_PORT)


class UnrealConnection:
    def __init__(self):
        self.socket = None
        self.connected = False

    def connect(self) -> bool:
        try:
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass
                self.socket = None
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5)
            self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            self.socket.connect((UNREAL_HOST, UNREAL_PORT))
            self.connected = True
            logger.info("Connected to Unreal Engine at %s:%d", UNREAL_HOST, UNREAL_PORT)
            return True
        except Exception as e:
            logger.error("Failed to connect to Unreal at %s:%d: %s", UNREAL_HOST, UNREAL_PORT, e)
            self.connected = False
            return False

    def disconnect(self):
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        self.socket = None
        self.connected = False

    def receive_full_response(self, sock, buffer_size=4096) -> bytes:
        chunks = []
        sock.settimeout(5)
        try:
            while True:
                chunk = sock.recv(buffer_size)
                if not chunk:
                    if not chunks:
                        raise Exception("Connection closed before receiving data")
                    break
                chunks.append(chunk)
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    return data
                except json.JSONDecodeError:
                    continue
        except socket.timeout:
            if chunks:
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    return data
                except Exception:
                    pass
            raise Exception("Timeout receiving Unreal response")

    def send_command(self, command: str, params: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            self.connected = False

        if not self.connect():
            return {"status": "error", "error": f"Cannot connect to Unreal at {UNREAL_HOST}:{UNREAL_PORT}"}

        try:
            command_obj = {"type": command, "params": params or {}}
            command_json = json.dumps(command_obj)
            self.socket.sendall(command_json.encode('utf-8'))
            response_data = self.receive_full_response(self.socket)
            response = json.loads(response_data.decode('utf-8'))

            if response.get("status") == "error":
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                if "error" not in response:
                    response["error"] = error_message
            elif response.get("success") is False:
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                response = {"status": "error", "error": error_message}

            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            self.connected = False
            return response
        except Exception as e:
            logger.error("Error sending command: %s", e)
            self.connected = False
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            return {"status": "error", "error": str(e)}


_unreal_connection: Optional[UnrealConnection] = None


def get_unreal_connection() -> Optional[UnrealConnection]:
    global _unreal_connection
    try:
        if _unreal_connection is None:
            _unreal_connection = UnrealConnection()
            if not _unreal_connection.connect():
                logger.warning("Could not connect to Unreal Engine at %s:%d", UNREAL_HOST, UNREAL_PORT)
                _unreal_connection = None
        return _unreal_connection
    except Exception as e:
        logger.error("Error getting Unreal connection: %s", e)
        return None


@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    global _unreal_connection
    logger.info("UnrealMCP HTTP server starting")
    try:
        _unreal_connection = get_unreal_connection()
        if _unreal_connection:
            logger.info("Connected to Unreal Engine on startup")
        else:
            logger.warning("Unreal Engine not reachable — tools will return errors until it starts")
    except Exception as e:
        logger.error("Startup error: %s", e)
        _unreal_connection = None
    try:
        yield {}
    finally:
        if _unreal_connection:
            _unreal_connection.disconnect()
        logger.info("UnrealMCP HTTP server stopped")


mcp = FastMCP(
    "UnrealMCP",
    description="Unreal Engine integration via Model Context Protocol (HTTP/SSE)",
    lifespan=server_lifespan,
)

# Register the same tools as the stdio server
from tools.editor_tools import register_editor_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.node_tools import register_blueprint_node_tools
from tools.project_tools import register_project_tools
from tools.umg_tools import register_umg_tools

register_editor_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_project_tools(mcp)
register_umg_tools(mcp)

from tools import project_config as _project_config
from tools import recipe_framework as _recipe_framework

_recipe_framework.init_registry(mcp)


@mcp.tool()
def reload_config() -> Dict[str, Any]:
    """Reload <ProjectRoot>/mcp-project.json from disk."""
    return _project_config.reload_config()


@mcp.tool()
def reload_recipes() -> Dict[str, Any]:
    """Rediscover and re-register all recipes under the configured recipesDir."""
    return _recipe_framework.reload_recipes_impl()


try:
    if _project_config.load_config() is not None:
        _count, _names, _errors = _recipe_framework.register_all_recipes()
        logger.info("MCP Content Pipeline: %d recipes registered", _count)
    else:
        logger.info("MCP Content Pipeline: no mcp-project.json")
except Exception as _startup_err:
    logger.exception("MCP Content Pipeline startup failed: %s", _startup_err)


if __name__ == "__main__":
    import uvicorn
    logger.info("Starting UnrealMCP HTTP/SSE server on port %d", MCP_HTTP_PORT)
    # FastMCP exposes an ASGI app for SSE transport;
    # we drive it with uvicorn to control host/port.
    try:
        # mcp >= 1.1 exposes sse_app()
        app = mcp.sse_app()
    except AttributeError:
        # fallback: let FastMCP handle the loop itself on default port
        logger.warning("sse_app() not available — falling back to mcp.run(transport='sse')")
        mcp.run(transport="sse")
        raise SystemExit(0)
    uvicorn.run(app, host="0.0.0.0", port=MCP_HTTP_PORT)
