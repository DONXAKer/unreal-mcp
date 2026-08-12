#include "EpicUnrealMCPBridge.h"
#include "EpicUnrealMCPModule.h"
#include "IUnrealMCPCommandHandler.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Timespan.h"
#include <atomic>
// Add Blueprint related includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
// Include our new command handler classes
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/UMGCommands.h"
#include "Commands/AssetCommands.h"
#include "Commands/TextureCommands.h"
#include "Commands/MaterialCommands.h"
#include "Commands/MeshCommands.h"
#include "Commands/LevelCommands.h"
#include "Commands/DataAssetCommands.h"
#include "Commands/NiagaraCommands.h"
#include "Commands/InputCommands.h"
#include "Commands/AnimationBPCommands.h"
#include "Commands/PIECommands.h"
#include "Commands/UMGTestCommands.h"
#include "Commands/UMGRuntimeCommands.h"
#include "Commands/EnhancedInputCommands.h"
#include "Commands/LandscapeCommands.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

UEpicUnrealMCPBridge::UEpicUnrealMCPBridge()
{
    EditorCommands = MakeShared<FEpicUnrealMCPEditorCommands>();
    BlueprintCommands = MakeShared<FEpicUnrealMCPBlueprintCommands>();
    BlueprintGraphCommands = MakeShared<FEpicUnrealMCPBlueprintGraphCommands>();
    UMGCommands = MakeShared<FUnrealMCPUMGCommands>();
    AssetCommands = MakeShared<FAssetCommands>();
    TextureCommands = MakeShared<FTextureCommands>();
    MaterialCommands = MakeShared<FMaterialCommands>();
    MeshCommands = MakeShared<FMeshCommands>();
    LevelCommands = MakeShared<FLevelCommands>();
    DataAssetCommands = MakeShared<FDataAssetCommands>();
    NiagaraCommands = MakeShared<FNiagaraCommands>();
    InputCommands = MakeShared<FInputCommands>();
    AnimationBPCommands = MakeShared<FAnimationBPCommands>();
    PIECommands = MakeShared<FPIECommands>();
    UMGTestCommands = MakeShared<FUMGTestCommands>();
    UMGRuntimeCommands = MakeShared<FUMGRuntimeCommands>();
    EnhancedInputCommands = MakeShared<FEnhancedInputCommands>();
    ConsoleCommands = MakeShared<FUnrealMCPConsoleCommands>();
    LandscapeCommands = MakeShared<FLandscapeCommands>();
}

UEpicUnrealMCPBridge::~UEpicUnrealMCPBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintGraphCommands.Reset();
    UMGCommands.Reset();
    AssetCommands.Reset();
    TextureCommands.Reset();
    MaterialCommands.Reset();
    MeshCommands.Reset();
    LevelCommands.Reset();
    DataAssetCommands.Reset();
    NiagaraCommands.Reset();
    InputCommands.Reset();
    AnimationBPCommands.Reset();
    PIECommands.Reset();
    UMGTestCommands.Reset();
    UMGRuntimeCommands.Reset();
    EnhancedInputCommands.Reset();
    ConsoleCommands.Reset();
    LandscapeCommands.Reset();
}

// Initialize subsystem
void UEpicUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Initializing"));
    
    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = MCP_SERVER_PORT;

    // Per-process port override — позволяет нескольким инстансам Editor'а поднять
    // каждый свой MCP-мост (нужно для 2-player визуального playtest-харнесса).
    // Приоритет: аргумент командной строки -MCPPort=<n>, затем env UNREALMCP_PORT.
    // Без флага дефолт 55557 сохраняется (обратная совместимость).
    int32 OverridePort = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("MCPPort="), OverridePort) && OverridePort > 0 && OverridePort <= 65535)
    {
        Port = static_cast<uint16>(OverridePort);
        UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Port overridden via -MCPPort= to %d"), Port);
    }
    else
    {
        const FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREALMCP_PORT"));
        if (!EnvPort.IsEmpty())
        {
            const int32 ParsedEnvPort = FCString::Atoi(*EnvPort);
            if (ParsedEnvPort > 0 && ParsedEnvPort <= 65535)
            {
                Port = static_cast<uint16>(ParsedEnvPort);
                UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Port overridden via UNREALMCP_PORT to %d"), Port);
            }
        }
    }

    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    // Start the server automatically
    StartServer();
}

