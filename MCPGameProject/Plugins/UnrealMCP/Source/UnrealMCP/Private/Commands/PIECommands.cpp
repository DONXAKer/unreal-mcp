#include "Commands/PIECommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"

FPIECommands::FPIECommands()
{
}

TSharedPtr<FJsonObject> FPIECommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("pie_start"))       return HandlePieStart(Params);
    if (CommandType == TEXT("pie_stop"))        return HandlePieStop(Params);
    if (CommandType == TEXT("pie_status"))      return HandlePieStatus(Params);
    if (CommandType == TEXT("pie_screenshot"))  return HandlePieScreenshot(Params);
    if (CommandType == TEXT("simulate_key"))    return HandleSimulateKey(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown PIE command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FPIECommands::HandlePieStart(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor is null — not running in editor"));
    }

    // Если PIE уже идёт — не стартуем повторно, иначе UE падает.
    if (GEditor->PlayWorld != nullptr)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("started"), false);
        Result->SetStringField(TEXT("note"), TEXT("PIE already running — call pie_stop first"));
        Result->SetStringField(TEXT("world_name"), GEditor->PlayWorld->GetName());
        return Result;
    }

    FString LevelName;
    Params->TryGetStringField(TEXT("level_name"), LevelName);

    FString Mode = TEXT("selected_viewport");
    Params->TryGetStringField(TEXT("mode"), Mode);

    // Сборка параметров запуска PIE. Дефолтный конструктор уже выставляет
    // WorldType = PlayInEditor и подтягивает текущие проектные Editor Play
    // Settings (где задано Selected Viewport / New Editor Window).
    FRequestPlaySessionParams SessionParams;

    // Через EditorPlaySettings можно подкрутить запускаемый режим, но для
    // простоты пускаем с дефолтами проекта — пользователь сам в Editor задаёт
    // "Selected Viewport" / "New Editor Window" в UI. Параметр `mode` сейчас
    // только пишется в ответ как hint и логируется.
    GEditor->RequestPlaySession(SessionParams);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("started"), true);
    Result->SetStringField(TEXT("mode"), Mode);
    if (!LevelName.IsEmpty())
    {
        Result->SetStringField(TEXT("level_name_hint"), LevelName);
    }
    Result->SetStringField(TEXT("note"), TEXT("PIE start requested — world will be created on next tick. Poll pie_status to confirm."));
    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandlePieStop(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor is null — not running in editor"));
    }

    const bool bWasRunning = (GEditor->PlayWorld != nullptr);
    if (bWasRunning)
    {
        GEditor->RequestEndPlayMap();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("stopped"), true);
    Result->SetBoolField(TEXT("was_running"), bWasRunning);
    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandlePieStatus(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    if (!GEditor)
    {
        Result->SetBoolField(TEXT("is_running"), false);
        Result->SetStringField(TEXT("note"), TEXT("GEditor is null"));
        return Result;
    }

    UWorld* PlayWorld = GEditor->PlayWorld;
    if (!PlayWorld)
    {
        Result->SetBoolField(TEXT("is_running"), false);
        Result->SetStringField(TEXT("world_name"), TEXT(""));
        Result->SetNumberField(TEXT("elapsed_seconds"), 0.0);
        Result->SetBoolField(TEXT("has_player_controller"), false);
        return Result;
    }

    Result->SetBoolField(TEXT("is_running"), true);
    Result->SetStringField(TEXT("world_name"), PlayWorld->GetName());
    Result->SetNumberField(TEXT("elapsed_seconds"), PlayWorld->GetTimeSeconds());

    APlayerController* PC = PlayWorld->GetFirstPlayerController();
    Result->SetBoolField(TEXT("has_player_controller"), PC != nullptr);

    // Дополнительно — текущий уровень.
    if (PlayWorld->GetCurrentLevel() && PlayWorld->GetCurrentLevel()->GetOuter())
    {
        Result->SetStringField(TEXT("current_level"), PlayWorld->GetCurrentLevel()->GetOuter()->GetName());
    }

    // Имя GameViewportClient, если есть — для смок-тестов.
    if (PlayWorld->GetGameViewport())
    {
        Result->SetBoolField(TEXT("has_game_viewport"), true);
    }
    else
    {
        Result->SetBoolField(TEXT("has_game_viewport"), false);
    }

    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandlePieScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    FString Filename = TEXT("PIEScreenshot.png");
    Params->TryGetStringField(TEXT("filename"), Filename);
    if (!Filename.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        Filename += TEXT(".png");
    }

    bool bShowUI = true;
    Params->TryGetBoolField(TEXT("show_ui"), bShowUI);

    const FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
    IFileManager::Get().MakeDirectory(*ScreenshotDir, /*Tree=*/true);
    const FString FullPath = ScreenshotDir / Filename;

    if (IFileManager::Get().FileExists(*FullPath))
    {
        IFileManager::Get().Delete(*FullPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
    }

    if (!GEditor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor is null"));
    }

    UWorld* PlayWorld = GEditor->PlayWorld;
    UGameViewportClient* GameViewport = PlayWorld ? PlayWorld->GetGameViewport() : nullptr;

    if (GameViewport && GameViewport->Viewport)
    {
        // Корректный путь для PIE: триггерим hi-res screenshot через сам
        // game viewport. Снимок попадёт в стандартную папку, имя — Filename
        // (мы переопределяем через FHighResScreenshotConfig).
        FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
        Config.FilenameOverride = FullPath;
        Config.SetHDRCapture(false);
        // Если bShowUI=true — желаем захватить UMG; HighResScreenshotConfig
        // не имеет прямого флага, но штатный TakeHighResScreenShot захватывает
        // финальный кадр со всем UI поверх. Для bShowUI=false вызовем
        // FScreenshotRequest::RequestScreenshot, у которого есть параметр.
        if (bShowUI)
        {
            GameViewport->Viewport->TakeHighResScreenShot();
        }
        else
        {
            FScreenshotRequest::RequestScreenshot(FullPath, /*bInShowUI=*/false, /*bAddFilenameSuffix=*/false);
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("created"));
        Result->SetStringField(TEXT("assetPath"), FullPath);
        Result->SetStringField(TEXT("filename"), Filename);
        Result->SetBoolField(TEXT("show_ui"), bShowUI);
        Result->SetStringField(TEXT("source"), TEXT("game_viewport"));
        Result->SetStringField(TEXT("note"), TEXT("PIE screenshot queued; file appears within 1-2 ticks"));
        return Result;
    }

    // Fallback: PIE ещё не готов — стандартный editor screenshot путь.
    FScreenshotRequest::RequestScreenshot(FullPath, bShowUI, /*bAddFilenameSuffix=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), TEXT("created"));
    Result->SetStringField(TEXT("assetPath"), FullPath);
    Result->SetStringField(TEXT("filename"), Filename);
    Result->SetBoolField(TEXT("show_ui"), bShowUI);
    Result->SetStringField(TEXT("source"), TEXT("fallback_editor_viewport"));
    Result->SetStringField(TEXT("note"), TEXT("No active PIE GameViewport — fell back to editor screenshot path"));
    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandleSimulateKey(const TSharedPtr<FJsonObject>& Params)
{
    FString KeyName;
    if (!Params->TryGetStringField(TEXT("key"), KeyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));
    }

    const FKey Key(*KeyName);
    if (!Key.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown engine key: '%s'"), *KeyName));
    }

    if (!GEditor || !GEditor->PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world — call pie_start first"));
    }

    APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
    if (!PC)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No PlayerController in PIE world"));
    }

    // Pressed + Released — синхронно, один и тот же tick.
    PC->InputKey(FInputKeyParams(Key, IE_Pressed, /*Delta*/ 1.0, /*bGamepad*/ false));
    PC->InputKey(FInputKeyParams(Key, IE_Released, /*Delta*/ 1.0, /*bGamepad*/ false));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("sent"), true);
    Result->SetStringField(TEXT("key"), KeyName);
    Result->SetStringField(TEXT("controller_name"), PC->GetName());
    return Result;
}
