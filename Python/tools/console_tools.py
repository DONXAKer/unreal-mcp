"""
Console-command execution tools for Unreal MCP.

Allows Claude (or any MCP client) to fire arbitrary Unreal Engine console
commands into the running editor — `Automation RunTests <Filter>`, CVar
mutations (`r.<Var> <Value>`), diagnostic commands (`stat fps`), etc. —
without manually clicking the Output Log.

Capture model: a temporary FOutputDevice is attached to GLog around the
GEngine->Exec() call. The result is fire-and-return-captured-log-immediately:
async commands (Automation tests are the canonical example) will only return
their "scheduled" lines synchronously; later test-result lines must be read
through a separate mechanism (TBD task).

Requires the Unreal Editor to be running with the UnrealMCP plugin loaded
(port 55557).
"""

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_console_tools(mcp: FastMCP) -> None:
    """Register console-command execution tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def execute_console_command(
        ctx: Context[Any, Any, Any],
        cmd: str,
    ) -> dict[str, Any]:
        """Execute an arbitrary Unreal Engine console command in the running editor.

        Captures GLog output during the call and returns it as an array of
        formatted lines ("LogCategory: Verbosity: message"). Suitable for:

          - Automation RunTests <Filter>  — run C++ Automation Spec tests
                                            (async — returns only "scheduled" lines).
          - stat fps                      — diagnostics.
          - r.<CVar> <value>              — change console variables.

        Args:
            cmd: The command string, exactly as it would be typed into the
                 Output Log (e.g. "Automation RunTests WarCard").

        Returns:
            {
              success: bool,        — Exec() returned true AND no internal error,
              command: str,         — echo of the input cmd,
              log_lines: [str],     — captured GLog output during Exec (up to 500),
              log_truncated: bool,  — true if the 500-line cap was hit,
              lines_captured: int,  — len(log_lines),
              error?: str           — present only when something went wrong.
            }
        """
        from unreal_mcp_server import get_unreal_connection

        if not cmd or not cmd.strip():
            return {"success": False, "error": "Empty command"}

        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "error": "No Unreal connection"}

        response = unreal.send_command("execute_console_command", {"cmd": cmd})
        return response or {"success": False, "error": "No response"}

    logger.info("Registered console tools (execute_console_command)")
