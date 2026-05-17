#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/AssetCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "WidgetBlueprintFactory.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/Engine.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
// Phase 3A (v1.15.0) — Blueprint Interfaces
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "UObject/Interface.h"
#include "UObject/TopLevelAssetPath.h"
// Phase 3D (v1.15.0) — Compile diagnostics
#include "KismetCompilerModule.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"

FEpicUnrealMCPBlueprintCommands::FEpicUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("create_blueprint_from_template"))
    {
        return HandleCreateBlueprintFromTemplate(Params);
    }
    else if (CommandType == TEXT("reparent_blueprint"))
    {
        return HandleReparentBlueprint(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("set_mesh_material_color"))
    {
        return HandleSetMeshMaterialColor(Params);
    }
    // Material management commands
    else if (CommandType == TEXT("get_available_materials"))
    {
        return HandleGetAvailableMaterials(Params);
    }
    else if (CommandType == TEXT("apply_material_to_actor"))
    {
        return HandleApplyMaterialToActor(Params);
    }
    else if (CommandType == TEXT("apply_material_to_blueprint"))
    {
        return HandleApplyMaterialToBlueprint(Params);
    }
    else if (CommandType == TEXT("get_actor_material_info"))
    {
        return HandleGetActorMaterialInfo(Params);
    }
    else if (CommandType == TEXT("get_blueprint_material_info"))
    {
        return HandleGetBlueprintMaterialInfo(Params);
    }
    // Blueprint analysis commands
    else if (CommandType == TEXT("read_blueprint_content"))
    {
        return HandleReadBlueprintContent(Params);
    }
    else if (CommandType == TEXT("analyze_blueprint_graph"))
    {
        return HandleAnalyzeBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("get_blueprint_variable_details"))
    {
        return HandleGetBlueprintVariableDetails(Params);
    }
    else if (CommandType == TEXT("get_blueprint_function_details"))
    {
        return HandleGetBlueprintFunctionDetails(Params);
    }
    else if (CommandType == TEXT("set_blueprint_property"))
    {
        return HandleSetBlueprintProperty(Params);
    }
    else if (CommandType == TEXT("set_component_property"))
    {
        return HandleSetComponentProperty(Params);
    }
    // Component lifecycle (Phase 1A — v1.11.0)
    else if (CommandType == TEXT("delete_component_from_blueprint"))
    {
        return HandleDeleteComponentFromBlueprint(Params);
    }
    else if (CommandType == TEXT("rename_component"))
    {
        return HandleRenameComponent(Params);
    }
    else if (CommandType == TEXT("list_components"))
    {
        return HandleListComponents(Params);
    }
    else if (CommandType == TEXT("set_component_transform"))
    {
        return HandleSetComponentTransform(Params);
    }
    // Discovery (Phase 1E — v1.12.0)
    else if (CommandType == TEXT("list_blueprints"))
    {
        return HandleListBlueprints(Params);
    }
    else if (CommandType == TEXT("get_blueprint_class_info"))
    {
        return HandleGetBlueprintClassInfo(Params);
    }
    // Phase 3A (v1.15.0) — Blueprint Interfaces
    else if (CommandType == TEXT("create_blueprint_interface"))
    {
        return HandleCreateBlueprintInterface(Params);
    }
    else if (CommandType == TEXT("implement_blueprint_interface"))
    {
        return HandleImplementBlueprintInterface(Params);
    }
    else if (CommandType == TEXT("remove_blueprint_interface"))
    {
        return HandleRemoveBlueprintInterface(Params);
    }
    else if (CommandType == TEXT("add_interface_function"))
    {
        return HandleAddInterfaceFunction(Params);
    }
    // Phase 3D (v1.15.0) — Compile diagnostics
    else if (CommandType == TEXT("compile_blueprint_verbose"))
    {
        return HandleCompileBlueprintVerbose(Params);
    }
    else if (CommandType == TEXT("validate_blueprint"))
    {
        return HandleValidateBlueprint(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Read optional folder_path and normalize to /Game/<...>/ form
    FString PackagePath = TEXT("/Game/Blueprints/");
    FString FolderPathRaw;
    if (Params->TryGetStringField(TEXT("folder_path"), FolderPathRaw) && !FolderPathRaw.IsEmpty())
    {
        FolderPathRaw.TrimStartAndEndInline();

        // If the user omitted the /Game/ prefix, prepend it
        if (!FolderPathRaw.StartsWith(TEXT("/Game/")) && !FolderPathRaw.StartsWith(TEXT("Game/")))
        {
            FolderPathRaw = TEXT("/Game/") + FolderPathRaw;
        }
        // Handle bare "Game/..." -> "/Game/..."
        if (FolderPathRaw.StartsWith(TEXT("Game/")))
        {
            FolderPathRaw = TEXT("/") + FolderPathRaw;
        }

        // Ensure leading /
        if (!FolderPathRaw.StartsWith(TEXT("/")))
        {
            FolderPathRaw = TEXT("/") + FolderPathRaw;
        }
        // Ensure trailing /
        if (!FolderPathRaw.EndsWith(TEXT("/")))
        {
            FolderPathRaw += TEXT("/");
        }

        PackagePath = FolderPathRaw;
    }

    FString AssetName = BlueprintName;
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath + AssetName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);

    // Resolve parent class — поддерживает Actor (A-prefix), Widget (U-prefix) и полные пути
    UClass* SelectedParentClass = AActor::StaticClass();

    if (!ParentClass.IsEmpty())
    {
        UClass* FoundClass = nullptr;

        // Прямые совпадения для часто используемых классов
        if (ParentClass == TEXT("UserWidget") || ParentClass == TEXT("UUserWidget"))
        {
            FoundClass = UUserWidget::StaticClass();
        }
        else if (ParentClass == TEXT("Pawn") || ParentClass == TEXT("APawn"))
        {
            FoundClass = APawn::StaticClass();
        }
        else if (ParentClass == TEXT("Actor") || ParentClass == TEXT("AActor"))
        {
            FoundClass = AActor::StaticClass();
        }
        else
        {
            // Попытка загрузить по полному пути или через стандартные модули
            const TArray<FString> SearchPaths = {
                ParentClass,
                FString::Printf(TEXT("/Script/UMG.%s"), *ParentClass),
                FString::Printf(TEXT("/Script/Engine.%s"), *ParentClass),
                FString::Printf(TEXT("/Script/Client.%s"), *ParentClass),
            };

            for (const FString& Path : SearchPaths)
            {
                FoundClass = LoadClass<UObject>(nullptr, *Path);
                if (FoundClass)
                {
                    break;
                }
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ParentClass);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find parent class '%s', defaulting to AActor"), *ParentClass);
        }
    }

    // Выбор фабрики и типа создаваемого blueprint'а в зависимости от родительского класса.
    // UWidgetBlueprintFactory::FactoryCreateNew assert'ит, что передан UWidgetBlueprint::StaticClass(),
    // а UBlueprintFactory требует UBlueprint::StaticClass() — разный класс для разных фабрик.
    UFactory* Factory = nullptr;
    UClass* BlueprintClassToCreate = UBlueprint::StaticClass();
    if (SelectedParentClass->IsChildOf(UUserWidget::StaticClass()))
    {
        UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
        WidgetFactory->ParentClass = SelectedParentClass;
        Factory = WidgetFactory;
        BlueprintClassToCreate = UWidgetBlueprint::StaticClass();
    }
    else
    {
        UBlueprintFactory* ActorFactory = NewObject<UBlueprintFactory>();
        ActorFactory->ParentClass = SelectedParentClass;
        Factory = ActorFactory;
    }

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(BlueprintClassToCreate, Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        // Save the asset to disk immediately so it persists without a manual Save All
        FString FullAssetPath = PackagePath + AssetName;
        if (!UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/ false))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Blueprint created but failed to save asset: %s"), *FullAssetPath));
        }

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), FullAssetPath);
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReparentBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NewParentClass;
    if (!Params->TryGetStringField(TEXT("new_parent_class"), NewParentClass))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
    }

    // ── Load the Blueprint (supports short name or full /Game/... path) ──────
    UBlueprint* Blueprint = nullptr;
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    if (!Blueprint)
    {
        Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    }
    // Widget-blueprint fallback: try /Game/UI/, /Game/Widgets/, /Game/UI/Widgets/ too.
    if (!Blueprint)
    {
        const TArray<FString> SearchPaths = {
            TEXT("/Game/UI/"),
            TEXT("/Game/Widgets/"),
            TEXT("/Game/UI/Widgets/"),
        };
        for (const FString& Dir : SearchPaths)
        {
            const FString ObjectPath = Dir + BlueprintName + TEXT(".") + BlueprintName;
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
            {
                Blueprint = Cast<UBlueprint>(Asset);
                if (Blueprint) break;
            }
        }
    }

    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // ── Resolve new parent class — same lookup pattern as HandleCreateBlueprint ──
    UClass* FoundClass = nullptr;

    if (NewParentClass == TEXT("UserWidget") || NewParentClass == TEXT("UUserWidget"))
    {
        FoundClass = UUserWidget::StaticClass();
    }
    else if (NewParentClass == TEXT("Pawn") || NewParentClass == TEXT("APawn"))
    {
        FoundClass = APawn::StaticClass();
    }
    else if (NewParentClass == TEXT("Actor") || NewParentClass == TEXT("AActor"))
    {
        FoundClass = AActor::StaticClass();
    }
    else
    {
        const TArray<FString> SearchPaths = {
            NewParentClass,
            FString::Printf(TEXT("/Script/UMG.%s"), *NewParentClass),
            FString::Printf(TEXT("/Script/Engine.%s"), *NewParentClass),
            FString::Printf(TEXT("/Script/Client.%s"), *NewParentClass),
        };

        for (const FString& Path : SearchPaths)
        {
            FoundClass = LoadClass<UObject>(nullptr, *Path);
            if (FoundClass)
            {
                break;
            }
        }
    }

    if (!FoundClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not resolve parent class: %s"), *NewParentClass));
    }

    // ── Perform the reparent ─────────────────────────────────────────────────
    // Standard UE flow: swap ParentClass → refresh all nodes (rebuilds graphs against new
    // parent's functions/variables) → recompile so the generated class matches.
    UClass* OldParent = Blueprint->ParentClass;
    Blueprint->ParentClass = FoundClass;

    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    if (UPackage* Pkg = Blueprint->GetPackage())
    {
        Pkg->MarkPackageDirty();
    }

    // Save asset. Strip trailing ".Name" from object path to get package name.
    FString PackageName = Blueprint->GetPathName();
    int32 DotIdx;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/ false);

    // ── Build response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("old_parent_class"), OldParent ? OldParent->GetName() : TEXT("None"));
    Result->SetStringField(TEXT("new_parent_class"), FoundClass->GetName());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create the component - dynamically find the component class by name.
    //
    // UE 5.7 NOTE: `FindObject<UClass>(nullptr, *ShortName)` perestal rabotat'
    // dlya klassov iz /Script/Engine, /Script/UMG i t.d. — ranshe rabotal cherez
    // ANY_PACKAGE, kotoryj deprecated. Pravil'nyj API:
    //   1) FindFirstObject<UClass>(ShortName, ...) — global'nyj resolve po imeni.
    //   2) Esli ne najdeno — LoadObject<UClass>(nullptr, TEXT("/Script/<Module>.<Name>")).
    //   3) Vsegda proveryaem IsChildOf(UActorComponent).
    UClass* ComponentClass = nullptr;

    // Stroim spisok imen-kandidatov: kak prishlo, s suffiksom Component,
    // s prefiksom U, i obe modifikatsii vmeste.
    TArray<FString> NameCandidates;
    NameCandidates.Add(ComponentType);
    if (!ComponentType.EndsWith(TEXT("Component")))
    {
        NameCandidates.Add(ComponentType + TEXT("Component"));
    }
    if (!ComponentType.StartsWith(TEXT("U")))
    {
        NameCandidates.Add(TEXT("U") + ComponentType);
        if (!ComponentType.EndsWith(TEXT("Component")))
        {
            NameCandidates.Add(TEXT("U") + ComponentType + TEXT("Component"));
        }
    }
    // Esli prishlo s U-prefiksom (UStaticMeshComponent), takzhe probuem bez nego —
    // FindFirstObject ishchet po realnomu imeni klassa, kotoroe v UE huddraneno
    // bez prefiksa (StaticMeshComponent), no nekotorye refleksive-tablitsy mogut
    // soderzhat' i s prefiksom. Doblavlyaem bez U dlya nadezhnosti.
    if (ComponentType.StartsWith(TEXT("U")) && ComponentType.Len() > 1)
    {
        NameCandidates.Add(ComponentType.Mid(1));
    }

    // Faza 1 — FindFirstObject po korotkomu imeni.
    for (const FString& Name : NameCandidates)
    {
        ComponentClass = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::None,
            ELogVerbosity::Warning, TEXT("MCP AddComponentToBlueprint"));
        if (ComponentClass && ComponentClass->IsChildOf(UActorComponent::StaticClass()))
        {
            break;
        }
        ComponentClass = nullptr;
    }

    // Faza 2 — LoadObject po polnomu path'u v izvestnyh modulyah.
    if (!ComponentClass)
    {
        static const TCHAR* ModulePaths[] = {
            TEXT("/Script/Engine."),
            TEXT("/Script/UMG."),
            TEXT("/Script/UMGEditor."),
            TEXT("/Script/PhysicsCore."),
            TEXT("/Script/CinematicCamera."),
            TEXT("/Script/Niagara."),
            TEXT("/Script/AIModule."),
            TEXT("/Script/GameplayTasks."),
            TEXT("/Script/MovieScene."),
        };
        for (const FString& Name : NameCandidates)
        {
            // Korotkoe imya bez U-prefiksa, esli est', — eto realnoe imya klassa v reflekstsi.
            FString CleanName = Name;
            if (CleanName.StartsWith(TEXT("U")) && CleanName.Len() > 1)
            {
                CleanName = CleanName.Mid(1);
            }
            for (const TCHAR* ModulePath : ModulePaths)
            {
                const FString FullPath = FString(ModulePath) + CleanName;
                ComponentClass = LoadObject<UClass>(nullptr, *FullPath);
                if (ComponentClass && ComponentClass->IsChildOf(UActorComponent::StaticClass()))
                {
                    break;
                }
                ComponentClass = nullptr;
            }
            if (ComponentClass)
            {
                break;
            }
        }
    }

    // Final'naya valitsatsiya — eto dolzhen byt' AActorComponent-naslednik.
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    // Add the component to the blueprint
    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (NewNode)
    {
        // Set transform if provided
        USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
        if (SceneComponent)
        {
            if (Params->HasField(TEXT("location")))
            {
                SceneComponent->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
            }
            if (Params->HasField(TEXT("rotation")))
            {
                SceneComponent->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
            }
            if (Params->HasField(TEXT("scale")))
            {
                SceneComponent->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
            }
        }

        // Add to root if no parent specified
        Blueprint->SimpleConstructionScript->AddNode(NewNode);

        // Compile the blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("component_name"), ComponentName);
        ResultObj->SetStringField(TEXT("component_type"), ComponentType);
        return ResultObj;
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add component to blueprint"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Compile the blueprint
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Starting blueprint actor spawn"));
    
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing blueprint_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Missing actor_name parameter"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Looking for blueprint '%s'"), *BlueprintName);

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Blueprint not found: %s"), *BlueprintName);
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Blueprint found, getting transform parameters"));

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Location set to (%f, %f, %f)"), Location.X, Location.Y, Location.Z);
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Rotation set to (%f, %f, %f)"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Getting editor world"));

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to get editor world"));
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Creating spawn transform"));

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));

    // Add a small delay to allow the engine to process the newly compiled class
    FPlatformProcess::Sleep(0.2f);

    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to spawn actor from blueprint '%s' with GeneratedClass: %s"), 
           *BlueprintName, Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetName() : TEXT("NULL"));

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
    
    UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: SpawnActor completed, NewActor: %s"), 
           NewActor ? *NewActor->GetName() : TEXT("NULL"));
    
    if (NewActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: Setting actor label to '%s'"), *ActorName);
        NewActor->SetActorLabel(*ActorName);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: About to convert actor to JSON"));
        TSharedPtr<FJsonObject> Result = FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
        
        UE_LOG(LogTemp, Warning, TEXT("HandleSpawnBlueprintActor: JSON conversion completed, returning result"));
        return Result;
    }

    UE_LOG(LogTemp, Error, TEXT("HandleSpawnBlueprintActor: Failed to spawn blueprint actor"));
    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetMeshMaterialColor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    // Try to cast to StaticMeshComponent or PrimitiveComponent
    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Get color parameter
    TArray<float> ColorArray;
    const TArray<TSharedPtr<FJsonValue>>* ColorJsonArray;
    if (!Params->TryGetArrayField(TEXT("color"), ColorJsonArray) || ColorJsonArray->Num() != 4)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'color' must be an array of 4 float values [R, G, B, A]"));
    }

    for (const TSharedPtr<FJsonValue>& Value : *ColorJsonArray)
    {
        ColorArray.Add(FMath::Clamp(Value->AsNumber(), 0.0f, 1.0f));
    }

    FLinearColor Color(ColorArray[0], ColorArray[1], ColorArray[2], ColorArray[3]);

    // Get material slot index
    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Get parameter name
    FString ParameterName = TEXT("BaseColor");
    Params->TryGetStringField(TEXT("parameter_name"), ParameterName);

    // Get or create material
    UMaterialInterface* Material = nullptr;
    
    // Check if a specific material path was provided
    FString MaterialPath;
    if (Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (!Material)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
        }
    }
    else
    {
        // Use existing material on the component
        Material = PrimComponent->GetMaterial(MaterialSlot);
        if (!Material)
        {
            // Try to use a default material
            Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
            if (!Material)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No material found on component and failed to load default material"));
            }
        }
    }

    // Create a dynamic material instance
    UMaterialInstanceDynamic* DynMaterial = UMaterialInstanceDynamic::Create(Material, PrimComponent);
    if (!DynMaterial)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create dynamic material instance"));
    }

    // Set the color parameter
    DynMaterial->SetVectorParameterValue(*ParameterName, Color);

    // Apply the material to the component
    PrimComponent->SetMaterial(MaterialSlot, DynMaterial);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Log success
    UE_LOG(LogTemp, Log, TEXT("Successfully set material color on component %s: R=%f, G=%f, B=%f, A=%f"), 
        *ComponentName, Color.R, Color.G, Color.B, Color.A);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetStringField(TEXT("parameter_name"), ParameterName);
    
    TArray<TSharedPtr<FJsonValue>> ColorResultArray;
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.R));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.G));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.B));
    ColorResultArray.Add(MakeShared<FJsonValueNumber>(Color.A));
    ResultObj->SetArrayField(TEXT("color"), ColorResultArray);
    
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetAvailableMaterials(const TSharedPtr<FJsonObject>& Params)
{
    // Get parameters - make search path completely dynamic
    FString SearchPath;
    if (!Params->TryGetStringField(TEXT("search_path"), SearchPath))
    {
        // Default to empty string to search everywhere
        SearchPath = TEXT("");
    }
    
    bool bIncludeEngineMaterials = true;
    if (Params->HasField(TEXT("include_engine_materials")))
    {
        bIncludeEngineMaterials = Params->GetBoolField(TEXT("include_engine_materials"));
    }

    // Get asset registry module
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Create filter for materials
    FARFilter Filter;
    Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInstanceDynamic::StaticClass()->GetClassPathName());
    
    // Add search paths dynamically
    if (!SearchPath.IsEmpty())
    {
        // Ensure the path starts with /
        if (!SearchPath.StartsWith(TEXT("/")))
        {
            SearchPath = TEXT("/") + SearchPath;
        }
        // Ensure the path ends with / for proper directory search
        if (!SearchPath.EndsWith(TEXT("/")))
        {
            SearchPath += TEXT("/");
        }
        Filter.PackagePaths.Add(*SearchPath);
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in: %s"), *SearchPath);
    }
    else
    {
        // Search in common game content locations
        Filter.PackagePaths.Add(TEXT("/Game/"));
        UE_LOG(LogTemp, Log, TEXT("Searching for materials in all game content"));
    }
    
    if (bIncludeEngineMaterials)
    {
        Filter.PackagePaths.Add(TEXT("/Engine/"));
        UE_LOG(LogTemp, Log, TEXT("Including Engine materials in search"));
    }
    
    Filter.bRecursivePaths = true;

    // Get assets from registry
    TArray<FAssetData> AssetDataArray;
    AssetRegistry.GetAssets(Filter, AssetDataArray);
    
    UE_LOG(LogTemp, Log, TEXT("Asset registry found %d materials"), AssetDataArray.Num());

    // Also try manual search using EditorAssetLibrary for more comprehensive results
    TArray<FString> AllAssetPaths;
    if (!SearchPath.IsEmpty())
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(SearchPath, true, false);
    }
    else
    {
        AllAssetPaths = UEditorAssetLibrary::ListAssets(TEXT("/Game/"), true, false);
    }
    
    // Filter for materials from the manual search
    for (const FString& AssetPath : AllAssetPaths)
    {
        if (AssetPath.Contains(TEXT("Material")) && !AssetPath.Contains(TEXT(".uasset")))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            if (Asset && Asset->IsA<UMaterialInterface>())
            {
                // Check if we already have this asset from registry search
                bool bAlreadyFound = false;
                for (const FAssetData& ExistingData : AssetDataArray)
                {
                    if (ExistingData.GetObjectPathString() == AssetPath)
                    {
                        bAlreadyFound = true;
                        break;
                    }
                }
                
                if (!bAlreadyFound)
                {
                    // Create FAssetData manually for this asset
                    FAssetData ManualAssetData(Asset);
                    AssetDataArray.Add(ManualAssetData);
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Total materials found after manual search: %d"), AssetDataArray.Num());

    // Convert to JSON
    TArray<TSharedPtr<FJsonValue>> MaterialArray;
    for (const FAssetData& AssetData : AssetDataArray)
    {
        TSharedPtr<FJsonObject> MaterialObj = MakeShared<FJsonObject>();
        MaterialObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
        MaterialObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        MaterialObj->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
        MaterialObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
        
        MaterialArray.Add(MakeShared<FJsonValueObject>(MaterialObj));
        
        UE_LOG(LogTemp, Verbose, TEXT("Found material: %s at %s"), *AssetData.AssetName.ToString(), *AssetData.GetObjectPathString());
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("materials"), MaterialArray);
    ResultObj->SetNumberField(TEXT("count"), MaterialArray.Num());
    ResultObj->SetStringField(TEXT("search_path_used"), SearchPath.IsEmpty() ? TEXT("/Game/") : SearchPath);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
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

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Find mesh components and apply material
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    bool bAppliedToAny = false;
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            MeshComp->SetMaterial(MaterialSlot, Material);
            bAppliedToAny = true;
        }
    }

    if (!bAppliedToAny)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No mesh components found on actor"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleApplyMaterialToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    int32 MaterialSlot = 0;
    if (Params->HasField(TEXT("material_slot")))
    {
        MaterialSlot = Params->GetIntegerField(TEXT("material_slot"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Load the material
    UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
    if (!Material)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load material: %s"), *MaterialPath));
    }

    // Apply the material
    PrimComponent->SetMaterial(MaterialSlot, Material);

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
    ResultObj->SetNumberField(TEXT("material_slot"), MaterialSlot);
    ResultObj->SetBoolField(TEXT("success"), true);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetActorMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
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

    // Get mesh components and their materials
    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    
    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (MeshComp)
        {
            for (int32 i = 0; i < MeshComp->GetNumMaterials(); i++)
            {
                TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
                SlotInfo->SetNumberField(TEXT("slot"), i);
                SlotInfo->SetStringField(TEXT("component"), MeshComp->GetName());
                
                UMaterialInterface* Material = MeshComp->GetMaterial(i);
                if (Material)
                {
                    SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                    SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                    SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
                }
                else
                {
                    SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                    SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                    SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
                }
                
                MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Get material slot information
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    int32 NumMaterials = 0;
    
    // Check if we have a static mesh assigned
    UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
    if (StaticMesh)
    {
        NumMaterials = StaticMesh->GetNumSections(0); // Get number of material slots for LOD 0
        
        for (int32 i = 0; i < NumMaterials; i++)
        {
            TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();
            SlotInfo->SetNumberField(TEXT("slot"), i);
            SlotInfo->SetStringField(TEXT("component"), ComponentName);
            
            UMaterialInterface* Material = MeshComponent->GetMaterial(i);
            if (Material)
            {
                SlotInfo->SetStringField(TEXT("material_name"), Material->GetName());
                SlotInfo->SetStringField(TEXT("material_path"), Material->GetPathName());
                SlotInfo->SetStringField(TEXT("material_class"), Material->GetClass()->GetName());
            }
            else
            {
                SlotInfo->SetStringField(TEXT("material_name"), TEXT("None"));
                SlotInfo->SetStringField(TEXT("material_path"), TEXT(""));
                SlotInfo->SetStringField(TEXT("material_class"), TEXT(""));
            }
            
            MaterialSlots.Add(MakeShared<FJsonValueObject>(SlotInfo));
        }
    }
    else
    {
        // If no static mesh is assigned, we can't determine material slots
        UE_LOG(LogTemp, Warning, TEXT("No static mesh assigned to component %s in blueprint %s"), *ComponentName, *BlueprintName);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetArrayField(TEXT("material_slots"), MaterialSlots);
    ResultObj->SetNumberField(TEXT("total_slots"), MaterialSlots.Num());
    ResultObj->SetBoolField(TEXT("has_static_mesh"), StaticMesh != nullptr);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleReadBlueprintContent(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Get optional parameters
    bool bIncludeEventGraph = true;
    bool bIncludeFunctions = true;
    bool bIncludeVariables = true;
    bool bIncludeComponents = true;
    bool bIncludeInterfaces = true;

    Params->TryGetBoolField(TEXT("include_event_graph"), bIncludeEventGraph);
    Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
    Params->TryGetBoolField(TEXT("include_variables"), bIncludeVariables);
    Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    Params->TryGetBoolField(TEXT("include_interfaces"), bIncludeInterfaces);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

    // Include variables if requested
    if (bIncludeVariables)
    {
        TArray<TSharedPtr<FJsonValue>> VariableArray;
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
            VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
            VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
            VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
            VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
        }
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
    }

    // Include functions if requested
    if (bIncludeFunctions)
    {
        TArray<TSharedPtr<FJsonValue>> FunctionArray;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph)
            {
                TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
                FuncObj->SetStringField(TEXT("name"), Graph->GetName());
                FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));
                
                // Count nodes in function
                int32 NodeCount = Graph->Nodes.Num();
                FuncObj->SetNumberField(TEXT("node_count"), NodeCount);
                
                FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
            }
        }
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
    }

    // Include event graph if requested
    if (bIncludeEventGraph)
    {
        TSharedPtr<FJsonObject> EventGraphObj = MakeShared<FJsonObject>();
        
        // Find the main event graph
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph && Graph->GetName() == TEXT("EventGraph"))
            {
                EventGraphObj->SetStringField(TEXT("name"), Graph->GetName());
                EventGraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
                
                // Get basic node information
                TArray<TSharedPtr<FJsonValue>> NodeArray;
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node)
                    {
                        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                        NodeObj->SetStringField(TEXT("name"), Node->GetName());
                        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                        NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                    }
                }
                EventGraphObj->SetArrayField(TEXT("nodes"), NodeArray);
                break;
            }
        }
        
        ResultObj->SetObjectField(TEXT("event_graph"), EventGraphObj);
    }

    // Include components if requested
    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> ComponentArray;
        if (Blueprint->SimpleConstructionScript)
        {
            for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Node && Node->ComponentTemplate)
                {
                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                    CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
                    CompObj->SetBoolField(TEXT("is_root"), Node == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode());
                    ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
            }
        }
        ResultObj->SetArrayField(TEXT("components"), ComponentArray);
    }

    // Include interfaces if requested
    if (bIncludeInterfaces)
    {
        TArray<TSharedPtr<FJsonValue>> InterfaceArray;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
            InterfaceObj->SetStringField(TEXT("name"), Interface.Interface ? Interface.Interface->GetName() : TEXT("Unknown"));
            InterfaceArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
        }
        ResultObj->SetArrayField(TEXT("interfaces"), InterfaceArray);
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAnalyzeBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString GraphName = TEXT("EventGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    // Get optional parameters
    bool bIncludeNodeDetails = true;
    bool bIncludePinConnections = true;
    bool bTraceExecutionFlow = true;

    Params->TryGetBoolField(TEXT("include_node_details"), bIncludeNodeDetails);
    Params->TryGetBoolField(TEXT("include_pin_connections"), bIncludePinConnections);
    Params->TryGetBoolField(TEXT("trace_execution_flow"), bTraceExecutionFlow);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    // Find the specified graph
    UEdGraph* TargetGraph = nullptr;
    
    // Check event graphs first
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph && Graph->GetName() == GraphName)
        {
            TargetGraph = Graph;
            break;
        }
    }
    
    // Check function graphs if not found
    if (!TargetGraph)
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName() == GraphName)
            {
                TargetGraph = Graph;
                break;
            }
        }
    }

    if (!TargetGraph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    TSharedPtr<FJsonObject> GraphData = MakeShared<FJsonObject>();
    GraphData->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
    GraphData->SetStringField(TEXT("graph_type"), TargetGraph->GetClass()->GetName());

    // Analyze nodes
    TArray<TSharedPtr<FJsonValue>> NodeArray;
    TArray<TSharedPtr<FJsonValue>> ConnectionArray;

    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (Node)
        {
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("name"), Node->GetName());
            NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

            if (bIncludeNodeDetails)
            {
                NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
                NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
                NodeObj->SetBoolField(TEXT("can_rename"), Node->bCanRenameNode);
            }

            // Include pin information if requested
            if (bIncludePinConnections)
            {
                TArray<TSharedPtr<FJsonValue>> PinArray;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin)
                    {
                        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                        PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
                        PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());
                        
                        // Record connections for this pin
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (LinkedPin && LinkedPin->GetOwningNode())
                            {
                                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                                ConnObj->SetStringField(TEXT("from_node"), Pin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                                ConnObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->GetName());
                                ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
                                ConnectionArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                            }
                        }
                        
                        PinArray.Add(MakeShared<FJsonValueObject>(PinObj));
                    }
                }
                NodeObj->SetArrayField(TEXT("pins"), PinArray);
            }

            NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    GraphData->SetArrayField(TEXT("nodes"), NodeArray);
    GraphData->SetArrayField(TEXT("connections"), ConnectionArray);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    ResultObj->SetObjectField(TEXT("graph_data"), GraphData);
    ResultObj->SetBoolField(TEXT("success"), true);

    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintVariableDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString VariableName;
    bool bSpecificVariable = Params->TryGetStringField(TEXT("variable_name"), VariableName);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> VariableArray;

    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        // If looking for specific variable, skip others
        if (bSpecificVariable && Variable.VarName.ToString() != VariableName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Variable.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
        VarObj->SetStringField(TEXT("sub_category"), Variable.VarType.PinSubCategory.ToString());
        VarObj->SetStringField(TEXT("default_value"), Variable.DefaultValue);
        VarObj->SetStringField(TEXT("friendly_name"), Variable.FriendlyName.IsEmpty() ? Variable.VarName.ToString() : Variable.FriendlyName);
        
        // Get tooltip from metadata (VarTooltip doesn't exist in UE 5.5)
        FString TooltipValue;
        if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
        {
            TooltipValue = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
        }
        VarObj->SetStringField(TEXT("tooltip"), TooltipValue);
        
        VarObj->SetStringField(TEXT("category"), Variable.Category.ToString());

        // Property flags
        VarObj->SetBoolField(TEXT("is_editable"), (Variable.PropertyFlags & CPF_Edit) != 0);
        VarObj->SetBoolField(TEXT("is_blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
        VarObj->SetBoolField(TEXT("is_editable_in_instance"), (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
        VarObj->SetBoolField(TEXT("is_config"), (Variable.PropertyFlags & CPF_Config) != 0);

        // Replication
        VarObj->SetNumberField(TEXT("replication"), (int32)Variable.ReplicationCondition);

        VariableArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificVariable)
    {
        ResultObj->SetStringField(TEXT("variable_name"), VariableName);
        if (VariableArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("variable"), VariableArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("variables"), VariableArray);
        ResultObj->SetNumberField(TEXT("variable_count"), VariableArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintFunctionDetails(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString FunctionName;
    bool bSpecificFunction = Params->TryGetStringField(TEXT("function_name"), FunctionName);

    bool bIncludeGraph = true;
    Params->TryGetBoolField(TEXT("include_graph"), bIncludeGraph);

    // Load the blueprint
    UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load blueprint: %s"), *BlueprintPath));
    }

    TArray<TSharedPtr<FJsonValue>> FunctionArray;

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        // If looking for specific function, skip others
        if (bSpecificFunction && Graph->GetName() != FunctionName)
        {
            continue;
        }

        TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
        FuncObj->SetStringField(TEXT("name"), Graph->GetName());
        FuncObj->SetStringField(TEXT("graph_type"), TEXT("Function"));

        // Get function signature from graph
        TArray<TSharedPtr<FJsonValue>> InputPins;
        TArray<TSharedPtr<FJsonValue>> OutputPins;

        // Find function entry and result nodes
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node)
            {
                if (Node->GetClass()->GetName().Contains(TEXT("FunctionEntry")))
                {
                    // Process input parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != TEXT("then"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
                else if (Node->GetClass()->GetName().Contains(TEXT("FunctionResult")))
                {
                    // Process output parameters
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (Pin && Pin->Direction == EGPD_Input && Pin->PinName != TEXT("exec"))
                        {
                            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                            OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
                        }
                    }
                }
            }
        }

        FuncObj->SetArrayField(TEXT("input_parameters"), InputPins);
        FuncObj->SetArrayField(TEXT("output_parameters"), OutputPins);
        FuncObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Include graph details if requested
        if (bIncludeGraph)
        {
            TArray<TSharedPtr<FJsonValue>> NodeArray;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node)
                {
                    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
                    NodeObj->SetStringField(TEXT("name"), Node->GetName());
                    NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                    NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                    NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                }
            }
            FuncObj->SetArrayField(TEXT("graph_nodes"), NodeArray);
        }

        FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    
    if (bSpecificFunction)
    {
        ResultObj->SetStringField(TEXT("function_name"), FunctionName);
        if (FunctionArray.Num() > 0)
        {
            ResultObj->SetObjectField(TEXT("function"), FunctionArray[0]->AsObject());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
        }
    }
    else
    {
        ResultObj->SetArrayField(TEXT("functions"), FunctionArray);
        ResultObj->SetNumberField(TEXT("function_count"), FunctionArray.Num());
    }

    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

// ─────────────────────────────────────────────────────────────────────────────
// create_blueprint_from_template (MCP-CONTENT-002)
//
// Duplicates an existing Blueprint asset and applies optional defaultsOverride
// to the duplicated Blueprint's CDO (properties on the generated class default
// object). The BP is compiled after overrides are applied so defaults take
// effect in spawned instances.
//
// Returns the unified pipeline response ({ ok, status, assetPath, meta }).
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprintFromTemplate(const TSharedPtr<FJsonObject>& Params)
{
    // assetPath (new BP) is required.
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString TemplatePath;
    if (!Params->TryGetStringField(TEXT("templatePath"), TemplatePath) || TemplatePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_TEMPLATE_PATH"),
            TEXT("Required param 'templatePath' is missing or empty"));
    }

    UObject* TemplateObj = UEditorAssetLibrary::LoadAsset(TemplatePath);
    UBlueprint* TemplateBP = Cast<UBlueprint>(TemplateObj);
    if (!TemplateBP)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("templatePath"), TemplatePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("TEMPLATE_NOT_FOUND"),
            FString::Printf(TEXT("Template BP not found at '%s'"), *TemplatePath),
            Details);
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }

    const bool bUpdate = (Decision.Action == TEXT("update"));
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));

    UBlueprint* TargetBP = nullptr;

    if (bUpdate && Decision.ExistingAsset)
    {
        // "update" reuses the existing BP; we just apply defaultsOverride + recompile.
        TargetBP = Cast<UBlueprint>(Decision.ExistingAsset);
        if (!TargetBP)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("EXISTING_ASSET_NOT_BP"),
                FString::Printf(TEXT("Asset at '%s' exists but is not a UBlueprint"), *AssetPath));
        }
    }
    else
    {
        // "overwrite": delete old first. "create": no preexisting.
        if (bOverwrite && Decision.ExistingAsset)
        {
            if (!UEditorAssetLibrary::DeleteAsset(AssetPath))
            {
                return FAssetCommonUtils::MakeFailureResponse(
                    TEXT("ue_internal"),
                    TEXT("DELETE_BEFORE_DUPLICATE_FAILED"),
                    FString::Printf(TEXT("Could not delete existing asset at '%s' before overwrite"), *AssetPath));
            }
        }

        UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(TemplatePath, AssetPath);
        TargetBP = Cast<UBlueprint>(Duplicated);
        if (!TargetBP)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("templatePath"), TemplatePath);
            Details->SetStringField(TEXT("destPath"), AssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("BP_DUPLICATE_FAILED"),
                TEXT("UEditorAssetLibrary::DuplicateAsset returned null or non-BP"),
                Details);
        }
    }

    // Compile once before applying CDO overrides so the generated class exists
    // with all inherited properties available.
    FKismetEditorUtilities::CompileBlueprint(TargetBP);

    // Apply defaultsOverride to CDO.
    TArray<FString> AppliedKeys;
    TMap<FString, FString> SkippedKeys;

    const TSharedPtr<FJsonObject>* DefaultsObjPtr = nullptr;
    const bool bHasDefaults =
        Params->TryGetObjectField(TEXT("defaultsOverride"), DefaultsObjPtr) &&
        DefaultsObjPtr && (*DefaultsObjPtr).IsValid();

    if (bHasDefaults)
    {
        UClass* GeneratedClass = TargetBP->GeneratedClass;
        UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;

        if (!CDO)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("CDO_UNAVAILABLE"),
                FString::Printf(TEXT("GeneratedClass CDO missing for '%s' after compile"), *AssetPath));
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*DefaultsObjPtr)->Values)
        {
            const FString& PropName = Pair.Key;
            const TSharedPtr<FJsonValue>& Value = Pair.Value;

            FProperty* Prop = GeneratedClass->FindPropertyByName(FName(*PropName));
            if (!Prop)
            {
                SkippedKeys.Add(PropName, TEXT("property_not_found"));
                continue;
            }

            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);

            // Numeric → FNumericProperty handles int / float / etc.
            if (Value->Type == EJson::Number)
            {
                if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
                {
                    if (NumProp->IsFloatingPoint())
                    {
                        NumProp->SetFloatingPointPropertyValue(ValuePtr, Value->AsNumber());
                    }
                    else
                    {
                        NumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value->AsNumber()));
                    }
                    AppliedKeys.Add(PropName);
                }
                else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
                {
                    BoolProp->SetPropertyValue(ValuePtr, Value->AsNumber() != 0.0);
                    AppliedKeys.Add(PropName);
                }
                else
                {
                    SkippedKeys.Add(PropName, TEXT("property_not_numeric"));
                }
            }
            else if (Value->Type == EJson::Boolean)
            {
                if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
                {
                    BoolProp->SetPropertyValue(ValuePtr, Value->AsBool());
                    AppliedKeys.Add(PropName);
                }
                else
                {
                    SkippedKeys.Add(PropName, TEXT("property_not_bool"));
                }
            }
            else if (Value->Type == EJson::String)
            {
                const FString Str = Value->AsString();

                // Object properties — treat string as asset path and LoadAsset it.
                if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
                {
                    UObject* Loaded = UEditorAssetLibrary::LoadAsset(Str);
                    if (!Loaded)
                    {
                        SkippedKeys.Add(PropName, FString::Printf(TEXT("asset_not_found:%s"), *Str));
                    }
                    else if (!Loaded->IsA(ObjProp->PropertyClass))
                    {
                        SkippedKeys.Add(PropName, FString::Printf(TEXT("asset_wrong_class:%s"), *Loaded->GetClass()->GetName()));
                    }
                    else
                    {
                        ObjProp->SetObjectPropertyValue(ValuePtr, Loaded);
                        AppliedKeys.Add(PropName);
                    }
                }
                else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
                {
                    StrProp->SetPropertyValue(ValuePtr, Str);
                    AppliedKeys.Add(PropName);
                }
                else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
                {
                    NameProp->SetPropertyValue(ValuePtr, FName(*Str));
                    AppliedKeys.Add(PropName);
                }
                else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
                {
                    TextProp->SetPropertyValue(ValuePtr, FText::FromString(Str));
                    AppliedKeys.Add(PropName);
                }
                else
                {
                    SkippedKeys.Add(PropName, TEXT("string_for_non_string_prop"));
                }
            }
            else
            {
                SkippedKeys.Add(PropName, TEXT("unsupported_json_type"));
            }
        }

        CDO->Modify();
    }

    // Recompile + save to persist CDO overrides.
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBP);
    FKismetEditorUtilities::CompileBlueprint(TargetBP);
    TargetBP->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("templatePath"), TemplatePath);

    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    for (const FString& K : AppliedKeys) AppliedJson.Add(MakeShared<FJsonValueString>(K));
    Meta->SetArrayField(TEXT("appliedDefaults"), AppliedJson);

    if (SkippedKeys.Num() > 0)
    {
        TSharedPtr<FJsonObject> Sk = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& P : SkippedKeys)
        {
            Sk->SetStringField(P.Key, P.Value);
        }
        Meta->SetObjectField(TEXT("skippedDefaults"), Sk);
    }

    const FString Status = bUpdate ? TEXT("updated") : (bOverwrite ? TEXT("overwritten") : TEXT("created"));
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadBlueprintByName — широкий поиск по короткому имени или полному пути
// ─────────────────────────────────────────────────────────────────────────────

