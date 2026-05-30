#include "Commands/InputCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"

// Enhanced Input (UE5.7) — read-only inspection helpers.
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "EnhancedActionKeyMapping.h"

FInputCommands::FInputCommands()
{
}

TSharedPtr<FJsonObject> FInputCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_input_mapping"))
        return HandleCreateInputMapping(Params);
    if (CommandType == TEXT("input_action_get_info"))
        return HandleInputActionGetInfo(Params);
    if (CommandType == TEXT("input_mapping_context_get_info"))
        return HandleInputMappingContextGetInfo(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown input command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FInputCommands::HandleCreateInputMapping(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString ActionName;
    if (!Params->TryGetStringField(TEXT("action_name"), ActionName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));

    FString KeyName;
    if (!Params->TryGetStringField(TEXT("key"), KeyName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));

    // ── Optional params ──────────────────────────────────────────────────────
    // Accept "input_type" (Python contract) or "mapping_type" (alt naming).
    FString InputType = TEXT("Action");
    if (!Params->TryGetStringField(TEXT("input_type"), InputType))
    {
        Params->TryGetStringField(TEXT("mapping_type"), InputType);
    }

    double Scale = 1.0;
    Params->TryGetNumberField(TEXT("scale"), Scale);

    bool bShift = false, bCtrl = false, bAlt = false, bCmd = false;
    Params->TryGetBoolField(TEXT("shift"), bShift);
    Params->TryGetBoolField(TEXT("ctrl"),  bCtrl);
    Params->TryGetBoolField(TEXT("alt"),   bAlt);
    Params->TryGetBoolField(TEXT("cmd"),   bCmd);

    // ── Validate key against the engine's registered key list ────────────────
    const FKey Key(*KeyName);
    if (!Key.IsValid())
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown engine key: '%s'. See EKeys::* identifiers (e.g. 'SpaceBar', 'LeftMouseButton')"), *KeyName));

    UInputSettings* InputSettings = UInputSettings::GetInputSettings();
    if (!InputSettings)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to access UInputSettings"));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("action_name"), ActionName);
    Result->SetStringField(TEXT("key"), KeyName);

    if (InputType.Equals(TEXT("Axis"), ESearchCase::IgnoreCase))
    {
        FInputAxisKeyMapping AxisMapping;
        AxisMapping.AxisName = FName(*ActionName);
        AxisMapping.Key      = Key;
        AxisMapping.Scale    = static_cast<float>(Scale);

        // AddAxisMapping(bool bForceRebuildKeymaps=true). Internal dedup not guaranteed,
        // so remove any existing identical mapping first.
        InputSettings->RemoveAxisMapping(AxisMapping, /*bForceRebuildKeymaps=*/ false);
        InputSettings->AddAxisMapping(AxisMapping, /*bForceRebuildKeymaps=*/ true);

        Result->SetStringField(TEXT("mapping_type"), TEXT("Axis"));
        Result->SetNumberField(TEXT("scale"), Scale);
    }
    else
    {
        // Action mapping (default).
        FInputActionKeyMapping ActionMapping;
        ActionMapping.ActionName = FName(*ActionName);
        ActionMapping.Key        = Key;
        ActionMapping.bShift     = bShift;
        ActionMapping.bCtrl      = bCtrl;
        ActionMapping.bAlt       = bAlt;
        ActionMapping.bCmd       = bCmd;

        InputSettings->RemoveActionMapping(ActionMapping, /*bForceRebuildKeymaps=*/ false);
        InputSettings->AddActionMapping(ActionMapping, /*bForceRebuildKeymaps=*/ true);

        Result->SetStringField(TEXT("mapping_type"), TEXT("Action"));
        Result->SetBoolField(TEXT("shift"), bShift);
        Result->SetBoolField(TEXT("ctrl"),  bCtrl);
        Result->SetBoolField(TEXT("alt"),   bAlt);
        Result->SetBoolField(TEXT("cmd"),   bCmd);
    }

    // Persist to DefaultInput.ini so the mapping survives editor restart.
    InputSettings->SaveKeyMappings();

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enhanced Input — read-only inspection
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    /** EInputActionValueType → строка для JSON-ответа. */
    FString ValueTypeToString(EInputActionValueType ValueType)
    {
        switch (ValueType)
        {
        case EInputActionValueType::Boolean: return TEXT("Boolean");
        case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
        case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
        case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
        default:                             return TEXT("Unknown");
        }
    }

    /** Собрать массив имён классов UObject-инстансов (триггеры/модификаторы). */
    template <typename T>
    TArray<TSharedPtr<FJsonValue>> ClassNamesOf(const TArray<TObjectPtr<T>>& Items)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const TObjectPtr<T>& Item : Items)
        {
            if (Item && Item->GetClass())
            {
                Out.Add(MakeShared<FJsonValueString>(Item->GetClass()->GetName()));
            }
        }
        return Out;
    }
}

TSharedPtr<FJsonObject> FInputCommands::HandleInputActionGetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString ActionPath;
    if (!Params->TryGetStringField(TEXT("action_path"), ActionPath) || ActionPath.IsEmpty())
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_path' parameter"));

    UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (!Action)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("action_path"), ActionPath);
    Result->SetStringField(TEXT("value_type"), ValueTypeToString(Action->ValueType));
    Result->SetArrayField(TEXT("triggers"),  ClassNamesOf(Action->Triggers));
    Result->SetArrayField(TEXT("modifiers"), ClassNamesOf(Action->Modifiers));
    return Result;
}

TSharedPtr<FJsonObject> FInputCommands::HandleInputMappingContextGetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString ContextPath;
    if (!Params->TryGetStringField(TEXT("context_path"), ContextPath) || ContextPath.IsEmpty())
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'context_path' parameter"));

    UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, *ContextPath);
    if (!Context)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputMappingContext not found: %s"), *ContextPath));

    TArray<TSharedPtr<FJsonValue>> MappingsJson;
    for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());
        Entry->SetStringField(TEXT("action"),
            Mapping.Action ? Mapping.Action->GetPathName() : FString());
        Entry->SetArrayField(TEXT("triggers"),  ClassNamesOf(Mapping.Triggers));
        Entry->SetArrayField(TEXT("modifiers"), ClassNamesOf(Mapping.Modifiers));
        MappingsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("context_path"), ContextPath);
    Result->SetArrayField(TEXT("mappings"), MappingsJson);
    return Result;
}
