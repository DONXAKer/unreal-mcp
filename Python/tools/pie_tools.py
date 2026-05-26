"""
PIE / UMG Automation Tools for Unreal MCP.

Playwright-like e2e tooling: start/stop Play-In-Editor, poll for widgets,
simulate clicks/keys, take game-viewport screenshots.

All commands require the Unreal Editor to be running with the UnrealMCP plugin
loaded (port 55557). UMG-test commands additionally require an active PIE
session — start it with `pie_start` first.
"""

import asyncio
import logging
import time
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_pie_tools(mcp: FastMCP) -> None:
    """Register PIE + UMG automation tools with the MCP server."""
    mcp = wrap_with_envelope(mcp)

    # ─────────────────────────────────────────────────────────────────
    # PIE lifecycle
    # ─────────────────────────────────────────────────────────────────

    @mcp.tool()
    def pie_start(
        ctx: Context[Any, Any, Any],
        level_name: str = "",
        mode: str = "selected_viewport",
    ) -> dict[str, Any]:
        """Start a Play-In-Editor session.

        Args:
            level_name: Optional. Short name of the level to load before starting
                        (advisory only — the plugin currently does not auto-load;
                        call `load_level` first if you need a specific map).
            mode: "selected_viewport" (default) or "new_window". The mode is
                  advisory in the current implementation; PIE uses the project's
                  configured Editor Play Settings.

        Returns:
            { started: bool, mode: str, note: str }

        Note:
            PIE startup is async. Poll `pie_status` until `is_running == true`
            and `has_player_controller == true` before issuing input commands.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {"mode": mode}
        if level_name:
            params["level_name"] = level_name

        response = unreal.send_command("pie_start", params)
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def pie_stop(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """Stop the active Play-In-Editor session.

        Idempotent — succeeds whether or not PIE was running.

        Returns:
            { stopped: bool, was_running: bool }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("pie_stop", {})
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def pie_status(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """Query the current PIE state.

        Returns:
            {
              is_running: bool,
              world_name: str,
              elapsed_seconds: float,
              has_player_controller: bool,
              has_game_viewport: bool,
              current_level: str (if running),
            }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("pie_status", {})
        return response or {"status": "error", "error": "No response"}

    # ─────────────────────────────────────────────────────────────────
    # UMG test / automation
    # ─────────────────────────────────────────────────────────────────

    @mcp.tool()
    def find_widget(ctx: Context[Any, Any, Any], widget_name: str) -> dict[str, Any]:
        """One-shot probe: is a UMG widget with this name currently alive in PIE?

        Searches every active UUserWidget in the PIE world (both the root
        UUserWidget itself and every UWidget inside its WidgetTree).
        Comparison is case-insensitive against UWidget::GetName().

        Args:
            widget_name: Name of the widget to look for (e.g. "LoginButton",
                         "WBP_GameResult_C_0").

        Returns:
            { found: bool, widget_name, widget_class, owner_user_widget, visible }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("find_widget", {"widget_name": widget_name})
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    async def wait_for_widget(
        ctx: Context[Any, Any, Any],
        widget_name: str,
        timeout_ms: int = 5000,
        poll_interval_ms: int = 200,
    ) -> dict[str, Any]:
        """Poll until a UMG widget appears, or until timeout.

        Calls `find_widget` repeatedly with `poll_interval_ms` sleep between
        attempts. Returns as soon as the widget is found (or once total
        elapsed exceeds `timeout_ms`).

        Args:
            widget_name: Name of the widget to wait for.
            timeout_ms: Maximum time to wait, in milliseconds. Default 5000.
            poll_interval_ms: Sleep between probes. Default 200ms.

        Returns:
            { found: bool, elapsed_ms: int, attempts: int, last_response: dict }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        deadline = time.monotonic() + (timeout_ms / 1000.0)
        start = time.monotonic()
        attempts = 0
        last: dict[str, Any] = {}

        while True:
            attempts += 1
            response = unreal.send_command("find_widget", {"widget_name": widget_name}) or {}
            last = response

            # Plugin returns {"status":"success","result":{...,"found":bool}} or
            # {"status":"error", ...}. The envelope flattens — also handle a
            # raw {"found":...} reply.
            result = response.get("result") if isinstance(response, dict) else None
            found = False
            if isinstance(result, dict):
                found = bool(result.get("found"))
            elif isinstance(response, dict):
                found = bool(response.get("found"))

            if found:
                return {
                    "found": True,
                    "elapsed_ms": int((time.monotonic() - start) * 1000),
                    "attempts": attempts,
                    "last_response": last,
                }

            if time.monotonic() >= deadline:
                return {
                    "found": False,
                    "elapsed_ms": int((time.monotonic() - start) * 1000),
                    "attempts": attempts,
                    "last_response": last,
                }

            await asyncio.sleep(poll_interval_ms / 1000.0)

    @mcp.tool()
    def click_widget_by_name(ctx: Context[Any, Any, Any], widget_name: str) -> dict[str, Any]:
        """Simulate a left-mouse click on a named UMG widget in PIE.

        The plugin locates the widget by GetName(), reads its cached Slate
        geometry, computes the center in screen-absolute coordinates, and
        sends a left-mouse down+up event pair through FSlateApplication.

        Args:
            widget_name: Name of the widget to click.

        Returns:
            { clicked: bool, widget_name, widget_class, screen_x, screen_y,
              local_size_x, local_size_y }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("click_widget_by_name", {"widget_name": widget_name})
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def get_widget_tree(ctx: Context[Any, Any, Any]) -> dict[str, Any]:
        """DOM-like snapshot of every active UUserWidget in the PIE world.

        Returns:
            {
              user_widgets: [
                { name, class, is_in_viewport, root: <recursive tree> },
                ...
              ],
              count: int
            }

        Each tree node carries: name, class, visible, local_size_x/y, and
        for panel widgets a `children` array.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("get_widget_tree", {})
        return response or {"status": "error", "error": "No response"}

    # ─────────────────────────────────────────────────────────────────
    # Screenshot + input
    # ─────────────────────────────────────────────────────────────────

    @mcp.tool()
    def pie_screenshot(
        ctx: Context[Any, Any, Any],
        filename: str = "PIEScreenshot.png",
        show_ui: bool = True,
    ) -> dict[str, Any]:
        """Capture the PIE game viewport (NOT the editor viewport) to a PNG.

        Output goes to <ProjectSaved>/Screenshots/<filename>. Capture is async
        (queued for the next 1-2 ticks).

        Args:
            filename: Output filename (".png" appended if missing). Default
                      "PIEScreenshot.png".
            show_ui:  Include UMG overlays in the capture. Default True.

        Returns:
            { status, assetPath, filename, show_ui, source, note }
            `source` is "game_viewport" when PIE is active, or
            "fallback_editor_viewport" otherwise.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "pie_screenshot",
            {"filename": filename, "show_ui": show_ui},
        )
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def simulate_key(ctx: Context[Any, Any, Any], key: str) -> dict[str, Any]:
        """Simulate a key press+release on the first PIE PlayerController.

        Uses APlayerController::InputKey under the hood (Pressed then Released
        in the same call, single tick). For mouse buttons prefer
        `click_widget_by_name`.

        Args:
            key: Engine key name — e.g. "SpaceBar", "E", "Enter",
                 "LeftMouseButton" (see EKeys::* identifiers in UE source).

        Returns:
            { sent: bool, key: str, controller_name: str }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("simulate_key", {"key": key})
        return response or {"status": "error", "error": "No response"}

    logger.info("Registered PIE / UMG automation tools (pie_*, find_widget, wait_for_widget, click_widget_by_name, get_widget_tree, simulate_key)")