UBlueprint* FEpicUnrealMCPBlueprintCommands::LoadBlueprintByName(const FString& BlueprintName)
{
    // Если передан полный /Game/... путь — пробуем напрямую
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        if (UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName)))
        {
            return BP;
        }
    }

    // Поиск через стандартный хелпер (ищет в /Game/Blueprints/)
    if (UBlueprint* BP = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName))
    {
        return BP;
    }

    // Расширенный поиск по популярным путям
    const TArray<FString> SearchDirs = {
        TEXT("/Game/"),
        TEXT("/Game/Blueprints/"),
        TEXT("/Game/UI/"),
        TEXT("/Game/Widgets/"),
        TEXT("/Game/Characters/"),
        TEXT("/Game/Pawns/"),
        TEXT("/Game/GameModes/"),
        TEXT("/Game/Controllers/"),
    };

    for (const FString& Dir : SearchDirs)
    {
        const FString ObjectPath = Dir + BlueprintName + TEXT(".") + BlueprintName;
        if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
        {
            if (UBlueprint* BP = Cast<UBlueprint>(Asset))
            {
                return BP;
            }
        }
    }

    // Последняя попытка через AssetRegistry (рекурсивный поиск)
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.PackagePaths.Add(TEXT("/Game/"));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> AssetDataArray;
    AssetRegistry.GetAssets(Filter, AssetDataArray);

    for (const FAssetData& AssetData : AssetDataArray)
    {
        if (AssetData.AssetName.ToString() == BlueprintName)
        {
            return Cast<UBlueprint>(AssetData.GetAsset());
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_blueprint_property
//
// Устанавливает свойство на CDO (Class Default Object) Blueprint'а.
// Поддерживаемые типы property_value:
//   - bool   → FBoolProperty
//   - number → FNumericProperty (int / float)
//   - string → FStrProperty, FNameProperty, FTextProperty
//              FClassProperty  (TSubclassOf<X>) — ищет класс по имени
//              FObjectProperty — загружает asset по пути
// После установки Blueprint компилируется и сохраняется.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params)
{
    // ── 1. Обязательные параметры ────────────────────────────────────────────
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    const TSharedPtr<FJsonValue>* PropertyValueField = Params->Values.Find(TEXT("property_value"));
    if (!PropertyValueField || !PropertyValueField->IsValid())
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    }
    const TSharedPtr<FJsonValue>& PropertyValue = *PropertyValueField;

    // ── 2. Находим Blueprint ─────────────────────────────────────────────────
    UBlueprint* Blueprint = LoadBlueprintByName(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // ── 3. Компилируем чтобы GeneratedClass и CDO были актуальными ──────────
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UClass* GeneratedClass = Blueprint->GeneratedClass;
    if (!GeneratedClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' has no GeneratedClass after compile"), *BlueprintName));
    }

    UObject* CDO = GeneratedClass->GetDefaultObject();
    if (!CDO)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not get CDO for Blueprint '%s'"), *BlueprintName));
    }

    // ── 4. Ищем FProperty ───────────────────────────────────────────────────
    FProperty* Prop = GeneratedClass->FindPropertyByName(FName(*PropertyName));
    if (!Prop)
    {
        // Попробуем родительскую иерархию
        Prop = FindFProperty<FProperty>(GeneratedClass, FName(*PropertyName));
    }
    if (!Prop)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Property '%s' not found on Blueprint '%s'"), *PropertyName, *BlueprintName));
    }

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
    const EJson JsonType = PropertyValue->Type;

    // ── 5. Устанавливаем значение в зависимости от типа property ────────────

    // Bool
    if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
    {
        bool bVal = false;
        if (JsonType == EJson::Boolean)
        {
            bVal = PropertyValue->AsBool();
        }
        else if (JsonType == EJson::Number)
        {
            bVal = (PropertyValue->AsNumber() != 0.0);
        }
        else if (JsonType == EJson::String)
        {
            const FString StrVal = PropertyValue->AsString().ToLower();
            bVal = (StrVal == TEXT("true") || StrVal == TEXT("1") || StrVal == TEXT("yes"));
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Cannot assign JSON type to bool property '%s'"), *PropertyName));
        }
        BoolProp->SetPropertyValue(ValuePtr, bVal);
    }
    // Numeric (int / float / double)
    else if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
    {
        double NumVal = 0.0;
        if (JsonType == EJson::Number)
        {
            NumVal = PropertyValue->AsNumber();
        }
        else if (JsonType == EJson::String)
        {
            NumVal = FCString::Atod(*PropertyValue->AsString());
        }
        else if (JsonType == EJson::Boolean)
        {
            NumVal = PropertyValue->AsBool() ? 1.0 : 0.0;
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Cannot assign JSON type to numeric property '%s'"), *PropertyName));
        }

        if (NumProp->IsFloatingPoint())
        {
            NumProp->SetFloatingPointPropertyValue(ValuePtr, NumVal);
        }
        else
        {
            NumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(NumVal));
        }
    }
    // ClassProperty (TSubclassOf<X>) — ищем класс по строковому имени
    else if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
    {
        if (JsonType != EJson::String)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("ClassProperty '%s' requires a string value (class name or path)"), *PropertyName));
        }

        const FString ClassNameOrPath = PropertyValue->AsString();
        UClass* FoundClass = nullptr;

        // Попытки поиска класса: полный путь, через модули, через AssetRegistry (Blueprint)
        const TArray<FString> ClassSearchPaths = {
            ClassNameOrPath,
            FString::Printf(TEXT("/Script/Engine.%s"), *ClassNameOrPath),
            FString::Printf(TEXT("/Script/Client.%s"), *ClassNameOrPath),
            FString::Printf(TEXT("/Script/UMG.%s"), *ClassNameOrPath),
            FString::Printf(TEXT("/Script/GameplayAbilities.%s"), *ClassNameOrPath),
        };

        for (const FString& Path : ClassSearchPaths)
        {
            FoundClass = LoadClass<UObject>(nullptr, *Path);
            if (FoundClass) break;
        }

        // Если не нашли через LoadClass — это Blueprint-класс; ищем через AssetRegistry
        if (!FoundClass)
        {
            UBlueprint* ValueBP = LoadBlueprintByName(ClassNameOrPath);
            if (ValueBP && ValueBP->GeneratedClass)
            {
                FoundClass = ValueBP->GeneratedClass;
            }
        }

        if (!FoundClass)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Could not resolve class '%s' for ClassProperty '%s'"),
                    *ClassNameOrPath, *PropertyName));
        }

        // Проверяем что найденный класс совместим с базовым типом свойства
        if (ClassProp->MetaClass && !FoundClass->IsChildOf(ClassProp->MetaClass))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Class '%s' is not a child of required meta class '%s' for property '%s'"),
                    *FoundClass->GetName(),
                    *ClassProp->MetaClass->GetName(),
                    *PropertyName));
        }

        ClassProp->SetObjectPropertyValue(ValuePtr, FoundClass);
    }
    // ObjectProperty — загружаем asset по пути
    else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
    {
        if (JsonType != EJson::String)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("ObjectProperty '%s' requires a string asset path"), *PropertyName));
        }

        const FString AssetPath = PropertyValue->AsString();
        UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(AssetPath);
        if (!LoadedObj)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Asset not found at path '%s' for ObjectProperty '%s'"),
                    *AssetPath, *PropertyName));
        }
        if (!LoadedObj->IsA(ObjProp->PropertyClass))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Asset class '%s' is incompatible with ObjectProperty '%s' (expects '%s')"),
                    *LoadedObj->GetClass()->GetName(),
                    *PropertyName,
                    *ObjProp->PropertyClass->GetName()));
        }

        ObjProp->SetObjectPropertyValue(ValuePtr, LoadedObj);
    }
    // String
    else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
    {
        if (JsonType == EJson::String)
        {
            StrProp->SetPropertyValue(ValuePtr, PropertyValue->AsString());
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("StrProperty '%s' requires a string value"), *PropertyName));
        }
    }
    // Name
    else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
    {
        if (JsonType == EJson::String)
        {
            NameProp->SetPropertyValue(ValuePtr, FName(*PropertyValue->AsString()));
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("NameProperty '%s' requires a string value"), *PropertyName));
        }
    }
    // Text
    else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
    {
        if (JsonType == EJson::String)
        {
            TextProp->SetPropertyValue(ValuePtr, FText::FromString(PropertyValue->AsString()));
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("TextProperty '%s' requires a string value"), *PropertyName));
        }
    }
    // EnumProperty (byte-backed enums, e.g. EAutoReceiveInput::Type)
    else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
    {
        UEnum* Enum = EnumProp->GetEnum();
        if (!Enum)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("EnumProperty '%s' has no UEnum"), *PropertyName));
        }

        int64 EnumVal = -1;
        if (JsonType == EJson::String)
        {
            // Принимаем "Player0", "EAutoReceiveInput::Player0" и просто "0"
            FString EnumStr = PropertyValue->AsString();
            // Пробуем найти по короткому имени (без префикса enum)
            EnumVal = Enum->GetValueByNameString(EnumStr);
            if (EnumVal == INDEX_NONE)
            {
                // Попробуем числовую строку
                if (EnumStr.IsNumeric())
                {
                    EnumVal = FCString::Atoi64(*EnumStr);
                }
            }
        }
        else if (JsonType == EJson::Number)
        {
            EnumVal = static_cast<int64>(PropertyValue->AsNumber());
        }

        if (EnumVal == INDEX_NONE)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Could not resolve enum value for property '%s'"), *PropertyName));
        }

        FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
        UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumVal);
    }
    // ByteProperty (byte-backed enum или просто uint8)
    else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
    {
        int64 ByteVal = -1;
        if (JsonType == EJson::Number)
        {
            ByteVal = static_cast<int64>(PropertyValue->AsNumber());
        }
        else if (JsonType == EJson::String)
        {
            const FString StrVal = PropertyValue->AsString();
            if (ByteProp->Enum)
            {
                ByteVal = ByteProp->Enum->GetValueByNameString(StrVal);
                if (ByteVal == INDEX_NONE && StrVal.IsNumeric())
                {
                    ByteVal = FCString::Atoi64(*StrVal);
                }
            }
            else if (StrVal.IsNumeric())
            {
                ByteVal = FCString::Atoi64(*StrVal);
            }
        }

        if (ByteVal < 0 || ByteVal > 255)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Invalid byte/enum value for property '%s'"), *PropertyName));
        }

        ByteProp->SetIntPropertyValue(ValuePtr, ByteVal);
    }
    else
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Property '%s' has unsupported type '%s'"),
                *PropertyName, *Prop->GetClass()->GetName()));
    }

    // ── 6. Помечаем CDO изменённым ──────────────────────────────────────────
    CDO->Modify();

    // ── 7. Компилируем и сохраняем ───────────────────────────────────────────
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    if (UPackage* Pkg = Blueprint->GetPackage())
    {
        Pkg->MarkPackageDirty();
    }

    FString PackageName = Blueprint->GetPathName();
    int32 DotIdx;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    // ── 8. Ответ ─────────────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("property_name"), PropertyName);
    Result->SetStringField(TEXT("property_type"), Prop->GetClass()->GetName());
    Result->SetStringField(TEXT("message"),
        FString::Printf(TEXT("Property '%s' successfully set on Blueprint '%s'"),
            *PropertyName, *BlueprintName));
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ---------------------------------------------------------------------------
// v1.9.0 — Set a property on a SCS component template via reflection
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
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

    UE_LOG(LogTemp, Display,
        TEXT("FEpicUnrealMCPBlueprintCommands::HandleSetComponentProperty: BP '%s', component '%s', property '%s'"),
        *BlueprintName, *ComponentName, *PropertyName);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript"), *BlueprintName));
    }

    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode || !ComponentNode->ComponentTemplate)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found in blueprint: %s"), *ComponentName));
    }

    UActorComponent* ComponentTemplate = ComponentNode->ComponentTemplate;

    FProperty* Property = ComponentTemplate->GetClass()->FindPropertyByName(*PropertyName);
    if (!Property)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Property '%s' not found on component '%s' (class %s)"),
            *PropertyName, *ComponentName, *ComponentTemplate->GetClass()->GetName()));
    }

    // Convert JSON value to a textual form acceptable by ImportText_Direct.
    TSharedPtr<FJsonValue> ValueJson = Params->Values.FindRef(TEXT("property_value"));
    FString StringValue;
    if (!ValueJson.IsValid())
    {
        StringValue.Empty();
    }
    else
    {
        switch (ValueJson->Type)
        {
            case EJson::Boolean:
                StringValue = ValueJson->AsBool() ? TEXT("true") : TEXT("false");
                break;
            case EJson::Number:
                StringValue = FString::SanitizeFloat(ValueJson->AsNumber());
                break;
            case EJson::String:
                StringValue = ValueJson->AsString();
                break;
            case EJson::Array:
            {
                // Serialise array as a sequence of comma-separated tokens for vector/rotator imports
                TArray<FString> Tokens;
                for (const TSharedPtr<FJsonValue>& Element : ValueJson->AsArray())
                {
                    if (!Element.IsValid()) continue;
                    if (Element->Type == EJson::Number)
                    {
                        Tokens.Add(FString::SanitizeFloat(Element->AsNumber()));
                    }
                    else if (Element->Type == EJson::String)
                    {
                        Tokens.Add(Element->AsString());
                    }
                    else if (Element->Type == EJson::Boolean)
                    {
                        Tokens.Add(Element->AsBool() ? TEXT("true") : TEXT("false"));
                    }
                }
                StringValue = FString::Printf(TEXT("(%s)"), *FString::Join(Tokens, TEXT(",")));
                break;
            }
            case EJson::Object:
            {
                // Best-effort: serialise to compact JSON; ImportText may not accept this for arbitrary structs.
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&StringValue);
                FJsonSerializer::Serialize(ValueJson->AsObject().ToSharedRef(), Writer);
                break;
            }
            default:
                StringValue.Empty();
                break;
        }
    }

    void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(ComponentTemplate);
    const TCHAR* ImportResult = Property->ImportText_Direct(*StringValue, PropertyAddr, ComponentTemplate, PPF_None);
    if (!ImportResult)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Failed to import value '%s' for property '%s' (type %s)"),
            *StringValue, *PropertyName, *Property->GetClass()->GetName()));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("component_name"), ComponentName);
    Result->SetStringField(TEXT("property_name"), PropertyName);
    Result->SetStringField(TEXT("property_type"), Property->GetClass()->GetName());
    Result->SetStringField(TEXT("value"), StringValue);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ---------------------------------------------------------------------------
