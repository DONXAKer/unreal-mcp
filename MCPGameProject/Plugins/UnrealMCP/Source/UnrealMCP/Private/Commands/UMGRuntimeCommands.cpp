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

#include "Components/Button.h"
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
    if (CommandType == TEXT("invoke_button_click"))
    {
        return HandleInvokeButtonClick(Params);
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

UWidget* FUMGRuntimeCommands::FindWidgetByName(
    UWorld* PlayWorld,
    const FString& Target,
    UUserWidget*& OutOwner,
    APlayerController* OwningPC)
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
        // MCP-PLUGIN-005: для split-screen PIE фильтруем по OwningPlayer.
        // Виджеты без явного owner (CreateWidget без указания PC) — принимаем
        // как "глобальные" для текущего клиента; пропускаем только если widget
        // явно принадлежит ДРУГОМУ PC (это и есть split-screen сценарий).
        if (OwningPC && UW->GetOwningPlayer() != nullptr && UW->GetOwningPlayer() != OwningPC)
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

    // Поиск виджета — фильтр по OwningPlayer для split-screen multi-client.
    UUserWidget* Owner = nullptr;
    UWidget* Found = FindWidgetByName(PlayWorld, WidgetName, Owner, PC);
    if (!Found)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
    }

    const FText TextFromString = FText::FromString(TextValue);
    FString ResolvedClass = Found->GetClass() ? Found->GetClass()->GetName() : TEXT("Unknown");

    // commit_method (опционально): "OnEnter" (default) / "Default" / "OnUserMovedFocus" / "OnCleared".
    // Это то, что получает Blueprint в delegate OnTextCommitted. По умолчанию OnEnter — типичный
    // flow логина / submit, чтобы Blueprint реагировал так же, как на реальный Enter.
    FString CommitMethodStr = TEXT("OnEnter");
    Params->TryGetStringField(TEXT("commit_method"), CommitMethodStr);
    ETextCommit::Type CommitMethod = ETextCommit::OnEnter;
    if (CommitMethodStr.Equals(TEXT("Default"), ESearchCase::IgnoreCase))               CommitMethod = ETextCommit::Default;
    else if (CommitMethodStr.Equals(TEXT("OnUserMovedFocus"), ESearchCase::IgnoreCase)) CommitMethod = ETextCommit::OnUserMovedFocus;
    else if (CommitMethodStr.Equals(TEXT("OnCleared"), ESearchCase::IgnoreCase))        CommitMethod = ETextCommit::OnCleared;

    // Запись в одну из 4 поддерживаемых UClass'ов. ВАЖНО: SetText() сам по себе НЕ
    // вызывает OnTextChanged / OnTextCommitted broadcast — Blueprint, который слушает
    // эти события (типичный паттерн «сохранить введённое в переменную»), останется в
    // неведении и при submit отправит старое/пустое значение. Поэтому после SetText
    // мы вручную broadcast'им оба события, эмулируя реальный пользовательский ввод.
    bool bSetText = false;
    if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Found))
    {
        ETB->SetText(TextFromString);
        ETB->OnTextChanged.Broadcast(TextFromString);
        ETB->OnTextCommitted.Broadcast(TextFromString, CommitMethod);
        bSetText = true;
    }
    else if (UEditableText* ET = Cast<UEditableText>(Found))
    {
        ET->SetText(TextFromString);
        ET->OnTextChanged.Broadcast(TextFromString);
        ET->OnTextCommitted.Broadcast(TextFromString, CommitMethod);
        bSetText = true;
    }
    else if (UMultiLineEditableTextBox* METB = Cast<UMultiLineEditableTextBox>(Found))
    {
        METB->SetText(TextFromString);
        METB->OnTextChanged.Broadcast(TextFromString);
        METB->OnTextCommitted.Broadcast(TextFromString, CommitMethod);
        bSetText = true;
    }
    else if (UMultiLineEditableText* MET = Cast<UMultiLineEditableText>(Found))
    {
        MET->SetText(TextFromString);
        MET->OnTextChanged.Broadcast(TextFromString);
        MET->OnTextCommitted.Broadcast(TextFromString, CommitMethod);
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

// ────────────────────────────── invoke_button_click ──────────────────────────────

TSharedPtr<FJsonObject> FUMGRuntimeCommands::HandleInvokeButtonClick(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
    }

    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    APlayerController* PC = ResolvePlayerController(ControllerIndex);
    if (!PC)
    {
        TSharedPtr<FJsonObject> Resp = FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("PlayerController not found for controller_index=%d"), ControllerIndex));
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("requested_index"), ControllerIndex);
        Details->SetNumberField(TEXT("numClients"), FUnrealMCPPIEUtils::GetNumPIEClients());
        Resp->SetObjectField(TEXT("details"), Details);
        return Resp;
    }

    UWorld* PlayWorld = FUnrealMCPPIEUtils::GetPIEWorldForClient(ControllerIndex);
    if (!PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world"));
    }

    UUserWidget* Owner = nullptr;
    UWidget* Found = FindWidgetByName(PlayWorld, WidgetName, Owner, PC);
    if (!Found)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' not found in any UUserWidget on controller %d"),
                *WidgetName, ControllerIndex));
    }

    UButton* Btn = Cast<UButton>(Found);
    if (!Btn)
    {
        TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
        Resp->SetBoolField(TEXT("success"), false);
        Resp->SetStringField(TEXT("error"),
            FString::Printf(TEXT("Widget '%s' is class '%s' — invoke_button_click supports only UButton"),
                *WidgetName, *Found->GetClass()->GetName()));
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("actualClass"), Found->GetClass()->GetName());
        Details->SetStringField(TEXT("widget_name"), WidgetName);
        Resp->SetObjectField(TEXT("details"), Details);
        return Resp;
    }

    if (!Btn->GetIsEnabled())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Button '%s' is disabled — OnClicked won't fire"), *WidgetName));
    }

    // Прямой broadcast делегата. Это путь, который UButton::SlateHandleClicked
    // вызывает после реального Slate-клика; здесь мы пропускаем Slate-слой,
    // что критично для UI-тестов без фокусированного PIE viewport.
    Btn->OnClicked.Broadcast();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("owner_user_widget"), Owner ? Owner->GetName() : TEXT(""));
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    Result->SetStringField(TEXT("controller_name"), PC->GetName());
    return Result;
}
