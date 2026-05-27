// UE5.7 Enhanced Input via MCP (MCP-PLUGIN-004).
//
// Legacy команда create_input_mapping (UInputSettings::AddActionMapping/AddAxisMapping)
// остаётся как есть — для backward compat. Эти новые команды работают с современным
// Enhanced Input System (UInputAction + UInputMappingContext + K2Node_EnhancedInputAction).

#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UInputAction;
class UInputMappingContext;
class UInputModifier;
class UInputTrigger;
class UBlueprint;
class UEdGraph;

/**
 * Handler класс для Enhanced Input команд.
 *
 * Поддерживаемые команды:
 *   create_input_action                    — создать UInputAction DataAsset.
 *   create_input_mapping_context           — создать UInputMappingContext DataAsset.
 *   add_input_action_mapping               — добавить key→action mapping в IMC,
 *                                            опционально с modifiers/triggers.
 *   add_enhanced_input_action_event_node   — создать K2Node_EnhancedInputAction
 *                                            в Blueprint event graph; пины
 *                                            (Value, Elapsed Time, ...) появляются
 *                                            через AllocateDefaultPins.
 *
 * Все ассет-создающие команды идемпотентны:
 *   если ассет с тем же путём уже существует — возвращают status="skipped"
 *   вместо краша.
 */
class FEnhancedInputCommands
{
public:
    FEnhancedInputCommands();

    /**
     * Маршрутизатор incoming Enhanced Input команд.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * create_input_action.
     * Params:
     *   name        (required) — авто-префикс "IA_" если отсутствует.
     *   value_type  (required) — "Bool" | "Axis1D" | "Axis2D" | "Axis3D".
     *   path        (opt, default "/Game/Input/Actions") — папка для ассета.
     * Returns: { ok, assetPath, status: "created"|"skipped" }
     */
    TSharedPtr<FJsonObject> HandleCreateInputAction(const TSharedPtr<FJsonObject>& Params);

    /**
     * create_input_mapping_context.
     * Params:
     *   name (required) — авто-префикс "IMC_" если отсутствует.
     *   path (opt, default "/Game/Input/Contexts").
     * Returns: { ok, assetPath, status: "created"|"skipped" }
     */
    TSharedPtr<FJsonObject> HandleCreateInputMappingContext(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_input_action_mapping.
     * Params:
     *   context_path (required) — путь к IMC.
     *   action_path  (required) — путь к IA.
     *   key          (required) — engine key string ("W", "LeftMouseButton", ...).
     *   modifiers    (opt) — список встроенных UInputModifier-имён ("Negate",
     *                       "DeadZone", "Scalar", "Swizzle.YXZ" и т.п.).
     *   triggers     (opt) — список встроенных UInputTrigger-имён ("Pressed",
     *                       "Released", "Down", "Hold", "Tap").
     * Returns: { ok, mapping_index, key, modifiers_applied, triggers_applied }.
     */
    TSharedPtr<FJsonObject> HandleAddInputActionMapping(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_enhanced_input_action_event_node.
     * Params:
     *   blueprint_path (required) — путь к BP (Pawn/PlayerController/Actor).
     *   action_path    (required) — путь к IA.
     *   trigger_event  (opt, default "Triggered") — pin to highlight
     *                                              ("Started"|"Triggered"|"Completed"|"Canceled"|"Ongoing").
     *   location       (opt, default [0,0]) — позиция узла.
     * Returns: { ok, node_id, pins:[{name, direction}, ...] }.
     *
     * Использует FUnrealMCPPinResolver (T001) — собирает pins[] из ноды
     * после AllocateDefaultPins + ReconstructNode (Value, Elapsed Time, ...).
     */
    TSharedPtr<FJsonObject> HandleAddEnhancedInputActionEventNode(const TSharedPtr<FJsonObject>& Params);

    // ─────── helpers ───────

    /** Резолв value_type строки → EInputActionValueType. */
    static bool ParseValueType(const FString& Str, uint8& OutValueType);

    /** Создать UInputModifier по имени (из встроенных). */
    static UInputModifier* CreateModifierByName(UObject* Outer, const FString& Name);

    /** Создать UInputTrigger по имени (из встроенных). */
    static UInputTrigger* CreateTriggerByName(UObject* Outer, const FString& Name);

    /** Найти и загрузить ассет (любой UObject) по полному пути; nullptr если нет. */
    static UObject* LoadAssetByPath(const FString& Path);

    /** Сохранить ассет в пакет (MarkPackageDirty + SaveAsset). */
    static void SaveAssetPackage(UObject* Asset);
};
