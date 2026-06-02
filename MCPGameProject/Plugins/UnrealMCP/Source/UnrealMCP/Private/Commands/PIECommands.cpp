#include "Commands/PIECommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/PIEUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "RenderingThread.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Engine/EngineBaseTypes.h"
#include "ImageUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"
#include "Widgets/SViewport.h"  // SViewport — полный тип для upcast GetGameViewportWidget()->SWidget (3.3.1)

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
    if (CommandType == TEXT("screen_click"))    return HandleScreenClick(Params);
    if (CommandType == TEXT("tick_world"))      return HandleTickWorld(Params);

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

    // 3.3.0: режим запуска PIE в ОТДЕЛЬНОМ floating game-окне. Зачем: дефолтный
    // selected_viewport рендерит PIE в level-вьюпорт ВНУТРИ окна редактора;
    // когда редактор в фоне/не realtime, этот вьюпорт не презентит реальный
    // кадр и pie_screenshot читает чёрный backbuffer (даже с форс-redraw 3.2.1).
    // Отдельное игровое окно — самостоятельный presented Slate-window: его
    // backbuffer всегда содержит свежий кадр со сценой+UMG, поэтому
    // TakeHighResScreenShot даёт НЕ чёрный кадр. Ось "destination окна"
    // ортогональна net mode (Standalone/ListenServer/Client) — комбинируется.
    const bool bNewWindow = Mode.Equals(TEXT("new_window"), ESearchCase::IgnoreCase);

    // Опциональный размер нового окна (default 1280x720). Применяется только
    // для new_window; для selected_viewport не трогаем настройки окна.
    int32 NewWindowWidth = 1280;
    int32 NewWindowHeight = 720;
    Params->TryGetNumberField(TEXT("window_width"), NewWindowWidth);
    Params->TryGetNumberField(TEXT("window_height"), NewWindowHeight);
    if (NewWindowWidth < 64)  NewWindowWidth = 64;
    if (NewWindowHeight < 64) NewWindowHeight = 64;

    // MCP-PLUGIN-003: multi-client поддержка. Default 1 — старое поведение.
    int32 NumClients = 1;
    Params->TryGetNumberField(TEXT("num_clients"), NumClients);
    if (NumClients < 1)  NumClients = 1;
    if (NumClients > 8)  NumClients = 8;  // engine cap, listen-server обычно 2..4.

    bool bDedicatedServer = false;
    Params->TryGetBoolField(TEXT("dedicated_server"), bDedicatedServer);

    // Конфигурируем ULevelEditorPlaySettings ПЕРЕД RequestPlaySession.
    //
    // FIX-UI-008: для num_clients>1 нужны N НЕЗАВИСИМЫХ standalone PIE-инстансов
    // в ОДНОМ процессе редактора (чтобы единственный MCP TCP-listener мог рулить
    // обоими). Раньше выставлялся только SetPlayNumberOfClients(N) — без net mode
    // и run-under-one-process, что при дефолтных настройках проекта давало ОДИН
    // общий UWorld с N локальными игроками (split-screen) → один набор UMG-виджетов
    // → войти мог только один клиент.
    //
    // Правильная комбинация (проверено по UE5.7 PlayLevel.cpp):
    //   SetRunUnderOneProcess(true)
    //     — в PlayLevel.cpp::1156-1172 ветка bIsSeparateProcess |= NumClients>1
    //       выполняется ТОЛЬКО при !RunUnderOneProcess. С RunUnderOneProcess=true
    //       движок остаётся в этом же процессе (StartPlayInEditorSession,
    //       а не StartPlayInNewProcessSession) — MCP сохраняет TCP-доступ.
    //   SetPlayNetMode(PIE_Standalone)
    //     — для Standalone bNeedsServer=false (PlayLevel.cpp::2837-2839): UE НЕ
    //       поднимает ни dedicated, ни listen-server. Если клиенты проекта ходят
    //       на внешний игровой сервер — UE-репликация не нужна.
    //   SetPlayNumberOfClients(N)
    //     — цикл PlayLevel.cpp::2886-2911 создаёт N PIE WorldContext'ов через
    //       CreateNewPlayInEditorInstance с LocalNetMode=PIE_Standalone. Каждый
    //       инстанс — полный game boot со своим GameInstance/GameMode/PlayerController
    //       и своим viewport'ом → свой WBP_Login. Серверного контекста не создаётся.
    //
    // bDedicatedServer оставлен как явный override (на случай отладки UE-server-flow):
    // он включает bLaunchSeparateServer и переводит net mode в PIE_Client.
    if (ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>())
    {
        PlaySettings->SetPlayNumberOfClients(NumClients);

        // 3.3.0: для new_window готовим параметры floating game-окна.
        // GeneratePIEViewportWindow → GetWindowSizeAndPositionForInstanceIndex
        // (PlayLevel.cpp, UE 5.7) читает именно эти public-поля настроек. Сеттеров
        // у них нет — присваиваем напрямую. Поля влияют ТОЛЬКО когда PIE
        // презентится в собственном окне (т.е. DestinationSlateViewport не задан,
        // см. ниже); для selected_viewport не трогаем — обратная совместимость.
        if (bNewWindow)
        {
            PlaySettings->NewWindowWidth = NewWindowWidth;
            PlaySettings->NewWindowHeight = NewWindowHeight;
            PlaySettings->CenterNewWindow = true;   // позицию определит движок (центр рабочей области)
            PlaySettings->PIEAlwaysOnTop = false;
            // Согласуем тип последнего play-режима с floating-окном, чтобы UI/настройки
            // редактора не конфликтовали с нашим запросом.
            PlaySettings->LastExecutedPlayModeType = EPlayModeType::PlayMode_InEditorFloating;
        }

        if (bDedicatedServer)
        {
            // Явный режим UE dedicated server (редко нужен).
            // PIE_Client + bLaunchSeparateServer → отдельный server-контекст,
            // клиенты к нему подключаются. PIEUtils отфильтрует server-world.
            PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Client);
            PlaySettings->bLaunchSeparateServer = true;
            PlaySettings->SetRunUnderOneProcess(true);
        }
        else if (NumClients > 1)
        {
            // Основной кейс: N независимых клиентских миров в ОДНОМ процессе.
            //
            // FIX-UI-008 v2: PIE_Standalone НЕ создаёт отдельные миры — при
            // NumberOfClients>1 движок делает ОДИН world со split-screen локальными
            // игроками (один набор UMG → войти может только один клиент). Отдельные
            // клиентские миры в UE PIE рождаются ТОЛЬКО при сетевом режиме.
            //
            // PIE_ListenServer + RunUnderOneProcess + NumberOfClients=N создаёт N
            // инстансов: instance 0 = listen-server-игрок, 1..N-1 = чистые клиенты.
            // Каждый — полный boot со своим world/GameInstance/PlayerController и
            // своим WBP_Login. Это стандартный "test multiplayer in PIE" сетап.
            // Отдельный dedicated-процесс НЕ поднимается (bLaunchSeparateServer=false),
            // поэтому единственный MCP TCP-listener рулит всеми инстансами.
            //
            // Если игровое состояние живёт на внешнем сервере — UE-репликация
            // listen-server здесь не используется (роль server для геймплея неважна),
            // нужна лишь как механизм, заставляющий PIE создать N отдельных миров.
            // listen-server world имеет NetMode==NM_ListenServer (НЕ NM_DedicatedServer),
            // поэтому PIEUtils его НЕ отфильтровывает — index 0 = server-игрок.
            PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
            PlaySettings->bLaunchSeparateServer = false;
            PlaySettings->SetRunUnderOneProcess(true);
        }
        else
        {
            // num_clients==1 — НЕ регрессируем общий кейс: классический single-PIE.
            PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
            PlaySettings->bLaunchSeparateServer = false;
        }
    }

    FRequestPlaySessionParams SessionParams;

    // 3.3.0: при mode="new_window" презентим PIE в ОТДЕЛЬНОМ floating game-окне.
    //
    // Механика (UE 5.7 PlayLevel.cpp): destination остаётся InProcess (в том же
    // процессе редактора — MCP сохраняет TCP-доступ), но мы ЯВНО гарантируем, что
    // DestinationSlateViewport НЕ задан. В CreateInnerProcessPIEGameInstance
    // (строка ~3119) при невалидном DestinationSlateViewport движок идёт в ветку
    // GeneratePIEViewportWindow — создаёт самостоятельный SWindow + SPIEViewport
    // с реальным present'ом (а НЕ вкладывает PIE в level-вьюпорт редактора, который
    // в фоне не презентит → чёрный backbuffer). После старта
    // PlayWorld->GetGameViewport()->Viewport указывает на backbuffer этого окна,
    // и существующий путь захвата (forced redraw + TakeHighResScreenShot из 3.2.1)
    // снимает живой кадр со сценой+UMG.
    //
    // SessionDestination оставляем дефолтным (InProcess) — НЕ NewProcess/Launcher:
    // отдельный процесс лишил бы MCP единственного TCP-listener'а. "Окно vs
    // вьюпорт" — это НЕ destination-ось, а отсутствие/наличие DestinationSlateViewport.
    //
    // Дефолтный selected_viewport не трогаем (smoke-тесты зовут pie_start без mode).
    if (bNewWindow)
    {
        // Гарантируем floating-window путь: явно очищаем (на случай если CDO/
        // прошлый запуск оставил привязку к вьюпорту) и не задаём кастомное окно.
        SessionParams.DestinationSlateViewport.Reset();
        SessionParams.CustomPIEWindow.Reset();
    }

    GEditor->RequestPlaySession(SessionParams);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("started"), true);
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetBoolField(TEXT("new_window"), bNewWindow);
    if (bNewWindow)
    {
        Result->SetNumberField(TEXT("window_width"), NewWindowWidth);
        Result->SetNumberField(TEXT("window_height"), NewWindowHeight);
    }
    Result->SetNumberField(TEXT("num_clients"), NumClients);
    Result->SetBoolField(TEXT("dedicated_server"), bDedicatedServer);
    if (!LevelName.IsEmpty())
    {
        Result->SetStringField(TEXT("level_name_hint"), LevelName);
    }

    // Описания клиентов пока пустые — миры создаются на следующих тиках.
    TArray<TSharedPtr<FJsonValue>> ClientsHint;
    for (int32 i = 0; i < NumClients; ++i)
    {
        TSharedPtr<FJsonObject> ClientObj = MakeShared<FJsonObject>();
        ClientObj->SetNumberField(TEXT("index"), i);
        ClientObj->SetStringField(TEXT("status"), TEXT("requested"));
        ClientsHint.Add(MakeShared<FJsonValueObject>(ClientObj));
    }
    Result->SetArrayField(TEXT("clients"), ClientsHint);

    Result->SetStringField(TEXT("note"), TEXT("PIE start requested — worlds will be created on next tick(s). Poll pie_status to confirm."));
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
        // MCP-PLUGIN-003: consistent shape — клиентский массив всегда присутствует,
        // даже когда PIE неактивен, чтобы тесты и обёртки не делали разный парсинг
        // is_running=true/false случаев.
        Result->SetNumberField(TEXT("num_clients"), 0);
        Result->SetArrayField(TEXT("clients"), TArray<TSharedPtr<FJsonValue>>());
        return Result;
    }

    Result->SetBoolField(TEXT("is_running"), true);
    Result->SetStringField(TEXT("world_name"), PlayWorld->GetName());
    Result->SetNumberField(TEXT("elapsed_seconds"), PlayWorld->GetTimeSeconds());
    Result->SetStringField(TEXT("plugin_version"), TEXT("2.20.0"));

    APlayerController* PC = PlayWorld->GetFirstPlayerController();
    Result->SetBoolField(TEXT("has_player_controller"), PC != nullptr);

    // Дополнительно — текущий уровень.
    if (PlayWorld->GetCurrentLevel() && PlayWorld->GetCurrentLevel()->GetOuter())
    {
        Result->SetStringField(TEXT("current_level"), PlayWorld->GetCurrentLevel()->GetOuter()->GetName());
    }

    // Имя GameViewportClient, если есть — для смок-тестов.
    Result->SetBoolField(TEXT("has_game_viewport"), PlayWorld->GetGameViewport() != nullptr);

    // MCP-PLUGIN-003: массив клиентов для multi-client PIE.
    const int32 NumClients = FUnrealMCPPIEUtils::GetNumPIEClients();
    Result->SetNumberField(TEXT("num_clients"), NumClients);

    TArray<TSharedPtr<FJsonValue>> ClientsArr;
    for (int32 i = 0; i < NumClients; ++i)
    {
        ClientsArr.Add(MakeShared<FJsonValueObject>(FUnrealMCPPIEUtils::DescribeClient(i)));
    }
    Result->SetArrayField(TEXT("clients"), ClientsArr);

    // FIX-UI-008 diag: реальное число PIE-WorldContext'ов + фактические play settings
    // (чтобы понять, создаёт ли движок отдельные миры и применяются ли наши настройки).
    if (GEngine)
    {
        int32 PieContextCount = 0;
        TArray<TSharedPtr<FJsonValue>> CtxArr;
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            if (Ctx.WorldType != EWorldType::PIE)
            {
                continue;
            }
            PieContextCount++;
            TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
            C->SetNumberField(TEXT("pie_instance"), Ctx.PIEInstance);
            const UWorld* W = Ctx.World();
            C->SetStringField(TEXT("world"), W ? W->GetName() : TEXT("<null>"));
            C->SetNumberField(TEXT("net_mode"), W ? (int32)W->GetNetMode() : -1);
            CtxArr.Add(MakeShared<FJsonValueObject>(C));
        }
        Result->SetNumberField(TEXT("num_pie_world_contexts"), PieContextCount);
        Result->SetArrayField(TEXT("pie_contexts"), CtxArr);
    }
    if (const ULevelEditorPlaySettings* PS = GetDefault<ULevelEditorPlaySettings>())
    {
        int32 PsNumClients = -1;       PS->GetPlayNumberOfClients(PsNumClients);
        EPlayNetMode PsNetMode = EPlayNetMode::PIE_Standalone; PS->GetPlayNetMode(PsNetMode);
        bool bPsRunUnderOne = false;   PS->GetRunUnderOneProcess(bPsRunUnderOne);
        TSharedPtr<FJsonObject> PSObj = MakeShared<FJsonObject>();
        PSObj->SetNumberField(TEXT("num_clients"), PsNumClients);
        PSObj->SetNumberField(TEXT("net_mode"), (int32)PsNetMode);  // 0=Standalone,1=ListenServer,2=Client
        PSObj->SetBoolField(TEXT("run_under_one_process"), bPsRunUnderOne);
        Result->SetObjectField(TEXT("play_settings"), PSObj);
    }

    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandlePieScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    FString Filename = TEXT("PIEScreenshot.png");
    Params->TryGetStringField(TEXT("filename"), Filename);

    // MCP-PLUGIN-003: controller_index → суффикс _client<N> в имени файла.
    int32 ControllerIndex = 0;
    const bool bHasControllerIdx = Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    if (bHasControllerIdx)
    {
        // Вставляем суффикс _clientN перед расширением.
        FString Base = Filename;
        FString Ext = TEXT(".png");
        if (Filename.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
        {
            Base = Filename.LeftChop(4);
        }
        Filename = FString::Printf(TEXT("%s_client%d%s"), *Base, ControllerIndex, *Ext);
    }
    else if (!Filename.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
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

    // MCP-PLUGIN-003: для multi-PIE — выбираем world нужного клиента.
    UWorld* PlayWorld = bHasControllerIdx
        ? FUnrealMCPPIEUtils::GetPIEWorldForClient(ControllerIndex)
        : ToRawPtr(GEditor->PlayWorld);
    UGameViewportClient* GameViewport = PlayWorld ? PlayWorld->GetGameViewport() : nullptr;

    if (GameViewport && GameViewport->Viewport)
    {
        FViewport* const Viewport = GameViewport->Viewport;

        // --- 3.3.1: Slate window readback ---
        // Решающий факт (2026-06-01): floating PIE-окно (mode="new_window")
        // корректно рендерит игру НА ЭКРАНЕ, но старые пути
        // TakeHighResScreenShot()/FScreenshotRequest читали scene-буфер/не ту
        // поверхность → чёрный PNG. Рендер работает — баг в readback.
        // Решение: читать реальный backbuffer Slate-окна игрового вьюпорта
        // через FSlateApplication::TakeScreenshot — он отдаёт то, что видно
        // на экране (3D-сцена + UMG поверх, финальный презентованный кадр).

        // 1) Найти SWindow игрового PIE-вьюпорта.
        TSharedPtr<SWindow> Window;
        TSharedPtr<SWidget> ViewportWidget;
        if (FSlateApplication::IsInitialized())
        {
            Window = GameViewport->GetWindow();
            ViewportWidget = GameViewport->GetGameViewportWidget();
            if (!Window.IsValid() && ViewportWidget.IsValid())
            {
                Window = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
            }
        }

        if (Window.IsValid())
        {
            // Подготовительный кадр: форсируем актуальный present
            // (редактор в фоне сам не перерисовывает PIE-вьюпорт).
            Viewport->InvalidateDisplay();
            Viewport->Draw(/*bShouldPresent=*/true);
            FlushRenderingCommands();

            // 2) TakeScreenshot ставит запрос на захват backbuffer'а окна на
            // следующем Draw. Сигнатура UE 5.7:
            //   void TakeScreenshot(const TSharedRef<SWidget>&, TArray<FColor>& OutColorData, FIntVector& OutSize)
            // Захватываем окно целиком (root widget окна).
            TArray<FColor> Bitmap;
            FIntVector OutSize(0, 0, 0);
            const bool bTaken = FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Bitmap, OutSize);

            // 3) Запрос обрабатывается на следующей отрисовке окна — рисуем
            // окно ещё раз и флашим render-thread, чтобы Bitmap заполнился.
            Viewport->InvalidateDisplay();
            Viewport->Draw(/*bShouldPresent=*/true);
            FlushRenderingCommands();

            const int32 Width  = OutSize.X;
            const int32 Height = OutSize.Y;
            if (bTaken && Width > 0 && Height > 0 && Bitmap.Num() >= Width * Height)
            {
                // 4) TakeScreenshot нередко отдаёт A=0 → PNG будет прозрачным
                // (чёрным). Принудительно делаем непрозрачным.
                for (FColor& Px : Bitmap)
                {
                    Px.A = 255;
                }

                // 5) Кодируем в PNG (FImageUtils — Engine) и пишем файл
                // (FFileHelper — Core). Без новых build-зависимостей.
                TArray64<uint8> CompressedPng;
                FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()), CompressedPng);

                if (CompressedPng.Num() > 0 && FFileHelper::SaveArrayToFile(CompressedPng, *FullPath))
                {
                    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                    Result->SetStringField(TEXT("status"), TEXT("created"));
                    Result->SetStringField(TEXT("assetPath"), FullPath);
                    Result->SetStringField(TEXT("filename"), Filename);
                    Result->SetBoolField(TEXT("show_ui"), bShowUI);
                    Result->SetStringField(TEXT("source"), TEXT("slate_window"));
                    Result->SetNumberField(TEXT("width"), Width);
                    Result->SetNumberField(TEXT("height"), Height);
                    Result->SetNumberField(TEXT("bytes"), CompressedPng.Num());
                    Result->SetStringField(TEXT("note"), TEXT("PIE screenshot captured via Slate window readback (real backbuffer: scene + UI)"));
                    return Result;
                }
            }
            // Slate-захват не дал данных — провалимся в scene-fallback ниже.
        }

        // --- Fallback (scene-путь, 3.2.1): окно не найдено или readback пуст ---
        // Корректный путь для PIE: триггерим hi-res screenshot через сам
        // game viewport. Снимок попадёт в стандартную папку, имя — Filename
        // (мы переопределяем через FHighResScreenshotConfig).
        FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
        Config.FilenameOverride = FullPath;
        Config.SetHDRCapture(false);

        // Подготовительный кадр: invalidate + честная отрисовка свежего present.
        Viewport->InvalidateDisplay();
        Viewport->Draw(/*bShouldPresent=*/true);
        FlushRenderingCommands();

        if (bShowUI)
        {
            Viewport->TakeHighResScreenShot();
        }
        else
        {
            FScreenshotRequest::RequestScreenshot(FullPath, /*bInShowUI=*/false, /*bAddFilenameSuffix=*/false);
        }

        Viewport->InvalidateDisplay();
        Viewport->Draw(/*bShouldPresent=*/true);
        FlushRenderingCommands();

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("created"));
        Result->SetStringField(TEXT("assetPath"), FullPath);
        Result->SetStringField(TEXT("filename"), Filename);
        Result->SetBoolField(TEXT("show_ui"), bShowUI);
        Result->SetStringField(TEXT("source"), TEXT("game_viewport"));
        Result->SetStringField(TEXT("note"), TEXT("PIE screenshot captured via forced viewport redraw (Slate window not found — scene fallback)"));
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

    // MCP-PLUGIN-003: controller_index (default 0 — старое поведение).
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    APlayerController* PC = FUnrealMCPPIEUtils::GetPlayerControllerByIndex(ControllerIndex);
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

    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed,  /*AmountDepressed*/ 1.0f));
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, /*AmountDepressed*/ 1.0f));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("sent"), true);
    Result->SetStringField(TEXT("key"), KeyName);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    Result->SetStringField(TEXT("controller_name"), PC->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandleScreenClick(const TSharedPtr<FJsonObject>& Params)
{
    // --- Параметры ---
    double X = 0.0, Y = 0.0;
    if (!Params->TryGetNumberField(TEXT("x"), X) || !Params->TryGetNumberField(TEXT("y"), Y))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'x' / 'y' parameters"));
    }

    FString ButtonName = TEXT("Left");
    Params->TryGetStringField(TEXT("button"), ButtonName);

    FKey EffectingButton = EKeys::LeftMouseButton;
    if (ButtonName.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
    {
        EffectingButton = EKeys::RightMouseButton;
    }
    else if (ButtonName.Equals(TEXT("Middle"), ESearchCase::IgnoreCase))
    {
        EffectingButton = EKeys::MiddleMouseButton;
    }
    else if (!ButtonName.Equals(TEXT("Left"), ESearchCase::IgnoreCase))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown button: '%s' (expected Left/Right/Middle)"), *ButtonName));
    }

    bool bNormalized = false;
    Params->TryGetBoolField(TEXT("normalized"), bNormalized);

    int32 ControllerIndex = 0;
    const bool bHasControllerIdx = Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    if (!GEditor || !GEditor->PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world — call pie_start first"));
    }
    if (!FSlateApplication::IsInitialized())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FSlateApplication not initialized"));
    }

    // --- Резолв game viewport / окна нужного клиента (как в pie_screenshot) ---
    UWorld* PlayWorld = bHasControllerIdx
        ? FUnrealMCPPIEUtils::GetPIEWorldForClient(ControllerIndex)
        : ToRawPtr(GEditor->PlayWorld);
    UGameViewportClient* GameViewport = PlayWorld ? PlayWorld->GetGameViewport() : nullptr;
    if (!GameViewport || !GameViewport->Viewport)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("No game viewport for controller_index=%d"), ControllerIndex));
    }

    // Найти SWindow игрового вьюпорта + сам widget вьюпорта (для геометрии).
    TSharedPtr<SWindow> Window = GameViewport->GetWindow();
    TSharedPtr<SWidget> ViewportWidget = GameViewport->GetGameViewportWidget();
    if (!Window.IsValid() && ViewportWidget.IsValid())
    {
        Window = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
    }
    if (!ViewportWidget.IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Game viewport widget not found"));
    }

    // --- Перевод viewport-пикселей в абсолютные экранные координаты ---
    // Геометрия widget'а вьюпорта даёт абсолютную (десктопную, DPI-scaled)
    // позицию верхнего-левого угла области рендера и её абсолютный размер.
    const FGeometry& ViewportGeom = ViewportWidget->GetCachedGeometry();
    const FVector2D AbsTopLeft = ViewportGeom.GetAbsolutePosition();
    const FVector2D AbsSize = ViewportGeom.GetAbsoluteSize();   // уже с учётом DPI

    // Размер вьюпорта в пикселях (для normalized и масштаба pixel→absolute).
    FIntPoint ViewportSizeXY = GameViewport->Viewport->GetSizeXY();
    const double VpW = ViewportSizeXY.X > 0 ? (double)ViewportSizeXY.X : (double)AbsSize.X;
    const double VpH = ViewportSizeXY.Y > 0 ? (double)ViewportSizeXY.Y : (double)AbsSize.Y;

    // Пиксели вьюпорта, к которым относится клик.
    const double PixelX = bNormalized ? X * VpW : X;
    const double PixelY = bNormalized ? Y * VpH : Y;

    // Масштаб: абсолютный размер области / размер вьюпорта в пикселях = DPI scale.
    const double ScaleX = VpW > 0.0 ? (double)AbsSize.X / VpW : 1.0;
    const double ScaleY = VpH > 0.0 ? (double)AbsSize.Y / VpH : 1.0;

    const FVector2D AbsPos(
        AbsTopLeft.X + PixelX * ScaleX,
        AbsTopLeft.Y + PixelY * ScaleY);

    // --- Инъекция реального Slate-клика (down + up) ---
    FSlateApplication& SlateApp = FSlateApplication::Get();
    TSet<FKey> EmptyPressed;
    const FModifierKeysState ModifierKeys;

    SlateApp.SetCursorPos(AbsPos);

    {
        FPointerEvent MouseDown(
            0,                  // PointerIndex (primary mouse)
            AbsPos,             // ScreenSpacePosition
            AbsPos,             // LastScreenSpacePosition
            EmptyPressed,       // PressedButtons
            EffectingButton,    // EffectingButton
            0.0f,               // WheelDelta
            ModifierKeys);
        SlateApp.ProcessMouseButtonDownEvent(TSharedPtr<FGenericWindow>(), MouseDown);
    }
    {
        FPointerEvent MouseUp(
            0,
            AbsPos,
            AbsPos,
            EmptyPressed,
            EffectingButton,
            0.0f,
            ModifierKeys);
        SlateApp.ProcessMouseButtonUpEvent(MouseUp);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("button"), ButtonName);
    Result->SetNumberField(TEXT("screen_x"), PixelX);   // пиксели внутри вьюпорта
    Result->SetNumberField(TEXT("screen_y"), PixelY);
    Result->SetNumberField(TEXT("abs_x"), AbsPos.X);     // абсолютные десктопные
    Result->SetNumberField(TEXT("abs_y"), AbsPos.Y);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    return Result;
}