// Phase 1A (v1.11.0) — Component lifecycle commands
// ---------------------------------------------------------------------------

namespace
{
    // Helper: locate SCS_Node by name (variable name match).
    static USCS_Node* FindSCSNodeByName(UBlueprint* Blueprint, const FString& ComponentName)
    {
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            return nullptr;
        }
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->GetVariableName().ToString() == ComponentName)
            {
                return Node;
            }
        }
        return nullptr;
    }
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleDeleteComponentFromBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript"), *BlueprintName));
    }

    USCS_Node* TargetNode = FindSCSNodeByName(Blueprint, ComponentName);
    if (!TargetNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found in blueprint: %s"), *ComponentName));
    }

    // Recursively count child nodes before removal (for response info).
    int32 ChildCount = TargetNode->GetChildNodes().Num();

    // RemoveNode handles cleanup of the child tree; pass true to also clear references.
    Blueprint->SimpleConstructionScript->RemoveNode(TargetNode);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    if (UPackage* Pkg = Blueprint->GetPackage())
    {
        Pkg->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("component_name"), ComponentName);
    Result->SetNumberField(TEXT("child_components_removed"), ChildCount);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleRenameComponent(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString OldName;
    if (!Params->TryGetStringField(TEXT("old_name"), OldName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'old_name' parameter"));
    }
    FString NewName;
    if (!Params->TryGetStringField(TEXT("new_name"), NewName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript"), *BlueprintName));
    }

    USCS_Node* TargetNode = FindSCSNodeByName(Blueprint, OldName);
    if (!TargetNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found in blueprint: %s"), *OldName));
    }

    // Ensure new name is unique within the SCS hierarchy.
    if (FindSCSNodeByName(Blueprint, NewName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component name already in use: %s"), *NewName));
    }

    // FBlueprintEditorUtils handles all SCS, variable-binding and graph-reference updates.
    FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, TargetNode, FName(*NewName));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    if (UPackage* Pkg = Blueprint->GetPackage())
    {
        Pkg->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("old_name"), OldName);
    Result->SetStringField(TEXT("new_name"), NewName);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleListComponents(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript"), *BlueprintName));
    }

    // Build a set of root nodes for the is_root flag lookup.
    TSet<USCS_Node*> RootSet;
    for (USCS_Node* RootNode : Blueprint->SimpleConstructionScript->GetRootNodes())
    {
        if (RootNode)
        {
            RootSet.Add(RootNode);
        }
    }

    // Build a child->parent map for parent_name lookup.
    TMap<USCS_Node*, USCS_Node*> ParentMap;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (!Node) continue;
        for (USCS_Node* Child : Node->GetChildNodes())
        {
            if (Child)
            {
                ParentMap.Add(Child, Node);
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> ComponentsArr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (!Node) continue;

        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());

        FString ClassName = TEXT("(unknown)");
        if (Node->ComponentClass)
        {
            ClassName = Node->ComponentClass->GetName();
        }
        else if (Node->ComponentTemplate)
        {
            ClassName = Node->ComponentTemplate->GetClass()->GetName();
        }
        NodeObj->SetStringField(TEXT("class"), ClassName);

        USCS_Node* const* ParentPtr = ParentMap.Find(Node);
        if (ParentPtr && *ParentPtr)
        {
            NodeObj->SetStringField(TEXT("parent_name"), (*ParentPtr)->GetVariableName().ToString());
        }
        else
        {
            NodeObj->SetStringField(TEXT("parent_name"), TEXT(""));
        }

        NodeObj->SetBoolField(TEXT("is_root"), RootSet.Contains(Node));

        // Relative transform (only meaningful for USceneComponent templates)
        USceneComponent* SceneTpl = Cast<USceneComponent>(Node->ComponentTemplate);
        if (SceneTpl)
        {
            const FVector Loc = SceneTpl->GetRelativeLocation();
            const FRotator Rot = SceneTpl->GetRelativeRotation();
            const FVector Scale = SceneTpl->GetRelativeScale3D();

            TArray<TSharedPtr<FJsonValue>> LocArr;
            LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
            LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
            LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
            NodeObj->SetArrayField(TEXT("relative_location"), LocArr);

            TArray<TSharedPtr<FJsonValue>> RotArr;
            RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
            RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
            RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
            NodeObj->SetArrayField(TEXT("relative_rotation"), RotArr);

            TArray<TSharedPtr<FJsonValue>> ScaleArr;
            ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
            ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
            ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));
            NodeObj->SetArrayField(TEXT("relative_scale"), ScaleArr);
        }

        ComponentsArr.Add(MakeShared<FJsonValueObject>(NodeObj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetArrayField(TEXT("components"), ComponentsArr);
    Result->SetNumberField(TEXT("count"), ComponentsArr.Num());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleSetComponentTransform(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    if (!Blueprint->SimpleConstructionScript)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript"), *BlueprintName));
    }

    USCS_Node* TargetNode = FindSCSNodeByName(Blueprint, ComponentName);
    if (!TargetNode || !TargetNode->ComponentTemplate)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found in blueprint: %s"), *ComponentName));
    }

    USceneComponent* SceneTpl = Cast<USceneComponent>(TargetNode->ComponentTemplate);
    if (!SceneTpl)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component '%s' is not a USceneComponent (no transform)"), *ComponentName));
    }

    bool bChanged = false;
    if (Params->HasField(TEXT("location")))
    {
        SceneTpl->SetRelativeLocation(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
        bChanged = true;
    }
    if (Params->HasField(TEXT("rotation")))
    {
        SceneTpl->SetRelativeRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
        bChanged = true;
    }
    if (Params->HasField(TEXT("scale")))
    {
        SceneTpl->SetRelativeScale3D(FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
        bChanged = true;
    }

    if (bChanged)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        if (UPackage* Pkg = Blueprint->GetPackage())
        {
            Pkg->MarkPackageDirty();
        }
    }

    const FVector Loc = SceneTpl->GetRelativeLocation();
    const FRotator Rot = SceneTpl->GetRelativeRotation();
    const FVector Scale = SceneTpl->GetRelativeScale3D();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("component_name"), ComponentName);

    TArray<TSharedPtr<FJsonValue>> LocArr;
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
    LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
    Result->SetArrayField(TEXT("relative_location"), LocArr);

    TArray<TSharedPtr<FJsonValue>> RotArr;
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
    RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
    Result->SetArrayField(TEXT("relative_rotation"), RotArr);

    TArray<TSharedPtr<FJsonValue>> ScaleArr;
    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
    ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));
    Result->SetArrayField(TEXT("relative_scale"), ScaleArr);

    Result->SetBoolField(TEXT("changed"), bChanged);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1E (v1.12.0) — Discovery
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleListBlueprints(const TSharedPtr<FJsonObject>& Params)
{
    // Read optional path (default /Game) and optional filter_class (short name like "Actor").
    FString SearchPath = TEXT("/Game");
    Params->TryGetStringField(TEXT("path"), SearchPath);

    if (!SearchPath.StartsWith(TEXT("/")))
    {
        SearchPath = TEXT("/") + SearchPath;
    }
    // AssetRegistry PackagePaths must NOT end with a trailing slash.
    if (SearchPath.Len() > 1 && SearchPath.EndsWith(TEXT("/")))
    {
        SearchPath.RemoveFromEnd(TEXT("/"));
    }

    FString FilterClass;
    Params->TryGetStringField(TEXT("filter_class"), FilterClass);

    // Resolve filter_class -> UClass*. Accept short name ("Actor"), C++ name ("AActor"),
    // or full /Script/Module.Class path.
    UClass* FilterUClass = nullptr;
    if (!FilterClass.IsEmpty())
    {
        const TArray<FString> SearchClassPaths = {
            FilterClass,
            FString::Printf(TEXT("/Script/Engine.%s"), *FilterClass),
            FString::Printf(TEXT("/Script/UMG.%s"), *FilterClass),
            FString::Printf(TEXT("/Script/Client.%s"), *FilterClass),
        };
        for (const FString& Path : SearchClassPaths)
        {
            FilterUClass = LoadClass<UObject>(nullptr, *Path);
            if (FilterUClass)
            {
                break;
            }
        }
        // Try common UE-naming variants (A-prefix actor, U-prefix object)
        if (!FilterUClass && !FilterClass.StartsWith(TEXT("/")))
        {
            const FString WithA = TEXT("A") + FilterClass;
            const FString WithU = TEXT("U") + FilterClass;
            const TArray<FString> Variants = {
                FString::Printf(TEXT("/Script/Engine.%s"), *WithA),
                FString::Printf(TEXT("/Script/Engine.%s"), *WithU),
                FString::Printf(TEXT("/Script/UMG.%s"), *WithU),
            };
            for (const FString& Path : Variants)
            {
                FilterUClass = LoadClass<UObject>(nullptr, *Path);
                if (FilterUClass)
                {
                    break;
                }
            }
        }
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    // WidgetBlueprint is a separate registered class; include so /Game UI BPs show up too.
    Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")));
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(*SearchPath);
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Out;
    Out.Reserve(Assets.Num());

    for (const FAssetData& Data : Assets)
    {
        // Parent class via tag (cheap, no asset load).
        FString ParentClassPath;
        Data.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath);

        // If a filter_class was supplied, gate by parent inheritance. Falls back to substring match
        // when the class can't be resolved (e.g. plugin classes not loaded).
        bool bMatchesFilter = true;
        if (FilterUClass)
        {
            UClass* AssetParentClass = nullptr;
            if (!ParentClassPath.IsEmpty())
            {
                AssetParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
            }
            bMatchesFilter = AssetParentClass && AssetParentClass->IsChildOf(FilterUClass);
        }
        else if (!FilterClass.IsEmpty())
        {
            bMatchesFilter = ParentClassPath.Contains(FilterClass);
        }

        if (!bMatchesFilter)
        {
            continue;
        }

        // Detect is_actor_class via parent class lookup.
        bool bIsActor = false;
        if (!ParentClassPath.IsEmpty())
        {
            if (UClass* ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath))
            {
                bIsActor = ParentClass->IsChildOf(AActor::StaticClass());
            }
        }

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Data.AssetName.ToString());
        Obj->SetStringField(TEXT("path"), Data.GetObjectPathString());
        Obj->SetStringField(TEXT("parent_class"), ParentClassPath);
        Obj->SetBoolField(TEXT("is_actor_class"), bIsActor);
        Out.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), SearchPath);
    if (!FilterClass.IsEmpty())
    {
        Result->SetStringField(TEXT("filter_class"), FilterClass);
    }
    Result->SetArrayField(TEXT("blueprints"), Out);
    Result->SetNumberField(TEXT("count"), Out.Num());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleGetBlueprintClassInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Blueprint->GetName());
    Result->SetStringField(TEXT("path"), Blueprint->GetPathName());

    // Parent class: full /Script/... path, plus display name.
    if (Blueprint->ParentClass)
    {
        Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetPathName());
        Result->SetStringField(TEXT("parent_class_name"), Blueprint->ParentClass->GetName());
    }
    else
    {
        Result->SetStringField(TEXT("parent_class"), TEXT(""));
    }

    // Class flags from GeneratedClass (the runtime UClass) — Blueprint itself doesn't carry these.
    bool bIsAbstract = false;
    bool bIsConst = false;
    if (Blueprint->GeneratedClass)
    {
        bIsAbstract = Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Abstract);
        bIsConst    = Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Const);
    }
    Result->SetBoolField(TEXT("is_abstract"), bIsAbstract);
    Result->SetBoolField(TEXT("is_const"), bIsConst);

    // Blueprint type enum → human-readable string (single source of truth, see EBlueprintType in Blueprint.h).
    FString BPTypeStr;
    switch (Blueprint->BlueprintType)
    {
        case BPTYPE_Normal:          BPTypeStr = TEXT("Normal");           break;
        case BPTYPE_Const:           BPTypeStr = TEXT("Const");            break;
        case BPTYPE_MacroLibrary:    BPTypeStr = TEXT("MacroLibrary");     break;
        case BPTYPE_Interface:       BPTypeStr = TEXT("Interface");        break;
        case BPTYPE_LevelScript:     BPTypeStr = TEXT("LevelScript");      break;
        case BPTYPE_FunctionLibrary: BPTypeStr = TEXT("FunctionLibrary");  break;
        default:                     BPTypeStr = TEXT("Unknown");          break;
    }
    Result->SetStringField(TEXT("blueprint_type"), BPTypeStr);

    // Implemented interfaces — list full paths.
    TArray<TSharedPtr<FJsonValue>> InterfaceArr;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Interface)
        {
            InterfaceArr.Add(MakeShared<FJsonValueString>(Interface.Interface->GetPathName()));
        }
    }
    Result->SetArrayField(TEXT("implemented_interfaces"), InterfaceArr);

    // Counts.
    const int32 NumVariables = Blueprint->NewVariables.Num();
    int32 NumFunctions = 0;
    for (UEdGraph* G : Blueprint->FunctionGraphs)
    {
        if (G) NumFunctions++;
    }
    int32 NumComponents = 0;
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate)
            {
                NumComponents++;
            }
        }
    }
    Result->SetNumberField(TEXT("num_variables"), NumVariables);
    Result->SetNumberField(TEXT("num_functions"), NumFunctions);
    Result->SetNumberField(TEXT("num_components"), NumComponents);

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3A (v1.15.0) — Blueprint Interfaces
// ─────────────────────────────────────────────────────────────────────────────

