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
FString UEpicUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("EpicUnrealMCPBridge: Executing command: %s"), *CommandType);

    // Create a promise to wait for the result
    // Промис живёт в TSharedRef, а не захватывается по move: тикер-делегат
    // копируемый, mutable-лямбда с move-only состоянием в него не влезает.
    TSharedRef<TPromise<FString>> PromiseRef = MakeShared<TPromise<FString>>();
    TFuture<FString> Future = PromiseRef->GetFuture();

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
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [this, CommandType, Params, PromiseRef](float) -> bool
    {
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
                ResultJson->SetStringField(TEXT("plugin_version"), TEXT("3.24.1"));
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
                     CommandType == TEXT("add_blend_space_player_node"))
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
                    PromiseRef->SetValue(ResultString);
                    return false;   // одноразовый тикер
                }
            }
            
            // Check if the result contains an error
            bool bSuccess = true;
            FString ErrorMessage;
            
            if (ResultJson->HasField(TEXT("success")))
            {
                bSuccess = ResultJson->GetBoolField(TEXT("success"));
                if (!bSuccess && ResultJson->HasField(TEXT("error")))
                {
                    ErrorMessage = ResultJson->GetStringField(TEXT("error"));
                }
            }
            
            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
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
        PromiseRef->SetValue(ResultString);
        return false;   // одноразовый тикер
    }), /*InDelay=*/0.0f);

    return Future.Get();
}