TSharedPtr<FJsonObject> FPIECommands::HandleTickWorld(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor is null — not running in editor"));
    }

    UWorld* PlayWorld = GEditor->PlayWorld;
    if (!PlayWorld)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active PIE world — call pie_start first"));
    }

    // Кол-во тиков (default 1, минимум 1, верхний разумный лимит — 1000
    // чтобы случайный 100000 не подвесил editor на полминуты).
    int32 NumTicks = 1;
    Params->TryGetNumberField(TEXT("num_ticks"), NumTicks);
    if (NumTicks < 1)  NumTicks = 1;
    if (NumTicks > 1000) NumTicks = 1000;

    // Дельта (default 1/60 секунды). Допускаем явный 0 — пусть редкие
    // тесты сами рулят временем (e.g. only-render-tick).
    double DeltaSeconds = 1.0 / 60.0;
    Params->TryGetNumberField(TEXT("delta_seconds"), DeltaSeconds);
    if (DeltaSeconds < 0.0) DeltaSeconds = 0.0;

    const float TimeBefore = PlayWorld->GetTimeSeconds();

    // World->Tick — синхронный, прокручивает Actor::Tick, TimerManager,
    // physics и т.п. В отличие от GEditor->Tick — не тикает редактор, только PIE world.
    for (int32 i = 0; i < NumTicks; ++i)
    {
        PlayWorld->Tick(LEVELTICK_All, static_cast<float>(DeltaSeconds));
    }

    const float TimeAfter = PlayWorld->GetTimeSeconds();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("ticked"), NumTicks);
    Result->SetNumberField(TEXT("delta_seconds"), DeltaSeconds);
    Result->SetNumberField(TEXT("total_delta"), DeltaSeconds * NumTicks);
    Result->SetNumberField(TEXT("world_time_before"), TimeBefore);
    Result->SetNumberField(TEXT("world_time_after"), TimeAfter);
    return Result;
}