// Helper: normalize folder_path to "/Game/.../"-form with trailing slash.
static FString NormalizePackagePath(const FString& InRaw, const FString& Fallback)
{
    if (InRaw.IsEmpty())
    {
        return Fallback;
    }
    FString P = InRaw;
    P.TrimStartAndEndInline();

    if (!P.StartsWith(TEXT("/Game/")) && !P.StartsWith(TEXT("Game/")))
    {
        P = TEXT("/Game/") + P;
    }
    if (P.StartsWith(TEXT("Game/")))
    {
        P = TEXT("/") + P;
    }
    if (!P.StartsWith(TEXT("/")))
    {
        P = TEXT("/") + P;
    }
    if (P.EndsWith(TEXT("/")))
    {
        // Strip trailing slash for AssetTools::CreateAsset (expects "/Game/Foo", not "/Game/Foo/")
        P.RemoveAt(P.Len() - 1);
    }
    return P;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCreateBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
    FString InterfaceName;
    if (!Params->TryGetStringField(TEXT("interface_name"), InterfaceName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'interface_name' parameter"));
    }

    FString PackagePathRaw;
    Params->TryGetStringField(TEXT("package_path"), PackagePathRaw);
    const FString PackagePath = NormalizePackagePath(PackagePathRaw, TEXT("/Game/Interfaces"));

    const FString FullAssetPath = PackagePath + TEXT("/") + InterfaceName;
    if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint interface already exists: %s"), *FullAssetPath));
    }

    // Configure factory for an Interface-type Blueprint.
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = UInterface::StaticClass();
    Factory->BlueprintType = BPTYPE_Interface;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(InterfaceName, PackagePath, UBlueprint::StaticClass(), Factory);
    if (!NewAsset)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AssetTools::CreateAsset returned null for '%s'"), *FullAssetPath));
    }

    UBlueprint* NewBP = Cast<UBlueprint>(NewAsset);
    if (!NewBP)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Created asset is not a UBlueprint: %s"), *FullAssetPath));
    }

    if (UPackage* Pkg = NewBP->GetPackage())
    {
        Pkg->MarkPackageDirty();
    }
    UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("interface_name"), InterfaceName);
    Result->SetStringField(TEXT("path"), FullAssetPath);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// Helper: load an interface UClass from a /Game/... path. Tries object class first
