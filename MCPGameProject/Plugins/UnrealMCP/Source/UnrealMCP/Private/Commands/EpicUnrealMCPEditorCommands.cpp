#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
// Lookdev (v3.14.0): PostProcessVolume + EViewModeIndex.
#include "Engine/PostProcessVolume.h"
#include "Engine/EngineBaseTypes.h"

FEpicUnrealMCPEditorCommands::FEpicUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor"))
    {
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    // Reflection-based actor introspection / viewport control (v1.9.0)
    else if (CommandType == TEXT("get_actor_properties"))
    {
        return HandleGetActorProperties(Params);
    }
    else if (CommandType == TEXT("set_actor_property"))
    {
        return HandleSetActorProperty(Params);
    }
    else if (CommandType == TEXT("focus_viewport"))
    {
        return HandleFocusViewport(Params);
    }
    else if (CommandType == TEXT("take_screenshot") || CommandType == TEXT("viewport_take_screenshot"))
    {
        return HandleTakeScreenshot(Params);
    }
    // Lookdev / deterministic framing (v3.14.0)
    else if (CommandType == TEXT("viewport_set_camera"))
    {
        return HandleViewportSetCamera(Params);
    }
    else if (CommandType == TEXT("viewport_get_camera"))
    {
        return HandleViewportGetCamera(Params);
    }
    else if (CommandType == TEXT("viewport_set_view_mode"))
    {
        return HandleViewportSetViewMode(Params);
    }
    else if (CommandType == TEXT("viewport_capture"))
    {
        return HandleViewportCapture(Params);
    }
    else if (CommandType == TEXT("actor_get_component_properties"))
    {
        return HandleActorGetComponentProperties(Params);
    }
    else if (CommandType == TEXT("actor_set_component_property"))
    {
        return HandleActorSetComponentProperty(Params);
    }
    else if (CommandType == TEXT("postprocess_get_params"))
    {
        return HandlePostProcessGetParams(Params);
    }
    else if (CommandType == TEXT("postprocess_set_params"))
    {
        return HandlePostProcessSetParams(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(Pattern))
        {
            MatchingActors.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
        if (NewMeshActor)
        {
            // Check for an optional static_mesh parameter to assign a mesh
            FString MeshPath;
            if (Params->TryGetStringField(TEXT("static_mesh"), MeshPath))
            {
                UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
                if (Mesh)
                {
                    NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Could not find static mesh at path: %s"), *MeshPath);
                }
            }
        }
        NewActor = NewMeshActor;
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

        // Return the created actor's details
        return FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FEpicUnrealMCPCommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FEpicUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // This function will now correctly call the implementation in BlueprintCommands
    FEpicUnrealMCPBlueprintCommands BlueprintCommands;
    return BlueprintCommands.HandleCommand(TEXT("spawn_blueprint_actor"), Params);
}

// ---------------------------------------------------------------------------
// v1.9.0 — Reflection-based actor introspection / viewport control
// ---------------------------------------------------------------------------

namespace
{
    // Helper: locate an actor in the editor world by its FName::ToString() (or label).
    static AActor* FindActorByNameLocal(const FString& ActorName)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
        if (!World)
        {
            return nullptr;
        }

        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Actor : AllActors)
        {
            if (!Actor)
            {
                continue;
            }
            if (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName)
            {
                return Actor;
            }
        }
        return nullptr;
    }
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    AActor* TargetActor = FindActorByNameLocal(ActorName);
    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPEditorCommands::HandleGetActorProperties: Reading reflection properties from '%s'"), *ActorName);

    TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();

    // Walk all reflected properties on the actor's class and emit name -> exported value
    for (TFieldIterator<FProperty> PropIt(TargetActor->GetClass()); PropIt; ++PropIt)
    {
        FProperty* Property = *PropIt;
        if (!Property)
        {
            continue;
        }

        const FString PropertyName = Property->GetName();
        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetActor);

        FString ExportedValue;
        Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, TargetActor, PPF_None);

        PropertiesObj->SetStringField(PropertyName, ExportedValue);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), ActorName);
    ResultObj->SetStringField(TEXT("class"), TargetActor->GetClass()->GetName());
    ResultObj->SetObjectField(TEXT("properties"), PropertiesObj);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    if (!Params->HasField(TEXT("property_value")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    }

    AActor* TargetActor = FindActorByNameLocal(ActorName);
    if (!TargetActor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPEditorCommands::HandleSetActorProperty: '%s'.'%s'"), *ActorName, *PropertyName);

    TSharedPtr<FJsonValue> ValueJson = Params->Values.FindRef(TEXT("property_value"));
    FString ErrorMessage;
    const bool bOk = FEpicUnrealMCPCommonUtils::SetObjectProperty(TargetActor, PropertyName, ValueJson, ErrorMessage);
    if (!bOk)
    {
        // Fallback: handle string types and unknown structs via ImportText_Direct
        FProperty* Property = TargetActor->GetClass()->FindPropertyByName(*PropertyName);
        if (Property)
        {
            FString StringValue = ValueJson.IsValid() ? ValueJson->AsString() : FString();
            void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(TargetActor);
            const TCHAR* ImportResult = Property->ImportText_Direct(*StringValue, PropertyAddr, TargetActor, PPF_None);
            if (ImportResult)
            {
                TargetActor->MarkPackageDirty();
                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("name"), ActorName);
                ResultObj->SetStringField(TEXT("property"), PropertyName);
                ResultObj->SetStringField(TEXT("value"), StringValue);
                return ResultObj;
            }
        }
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage.IsEmpty()
            ? FString::Printf(TEXT("Failed to set property '%s' on actor '%s'"), *PropertyName, *ActorName)
            : ErrorMessage);
    }

    TargetActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), ActorName);
    ResultObj->SetStringField(TEXT("property"), PropertyName);
    ResultObj->SetBoolField(TEXT("updated"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor not available"));
    }

    FString TargetName;
    const bool bHasTarget = Params->TryGetStringField(TEXT("target"), TargetName) && !TargetName.IsEmpty();

    if (bHasTarget)
    {
        AActor* TargetActor = FindActorByNameLocal(TargetName);
        if (!TargetActor)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *TargetName));
        }

        UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPEditorCommands::HandleFocusViewport: Focusing on actor '%s'"), *TargetName);

        TArray<AActor*> ActorsToFocus;
        ActorsToFocus.Add(TargetActor);
        GEditor->MoveViewportCamerasToActor(ActorsToFocus, /*bActiveViewportOnly=*/false);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("focused_actor"), TargetName);
        return ResultObj;
    }

    // No target actor — focus on supplied world-space location box
    if (!Params->HasField(TEXT("location")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Either 'target' or 'location' must be provided"));
    }

    FVector Center = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    double Distance = 1000.0;
    if (Params->HasField(TEXT("distance")))
    {
        Distance = Params->GetNumberField(TEXT("distance"));
    }

    const FVector Extent(Distance, Distance, Distance);
    const FBox FocusBox(Center - Extent, Center + Extent);

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPEditorCommands::HandleFocusViewport: Focusing on box around (%f, %f, %f)"),
        Center.X, Center.Y, Center.Z);

    GEditor->MoveViewportCamerasToBox(FocusBox, /*bActiveViewportOnly=*/false);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> LocationArr;
    LocationArr.Add(MakeShared<FJsonValueNumber>(Center.X));
    LocationArr.Add(MakeShared<FJsonValueNumber>(Center.Y));
    LocationArr.Add(MakeShared<FJsonValueNumber>(Center.Z));
    ResultObj->SetArrayField(TEXT("focused_location"), LocationArr);
    ResultObj->SetNumberField(TEXT("distance"), Distance);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // Параметры:
    //   filename       — относительное имя файла (по умолчанию "MCPScreenshot.png").
    //                    Полный path = <ProjectSaved>/Screenshots/<filename>.
    //   show_ui        — bool, по умолчанию false. true = захватить UI overlays.
    //   resolution_x   — int, опционально. Если задан вместе с resolution_y —
    //                    использует hi-res путь FHighResScreenshotConfig.
    //   resolution_y   — int, опционально.
    //
    // Захват асинхронный: FScreenshotRequest триггерится в Tick, файл появится
    // в течение 1-2 кадров. Возвращаем планируемый path сразу.
    FString Filename = TEXT("MCPScreenshot.png");
    Params->TryGetStringField(TEXT("filename"), Filename);
    if (!Filename.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        Filename += TEXT(".png");
    }

    bool bShowUI = false;
    Params->TryGetBoolField(TEXT("show_ui"), bShowUI);

    int32 ResolutionX = 0;
    int32 ResolutionY = 0;
    Params->TryGetNumberField(TEXT("resolution_x"), ResolutionX);
    Params->TryGetNumberField(TEXT("resolution_y"), ResolutionY);

    const FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
    IFileManager::Get().MakeDirectory(*ScreenshotDir, /*Tree=*/true);
    const FString FullPath = ScreenshotDir / Filename;

    // Если уже есть файл с таким именем — удаляем, чтобы вызывающий мог
    // надёжно poll'ить mtime/exists и не путать со старым кадром.
    if (IFileManager::Get().FileExists(*FullPath))
    {
        IFileManager::Get().Delete(*FullPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
    }

    if (ResolutionX > 0 && ResolutionY > 0)
    {
        // Hi-res путь — FHighResScreenshotConfig.
        FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
        Config.SetResolution(ResolutionX, ResolutionY, /*ResMult=*/1.0f);
        Config.bCaptureHDR = false;
        Config.FilenameOverride = FullPath;
    }

    FScreenshotRequest::RequestScreenshot(FullPath, bShowUI, /*bAddFilenameSuffix=*/false);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("created"));
    ResultObj->SetStringField(TEXT("assetPath"), FullPath);
    ResultObj->SetStringField(TEXT("filename"), Filename);
    ResultObj->SetBoolField(TEXT("show_ui"), bShowUI);
    if (ResolutionX > 0 && ResolutionY > 0)
    {
        ResultObj->SetNumberField(TEXT("resolution_x"), ResolutionX);
        ResultObj->SetNumberField(TEXT("resolution_y"), ResolutionY);
    }
    ResultObj->SetStringField(
        TEXT("note"),
        TEXT("Screenshot queued asynchronously; file will appear within 1-2 ticks"));
    return ResultObj;
}

