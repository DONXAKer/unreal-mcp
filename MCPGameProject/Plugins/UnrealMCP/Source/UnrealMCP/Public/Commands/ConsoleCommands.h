#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for arbitrary Unreal Engine console-command execution from MCP.
 *
 * Allows external automation (Claude Code via MCP) to fire any console command
 * — `Automation RunTests`, `stat fps`, CVar tweaks (`r.<Var> <Value>`) — into
 * the running editor process and receive the GLog output produced while the
 * command was executing.
 *
 * Capture model: a temporary FOutputDevice is attached to GLog around the
 * GEngine->Exec() call. The Exec is synchronous as far as the command's own
 * dispatch is concerned; commands that schedule async work (Automation
 * RunTests being the canonical example) return immediately with only the
 * "scheduled" lines captured — subsequent test-result lines will appear in
 * later log frames and require a follow-up read mechanism.
 *
 * Commands routed here:
 *   execute_console_command — run any GEngine->Exec() input and capture GLog.
 */
class FUnrealMCPConsoleCommands
{
public:
    FUnrealMCPConsoleCommands();

    /**
     * Routes incoming console-related commands.
     * @param CommandType  execute_console_command
     * @param Params       JSON parameters (see per-handler docs)
     * @return JSON response
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Execute an arbitrary console command via GEngine->Exec, capturing GLog
     * lines emitted during the call.
     *
     * Params:
     *   cmd (required) — the command string, exactly as it would be typed
     *                    into the Output Log (e.g. "Automation RunTests WarCard").
     *
     * Returns:
     *   {
     *     success: bool,        — true if Exec returned true AND no internal error,
     *     command: str,         — echo of the input cmd,
     *     log_lines: [str],     — captured GLog output during Exec,
     *     log_truncated: bool,  — true if MaxLines cap was hit,
     *     lines_captured: int,  — Lines.Num() at return time,
     *     error?: str           — present only when something went wrong (GEngine null, exception)
     *   }
     */
    TSharedPtr<FJsonObject> HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params);
};
