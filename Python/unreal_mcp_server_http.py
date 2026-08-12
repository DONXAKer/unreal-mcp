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
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from typing import Any

from mcp.server.fastmcp import FastMCP
from mcp.server.transport_security import TransportSecuritySettings

# FIX-MCP-003: UnrealConnection + timeout constants moved to
# unreal_connection.py, shared with unreal_mcp_server.py (used to be a
# byte-identical copy maintained by hand in both files). This server's own
# UNREAL_HOST default (host.docker.internal, for the Docker/pipeline case)
# differs from the stdio server's (127.0.0.1) — see unreal_connection.py's
# module docstring — so host/port are still resolved HERE and passed
# explicitly into UnrealConnection(host, port) below, rather than relying on
# the shared module's own default.
from unreal_connection import CONNECT_TIMEOUT, RECV_TIMEOUT, UnrealConnection

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler()],
)
logger = logging.getLogger("UnrealMCP-HTTP")

UNREAL_HOST = os.getenv("UNREAL_HOST", "host.docker.internal")
UNREAL_PORT = int(os.getenv("UNREAL_PORT", "55557"))
MCP_HTTP_PORT = int(os.getenv("MCP_HTTP_PORT", "3001"))

logger.info(
    "UnrealMCP HTTP: will connect to Unreal at %s:%d (recvTimeout=%.0fs, connectTimeout=%.0fs)",
    UNREAL_HOST, UNREAL_PORT, RECV_TIMEOUT, CONNECT_TIMEOUT,
)


_unreal_connection: UnrealConnection | None = None


def get_unreal_connection() -> UnrealConnection | None:
    global _unreal_connection
    try:
        if _unreal_connection is None:
            # Explicit host/port: this server's UNREAL_HOST default
            # (host.docker.internal) differs from UnrealConnection's own
            # bare-constructor default (127.0.0.1) — see the import comment
            # above.
            _unreal_connection = UnrealConnection(UNREAL_HOST, UNREAL_PORT)
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

# Register the same tools as the stdio server (full 13-module surface).
from tools.animation_tools import register_animation_tools
from tools.asset_tools import register_asset_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.data_asset_tools import register_data_asset_tools
from tools.editor_tools import register_editor_tools
from tools.graph_builder import register_graph_builder_tool
from tools.level_tools import register_level_tools
from tools.material_tools import register_material_tools
from tools.mesh_tools import register_mesh_tools
from tools.niagara_tools import register_niagara_tools
from tools.node_tools import register_blueprint_node_tools
from tools.project_tools import register_project_tools
from tools.texture_tools import register_texture_tools
from tools.umg_tools import register_umg_tools

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