// ---------------------------------------------------------------------------
// Lookdev / deterministic framing (v3.14.0)
// ---------------------------------------------------------------------------

namespace
{
    /**
     * Активный перспективный level-вьюпорт. Ортографические (Top/Front/Side)
     * пропускаются: лукдев-кадр всегда перспективный, а SetViewLocation на
     * орто-клиенте даёт неинтуитивный результат.
     */
    FLevelEditorViewportClient* GetPerspectiveViewportClient()
    {
        if (!GEditor)
        {
            return nullptr;
        }

        // Сначала пробуем клиент, с которым пользователь работает прямо сейчас.
        if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
        {
            return GCurrentLevelEditingViewportClient;
        }

        for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
        {
            if (Client && Client->IsPerspective())
            {
                return Client;
            }
        }
        return nullptr;
    }

    /** Строка режима из MCP -> EViewModeIndex. */
    bool ParseViewMode(const FString& Name, EViewModeIndex& OutMode)
    {
        const FString Key = Name.ToLower();
        if (Key == TEXT("lit"))                 { OutMode = VMI_Lit;                    return true; }
        if (Key == TEXT("unlit"))               { OutMode = VMI_Unlit;                  return true; }
        if (Key == TEXT("wireframe"))           { OutMode = VMI_Wireframe;              return true; }
        if (Key == TEXT("detaillighting"))      { OutMode = VMI_Lit_DetailLighting;     return true; }
        if (Key == TEXT("lightingonly"))        { OutMode = VMI_LightingOnly;           return true; }
        if (Key == TEXT("shadercomplexity"))    { OutMode = VMI_ShaderComplexity;       return true; }
        if (Key == TEXT("pathtracing"))         { OutMode = VMI_PathTracing;            return true; }
        return false;
    }

