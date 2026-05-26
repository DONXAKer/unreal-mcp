#include "Commands/UMGTestCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "UObject/UObjectIterator.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

FUMGTestCommands::FUMGTestCommands()
{
}

TSharedPtr<FJsonObject> FUMGTestCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("find_widget") || CommandType == TEXT("wait_for_widget"))
        return HandleFindWidget(Params);
    if (CommandType == TEXT("click_widget_by_name"))
        return HandleClickWidgetByName(Params);
    if (CommandType == TEXT("get_widget_tree"))
        return HandleGetWidgetTree(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown UMG-test command: %s"), *CommandType));
}

/* ────────────────────────────── helpers ────────────────────────────── */

UWidget* FUMGTestCommands::FindWidgetByName(UWorld* PlayWorld, const FString& Target, UUserWidget*& OutOwner)
{
    OutOwner = nullptr;
    if (!PlayWorld || Target.IsEmpty())
    {
        return nullptr;
    }

    // Итерируем все живые UUserWidget'ы; фильтруем по World.
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        UUserWidget* UW = *It;
        if (!IsValid(UW) || UW->GetWorld() != PlayWorld)
        {
            continue;
        }

        // Сам root UUserWidget тоже может совпадать по имени.
        if (UW->GetName().Equals(Target, ESearchCase::IgnoreCase))
        {
            OutOwner = UW;
            return UW;
        }

        // Идём по WidgetTree владельца, включая глубоко вложенных детей.
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

TSharedPtr<FJsonObject> FUMGTestCommands::BuildWidgetJson(UWidget* Widget)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    if (!Widget)
    {
        Json->SetStringField(TEXT("error"), TEXT("null widget"));
        return Json;
    }

    Json->SetStringField(TEXT("name"), Widget->GetName());
    Json->SetStringField(TEXT("class"), Widget->GetClass() ? Widget->GetClass()->GetName() : TEXT("Unknown"));
    Json->SetBoolField(TEXT("visible"), Widget->IsVisible());

    // Геометрия (cached) — для отладки.
    const FGeometry Geom = Widget->GetCachedGeometry();
    Json->SetNumberField(TEXT("local_size_x"), Geom.GetLocalSize().X);
    Json->SetNumberField(TEXT("local_size_y"), Geom.GetLocalSize().Y);

    // Рекурсивно — дети, если это панель.
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        TArray<TSharedPtr<FJsonValue>> Children;
        const int32 Count = Panel->GetChildrenCount();
        for (int32 i = 0; i < Count; ++i)
        {
            UWidget* Child = Panel->GetChildAt(i);
            if (Child)
            {
                Children.Add(MakeShared<FJsonValueObject>(BuildWidgetJson(Child)));
            }
        }
        Json->SetArrayField(TEXT("children"), Children);
    }

    return Json;
}

/* ────────────────────────────── handlers ───────────────────────────── */

TSharedPtr<FJsonObject> FUMGTestCommands::HandleFindWidget(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
    }

    if (!GEditor || !GEditor->PlayWorld)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("found"), false);
        Result->SetStringField(TEXT("widget_name"), WidgetName);
        Result->SetStringField(TEXT("note"), TEXT("No active PIE world — call pie_start first"));
        return Result;
    }

    UUserWidget* Owner = nullptr;
    UWidget* Found = FindWidgetByName(GEditor->PlayWorld, WidgetName, Owner);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("found"), Found != nullptr);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    if (Found)
    {
        Result->SetStringField(TEXT("widget_class"), Found->GetClass() ? Found->GetClass()->GetName() : TEXT(""));
        Result->SetStringField(TEXT("owner_user_widget"), Owner ? Owner->GetName() : TEXT(""));
        Result->SetBoolField(TEXT("visible"), Found->IsVisible());
    }
    return Result;
}

