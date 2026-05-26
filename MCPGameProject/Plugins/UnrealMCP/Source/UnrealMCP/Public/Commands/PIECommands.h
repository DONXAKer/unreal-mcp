#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Play-In-Editor lifecycle commands.
 *
 * Allows external automation (Claude Code via MCP) to start/stop a PIE session
 * and query its status. Used as the entry point for e2e gameplay tests
 * (Playwright-like flow against UMG + game systems).
 *
 * Commands routed here:
 *   pie_start  — start a new Play-In-Editor session
 *   pie_stop   — request end of the currently active PIE session
 *   pie_status — return { is_running, world_name, elapsed_seconds }
 */
class FPIECommands
{
public:
    FPIECommands();

    /**
     * Routes incoming PIE-lifecycle commands.
     * @param CommandType  pie_start | pie_stop | pie_status
     * @param Params       JSON parameters (most are optional, see per-handler docs)
     * @return JSON response
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Start a PIE session via GEditor->RequestPlaySession(FRequestPlaySessionParams).
     *
     * Params:
     *   level_name (opt) — short level name to override the editor's loaded map
     *                      before starting PIE. Currently advisory only — the
     *                      command logs it but does not auto-load the level
     *                      (load_level should be called first if needed).
     *   mode (opt, default "selected_viewport") — "selected_viewport" | "new_window"
     *
     * Returns: { started: bool, world_started: str, note: str }
     * NB: PIE startup is async — the actual world is created on the next tick(s).
     */
    TSharedPtr<FJsonObject> HandlePieStart(const TSharedPtr<FJsonObject>& Params);

    /**
     * End the active PIE session via GEditor->RequestEndPlayMap().
     * Idempotent — succeeds even if no PIE is running.
     *
     * Returns: { stopped: bool, was_running: bool }
     */
    TSharedPtr<FJsonObject> HandlePieStop(const TSharedPtr<FJsonObject>& Params);

    /**
     * Query current PIE state.
     *
     * Returns: { is_running, world_name, elapsed_seconds, has_player_controller }
     * elapsed_seconds is World->GetTimeSeconds() of the PIE world.
     */
    TSharedPtr<FJsonObject> HandlePieStatus(const TSharedPtr<FJsonObject>& Params);

    /**
     * Take a screenshot of the PIE game viewport (NOT the editor viewport).
     *
     * Uses UGameViewportClient::TakeHighResScreenShot() when GameViewport is
     * available — that captures the PIE rendering surface, not the editor UI.
     * Falls back to FScreenshotRequest::RequestScreenshot in case the game
     * viewport isn't ready.
     *
     * Params:
     *   filename (opt, default "PIEScreenshot.png") — basename inside Saved/Screenshots/.
     *   show_ui  (opt, default true) — capture UMG overlays as well.
     * Returns: { status, assetPath, filename, note }
     */
    TSharedPtr<FJsonObject> HandlePieScreenshot(const TSharedPtr<FJsonObject>& Params);

    /**
     * Simulate a keyboard key press+release on the first PlayerController of the PIE world.
     * Calls APlayerController::InputKey for IE_Pressed and IE_Released.
     *
     * Params:
     *   key (required) — engine key name (e.g. "SpaceBar", "E", "LeftMouseButton").
     * Returns: { sent, key, controller_name }
     */
    TSharedPtr<FJsonObject> HandleSimulateKey(const TSharedPtr<FJsonObject>& Params);
};