// (_C suffix) then falls back to UBlueprint::GeneratedClass.
static UClass* ResolveInterfaceClass(const FString& InterfacePath)
{
    if (InterfacePath.IsEmpty())
    {
        return nullptr;
    }
    // Strip any trailing ".Name" so we can decide what to load.
    FString PkgOnly = InterfacePath;
    int32 DotIdx;
    if (PkgOnly.FindChar('.', DotIdx))
    {
        PkgOnly = PkgOnly.Left(DotIdx);
    }

    // /Game/Path/Foo -> /Game/Path/Foo.Foo_C is the generated UClass.
    const FString AssetName = FPaths::GetBaseFilename(PkgOnly);
    const FString GeneratedClassPath = FString::Printf(TEXT("%s.%s_C"), *PkgOnly, *AssetName);

    UClass* IfaceClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
    if (IfaceClass)
    {
        return IfaceClass;
    }

    // Fallback: load the Blueprint asset and grab GeneratedClass.
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PkgOnly, *AssetName);
    if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath))
    {
        return BP->GeneratedClass;
    }
    return nullptr;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleImplementBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString InterfacePath;
    if (!Params->TryGetStringField(TEXT("interface_path"), InterfacePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'interface_path' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint && BlueprintName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UClass* InterfaceClass = ResolveInterfaceClass(InterfacePath);
    if (!InterfaceClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not resolve interface class: %s"), *InterfacePath));
    }

    // FBlueprintEditorUtils::ImplementNewInterface takes FTopLevelAssetPath of the interface class.
    const FTopLevelAssetPath InterfaceAssetPath(InterfaceClass->GetPathName());
    const bool bImplemented = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceAssetPath);
    if (!bImplemented)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("ImplementNewInterface failed for '%s' on Blueprint '%s' (already implemented or invalid class?)"),
                *InterfaceClass->GetPathName(), *BlueprintName));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    // Save the BP so the implementation persists.
    FString PackageName = Blueprint->GetPathName();
    int32 DotIdx;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("interface_path"), InterfaceClass->GetPathName());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleRemoveBlueprintInterface(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString InterfacePath;
    if (!Params->TryGetStringField(TEXT("interface_path"), InterfacePath))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'interface_path' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint && BlueprintName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UClass* InterfaceClass = ResolveInterfaceClass(InterfacePath);
    if (!InterfaceClass)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not resolve interface class: %s"), *InterfacePath));
    }

    const FTopLevelAssetPath InterfaceAssetPath(InterfaceClass->GetPathName());
    // bPreserveFunctions=true keeps any overridden functions in the Blueprint after the
    // interface is detached. This matches Editor's "Remove Interface" behaviour.
    FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfaceAssetPath, /*bPreserveFunctions=*/true);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FString PackageName = Blueprint->GetPathName();
    int32 DotIdx;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("interface_path"), InterfaceClass->GetPathName());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// Small helper for HandleAddInterfaceFunction: translates "bool"/"int"/"vector"/...