// Clean up resources when subsystem is destroyed
void UEpicUnrealMCPBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UEpicUnrealMCPBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("EpicUnrealMCPBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening
    if (!NewListenerSocket->Listen(5))
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealMCPServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("EpicUnrealMCPBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UEpicUnrealMCPBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Must be set BEFORE Kill(true) below: that call blocks the game thread
    // until the socket thread's Run() returns, and Run() will not return
    // while it is inside a still-waiting ExecuteCommand — which polls this
    // flag in ~100ms slices instead of one long WaitFor(mcp.CommandTimeoutSeconds).
    // Without this, closing the editor with a stuck command in flight froze it
    // for up to the full command timeout (previously up to 2 minutes at 120s;
    // would be up to 5 minutes at the current 300s ceiling).
    bShuttingDown.store(true, std::memory_order_relaxed);

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Server stopped"));
}

// Execute a command received from a client
// Потолок ожидания одной команды сокетным потоком. FMCPServerRunnable::Run
// обслуживает клиентов ПО ОДНОМУ и стоит здесь в ожидании, поэтому раньше
// любая застрявшая на game thread команда убивала мост ЦЕЛИКОМ, а не только
// себя: Accept() не вызывался, а Windows на переполненную accept-очередь
// отвечает RST — снаружи это WinError 10061, будто редактор закрыт.
// 0 возвращает старое поведение (ждать вечно).
//
// Значение согласовано с приёмным таймаутом Python-клиента
// (UnrealConnection.receive_full_response, env UNREAL_MCP_RECV_TIMEOUT).
// Держать потолок ВЫШЕ клиентского бессмысленно: клиент уходит раньше, ответ
// COMMAND_TIMEOUT читать становится некому, и наружу вместо внятной ошибки
// вылезает обрыв связи. Меняешь одно — проверь второе.
//
// TODO(FIX-MCP-001): 300 с — консервативная ВРЕМЕННАЯ цифра, поднятая со 120 с
// (2026-08-12, ревью-12), НЕ измеренная на боевой карте. 120 с было подобрано
// под старые клиентские 130 с и не укладывало `create_landscape` на 1009×1009
// (121–180 с наблюдалось живьём). Живого Editor'а для замера в этой сессии не
// было. Нужна отдельная задача: замерить `create_landscape` и
// `import_static_mesh` на боевых ассетах и уточнить оба числа (это и клиентский
// потолок ниже) по факту, а не по запасу.
static TAutoConsoleVariable<float> CVarMCPCommandTimeoutSeconds(
    TEXT("mcp.CommandTimeoutSeconds"),
    300.0f,
    TEXT("Max seconds the MCP socket thread waits for a command to finish on the game thread. 0 = wait forever."),
    ECVF_Default);

// Общее состояние сокетного потока и тикер-делегата.
//
// TPromise заполняется РОВНО один раз, и после таймаута заполнить его обязан
// тот, кто снял тикер: `~TPromise` в UE 5.8 держит `check(State->IsComplete())`,
// поэтому уничтожение делегата с незаполненным промисом роняет редактор
// ассертом на выходе. Гонку «тикер как раз сработал» закрывает
// compare_exchange — второй вызов становится no-op вместо второго SetValue.
struct FMCPCommandState
{
    TPromise<FString> Promise;
    std::atomic<bool> bFulfilled{false};

    // Флаг отмены, который ПРОВЕРЯЕТ САМ делегат — замена FTSTicker::RemoveTicker
    // на пути таймаута. RemoveTicker документированно блокирует вызывающего
    // busy-Yield-циклом, если делегат в этот момент уже исполняется
    // (`Ticker.cpp`: `while (ExecutingThreadId != 0) FPlatformProcess::Yield();`),
    // то есть ровно в сценарии, ради которого таймаут и вводился (команда реально
    // идёт дольше потолка). bCancelled стоит только СНАРУЖИ, до того как отдать
    // клиенту COMMAND_TIMEOUT; делегат читает его В САМОМ НАЧАЛЕ, до вызова
    // обработчика — если тикер к этому моменту ещё не стартовал (обычный случай:
    // game thread был занят чем-то другим), делегат просто не выполняет команду
    // и заполняет уже занятый промис no-op'ом через Fulfill. Если тикер УЖЕ
    // исполняется (внутри обработчика) — флаг проверять поздно, и это ожидаемо:
    // команда доигрывается сама, а её результат просто никто не забирает.
    std::atomic<bool> bCancelled{false};

    void Fulfill(const FString& Value)
    {
        bool bExpected = false;
        if (bFulfilled.compare_exchange_strong(bExpected, true))
        {
            Promise.SetValue(Value);
        }
    }
};

FString UEpicUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Executing command: %s"), *CommandType);

    // Create a promise to wait for the result
    // Состояние живёт в TSharedRef, а не захватывается по move: тикер-делегат
    // копируемый, mutable-лямбда с move-only состоянием в него не влезает.
    TSharedRef<FMCPCommandState> StateRef = MakeShared<FMCPCommandState>();
    TFuture<FString> Future = StateRef->Promise.GetFuture();

    // ── Исполнение на game thread ЧЕРЕЗ CORE TICKER, а не через AsyncTask ──────
    // Раньше здесь был AsyncTask(ENamedThreads::GameThread, ...): обработчик
    // команды исполнялся ВНУТРИ TaskGraph-задачи (ProcessThreadUntilIdle).
    // Любая синхронная блокировка внутри такой задачи — загрузка ассета
    // (StaticLoadObject → FlushAsyncLoading), компиляция Blueprint'а
    // (FKismetEditorUtilities::CompileBlueprint), FlushRenderingCommands,
    // ожидание компиляции Niagara/шейдеров — повторно входит в цикл разбора
    // задач именованного потока и роняет редактор ассертом
    //     Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1
    //     [Async/TaskGraph.cpp] [Line: 705]
    // Стопроцентный репро был на set_component_property с UNiagaraSystem*
    // (ImportText_Direct → синхронная загрузка NS_* → ожидание компиляции
    // Niagara-скриптов). Тот же класс краха ловили landscape_set_properties
    // и import_asset_tasks() через Interchange.
    //
    // FTSTicker тикается из FEngineLoop::Tick — верхний уровень стека game
    // thread'а, вне контекста задачи. Оттуда синхронные загрузки и компиляции
    // легальны. Точечные обходы (отложение работы через FTSTicker внутри
    // отдельных handler'ов) больше не нужны: безопасен весь конвейер команд.
    // Возврат AddTicker (FDelegateHandle) больше не сохраняется: RemoveTicker
    // на пути таймаута снят целиком (см. FMCPCommandState::bCancelled и хвост
    // этой функции) — единственным потребителем хендла был именно тот вызов.
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [this, CommandType, Params, StateRef](float) -> bool
    {
        // Проверка ПЕРВЫМ делом, до какой-либо работы: если вызывающий уже
        // отдал наружу COMMAND_TIMEOUT (см. хвост ExecuteCommand), выполнять
        // команду СЕЙЧАС значит выполнить её ВТОРОЙ раз — клиент, получивший
        // таймаут, уже мог сделать retry по новому TCP-соединению. Не
        // достижимо, если делегат к моменту отмены уже начал выполняться —
        // тогда он доигрывает как обычно, а Fulfill ниже становится no-op.
        if (StateRef->bCancelled.load(std::memory_order_relaxed))
        {
            StateRef->Fulfill(FString()); // no-op if the timeout path already fulfilled it
            return false;   // одноразовый тикер
        }

        TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);

        try
        {
            TSharedPtr<FJsonObject> ResultJson;
            
            if (CommandType == TEXT("ping"))
            {
                ResultJson = MakeShareable(new FJsonObject);
                ResultJson->SetStringField(TEXT("message"), TEXT("pong"));
                // Plugin build version. Pipeline guards (check_unreal) require
                // this field's presence to confirm the editor loaded a fresh
                // plugin binary — a stale pre-2.0.0 plugin answers "pong"
                // without it. Keep in sync with UnrealMCP.uplugin "VersionName".
                // Дублирует UnrealMCP.uplugin::VersionName — бампить ОБА места,
                // иначе ping врёт и handshake Python-клиента сверяет не то.
                ResultJson->SetStringField(TEXT("plugin_version"), TEXT("3.26.0"));
            }
            // Editor Commands (including actor manipulation)
            else if (CommandType == TEXT("get_actors_in_level") ||
                     CommandType == TEXT("find_actors_by_name") ||
                     CommandType == TEXT("spawn_actor") ||
                     CommandType == TEXT("delete_actor") ||
                     CommandType == TEXT("set_actor_transform") ||
                     CommandType == TEXT("spawn_blueprint_actor") ||
                     CommandType == TEXT("get_actor_properties") ||
                     CommandType == TEXT("set_actor_property") ||
                     CommandType == TEXT("focus_viewport") ||
                     CommandType == TEXT("take_screenshot") ||
                     CommandType == TEXT("viewport_take_screenshot") ||
                     // Lookdev / deterministic framing (v3.14.0)
                     CommandType == TEXT("viewport_set_camera") ||
                     CommandType == TEXT("viewport_get_camera") ||
                     CommandType == TEXT("viewport_set_view_mode") ||
                     CommandType == TEXT("viewport_capture") ||
                     CommandType == TEXT("actor_get_component_properties") ||
                     CommandType == TEXT("actor_set_component_property") ||
                     CommandType == TEXT("postprocess_get_params") ||
                     CommandType == TEXT("postprocess_set_params"))
            {
                ResultJson = EditorCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Commands
            else if (CommandType == TEXT("create_blueprint") ||
                     CommandType == TEXT("create_blueprint_from_template") ||
                     CommandType == TEXT("reparent_blueprint") ||
                     CommandType == TEXT("add_component_to_blueprint") ||
                     CommandType == TEXT("set_physics_properties") ||
                     CommandType == TEXT("compile_blueprint") ||
                     CommandType == TEXT("save_blueprint") ||
                     CommandType == TEXT("set_static_mesh_properties") ||
                     CommandType == TEXT("set_mesh_material_color") ||
                     CommandType == TEXT("get_available_materials") ||
                     CommandType == TEXT("apply_material_to_actor") ||
                     CommandType == TEXT("apply_material_to_blueprint") ||
                     CommandType == TEXT("get_actor_material_info") ||
                     CommandType == TEXT("get_blueprint_material_info") ||
                     CommandType == TEXT("read_blueprint_content") ||
                     CommandType == TEXT("analyze_blueprint_graph") ||
                     CommandType == TEXT("get_blueprint_variable_details") ||
                     CommandType == TEXT("get_blueprint_function_details") ||
                     CommandType == TEXT("set_blueprint_property") ||
                     CommandType == TEXT("set_component_property") ||
                     CommandType == TEXT("delete_component_from_blueprint") ||
                     CommandType == TEXT("rename_component") ||
                     CommandType == TEXT("list_components") ||
                     CommandType == TEXT("set_component_transform") ||
                     // Phase 1E (v1.12.0) — Discovery
                     CommandType == TEXT("list_blueprints") ||
                     CommandType == TEXT("get_blueprint_class_info") ||
                     // Phase 3A (v1.15.0) — Blueprint Interfaces
                     CommandType == TEXT("create_blueprint_interface") ||
                     CommandType == TEXT("implement_blueprint_interface") ||
                     CommandType == TEXT("remove_blueprint_interface") ||
                     CommandType == TEXT("add_interface_function") ||
                     // Phase 3D (v1.15.0) — Compile diagnostics
                     CommandType == TEXT("compile_blueprint_verbose") ||
                     CommandType == TEXT("validate_blueprint"))
            {
                ResultJson = BlueprintCommands->HandleCommand(CommandType, Params);
            }
            // UMG Widget Commands
            else if (CommandType == TEXT("add_widget_to_umg") ||
                     CommandType == TEXT("add_text_block_to_widget") ||
                     CommandType == TEXT("add_button_to_widget") ||
                     CommandType == TEXT("add_panel_widget_to_widget") ||
                     CommandType == TEXT("delete_widget_from_umg") ||
                     CommandType == TEXT("reorder_widget") ||
                     CommandType == TEXT("reparent_widget") ||
                     CommandType == TEXT("set_widget_property") ||
                     CommandType == TEXT("get_umg_hierarchy") ||
                     CommandType == TEXT("create_umg_widget_blueprint") ||
                     CommandType == TEXT("bind_widget_event") ||
                     CommandType == TEXT("add_widget_to_viewport") ||
                     CommandType == TEXT("set_text_block_binding"))
            {
                ResultJson = UMGCommands->HandleCommand(CommandType, Params);
            }
            // Input mapping commands (project-wide DefaultInput.ini)
            // + read-only Enhanced Input inspection (v3.1.0)
            else if (CommandType == TEXT("create_input_mapping") ||
                     CommandType == TEXT("input_action_get_info") ||
                     CommandType == TEXT("input_mapping_context_get_info"))
            {
                ResultJson = InputCommands->HandleCommand(CommandType, Params);
            }
            // Asset Pipeline Commands (MCP Content Pipeline foundation)
            else if (CommandType == TEXT("asset_exists") ||
                     CommandType == TEXT("delete_asset"))
            {
                ResultJson = AssetCommands->HandleCommand(CommandType, Params);
            }
            // Texture Pipeline Commands (MCP-CONTENT-002)
            else if (CommandType == TEXT("import_texture") ||
                     CommandType == TEXT("texture_set_properties") ||
                     CommandType == TEXT("generate_placeholder_texture"))
            {
                ResultJson = TextureCommands->HandleCommand(CommandType, Params);
            }
            // Material Pipeline Commands (MCP-CONTENT-002 + FEAT-MAT-001 v3.8.0)
            else if (CommandType == TEXT("create_material_instance") ||
                     CommandType == TEXT("set_material_instance_params") ||
                     CommandType == TEXT("material_create") ||
                     CommandType == TEXT("material_add_node") ||
                     CommandType == TEXT("material_connect") ||
                     CommandType == TEXT("material_set_node_param") ||
                     CommandType == TEXT("material_get_graph"))
            {
                ResultJson = MaterialCommands->HandleCommand(CommandType, Params);
            }
            // Mesh Pipeline Commands (MCP-CONTENT-003a)
            else if (CommandType == TEXT("import_static_mesh") ||
                     CommandType == TEXT("generate_box_static_mesh") ||
                     CommandType == TEXT("set_nanite_enabled") ||
                     CommandType == TEXT("import_skeletal_mesh") ||
                     CommandType == TEXT("mesh_get_stats") ||
                     CommandType == TEXT("mesh_set_material") ||
                     CommandType == TEXT("mesh_set_build_scale"))
            {
                ResultJson = MeshCommands->HandleCommand(CommandType, Params);
            }
            // Level Pipeline Commands (MCP-CONTENT-003b)
            else if (CommandType == TEXT("create_level") ||
                     CommandType == TEXT("load_level") ||
                     CommandType == TEXT("save_level") ||
                     CommandType == TEXT("spawn_actor_in_level") ||
                     CommandType == TEXT("remove_actor_from_level") ||
                     CommandType == TEXT("set_actor_transform_in_level") ||
                     CommandType == TEXT("list_actors_in_level"))
            {
                ResultJson = LevelCommands->HandleCommand(CommandType, Params);
            }
            // Landscape Commands (v3.10.0; landscape_set_properties v3.21.0)
            else if (CommandType == TEXT("create_landscape") ||
                     CommandType == TEXT("landscape_set_properties"))
            {
                ResultJson = LandscapeCommands->HandleCommand(CommandType, Params);
            }
            // Data Asset + Sound Pipeline Commands
            else if (CommandType == TEXT("import_datatable_from_csv") ||
                     CommandType == TEXT("set_datatable_row") ||
                     CommandType == TEXT("get_datatable_rows") ||
                     CommandType == TEXT("import_sound_wave"))
            {
                ResultJson = DataAssetCommands->HandleCommand(CommandType, Params);
            }
            // Niagara VFX Pipeline Commands (emitter graph authoring added v3.22.0)
            else if (CommandType == TEXT("copy_niagara_system") ||
                     CommandType == TEXT("set_niagara_parameters") ||
                     CommandType == TEXT("niagara_get_info") ||
                     CommandType == TEXT("niagara_add_emitter") ||
                     CommandType == TEXT("niagara_set_emitter_properties") ||
                     CommandType == TEXT("niagara_set_module_input"))
            {
                ResultJson = NiagaraCommands->HandleCommand(CommandType, Params);
            }
            // PIE lifecycle + automation (v2.4.0 — Playwright-like e2e; v2.5.0 — tick_world)
            else if (CommandType == TEXT("pie_start") ||
                     CommandType == TEXT("pie_stop") ||
                     CommandType == TEXT("pie_status") ||
                     CommandType == TEXT("pie_screenshot") ||
                     CommandType == TEXT("simulate_key") ||
                     CommandType == TEXT("screen_click") ||
                     CommandType == TEXT("tick_world"))
            {
                ResultJson = PIECommands->HandleCommand(CommandType, Params);
            }
            // UMG runtime test / input simulation (v2.4.0)
            else if (CommandType == TEXT("find_widget") ||
                     CommandType == TEXT("wait_for_widget") ||
                     CommandType == TEXT("click_widget_by_name") ||
                     CommandType == TEXT("get_widget_tree"))
            {
                ResultJson = UMGTestCommands->HandleCommand(CommandType, Params);
            }
            // UMG runtime mutations (v2.8.0 — MCP-PLUGIN-002: Unicode text input)
            else if (CommandType == TEXT("set_text_on_widget")
                  || CommandType == TEXT("invoke_button_click"))
            {
                ResultJson = UMGRuntimeCommands->HandleCommand(CommandType, Params);
            }
            // Enhanced Input — UE5.7 native (v2.10.0 — MCP-PLUGIN-004)
            else if (CommandType == TEXT("create_input_action") ||
                     CommandType == TEXT("create_input_mapping_context") ||
                     CommandType == TEXT("add_input_action_mapping") ||
                     CommandType == TEXT("add_enhanced_input_action_event_node"))
            {
                ResultJson = EnhancedInputCommands->HandleCommand(CommandType, Params);
            }
            // Arbitrary console-command execution (v2.6.0 — engine Exec proxy)
            else if (CommandType == TEXT("execute_console_command"))
            {
                ResultJson = ConsoleCommands->HandleCommand(CommandType, Params);
            }
            // Phase 3B (v1.16.0) — Animation Blueprint Commands
            else if (CommandType == TEXT("create_animation_blueprint") ||
                     CommandType == TEXT("set_anim_skeleton") ||
                     CommandType == TEXT("add_state_machine") ||
                     CommandType == TEXT("add_anim_state") ||
                     CommandType == TEXT("add_anim_transition") ||
                     CommandType == TEXT("add_play_anim_node") ||
                     CommandType == TEXT("add_blend_space_player_node") ||
                     // MCP-ANIM-001 (реально выпущено как часть 3.24.0, см. CHANGELOG)
                     CommandType == TEXT("import_anim_sequence") ||
                     // FIX-ANIM-MCP-001 (v3.26.0) — subject-first алиас, тот же handler
                     CommandType == TEXT("anim_sequence_import"))
            {
                ResultJson = AnimationBPCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Graph Commands
            else if (CommandType == TEXT("add_blueprint_node") ||
                     CommandType == TEXT("connect_nodes") ||
                     CommandType == TEXT("create_variable") ||
                     CommandType == TEXT("set_blueprint_variable_properties") ||
                     CommandType == TEXT("add_event_node") ||
                     CommandType == TEXT("add_component_bound_event") ||
                     CommandType == TEXT("delete_node") ||
                     CommandType == TEXT("set_node_property") ||
                     CommandType == TEXT("create_function") ||
                     CommandType == TEXT("add_function_input") ||
                     CommandType == TEXT("add_function_output") ||
                     CommandType == TEXT("delete_function") ||
                     CommandType == TEXT("rename_function") ||
                     CommandType == TEXT("add_input_action_node") ||
                     CommandType == TEXT("find_blueprint_nodes") ||
                     CommandType == TEXT("rename_blueprint_variable") ||
                     CommandType == TEXT("delete_blueprint_variable") ||
                     CommandType == TEXT("set_variable_default_value") ||
                     CommandType == TEXT("list_blueprint_variables") ||
                     CommandType == TEXT("set_blueprint_variable_flags") ||
                     // Phase 1C (v1.12.0) — Function lifecycle
                     CommandType == TEXT("list_blueprint_functions") ||
                     CommandType == TEXT("add_function_local_variable") ||
                     CommandType == TEXT("set_function_flags") ||
                     // Phase 1D (v1.12.0) — Custom events
                     CommandType == TEXT("create_custom_event") ||
                     CommandType == TEXT("add_custom_event_input") ||
                     // Phase 3C (v1.15.0) — Pin-level operations
                     CommandType == TEXT("split_struct_pin") ||
                     CommandType == TEXT("recombine_struct_pin") ||
                     CommandType == TEXT("set_pin_default_value") ||
                     CommandType == TEXT("get_pin_info") ||
                     CommandType == TEXT("disconnect_pin"))
            {
                ResultJson = BlueprintGraphCommands->HandleCommand(CommandType, Params);
            }
            else
            {
                // Точка расширения: project-specific обработчики, зарегистрированные
                // внешними плагинами через FEpicUnrealMCPModule::RegisterCommandHandler.
                // Ядро плагина generic — про конкретные игры оно не знает.
                if (FEpicUnrealMCPModule::IsAvailable())
                {
                    for (const TSharedPtr<IUnrealMCPCommandHandler>& Handler : FEpicUnrealMCPModule::Get().GetCommandHandlers())
                    {
                        if (Handler.IsValid() && Handler->CanHandleCommand(CommandType))
                        {
                            ResultJson = Handler->HandleCommand(CommandType, Params);
                            break;
                        }
                    }
                }

                if (!ResultJson.IsValid())
                {
                    ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                    ResponseJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));

                    FString ResultString;
                    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
                    FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
                    StateRef->Fulfill(ResultString);
                    return false;   // одноразовый тикер
                }
            }
            
            // Check if the result contains an error.
            //
            // FIX-MCP-001: два независимых дефекта в этом блоке чинятся разом:
            //  1. `GetBoolField` не проверяет тип поля (`JsonObject.cpp`:
            //     `GetField<EJson::None>(...)->AsBool()`) — на нестрогом значении
            //     вроде `{"ok": 1}` тихо отдаёт false вместо честной ошибки типа.
            //     `TryGetBoolField` (`Field->TryGetBool`) требует ИМЕННО
            //     JSON-boolean.
            //  2. `success` проверялся раньше `ok` через `else if` — ответ с
            //     ОБОИМИ полями судился только по первому найденному. Провалом
            //     теперь считается ДИЗЪЮНКЦИЯ: `success == false` ИЛИ
            //     `ok == false`, кто бы из них ни присутствовал.
            bool bSuccessValue = true;
            const bool bHasSuccessField = ResultJson->TryGetBoolField(TEXT("success"), bSuccessValue);
            bool bOkValue = true;
            const bool bHasOkField = ResultJson->TryGetBoolField(TEXT("ok"), bOkValue);

            const bool bSuccess =
                !((bHasSuccessField && !bSuccessValue) || (bHasOkField && !bOkValue));

            FString ErrorMessage;
            // Структурный error-объект новой asset-схемы {category, code, message, details}.
            TSharedPtr<FJsonObject> StructuredError;

            if (!bSuccess)
            {
                // Структурный error (asset-схема) приоритетнее строкового — там
                // есть category/code/details, которые иначе теряются.
                const TSharedPtr<FJsonObject>* ErrObj = nullptr;
                if (ResultJson->TryGetObjectField(TEXT("error"), ErrObj)
                    && ErrObj != nullptr && (*ErrObj).IsValid())
                {
                    StructuredError = *ErrObj;
                    (*ErrObj)->TryGetStringField(TEXT("message"), ErrorMessage);
                }
                else
                {
                    ResultJson->TryGetStringField(TEXT("error"), ErrorMessage);
                }
            }

            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
            }
            else if (StructuredError.IsValid())
            {
                // Отказ asset-слоя: код и категорию доносим ЦЕЛИКОМ, а не
                // схлопываем в строку. Python-обёртка разбирает верхнеуровневый
                // `ok` раньше `status`, поэтому вызывающий получает настоящие
                // category/code/details вместо безымянного BRIDGE_ERROR и может
                // отличить «ассета нет» от «редактор занят».
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetBoolField(TEXT("ok"), false);
                ResponseJson->SetObjectField(TEXT("error"), StructuredError);
            }
            else
            {
                // Set error status and include the error message
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), ErrorMessage);
                // MCP-PLUGIN-001: пробросить details (availablePins и пр.) если
                // handler вернул их через FUnrealMCPPinResolver::MakeErrorResponse.
                // Без этого критерий "details.availablePins на failed pin resolve"
                // не работает — bridge ранее отбрасывал поле.
                const TSharedPtr<FJsonObject>* DetailsObj = nullptr;
                if (ResultJson->TryGetObjectField(TEXT("details"), DetailsObj)
                    && DetailsObj != nullptr && (*DetailsObj).IsValid())
                {
                    ResponseJson->SetObjectField(TEXT("details"), *DetailsObj);
                }
            }
        }
        catch (const std::exception& e)
        {
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
        }
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
        StateRef->Fulfill(ResultString);
        return false;   // одноразовый тикер
    }), /*InDelay=*/0.0f);

    const float TimeoutSeconds = CVarMCPCommandTimeoutSeconds.GetValueOnAnyThread();

    // Ждём короткими слайсами вместо одного Future.WaitFor(TimeoutSeconds) по
    // двум независимым причинам:
    //  1. Stop()/StopServer() ставит bShuttingDown ПЕРЕД ServerThread->Kill(true),
    //     который блокирует game thread до возврата из Run() на сокетном потоке —
    //     а Run() не вернётся, пока мы тут ждём. Без поллинга закрытие редактора
    //     с висящей командой замирало на весь TimeoutSeconds (раньше до 2 минут
    //     при 120с потолке; сейчас было бы до 5 минут при 300с).
    //  2. Каждый слайс — это и есть проверка "не появился ли уже готовый
    //     результат" ближе к реальному времени готовности, а не одним поздним
    //     чтением после целого TimeoutSeconds.
    bool bCompleted = false;
    const double DeadlineSeconds = (TimeoutSeconds > 0.0f)
        ? (FPlatformTime::Seconds() + TimeoutSeconds)
        : TNumericLimits<double>::Max();
    constexpr double WaitSliceSeconds = 0.1;

    for (;;)
    {
        if (bShuttingDown.load(std::memory_order_relaxed))
        {
            break;
        }
        const double RemainingSeconds = DeadlineSeconds - FPlatformTime::Seconds();
        if (RemainingSeconds <= 0.0)
        {
            break;
        }
        const double SliceSeconds = FMath::Min(WaitSliceSeconds, RemainingSeconds);
        if (Future.WaitFor(FTimespan::FromSeconds(SliceSeconds)))
        {
            bCompleted = true;
            break;
        }
    }

    if (bCompleted)
    {
        return Future.Get();
    }

    // Не дождались — из-за истёкшего TimeoutSeconds ИЛИ из-за bShuttingDown.
    // Финальная проверка на узкое окно "тикер как раз успел выполниться между
    // последним WaitFor слайсом и этой строкой": раньше готовый результат в
    // этом окне БЕЗУСЛОВНО отбрасывался в пользу сконструированного
    // COMMAND_TIMEOUT — ассет создан, assetPath известен, а наружу всё равно
    // уходил отказ, и вызывающий ретраил уже сделанную работу.
    if (Future.IsReady())
    {
        return Future.Get();
    }

    // StateRef->bCancelled не отменяет уже исполняющийся делегат (это
    // невозможно синхронно — команда либо ещё не стартовала, и тогда просто не
    // выполнится вовсе, либо стартовала и доиграет сама вне зависимости от
    // этого флага). Мы намеренно НЕ зовём FTSTicker::RemoveTicker: он
    // документированно блокирует ЭТОТ поток busy-Yield-циклом, если делегат
    // сейчас исполняется, — то есть тем же ожиданием, которого этот путь
    // должен избежать.
    StateRef->bCancelled.store(true, std::memory_order_relaxed);

    const bool bDueToShutdown = bShuttingDown.load(std::memory_order_relaxed);
    UE_LOG(LogTemp, Error,
        TEXT("EpicUnrealMCPBridge: command '%s' did not finish within %.0fs on the game thread (%s). ")
        TEXT("If it had already started, it will still finish in the background — this response is not that result."),
        *CommandType, TimeoutSeconds, bDueToShutdown ? TEXT("editor shutting down") : TEXT("mcp.CommandTimeoutSeconds"));

    const FString TimeoutMessage = bDueToShutdown
        ? FString::Printf(TEXT("Command '%s' was still pending when the Editor began shutting down"), *CommandType)
        : FString::Printf(
            TEXT("Command '%s' did not finish within %.0f s on the game thread (mcp.CommandTimeoutSeconds)"),
            *CommandType, TimeoutSeconds);
    const TCHAR* TimeoutCode = bDueToShutdown ? TEXT("EDITOR_SHUTTING_DOWN") : TEXT("COMMAND_TIMEOUT");

    // Отдаём код в структурном виде: обёртка разбирает верхнеуровневый `ok`
    // раньше `status`, поэтому COMMAND_TIMEOUT доходит до вызывающего как
    // код, а не тонет в безымянном BRIDGE_ERROR. Отличить «команда упала»
    // от «команда, возможно, ещё идёт — ретраить опасно» иначе нечем.
    TSharedPtr<FJsonObject> TimeoutError = MakeShareable(new FJsonObject);
    TimeoutError->SetStringField(TEXT("category"), TEXT("ue_internal"));
    TimeoutError->SetStringField(TEXT("code"), TimeoutCode);
    TimeoutError->SetStringField(TEXT("message"), TimeoutMessage);
    TimeoutError->SetObjectField(TEXT("details"), MakeShared<FJsonObject>());

    TSharedPtr<FJsonObject> TimeoutJson = MakeShareable(new FJsonObject);
    TimeoutJson->SetStringField(TEXT("status"), TEXT("error"));
    TimeoutJson->SetBoolField(TEXT("ok"), false);
    TimeoutJson->SetObjectField(TEXT("error"), TimeoutError);
    TimeoutJson->SetStringField(TEXT("code"), TimeoutCode);

    FString TimeoutString;
    TSharedRef<TJsonWriter<>> TimeoutWriter = TJsonWriterFactory<>::Create(&TimeoutString);
    FJsonSerializer::Serialize(TimeoutJson.ToSharedRef(), TimeoutWriter);

    StateRef->Fulfill(TimeoutString);
    return TimeoutString;
}