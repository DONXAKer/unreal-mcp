#include "Commands/InputCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"

FInputCommands::FInputCommands()
{
}

TSharedPtr<FJsonObject> FInputCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_input_mapping"))
        return HandleCreateInputMapping(Params);

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