TSharedPtr<FJsonObject> FUMGTestCommands::HandleClickWidgetByName(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
    }

    if (!GEditor || !GEditor->PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world — call pie_start first"));
    }

    UUserWidget* Owner = nullptr;
    UWidget* Target = FindWidgetByName(GEditor->PlayWorld, WidgetName, Owner);
    if (!Target)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
    }

    // Получаем cached geometry и считаем центр в абсолютных (screen) координатах.
    const FGeometry Geom = Target->GetCachedGeometry();
    const FVector2D LocalCenter = Geom.GetLocalSize() * 0.5f;
    const FVector2D AbsoluteCenter = Geom.LocalToAbsolute(LocalCenter);

    if (!FSlateApplication::IsInitialized())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FSlateApplication not initialized"));
    }

    FSlateApplication& SlateApp = FSlateApplication::Get();

    // Собираем FPointerEvent для левой кнопки мыши.
    // PointerIndex 0 = primary mouse, ButtonIndex = FKey::LeftMouseButton.
    TSet<FKey> EmptyPressed;
    const FModifierKeysState ModifierKeys;  // default = no modifiers

    // Двигаем курсор в нужную точку (визуально и логически).
    SlateApp.SetCursorPos(AbsoluteCenter);

    // Mouse Down. PlatformWindow=nullptr (Slate сам найдёт окно под курсором).
    {
        FPointerEvent MouseDown(
            0,                              // PointerIndex
            AbsoluteCenter,                 // ScreenSpacePosition
            AbsoluteCenter,                 // LastScreenSpacePosition
            EmptyPressed,                   // PressedButtons
            EKeys::LeftMouseButton,         // EffectingButton
            0.0f,                           // WheelDelta
            ModifierKeys
        );
        SlateApp.ProcessMouseButtonDownEvent(TSharedPtr<FGenericWindow>(), MouseDown);
    }

    // Mouse Up.
    {
        FPointerEvent MouseUp(
            0,
            AbsoluteCenter,
            AbsoluteCenter,
            EmptyPressed,
            EKeys::LeftMouseButton,
            0.0f,
            ModifierKeys
        );
        SlateApp.ProcessMouseButtonUpEvent(MouseUp);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("clicked"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_class"), Target->GetClass() ? Target->GetClass()->GetName() : TEXT(""));
    Result->SetNumberField(TEXT("screen_x"), AbsoluteCenter.X);
    Result->SetNumberField(TEXT("screen_y"), AbsoluteCenter.Y);
    Result->SetNumberField(TEXT("local_size_x"), Geom.GetLocalSize().X);
    Result->SetNumberField(TEXT("local_size_y"), Geom.GetLocalSize().Y);
    return Result;
}

TSharedPtr<FJsonObject> FUMGTestCommands::HandleGetWidgetTree(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> WidgetsArr;

    if (!GEditor || !GEditor->PlayWorld)
    {
        Result->SetArrayField(TEXT("user_widgets"), WidgetsArr);
        Result->SetStringField(TEXT("note"), TEXT("No active PIE world"));
        return Result;
    }

    UWorld* PlayWorld = GEditor->PlayWorld;
    for (TObjectIterator<UUserWidget> It; It; ++It)
    {
        UUserWidget* UW = *It;
        if (!IsValid(UW) || UW->GetWorld() != PlayWorld)
        {
            continue;
        }

        TSharedPtr<FJsonObject> UWJson = MakeShared<FJsonObject>();
        UWJson->SetStringField(TEXT("name"), UW->GetName());
        UWJson->SetStringField(TEXT("class"), UW->GetClass() ? UW->GetClass()->GetName() : TEXT(""));
        UWJson->SetBoolField(TEXT("is_in_viewport"), UW->IsInViewport());

        if (UW->WidgetTree && UW->WidgetTree->RootWidget)
        {
            UWJson->SetObjectField(TEXT("root"), BuildWidgetJson(UW->WidgetTree->RootWidget));
        }
        WidgetsArr.Add(MakeShared<FJsonValueObject>(UWJson));
    }

    Result->SetArrayField(TEXT("user_widgets"), WidgetsArr);
    Result->SetNumberField(TEXT("count"), WidgetsArr.Num());
    return Result;
}
