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
        num_clients: int = 1,
        dedicated_server: bool = False,
        window_width: int = 0,
        window_height: int = 0,
    ) -> dict[str, Any]:
        """Start a Play-In-Editor session.

        Args:
            level_name: Optional. Short name of the level to load before starting
                        (advisory only — the plugin currently does not auto-load;
                        call `load_level` first if you need a specific map).
            mode: "selected_viewport" (default) or "new_window".
                  - "selected_viewport": PIE renders inside the editor level
                    viewport. In a backgrounded/non-realtime editor this viewport
                    does not present a real frame → screenshots can be black.
                  - "new_window" (3.3.0): PIE launches in a separate presented
                    floating game window (same editor process — MCP keeps its TCP
                    listener). Its backbuffer holds a live scene+UMG frame, so
                    `pie_screenshot` captures a non-black frame in automated runs.
            window_width: Optional (3.3.0). Floating window width in px for
                          mode="new_window". 0 → plugin default 1280. Ignored for
                          selected_viewport.
            window_height: Optional (3.3.0). Floating window height in px for
                           mode="new_window". 0 → plugin default 720. Ignored for
                           selected_viewport.
            num_clients: MCP-PLUGIN-003 — number of PIE clients to spawn
                         (1..8). Default 1 (legacy behaviour). Each client gets
                         its own UWorld / PlayerController. Use `controller_index`
                         in subsequent commands to target a specific one.
            dedicated_server: If True, spawn a separate dedicated server in
                              addition to the clients. Default False (listen-server).

        Returns:
            { started, mode, num_clients, dedicated_server, clients: [...], note }

        Note:
            PIE startup is async. Poll `pie_status` until `is_running == true`
            and `num_clients` clients actually appear in the `clients[]` array.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {
            "mode": mode,
            "num_clients": num_clients,
            "dedicated_server": dedicated_server,
        }
        if level_name:
            params["level_name"] = level_name
        # 3.3.0: размер floating-окна — шлём только если задан и режим new_window.
        if window_width > 0:
            params["window_width"] = window_width
        if window_height > 0:
            params["window_height"] = window_height

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
    def find_widget(
        ctx: Context[Any, Any, Any],
        widget_name: str,
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """One-shot probe: is a UMG widget with this name currently alive in PIE?

        Searches every active UUserWidget in the PIE world (both the root
        UUserWidget itself and every UWidget inside its WidgetTree).
        Comparison is case-insensitive against UWidget::GetName().

        Args:
            widget_name: Name of the widget to look for (e.g. "LoginButton",
                         "WBP_GameResult_C_0").
            controller_index: MCP-PLUGIN-003 — which PIE client to search.
                              Default 0. Multi-client PIE has one UWorld per
                              client; setting this filters the iteration.

        Returns:
            { found: bool, widget_name, widget_class, owner_user_widget, visible }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "find_widget",
            {"widget_name": widget_name, "controller_index": controller_index},
        )
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    async def wait_for_widget(
        ctx: Context[Any, Any, Any],
        widget_name: str,
        timeout_ms: int = 5000,
        poll_interval_ms: int = 200,
        controller_index: int = 0,
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
            response = unreal.send_command(
                "find_widget",
                {"widget_name": widget_name, "controller_index": controller_index},
            ) or {}
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
    def click_widget_by_name(
        ctx: Context[Any, Any, Any],
        widget_name: str,
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Simulate a left-mouse click on a named UMG widget in PIE.

        The plugin locates the widget by GetName(), reads its cached Slate
        geometry, computes the center in screen-absolute coordinates, and
        sends a left-mouse down+up event pair through FSlateApplication.

        Args:
            widget_name: Name of the widget to click.
            controller_index: MCP-PLUGIN-003 — restrict widget search to the
                              UWorld of the N-th PIE client. Default 0.

        Returns:
            { clicked: bool, widget_name, widget_class, screen_x, screen_y,
              local_size_x, local_size_y }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "click_widget_by_name",
            {"widget_name": widget_name, "controller_index": controller_index},
        )
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def get_widget_tree(
        ctx: Context[Any, Any, Any],
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """DOM-like snapshot of every active UUserWidget in the PIE world.

        Args:
            controller_index: MCP-PLUGIN-003 — restrict to the UWorld of the
                              N-th PIE client. Default 0.

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

        response = unreal.send_command(
            "get_widget_tree",
            {"controller_index": controller_index},
        )
        return response or {"status": "error", "error": "No response"}

    # ─────────────────────────────────────────────────────────────────
    # Screenshot + input
    # ─────────────────────────────────────────────────────────────────

    @mcp.tool()
    def pie_screenshot(
        ctx: Context[Any, Any, Any],
        filename: str = "PIEScreenshot.png",
        show_ui: bool = True,
        controller_index: int | None = None,
    ) -> dict[str, Any]:
        """Capture the PIE game viewport (NOT the editor viewport) to a PNG.

        Output goes to <ProjectSaved>/Screenshots/<filename>. Capture is async
        (queued for the next 1-2 ticks).

        Args:
            filename: Output filename (".png" appended if missing). Default
                      "PIEScreenshot.png".
            show_ui:  Include UMG overlays in the capture. Default True.
            controller_index: MCP-PLUGIN-003 — which PIE client's viewport to
                              capture. When set, suffix `_clientN` is inserted
                              before extension (foo.png → foo_client1.png).
                              Default None (legacy: capture first PIE world,
                              no suffix).

        Returns:
            { status, assetPath, filename, show_ui, source, note }
            `source` is "game_viewport" when PIE is active, or
            "fallback_editor_viewport" otherwise.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {"filename": filename, "show_ui": show_ui}
        if controller_index is not None:
            params["controller_index"] = controller_index

        response = unreal.send_command("pie_screenshot", params)
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def tick_world(
        ctx: Context[Any, Any, Any],
        num_ticks: int = 1,
        delta_seconds: float = 1.0 / 60.0,
    ) -> dict[str, Any]:
        """Advance the PIE world simulation by N synchronous ticks.

        Useful for deterministic e2e tests that should not depend on real
        wall-clock time — e.g. step-by-step state machines, animation
        progression, timer expiration. Calls World->Tick(LEVELTICK_All,
        delta_seconds) on `GEditor->PlayWorld` for `num_ticks` iterations
        (plugin clamps to 1..1000).

        Args:
            num_ticks: How many ticks to advance. Default 1.
            delta_seconds: Fake DeltaTime per tick (seconds). Default 1/60.

        Returns:
            { ticked, delta_seconds, total_delta, world_time_before, world_time_after }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "tick_world",
            {"num_ticks": num_ticks, "delta_seconds": delta_seconds},
        )
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    async def wait_for_condition(
        ctx: Context[Any, Any, Any],
        command: str,
        params: dict[str, Any] | None = None,
        predicate_path: str = "found",
        timeout_ms: int = 5000,
        poll_interval_ms: int = 200,
    ) -> dict[str, Any]:
        """Generic poller — call any MCP command in a loop until a field becomes truthy.

        Sends `command` with `params` repeatedly, sleeping `poll_interval_ms`
        between attempts. Returns as soon as the value at `predicate_path`
        evaluates truthy (or once total elapsed exceeds `timeout_ms`).

        `predicate_path` is a dotted path into the response — e.g. "found",
        "is_running", "result.has_player_controller". The poller looks both
        in the raw response and inside a "result" wrapper (envelope).

        Args:
            command: MCP command name to invoke each poll, e.g. "pie_status".
            params: Params dict for the command. Default {}.
            predicate_path: Dotted path that must become truthy. Default "found".
            timeout_ms: Maximum total wait, in milliseconds. Default 5000.
            poll_interval_ms: Sleep between probes. Default 200ms.

        Returns:
            { satisfied: bool, elapsed_ms: int, attempts: int,
              last_response: dict, predicate_path: str }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params = params or {}
        path_parts = [p for p in predicate_path.split(".") if p]

        def _resolve(obj: Any, parts: list[str]) -> Any:
            cur: Any = obj
            for part in parts:
                if isinstance(cur, dict) and part in cur:
                    cur = cur[part]
                else:
                    return None
            return cur

        def _is_truthy(resp: Any) -> bool:
            # Try raw response first, then unwrapped envelope under "result".
            val = _resolve(resp, path_parts)
            if val is None and isinstance(resp, dict) and "result" in resp:
                val = _resolve(resp.get("result"), path_parts)
            return bool(val)

        deadline = time.monotonic() + (timeout_ms / 1000.0)
        start = time.monotonic()
        attempts = 0
        last: dict[str, Any] = {}

        while True:
            attempts += 1
            response = unreal.send_command(command, params) or {}
            last = response

            if _is_truthy(response):
                return {
                    "satisfied": True,
                    "elapsed_ms": int((time.monotonic() - start) * 1000),
                    "attempts": attempts,
                    "last_response": last,
                    "predicate_path": predicate_path,
                }

            if time.monotonic() >= deadline:
                return {
                    "satisfied": False,
                    "elapsed_ms": int((time.monotonic() - start) * 1000),
                    "attempts": attempts,
                    "last_response": last,
                    "predicate_path": predicate_path,
                }

            await asyncio.sleep(poll_interval_ms / 1000.0)

    @mcp.tool()
    def simulate_key(
        ctx: Context[Any, Any, Any],
        key: str,
        controller_index: int = 0,
        action: str = "press",
    ) -> dict[str, Any]:
        """Simulate a key on a PIE PlayerController via APlayerController::InputKey.

        Args:
            key: Engine key name — e.g. "SpaceBar", "E", "Enter",
                 "LeftMouseButton" (see EKeys::* identifiers in UE source).
            controller_index: MCP-PLUGIN-003 — which PIE client receives the
                              key. Default 0 (legacy behaviour: first PC).
            action: "press" (Pressed+Released в одном тике, дефолт), "down"
                    (только нажатие — удержание) или "up" (только отпускание).
                    Для Enhanced Input (экшен без явного триггера → событие
                    Triggered нужно удержание ≥1 тик) тестируй так:
                    simulate_key(key, action="down") → tick'и реального времени →
                    simulate_key(key, action="up").

        Returns:
            { sent: bool, key: str, action: str, controller_index: int, controller_name: str }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "simulate_key",
            {"key": key, "controller_index": controller_index, "action": action},
        )
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def screen_click(
        ctx: Context[Any, Any, Any],
        x: float,
        y: float,
        button: str = "Left",
        controller_index: int = 0,
        normalized: bool = False,
        action: str = "click",
    ) -> dict[str, Any]:
        """Inject a REAL mouse click at PIE viewport pixel coords through Slate.

        Unlike `simulate_key("LeftMouseButton")` (which injects at
        APlayerController::InputKey and BYPASSES Slate hit-testing), this sends
        ProcessMouseButtonDownEvent/UpEvent through FSlateApplication — exactly
        like a physical mouse. So `SelfHitTestInvisible` widgets pass the click
        to the world (e.g. a grid cell), while buttons intercept it. Use this to
        test UI hit-test / click pass-through, and to click dynamically created
        widgets (NewObject, not BindWidget) that report 0,0 geometry in
        `click_widget_by_name`.

        Coordinates are relative to the PIE game viewport's top-left. The plugin
        converts them to absolute desktop coords using the viewport window
        geometry (window position + DPI scale).

        Args:
            x: Viewport-relative X in pixels (or 0..1 fraction if normalized).
            y: Viewport-relative Y in pixels (or 0..1 fraction if normalized).
            button: "Left" | "Right" | "Middle". Default "Left".
            controller_index: Which PIE client window (multi-PIE). Default 0.
            normalized: If True, x/y are 0..1 fractions of the viewport size
                        (e.g. center = 0.5, 0.5). Default False.
            action: "click" (down+up, default), "down" (press only) or "up"
                    (release only). To drive WORLD clicks через Enhanced Input
                    (ETriggerEvent::Triggered нужен зажатый хотя бы 1 тик):
                    screen_click(..., action="down") → tick_world(2) →
                    screen_click(..., action="up"). Для UMG-кнопок хватает
                    "click".

        Examples:
            screen_click(640, 360)                       # pixel click, left button
            screen_click(0.5, 0.5, normalized=True)      # exact viewport center
            screen_click(100, 50, button="Right")        # right-click near top-left
            screen_click(0.6, 0.5, normalized=True, action="down")  # world press

        Returns:
            { ok: bool, button: str, screen_x: float, screen_y: float,
              abs_x: float, abs_y: float, controller_index: int }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command(
            "screen_click",
            {
                "x": x,
                "y": y,
                "button": button,
                "controller_index": controller_index,
                "normalized": normalized,
                "action": action,
            },
        )
        return response or {"status": "error", "error": "No response"}

    logger.info("Registered PIE / UMG automation tools (pie_*, find_widget, wait_for_widget, click_widget_by_name, get_widget_tree, simulate_key, screen_click, tick_world, wait_for_condition)")
