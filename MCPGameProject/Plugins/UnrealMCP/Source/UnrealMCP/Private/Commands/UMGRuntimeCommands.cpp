// UMG runtime command implementations (MCP-PLUGIN-002).

#include "Commands/UMGRuntimeCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/PIEUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "UObject/UObjectIterator.h"

#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"

FUMGRuntimeCommands::FUMGRuntimeCommands()
{
}

TSharedPtr<FJsonObject> FUMGRuntimeCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("set_text_on_widget"))
    {
        return HandleSetTextOnWidget(Params);
    }
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown UMG-runtime command: %s"), *CommandType));
}

// ────────────────────────────── helpers ──────────────────────────────

APlayerController* FUMGRuntimeCommands::ResolvePlayerController(int32 ControllerIndex)
{
    // MCP-PLUGIN-003: реальный multi-client lookup через FUnrealMCPPIEUtils.
    return FUnrealMCPPIEUtils::GetPlayerControllerByIndex(ControllerIndex);
}

UWidget* FUMGRuntimeCommands::FindWidgetByName(UWorld* PlayWorld, const FString& Target, UUserWidget*& OutOwner)
{
    OutOwner = nullptr;
    if (!PlayWorld || Target.IsEmpty())
    {
        return nullptr;
    }

    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        UUserWidget* UW = *It;
        if (!IsValid(UW) || UW->GetWorld() != PlayWorld)
        {
            continue;
        }

        if (UW->GetName().Equals(Target, ESearchCase::IgnoreCase))
        {
            OutOwner = UW;
            return UW;
        }

        if (UWidgetTree* Tree = UW->WidgetTree)
        {
            UWidget* Found = nullptr;
            Tree->ForEachWidgetAndDescendants([&](UWidget* W)
            {
                if (Found) return;
                if (W && W->GetName().Equals(Target, ESearchCase::IgnoreCase))
                {
                    Found = W;
                }
            });
            if (Found)
            {
                OutOwner = UW;
                return Found;
            }
        }
    }
    return nullptr;
}

// ────────────────────────────── set_text_on_widget ──────────────────────────────

TSharedPtr<FJsonObject> FUMGRuntimeCommands::HandleSetTextOnWidget(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
    }

    FString TextValue;
    if (!Params->TryGetStringField(TEXT("text"), TextValue))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'text' parameter"));
    }

    // controller_index — пока используется только для consistency API; реальный
    // multi-client lookup появится в T003 (FUnrealMCPPIEUtils::GetPlayerControllerByIndex).
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    // PIE world должен быть активен.
    if (!GEditor || !GEditor->PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world — call pie_start first"));
    }

    // MCP-PLUGIN-003: для multi-client используем world выбранного клиента.
    UWorld* PlayWorld = FUnrealMCPPIEUtils::GetPIEWorldForClient(ControllerIndex);
    if (!PlayWorld)
    {
        PlayWorld = GEditor->PlayWorld;
    }

    APlayerController* PC = ResolvePlayerController(ControllerIndex);
    if (!PC)
    {
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("success"), false);
        Response->SetStringField(TEXT("error"),
            FString::Printf(TEXT("No PlayerController for controller_index=%d"), ControllerIndex));
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("requested_index"), ControllerIndex);
        Details->SetNumberField(TEXT("numClients"), FUnrealMCPPIEUtils::GetNumPIEClients());
        Response->SetObjectField(TEXT("details"), Details);
        return Response;
    }

    // Поиск виджета.
    UUserWidget* Owner = nullptr;
    UWidget* Found = FindWidgetByName(PlayWorld, WidgetName, Owner);
    if (!Found)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
    }

    const FText TextFromString = FText::FromString(TextValue);
    FString ResolvedClass = Found->GetClass() ? Found->GetClass()->GetName() : TEXT("Unknown");

    // Запись в одну из 4 поддерживаемых UClass'ов.
    bool bSetText = false;
    if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Found))
    {
        ETB->SetText(TextFromString);
        bSetText = true;
    }
    else if (UEditableText* ET = Cast<UEditableText>(Found))
    {
        ET->SetText(TextFromString);
        bSetText = true;
    }
    else if (UMultiLineEditableTextBox* METB = Cast<UMultiLineEditableTextBox>(Found))
    {
        METB->SetText(TextFromString);
        bSetText = true;
    }
    else if (UMultiLineEditableText* MET = Cast<UMultiLineEditableText>(Found))
    {
        MET->SetText(TextFromString);
        bSetText = true;
    }

    if (!bSetText)
    {
        // Не поддерживаемый класс — error с details.actualClass.
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetBoolField(TEXT("success"), false);
        Response->SetStringField(TEXT("error"),
            FString::Printf(TEXT("Widget '%s' is class '%s' — set_text_on_widget supports only EditableText/EditableTextBox/MultiLineEditableText/MultiLineEditableTextBox"),
                *WidgetName, *ResolvedClass));
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("actualClass"), ResolvedClass);
        Details->SetStringField(TEXT("widget_name"), WidgetName);
        Response->SetObjectField(TEXT("details"), Details);
        return Response;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_class"), ResolvedClass);
    Result->SetStringField(TEXT("owner_user_widget"), Owner ? Owner->GetName() : TEXT(""));
    Result->SetNumberField(TEXT("text_length"), TextValue.Len());
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    Result->SetStringField(TEXT("controller_name"), PC->GetName());
    return Result;
}