    /**
     * Запись JSON-значения в свойство FPostProcessSettings по рефлексии.
     * Поддержаны float/double/int/bool и struct'ы FVector/FVector4/FLinearColor
     * (в JSON — массив из 3-4 чисел).
     */
    bool SetPostProcessProperty(FProperty* Prop, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
    {
        if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(JsonValue->AsNumber()));
            return true;
        }
        if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
        {
            DoubleProp->SetPropertyValue(ValuePtr, JsonValue->AsNumber());
            return true;
        }
        if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(JsonValue->AsNumber()));
            return true;
        }
        if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            BoolProp->SetPropertyValue(ValuePtr, JsonValue->AsBool());
            return true;
        }
        if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (!JsonValue->TryGetArray(Arr) || Arr->Num() < 3)
            {
                OutError = FString::Printf(TEXT("Property '%s' is a struct and expects an array of 3-4 numbers"), *Prop->GetName());
                return false;
            }

            const double X = (*Arr)[0]->AsNumber();
            const double Y = (*Arr)[1]->AsNumber();
            const double Z = (*Arr)[2]->AsNumber();
            const double W = Arr->Num() > 3 ? (*Arr)[3]->AsNumber() : 1.0;

            const UScriptStruct* Struct = StructProp->Struct;
            if (Struct == TBaseStructure<FVector4>::Get())
            {
                *static_cast<FVector4*>(ValuePtr) = FVector4(X, Y, Z, W);
                return true;
            }
            if (Struct == TBaseStructure<FVector>::Get())
            {
                *static_cast<FVector*>(ValuePtr) = FVector(X, Y, Z);
                return true;
            }
            if (Struct == TBaseStructure<FLinearColor>::Get())
            {
                *static_cast<FLinearColor*>(ValuePtr) = FLinearColor(
                    static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z), static_cast<float>(W));
                return true;
            }

            OutError = FString::Printf(TEXT("Unsupported struct type for property '%s': %s"), *Prop->GetName(), *Struct->GetName());
            return false;
        }

        OutError = FString::Printf(TEXT("Unsupported property type for '%s'"), *Prop->GetName());
        return false;
    }

    /** Чтение свойства FPostProcessSettings в JSON. nullptr = тип не поддержан. */
    TSharedPtr<FJsonValue> GetPostProcessProperty(FProperty* Prop, const void* ValuePtr)
    {
        if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
        {
            return MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(ValuePtr));
        }
        if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
        {
            return MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(ValuePtr));
        }
        if (const FIntProperty* IntProp = CastField<FIntProperty>(Prop))
        {
            return MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(ValuePtr));
        }
        if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
        {
            return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
        }
        if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
        {
            TArray<TSharedPtr<FJsonValue>> Arr;
            const UScriptStruct* Struct = StructProp->Struct;
            if (Struct == TBaseStructure<FVector4>::Get())
            {
                const FVector4& V = *static_cast<const FVector4*>(ValuePtr);
                Arr.Add(MakeShared<FJsonValueNumber>(V.X));
                Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
                Arr.Add(MakeShared<FJsonValueNumber>(V.Z));
                Arr.Add(MakeShared<FJsonValueNumber>(V.W));
                return MakeShared<FJsonValueArray>(Arr);
            }
            if (Struct == TBaseStructure<FVector>::Get())
            {
                const FVector& V = *static_cast<const FVector*>(ValuePtr);
                Arr.Add(MakeShared<FJsonValueNumber>(V.X));
                Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
                Arr.Add(MakeShared<FJsonValueNumber>(V.Z));
                return MakeShared<FJsonValueArray>(Arr);
            }
            if (Struct == TBaseStructure<FLinearColor>::Get())
            {
                const FLinearColor& C = *static_cast<const FLinearColor*>(ValuePtr);
                Arr.Add(MakeShared<FJsonValueNumber>(C.R));
                Arr.Add(MakeShared<FJsonValueNumber>(C.G));
                Arr.Add(MakeShared<FJsonValueNumber>(C.B));
                Arr.Add(MakeShared<FJsonValueNumber>(C.A));
                return MakeShared<FJsonValueArray>(Arr);
            }
        }
        return nullptr;
    }
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleViewportSetCamera(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* Client = GetPerspectiveViewportClient();
    if (!Client)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport client available"));
    }

    if (Params->HasField(TEXT("location")))
    {
        Client->SetViewLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }

    if (Params->HasField(TEXT("rotation")))
    {
        const FVector RotVec = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("rotation"));
        // Порядок как у UE-редактора и Python API: [pitch, yaw, roll].
        Client->SetViewRotation(FRotator(RotVec.X, RotVec.Y, RotVec.Z));
    }

    double FOV = 0.0;
    if (Params->TryGetNumberField(TEXT("fov"), FOV) && FOV > 0.0)
    {
        Client->ViewFOV = static_cast<float>(FOV);
    }

    Client->Invalidate();

    const FVector OutLoc = Client->GetViewLocation();
    const FRotator OutRot = Client->GetViewRotation();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("updated"));
    TArray<TSharedPtr<FJsonValue>> LocArr;
    LocArr.Add(MakeShared<FJsonValueNumber>(OutLoc.X));
    LocArr.Add(MakeShared<FJsonValueNumber>(OutLoc.Y));
    LocArr.Add(MakeShared<FJsonValueNumber>(OutLoc.Z));
    ResultObj->SetArrayField(TEXT("location"), LocArr);
    TArray<TSharedPtr<FJsonValue>> RotArr;
    RotArr.Add(MakeShared<FJsonValueNumber>(OutRot.Pitch));
    RotArr.Add(MakeShared<FJsonValueNumber>(OutRot.Yaw));
    RotArr.Add(MakeShared<FJsonValueNumber>(OutRot.Roll));
    ResultObj->SetArrayField(TEXT("rotation"), RotArr);
    ResultObj->SetNumberField(TEXT("fov"), Client->ViewFOV);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleViewportGetCamera(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* Client = GetPerspectiveViewportClient();
    if (!Client)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport client available"));
    }

    const FVector Loc = Client->GetViewLocation();
    const FRotator Rot = Client->GetViewRotation();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("ok"));
    TArray<TSharedPtr<FJsonValue>> LocArr;
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
    ResultObj->SetArrayField(TEXT("location"), LocArr);
    TArray<TSharedPtr<FJsonValue>> RotArr;
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
    ResultObj->SetArrayField(TEXT("rotation"), RotArr);
    ResultObj->SetNumberField(TEXT("fov"), Client->ViewFOV);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleViewportSetViewMode(const TSharedPtr<FJsonObject>& Params)
{
    FString ModeName;
    if (!Params->TryGetStringField(TEXT("mode"), ModeName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'mode' parameter"));
    }

    EViewModeIndex Mode = VMI_Lit;
    if (!ParseViewMode(ModeName, Mode))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Unknown view mode '%s'. Supported: lit, unlit, wireframe, detaillighting, lightingonly, shadercomplexity, pathtracing"),
            *ModeName));
    }

    FLevelEditorViewportClient* Client = GetPerspectiveViewportClient();
    if (!Client)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport client available"));
    }

    Client->SetViewMode(Mode);
    Client->Invalidate();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("updated"));
    ResultObj->SetStringField(TEXT("mode"), ModeName.ToLower());
    if (Mode == VMI_PathTracing)
    {
        // Path Tracer молча деградирует до чёрного/шумного кадра, если в
        // проекте выключен ray tracing — предупреждаем вызывающего явно.
        ResultObj->SetStringField(
            TEXT("note"),
            TEXT("Path Tracing requires ray tracing enabled in project settings; allow the frame to accumulate samples before capture"));
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandlePostProcessGetParams(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    AActor* Actor = FindActorByNameLocal(ActorName);
    if (!Actor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor);
    if (!Volume)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Actor '%s' is not a PostProcessVolume"), *ActorName));
    }

    FPostProcessSettings& Settings = Volume->Settings;
    UScriptStruct* SettingsStruct = FPostProcessSettings::StaticStruct();

    // Возвращаем ТОЛЬКО переопределённые параметры: полный дамп
    // FPostProcessSettings — сотни полей, из которых на кадр влияют единицы.
    TSharedPtr<FJsonObject> Overridden = MakeShared<FJsonObject>();
    int32 OverrideCount = 0;

    for (TFieldIterator<FProperty> It(SettingsStruct); It; ++It)
    {
        FProperty* Prop = *It;
        const FString PropName = Prop->GetName();
        if (PropName.StartsWith(TEXT("bOverride_")))
        {
            continue;
        }

        FBoolProperty* OverrideProp = FindFProperty<FBoolProperty>(SettingsStruct, *(TEXT("bOverride_") + PropName));
        if (!OverrideProp)
        {
            continue;
        }
        if (!OverrideProp->GetPropertyValue(OverrideProp->ContainerPtrToValuePtr<void>(&Settings)))
        {
            continue;
        }

        TSharedPtr<FJsonValue> Value = GetPostProcessProperty(Prop, Prop->ContainerPtrToValuePtr<void>(&Settings));
        if (Value.IsValid())
        {
            Overridden->SetField(PropName, Value);
            ++OverrideCount;
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("ok"));
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetBoolField(TEXT("unbound"), Volume->bUnbound);
    ResultObj->SetNumberField(TEXT("priority"), Volume->Priority);
    ResultObj->SetNumberField(TEXT("blend_weight"), Volume->BlendWeight);
    ResultObj->SetObjectField(TEXT("overridden"), Overridden);
    ResultObj->SetNumberField(TEXT("override_count"), OverrideCount);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandlePostProcessSetParams(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    const TSharedPtr<FJsonObject>* SettingsJson = nullptr;
    if (!Params->TryGetObjectField(TEXT("settings"), SettingsJson) || !SettingsJson)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'settings' object parameter"));
    }

    AActor* Actor = FindActorByNameLocal(ActorName);
    if (!Actor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    APostProcessVolume* Volume = Cast<APostProcessVolume>(Actor);
    if (!Volume)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Actor '%s' is not a PostProcessVolume"), *ActorName));
    }

    FPostProcessSettings& Settings = Volume->Settings;
    UScriptStruct* SettingsStruct = FPostProcessSettings::StaticStruct();

    Volume->Modify();

    TArray<TSharedPtr<FJsonValue>> Applied;
    TArray<TSharedPtr<FJsonValue>> Failed;

    for (const auto& Pair : (*SettingsJson)->Values)
    {
        // UE 5.8: ключ FJsonObject::Values — UE::FSharedString, а не FString;
        // неявного преобразования нет, разыменовываем в TCHAR*.
        const FString Key(*Pair.Key);

        FProperty* Prop = FindFProperty<FProperty>(SettingsStruct, *Key);
        if (!Prop)
        {
            Failed.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: no such property on FPostProcessSettings"), *Key)));
            continue;
        }

        FString Error;
        if (!SetPostProcessProperty(Prop, Prop->ContainerPtrToValuePtr<void>(&Settings), Pair.Value, Error))
        {
            Failed.Add(MakeShared<FJsonValueString>(Error));
            continue;
        }

        // Без парного bOverride_* движок игнорирует записанное значение —
        // это и есть причина, по которой generic set_actor_property здесь
        // "срабатывает", но кадр не меняется.
        if (FBoolProperty* OverrideProp = FindFProperty<FBoolProperty>(SettingsStruct, *(TEXT("bOverride_") + Key)))
        {
            OverrideProp->SetPropertyValue(OverrideProp->ContainerPtrToValuePtr<void>(&Settings), true);
        }

        Applied.Add(MakeShared<FJsonValueString>(Key));
    }

    bool bUnbound = false;
    if (Params->TryGetBoolField(TEXT("unbound"), bUnbound))
    {
        Volume->bUnbound = bUnbound;
    }
    double Priority = 0.0;
    if (Params->TryGetNumberField(TEXT("priority"), Priority))
    {
        Volume->Priority = static_cast<float>(Priority);
    }
    double BlendWeight = 0.0;
    if (Params->TryGetNumberField(TEXT("blend_weight"), BlendWeight))
    {
        Volume->BlendWeight = static_cast<float>(BlendWeight);
    }

    Volume->PostEditChange();
    Volume->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), Failed.Num() == 0 ? TEXT("updated") : TEXT("partial"));
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetArrayField(TEXT("applied"), Applied);
    ResultObj->SetArrayField(TEXT("failed"), Failed);
    ResultObj->SetStringField(
        TEXT("note"),
        TEXT("Level is dirty; call save_level to persist changes"));
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleViewportCapture(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* Client = GetPerspectiveViewportClient();
    if (!Client)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport client available"));
    }

    FViewport* Viewport = Client->Viewport;
    if (!Viewport)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Viewport client has no FViewport"));
    }

    FString Filename = TEXT("MCPViewport.png");
    Params->TryGetStringField(TEXT("filename"), Filename);
    if (!Filename.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        Filename += TEXT(".png");
    }

    const FString ShotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
    IFileManager::Get().MakeDirectory(*ShotDir, /*Tree=*/true);
    const FString FullPath = FPaths::ConvertRelativePathToFull(ShotDir / Filename);

    // Форсируем перерисовку: после viewport_set_camera / view_mode вьюпорт
    // помечен грязным, и без Draw() мы прочитали бы предыдущий кадр.
    Client->Invalidate();
    Viewport->Draw(/*bShouldPresent=*/true);

    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Viewport has zero size"));
    }

    TArray<FColor> Pixels;
    const FIntRect Rect(0, 0, Size.X, Size.Y);
    if (!Viewport->ReadPixels(Pixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), Rect))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Viewport ReadPixels failed"));
    }
    if (Pixels.Num() < Size.X * Size.Y)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Viewport returned fewer pixels than expected"));
    }

    // Readback часто отдаёт нулевую альфу — PNG с alpha=0 выглядит пустым в
    // любом просмотрщике, поэтому принудительно делаем кадр непрозрачным.
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = 255;
    }

    TArray64<uint8> PngData;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PngData);
    if (PngData.Num() == 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PNG compression produced no data"));
    }

    if (!FFileHelper::SaveArrayToFile(PngData, *FullPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to write %s"), *FullPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("created"));
    ResultObj->SetStringField(TEXT("assetPath"), FullPath);
    ResultObj->SetStringField(TEXT("filename"), Filename);
    ResultObj->SetNumberField(TEXT("width"), Size.X);
    ResultObj->SetNumberField(TEXT("height"), Size.Y);
    ResultObj->SetNumberField(TEXT("bytes"), PngData.Num());
    // В отличие от take_screenshot захват синхронный — polling не нужен.
    ResultObj->SetStringField(TEXT("note"), TEXT("Synchronous capture; file exists on return. Size follows the viewport."));
    return ResultObj;
}

// ---------------------------------------------------------------------------
// Свойства компонентов level-акторов (v3.15.0)
// ---------------------------------------------------------------------------

namespace
{
    /** Компонент актора по имени; при пустом имени — RootComponent. */
    UActorComponent* FindActorComponentByName(AActor* Actor, const FString& ComponentName)
    {
        if (!Actor)
        {
            return nullptr;
        }
        if (ComponentName.IsEmpty())
        {
            return Actor->GetRootComponent();
        }
        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
            {
                return Component;
            }
        }
        return nullptr;
    }
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleActorGetComponentProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    AActor* Actor = FindActorByNameLocal(ActorName);
    if (!Actor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    FString ComponentName;
    Params->TryGetStringField(TEXT("component_name"), ComponentName);

    // Без component_name перечисляем компоненты — так вызывающий узнаёт имена,
    // не угадывая их.
    if (ComponentName.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> CompArr;
        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (!Component)
            {
                continue;
            }
            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Component->GetName());
            CompObj->SetStringField(TEXT("class"), Component->GetClass()->GetName());
            CompArr.Add(MakeShared<FJsonValueObject>(CompObj));
        }
        TSharedPtr<FJsonObject> ListObj = MakeShared<FJsonObject>();
        ListObj->SetStringField(TEXT("status"), TEXT("ok"));
        ListObj->SetStringField(TEXT("actor_name"), ActorName);
        ListObj->SetArrayField(TEXT("components"), CompArr);
        return ListObj;
    }

    UActorComponent* Component = FindActorComponentByName(Actor, ComponentName);
    if (!Component)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorName));
    }

    // Опциональный фильтр по подстроке в имени свойства: у
    // UDirectionalLightComponent сотни полей, полный дамп бесполезен.
    FString Filter;
    Params->TryGetStringField(TEXT("filter"), Filter);

    TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Component->GetClass()); It; ++It)
    {
        FProperty* Prop = *It;
        const FString PropName = Prop->GetName();
        if (!Filter.IsEmpty() && !PropName.Contains(Filter))
        {
            continue;
        }
        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Component);
        FString Exported;
        Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
        PropsObj->SetStringField(PropName, Exported);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("ok"));
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("component_name"), Component->GetName());
    ResultObj->SetStringField(TEXT("component_class"), Component->GetClass()->GetName());
    ResultObj->SetObjectField(TEXT("properties"), PropsObj);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleActorSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    FString ComponentName;
    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName) ||
        !Params->TryGetStringField(TEXT("component_name"), ComponentName) ||
        !Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Missing 'actor_name', 'component_name' or 'property_name'"));
    }

    if (!Params->HasField(TEXT("property_value")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value'"));
    }

    AActor* Actor = FindActorByNameLocal(ActorName);
    if (!Actor)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    UActorComponent* Component = FindActorComponentByName(Actor, ComponentName);
    if (!Component)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorName));
    }

    FProperty* Prop = FindFProperty<FProperty>(Component->GetClass(), *PropertyName);
    if (!Prop)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Property '%s' not found on %s"), *PropertyName, *Component->GetClass()->GetName()));
    }

    Component->Modify();
    Actor->Modify();

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Component);
    const TSharedPtr<FJsonValue> JsonValue = Params->TryGetField(TEXT("property_value"));

    FString Error;
    bool bOk = SetPostProcessProperty(Prop, ValuePtr, JsonValue, Error);
    if (!bOk)
    {
        // Фолбэк для enum / FName / FString и прочего, что рефлексия разбирает
        // из текста, а типизированный сеттер выше — нет.
        FString AsText;
        if (JsonValue.IsValid() && JsonValue->TryGetString(AsText))
        {
            const TCHAR* Import = Prop->ImportText_Direct(*AsText, ValuePtr, nullptr, PPF_None);
            bOk = (Import != nullptr);
            if (!bOk)
            {
                Error = FString::Printf(TEXT("ImportText failed for '%s' value '%s'"), *PropertyName, *AsText);
            }
        }
    }

    if (!bOk)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    // Свету и атмосфере нужен явный пересчёт, иначе кадр не изменится до
    // следующего случайного инвалидейта.
    FPropertyChangedEvent ChangeEvent(Prop);
    Component->PostEditChangeProperty(ChangeEvent);
    if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
    {
        SceneComp->MarkRenderStateDirty();
    }
    Actor->PostEditChange();
    Actor->MarkPackageDirty();

    FString NewValue;
    Prop->ExportTextItem_Direct(NewValue, ValuePtr, nullptr, nullptr, PPF_None);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("status"), TEXT("updated"));
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("component_name"), Component->GetName());
    ResultObj->SetStringField(TEXT("property_name"), PropertyName);
    ResultObj->SetStringField(TEXT("value"), NewValue);
    ResultObj->SetStringField(TEXT("note"), TEXT("Level is dirty; call save_level to persist"));
    return ResultObj;
}