// the same way FFunctionIO::GetPropertyTypeFromString does. Kept local to avoid
// exporting that helper.
static FEdGraphPinType MakeInterfacePinType(const FString& TypeName)
{
    FEdGraphPinType PinType;
    if (TypeName == TEXT("bool"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (TypeName == TEXT("int") || TypeName == TEXT("int32"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (TypeName == TEXT("float"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (TypeName == TEXT("string"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (TypeName == TEXT("text"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
    }
    else if (TypeName == TEXT("name"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
    }
    else if (TypeName == TEXT("vector") || TypeName == TEXT("FVector"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (TypeName == TEXT("rotator") || TypeName == TEXT("FRotator"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (TypeName == TEXT("object"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
    }
    else
    {
        // Fallback: object — interface signatures can be tightened in the implementer.
        UE_LOG(LogTemp, Warning, TEXT("AddInterfaceFunction: Unknown type '%s', defaulting to object"), *TypeName);
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
    }
    return PinType;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleAddInterfaceFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString InterfaceName;
    if (!Params->TryGetStringField(TEXT("interface_name"), InterfaceName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'interface_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    // Locate the interface Blueprint. Search /Game/Blueprints/, /Game/Interfaces/, or use
    // a full /Game/... path if given.
    UBlueprint* Blueprint = nullptr;
    if (InterfaceName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(InterfaceName));
    }
    if (!Blueprint)
    {
        Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(InterfaceName);
    }
    if (!Blueprint)
    {
        const TArray<FString> SearchDirs = {
            TEXT("/Game/Interfaces/"),
            TEXT("/Game/Blueprints/Interfaces/"),
            TEXT("/Game/UI/Interfaces/"),
        };
        for (const FString& Dir : SearchDirs)
        {
            const FString ObjectPath = Dir + InterfaceName + TEXT(".") + InterfaceName;
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
            {
                Blueprint = Cast<UBlueprint>(Asset);
                if (Blueprint) break;
            }
        }
    }
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Interface Blueprint not found: %s"), *InterfaceName));
    }

    if (Blueprint->BlueprintType != BPTYPE_Interface)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' is not an Interface (BlueprintType=%d)"),
                *InterfaceName, (int32)Blueprint->BlueprintType));
    }

    // Reject duplicates.
    for (UEdGraph* G : Blueprint->FunctionGraphs)
    {
        if (G && G->GetFName().ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Interface function already exists: %s"), *FunctionName));
        }
    }

    // Create the function graph and register it with the Blueprint. AddFunctionGraph
    // takes care of creating FunctionEntry (and FunctionResult, if outputs are added).
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        FName(*FunctionName),
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create interface function graph"));
    }
    FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/true, nullptr);

    // Find the FunctionEntry node to attach inputs as user-defined pins.
    UK2Node_FunctionEntry* EntryNode = nullptr;
    for (UEdGraphNode* N : NewGraph->Nodes)
    {
        if (N && N->IsA<UK2Node_FunctionEntry>())
        {
            EntryNode = Cast<UK2Node_FunctionEntry>(N);
            break;
        }
    }
    if (!EntryNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FunctionEntry node not found after graph creation"));
    }

    // Inputs: array of {name, type}.
    int32 NumInputs = 0;
    const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
    if (Params->TryGetArrayField(TEXT("inputs"), InputsArr) && InputsArr)
    {
        for (const TSharedPtr<FJsonValue>& Val : *InputsArr)
        {
            const TSharedPtr<FJsonObject>* Obj = nullptr;
            if (!Val.IsValid() || !Val->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;

            FString PName, PType;
            if (!(*Obj)->TryGetStringField(TEXT("name"), PName)) continue;
            if (!(*Obj)->TryGetStringField(TEXT("type"), PType)) continue;

            // Input on interface function = output of FunctionEntry (matches FunctionIO).
            const FEdGraphPinType PinType = MakeInterfacePinType(PType);
            UEdGraphPin* NewPin = EntryNode->CreateUserDefinedPin(*PName, PinType, EGPD_Output);
            if (NewPin) ++NumInputs;
        }
    }

    // Outputs: array of {name, type}. For interface Blueprints, signature changes on
    // FunctionEntry are enough; AddFunctionGraph will instantiate FunctionResult lazily
    // when an implementer creates an override. We still emit warning if outputs requested.
    int32 NumOutputs = 0;
    const TArray<TSharedPtr<FJsonValue>>* OutputsArr = nullptr;
    if (Params->TryGetArrayField(TEXT("outputs"), OutputsArr) && OutputsArr)
    {
        // Find or auto-create FunctionResult.
        UK2Node_FunctionResult* ResultNode = nullptr;
        for (UEdGraphNode* N : NewGraph->Nodes)
        {
            if (N && N->IsA<UK2Node_FunctionResult>())
            {
                ResultNode = Cast<UK2Node_FunctionResult>(N);
                break;
            }
        }
        if (!ResultNode)
        {
            // Force its creation by adding a dummy output then removing it.
            FEdGraphPinType DummyType;
            DummyType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
            UEdGraphPin* DummyPin = EntryNode->CreateUserDefinedPin(TEXT("__DummyOutput"), DummyType, EGPD_Input);
            if (DummyPin)
            {
                for (int32 i = EntryNode->UserDefinedPins.Num() - 1; i >= 0; --i)
                {
                    if (EntryNode->UserDefinedPins[i]->PinName == TEXT("__DummyOutput"))
                    {
                        EntryNode->RemoveUserDefinedPin(EntryNode->UserDefinedPins[i]);
                        break;
                    }
                }
            }
            for (UEdGraphNode* N : NewGraph->Nodes)
            {
                if (N && N->IsA<UK2Node_FunctionResult>())
                {
                    ResultNode = Cast<UK2Node_FunctionResult>(N);
                    break;
                }
            }
        }

        if (ResultNode)
        {
            for (const TSharedPtr<FJsonValue>& Val : *OutputsArr)
            {
                const TSharedPtr<FJsonObject>* Obj = nullptr;
                if (!Val.IsValid() || !Val->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;

                FString PName, PType;
                if (!(*Obj)->TryGetStringField(TEXT("name"), PName)) continue;
                if (!(*Obj)->TryGetStringField(TEXT("type"), PType)) continue;

                const FEdGraphPinType PinType = MakeInterfacePinType(PType);
                // Outputs of an interface function are inputs on FunctionResult.
                UEdGraphPin* NewPin = ResultNode->CreateUserDefinedPin(*PName, PinType, EGPD_Input);
                if (NewPin) ++NumOutputs;
            }
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    NewGraph->NotifyGraphChanged();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    // Save the interface asset.
    FString PackageName = Blueprint->GetPathName();
    int32 DotIdx;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("interface_name"), InterfaceName);
    Result->SetStringField(TEXT("function_name"), FunctionName);
    Result->SetNumberField(TEXT("num_inputs"), NumInputs);
    Result->SetNumberField(TEXT("num_outputs"), NumOutputs);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3D (v1.15.0) — Compile diagnostics
// ─────────────────────────────────────────────────────────────────────────────

// Helper: convert FTokenizedMessage to flat JSON object with text + optional node_id.
static TSharedPtr<FJsonObject> TokenizedMessageToJson(const TSharedRef<FTokenizedMessage>& Message)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("text"), Message->ToText().ToString());

    // Scan tokens for first UObject reference that's an UEdGraphNode → expose node_id.
    for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
    {
        if (Token->GetType() == EMessageToken::Object)
        {
            // FUObjectToken::GetObject() returns FWeakObjectPtr (untyped). Resolve to UObject*.
            const FWeakObjectPtr& Ref = static_cast<const FUObjectToken&>(Token.Get()).GetObject();
            if (UObject* Resolved = Ref.Get())
            {
                if (UEdGraphNode* Node = Cast<UEdGraphNode>(Resolved))
                {
                    Obj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
                    Obj->SetStringField(TEXT("node_title"),
                        Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                    break;
                }
            }
        }
    }
    return Obj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleCompileBlueprintVerbose(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint && BlueprintName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    FCompilerResultsLog ResultsLog;
    ResultsLog.bAnnotateMentionedNodes = true;
    ResultsLog.SetSourcePath(Blueprint->GetPathName());
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Notes;

    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        const EMessageSeverity::Type Severity = Msg->GetSeverity();
        TSharedPtr<FJsonObject> Obj = TokenizedMessageToJson(Msg);

        if (Severity == EMessageSeverity::Error)
        {
            Errors.Add(MakeShared<FJsonValueObject>(Obj));
        }
        else if (Severity == EMessageSeverity::Warning ||
                 Severity == EMessageSeverity::PerformanceWarning)
        {
            Warnings.Add(MakeShared<FJsonValueObject>(Obj));
        }
        else
        {
            Notes.Add(MakeShared<FJsonValueObject>(Obj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetBoolField(TEXT("compiled"), ResultsLog.NumErrors == 0);
    Result->SetNumberField(TEXT("num_errors"), ResultsLog.NumErrors);
    Result->SetNumberField(TEXT("num_warnings"), ResultsLog.NumWarnings);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetArrayField(TEXT("notes"), Notes);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintCommands::HandleValidateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint && BlueprintName.StartsWith(TEXT("/")))
    {
        Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Run structural validation without invoking the full compiler. We feed the same
    // FCompilerResultsLog so node-level validators (UEdGraphNode::ValidateNodeDuringCompilation)
    // can emit their messages without producing a class-rebuild artifact.
    FCompilerResultsLog ResultsLog;
    ResultsLog.bAnnotateMentionedNodes = true;
    ResultsLog.SetSourcePath(Blueprint->GetPathName());

    // Walk all graphs. UE 5.7 stores Blueprint::*Graphs as TArray<TObjectPtr<UEdGraph>>,
    // which implicitly converts to UEdGraph* inside range-for.
    auto WalkGraphs = [&](const auto& Graphs)
    {
        for (UEdGraph* G : Graphs)
        {
            if (!G) continue;
            for (UEdGraphNode* Node : G->Nodes)
            {
                if (!Node) continue;
                Node->ValidateNodeDuringCompilation(ResultsLog);

                // Surface orphaned pins as explicit warnings — they cause silent
                // miscompiles otherwise.
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin && Pin->bOrphanedPin)
                    {
                        ResultsLog.Warning(
                            TEXT("Node @@ has orphaned pin '@@'"),
                            Node,
                            Pin);
                    }
                }
            }
        }
    };

    WalkGraphs(Blueprint->UbergraphPages);
    WalkGraphs(Blueprint->FunctionGraphs);
    WalkGraphs(Blueprint->MacroGraphs);
    WalkGraphs(Blueprint->IntermediateGeneratedGraphs);
    WalkGraphs(Blueprint->DelegateSignatureGraphs);

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        const EMessageSeverity::Type Severity = Msg->GetSeverity();
        TSharedPtr<FJsonObject> Obj = TokenizedMessageToJson(Msg);

        if (Severity == EMessageSeverity::Error)
        {
            Errors.Add(MakeShared<FJsonValueObject>(Obj));
        }
        else if (Severity == EMessageSeverity::Warning ||
                 Severity == EMessageSeverity::PerformanceWarning)
        {
            Warnings.Add(MakeShared<FJsonValueObject>(Obj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetNumberField(TEXT("num_errors"), Errors.Num());
    Result->SetNumberField(TEXT("num_warnings"), Warnings.Num());
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}