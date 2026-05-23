"""
Unreal Engine MCP Server — HTTP/SSE transport variant.

Runs the same MCP server as unreal_mcp_server.py but listens on an HTTP port
instead of stdio. This allows AI-Workflow pipeline agents to connect via URL.

Environment variables:
  UNREAL_HOST   - host where Unreal Engine is running (default: host.docker.internal)
  UNREAL_PORT   - TCP port of the Unreal MCP bridge (default: 55557)
  MCP_HTTP_PORT - port to listen on for HTTP/SSE (default: 3001)
"""

import json
import logging
import os
import socket
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from typing import Any

from mcp.server.fastmcp import FastMCP
from mcp.server.transport_security import TransportSecuritySettings

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
    def __init__(self) -> None:
        self.socket: socket.socket | None = None
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

    def disconnect(self) -> None:
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        self.socket = None
        self.connected = False

    def receive_full_response(self, sock: socket.socket, buffer_size: int = 4096) -> bytes:
        chunks: list[bytes] = []
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
        except TimeoutError:
            if chunks:
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    return data
                except Exception:
                    pass
            raise RuntimeError("Timeout receiving Unreal response") from None

        # Unreachable: loop exits via break only when EOF + partial chunks, which
        # already raises above. Kept to satisfy strict mypy "missing return".
        return b"".join(chunks)

    def send_command(self, command: str, params: dict[str, Any] = None) -> dict[str, Any] | None:
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None
            self.connected = False

        if not self.connect():
            return {"status": "error", "error": f"Cannot connect to Unreal at {UNREAL_HOST}:{UNREAL_PORT}"}
        assert self.socket is not None  # connect() set it on success

        try:
            command_obj = {"type": command, "params": params or {}}
            command_json = json.dumps(command_obj)
            self.socket.sendall(command_json.encode('utf-8'))
            response_data = self.receive_full_response(self.socket)
            response: dict[str, Any] = json.loads(response_data.decode('utf-8'))

            if response.get("status") == "error":
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                if "error" not in response:
                    response["error"] = error_message
            elif response.get("success") is False:
                error_message = response.get("error") or response.get("message", "Unknown Unreal error")
                response = {"status": "error", "error": error_message}

            if self.socket is not None:
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
            if self.socket is not None:
                try:
                    self.socket.close()
                except Exception:
                    pass
            self.socket = None
            return {"status": "error", "error": str(e)}


_unreal_connection: UnrealConnection | None = None


def get_unreal_connection() -> UnrealConnection | None:
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
async def server_lifespan(server: FastMCP) -> AsyncIterator[dict[str, Any]]:
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
    instructions="Unreal Engine integration via Model Context Protocol (Streamable HTTP).",
    lifespan=server_lifespan,
    # DNS-rebinding защита блокировала AI-Workflow backend (Java контейнер),
    # который ходит сюда через host.docker.internal:3001. Разрешаем именно
    # этот хост + стандартные локальные.
    transport_security=TransportSecuritySettings(
        allowed_hosts=[
            "host.docker.internal:*",
            "127.0.0.1:*",
            "localhost:*",
            "[::1]:*",
        ],
        allowed_origins=[
            "http://host.docker.internal:*",
            "http://127.0.0.1:*",
            "http://localhost:*",
            "http://[::1]:*",
        ],
    ),
)

# Register the same tools as the stdio server
from tools.blueprint_tools import register_blueprint_tools
from tools.editor_tools import register_editor_tools
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
    # FastMCP exposes an ASGI app for Streamable HTTP transport (single POST
    # endpoint with session header) — what AI-Workflow's Java McpClient speaks.
    # The legacy SSE transport (GET /sse + POST /messages/?session_id=) is kept
    # as a fallback for older clients via mount_sse().
    try:
        app = mcp.streamable_http_app()
        logger.info("Streamable HTTP endpoint: POST %s", mcp.settings.streamable_http_path)
    except AttributeError:
        logger.warning("streamable_http_app() not available — falling back to sse_app()")
        app = mcp.sse_app()
    uvicorn.run(app, host="0.0.0.0", port=MCP_HTTP_PORT)
