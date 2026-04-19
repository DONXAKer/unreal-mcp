#pragma once

#include "CoreMinimal.h"
#include "Json.h"

// Forward declarations — full includes are in the .cpp
class UWidget;

/**
 * Handler class for UMG Widget Blueprint commands.
 * Provides programmatic control over Widget Blueprint designer hierarchy —
 * creating widgets, setting their properties, and reading the tree structure.
 */
class FUnrealMCPUMGCommands
{
public:
    FUnrealMCPUMGCommands();

    /**
     * Routes an incoming UMG command to the appropriate handler.
     * @param CommandType  Command name (add_widget_to_umg, add_text_block_to_widget,
     *                     add_button_to_widget, set_widget_property, get_umg_hierarchy)
     * @param Params       JSON parameters
     * @return JSON response with result or error
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Add a widget to a Widget Blueprint's UMG Designer hierarchy.
     * Params: blueprint_path, widget_type, widget_name, parent_name (opt), is_variable (opt, default true)
     */
    TSharedPtr<FJsonObject> HandleAddWidgetToUMG(const TSharedPtr<FJsonObject>& Params);

    /**
     * Add a UTextBlock to a Widget Blueprint's root canvas panel.
     * Params: widget_name (short Widget Blueprint name), text_block_name,
     *         text (opt), position [X,Y], size [W,H], font_size (int), color [R,G,B,A].
     * The TextBlock is named so it matches a UPROPERTY(meta=(BindWidget)) field
     * in the parent C++ class, enabling auto-binding at runtime.
     */
    TSharedPtr<FJsonObject> HandleAddTextBlockToWidget(const TSharedPtr<FJsonObject>& Params);

    /**
     * Add a UButton with a child UTextBlock label to a Widget Blueprint's root canvas panel.
     * Params: widget_name, button_name, text (opt), position, size,
     *         font_size, color (text), background_color (button bg).
     * The Button is named so it matches a UPROPERTY(meta=(BindWidget)) field.
     */
    TSharedPtr<FJsonObject> HandleAddButtonToWidget(const TSharedPtr<FJsonObject>& Params);

    /**
     * Set a property on a widget inside a Widget Blueprint.
     * Supports explicit properties (Text, FontSize, Size, etc.) and
     * reflection-based fallback for simple float/bool/int fields.
     * Params: blueprint_path, widget_name, property_name, property_value
     */
    TSharedPtr<FJsonObject> HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params);

    /**
     * Get the widget hierarchy of a Widget Blueprint as a JSON tree.
     * Params: blueprint_path
     */
    TSharedPtr<FJsonObject> HandleGetUMGHierarchy(const TSharedPtr<FJsonObject>& Params);

    /** Maps a widget type name string to the corresponding UClass. Returns nullptr if unknown. */
    static UClass* GetWidgetClassByType(const FString& WidgetType);

    /** Recursively builds a JSON object describing a widget and its children. */
    static TSharedPtr<FJsonObject> BuildWidgetJson(UWidget* Widget);
};
