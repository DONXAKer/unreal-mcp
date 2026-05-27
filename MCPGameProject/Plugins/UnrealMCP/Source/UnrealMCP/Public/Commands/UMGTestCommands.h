#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWidget;
class UUserWidget;
class UWorld;

/**
 * Handler class for UMG runtime test/automation commands (Playwright-style).
 *
 * Operates on live UUserWidget instances in the active PIE world — NOT on
 * editor-time WidgetBlueprint assets (that's UMGCommands). All commands here
 * require an active PIE session (GEditor->PlayWorld != nullptr).
 *
 * Commands routed here:
 *   find_widget            — fast "does widget X exist right now" probe
 *   wait_for_widget        — alias of find_widget; polling is done on Python side
 *   click_widget_by_name   — simulate a left-mouse click on a named widget
 *   get_widget_tree        — DOM-like snapshot of all active user widgets
 */
class FUMGTestCommands
{
public:
    FUMGTestCommands();

    /**
     * Routes incoming UMG-test commands.
     * @param CommandType  find_widget | wait_for_widget | click_widget_by_name | get_widget_tree
     * @param Params       JSON parameters (per-handler docs in .cpp)
     * @return JSON response
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Find a widget by name in the active PIE world.
     * Params:
     *   widget_name (required) — UWidget GetName() match (case-insensitive).
     *                            Matches both UUserWidget root and any child
     *                            inside any active UUserWidget tree.
     * Returns: { found, widget_name, widget_class, owner_user_widget }
     *
     * NOTE: this is a one-shot probe. For polling-with-timeout, the Python
     * wrapper repeats this call with asyncio.sleep between attempts.
     */
    TSharedPtr<FJsonObject> HandleFindWidget(const TSharedPtr<FJsonObject>& Params);

    /**
     * Simulate a left-mouse click on a named widget.
     * Algorithm:
     *   1) Find UWidget by name (same logic as HandleFindWidget).
     *   2) Get its cached geometry → compute screen-absolute center.
     *   3) Send Slate FPointerEvent (button down + up) via FSlateApplication.
     *
     * Params:
     *   widget_name (required)
     * Returns: { clicked, widget_name, screen_x, screen_y }
     */
    TSharedPtr<FJsonObject> HandleClickWidgetByName(const TSharedPtr<FJsonObject>& Params);

    /**
     * Serialize the hierarchy of every active UUserWidget in the PIE world to JSON.
     * Useful as a DOM snapshot for assertions in automated tests.
     *
     * Params: (none)
     * Returns: { user_widgets: [ { class, name, root: <tree> }, ... ] }
     */
    TSharedPtr<FJsonObject> HandleGetWidgetTree(const TSharedPtr<FJsonObject>& Params);

    /**
     * Walk every UUserWidget currently constructed against the PlayWorld and
     * locate a widget whose GetName() matches Target (case-insensitive).
     * Searches both the UUserWidget itself and all UWidgets in its WidgetTree.
     *
     * @param OwningPC  [opt, MCP-PLUGIN-005] если задан — UUserWidget,
     *                  у которого GetOwningPlayer() != OwningPC, пропускается.
     *                  Критично для split-screen multi-client PIE.
     * @return The first match, or nullptr.
     */
    static UWidget* FindWidgetByName(
        UWorld* PlayWorld,
        const FString& Target,
        UUserWidget*& OutOwner,
        class APlayerController* OwningPC = nullptr);

    /** Recursively build a JSON object describing a widget and its named children (for get_widget_tree). */
    static TSharedPtr<FJsonObject> BuildWidgetJson(UWidget* Widget);
};
