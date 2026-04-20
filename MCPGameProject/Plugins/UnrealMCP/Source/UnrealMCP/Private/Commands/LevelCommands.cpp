#include "Commands/LevelCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h" // TActorIterator
#include "EditorAssetLibrary.h"
#include "FileHelpers.h" // UEditorLoadingAndSavingUtils
#include "Factories/WorldFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "PackageTools.h" // UPackageTools::UnloadPackages
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"

// ─────────────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Split "/Game/Maps/Arena" into ("/Game/Maps", "Arena").
    bool SplitPackagePath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
    {
        int32 LastSlash = INDEX_NONE;
        if (!InPath.FindLastChar(TCHAR('/'), LastSlash) || LastSlash <= 0)
        {
            return false;
        }
        OutPackagePath = InPath.Left(LastSlash);
        OutAssetName = InPath.Mid(LastSlash + 1);
        return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
    }

    // Build a JSON number array from an FVector / FRotator.
    TArray<TSharedPtr<FJsonValue>> VecToJson(const FVector& V)
    {
        TArray<TSharedPtr<FJsonValue>> A;
        A.Add(MakeShared<FJsonValueNumber>(V.X));
        A.Add(MakeShared<FJsonValueNumber>(V.Y));
        A.Add(MakeShared<FJsonValueNumber>(V.Z));
        return A;
    }
    TArray<TSharedPtr<FJsonValue>> RotToJson(const FRotator& R)
    {
        TArray<TSharedPtr<FJsonValue>> A;
        A.Add(MakeShared<FJsonValueNumber>(R.Pitch));
        A.Add(MakeShared<FJsonValueNumber>(R.Yaw));
        A.Add(MakeShared<FJsonValueNumber>(R.Roll));
        return A;
    }

    // Parse transform JSON: { loc:[x,y,z], rot:[pitch,yaw,roll], scale:[x,y,z] }.
    // Every component is optional; missing fields default to zero/zero/one.
    FTransform ParseTransform(const TSharedPtr<FJsonObject>& Obj)
    {
        FVector Loc(0, 0, 0);
        FRotator Rot(0, 0, 0);
        FVector Scale(1, 1, 1);
        if (!Obj.IsValid())
        {
            return FTransform(Rot, Loc, Scale);
        }

        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (Obj->TryGetArrayField(TEXT("loc"), Arr) && Arr && Arr->Num() >= 3)
        {
            Loc = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
        }
        if (Obj->TryGetArrayField(TEXT("rot"), Arr) && Arr && Arr->Num() >= 3)
        {
            // FRotator ctor order: (Pitch, Yaw, Roll).
            Rot = FRotator((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
        }
        if (Obj->TryGetArrayField(TEXT("scale"), Arr) && Arr && Arr->Num() >= 3)
        {
            Scale = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
        }
        return FTransform(Rot, Loc, Scale);
    }

    // Locate an actor in a world by ActorLabel first, then GetName().
    AActor* FindActorInWorld(UWorld* World, const FString& ActorName)
    {
        if (!World || ActorName.IsEmpty()) return nullptr;
        // Pass 1: label.
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* A = *It;
            if (A && A->GetActorLabel() == ActorName) return A;
        }
        // Pass 2: FName.
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* A = *It;
            if (A && A->GetName() == ActorName) return A;
        }
        return nullptr;
    }

    // Resolve an actor class from a /Script/... or /Game/... path.
    // Auto-appends "_C" for BP paths that lack it.
    UClass* ResolveActorClass(const FString& InClassPath, FString& OutError)
    {
        OutError.Reset();
        if (InClassPath.IsEmpty())
        {
            OutError = TEXT("actorClass is empty");
            return nullptr;
        }

        if (InClassPath.StartsWith(TEXT("/Script/")))
        {
            UClass* Cls = LoadClass<AActor>(nullptr, *InClassPath);
            if (!Cls)
            {
                OutError = FString::Printf(TEXT("LoadClass<AActor> returned null for '%s'"), *InClassPath);
            }
            return Cls;
        }

        if (InClassPath.StartsWith(TEXT("/Game/")))
        {
            FString Resolved = InClassPath;
            // Standard BP generated class path requires "_C" suffix on the object name.
            // Accept both "/Game/Path/BP_Foo" and "/Game/Path/BP_Foo.BP_Foo_C" forms.
            if (!Resolved.EndsWith(TEXT("_C")))
            {
                // If the path contains a "." we assume fully-qualified object
                // path; otherwise it's a short asset path — synthesize ".Name_C".
                int32 DotIdx = INDEX_NONE;
                if (Resolved.FindChar(TCHAR('.'), DotIdx))
                {
                    // "/Game/.../BP_Foo.BP_Foo" → append "_C"
                    Resolved += TEXT("_C");
                }
                else
                {
                    // "/Game/.../BP_Foo" → "/Game/.../BP_Foo.BP_Foo_C"
                    int32 LastSlash = INDEX_NONE;
                    Resolved.FindLastChar(TCHAR('/'), LastSlash);
                    const FString Name = (LastSlash != INDEX_NONE) ? Resolved.Mid(LastSlash + 1) : Resolved;
                    Resolved = Resolved + TEXT(".") + Name + TEXT("_C");
                }
            }

            UClass* Cls = LoadObject<UClass>(nullptr, *Resolved);
            if (!Cls)
            {
                // Fallback — load the Blueprint asset and read GeneratedClass.
                FString AssetPath = InClassPath;
                int32 Dot = INDEX_NONE;
                if (AssetPath.FindChar(TCHAR('.'), Dot))
                {
                    AssetPath = AssetPath.Left(Dot);
                }
                if (AssetPath.EndsWith(TEXT("_C")))
                {
                    AssetPath = AssetPath.LeftChop(2);
                }
                if (UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath)))
                {
                    Cls = BP->GeneratedClass;
                }
            }
            if (!Cls)
            {
                OutError = FString::Printf(TEXT("Could not resolve BP class from '%s'"), *InClassPath);
                return nullptr;
            }
            if (!Cls->IsChildOf(AActor::StaticClass()))
            {
                OutError = FString::Printf(TEXT("Class '%s' is not an AActor subclass"), *InClassPath);
                return nullptr;
            }
            return Cls;
        }

        OutError = FString::Printf(TEXT("Unrecognized class path prefix: '%s' (expected /Script/ or /Game/)"), *InClassPath);
        return nullptr;
    }

    // Apply a JSON `properties` map onto an actor via FProperty reflection.
    // Narrow v1 support: FStrProperty, FNameProperty, FIntProperty, FFloatProperty,
    // FDoubleProperty, FBoolProperty, FStructProperty(FVector).
    // Returns list of property names that could not be applied.
    TArray<FString> ApplyPropertyOverrides(AActor* Actor, const TSharedPtr<FJsonObject>& PropsObj)
    {
        TArray<FString> Skipped;
        if (!Actor || !PropsObj.IsValid()) return Skipped;

        UClass* Cls = Actor->GetClass();
        for (const auto& Pair : PropsObj->Values)
        {
            const FString& Key = Pair.Key;
            const TSharedPtr<FJsonValue>& Val = Pair.Value;

            FProperty* Prop = FindFProperty<FProperty>(Cls, *Key);
            if (!Prop)
            {
                Skipped.Add(Key);
                continue;
            }
            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);

            if (FStrProperty* SP = CastField<FStrProperty>(Prop))
            {
                FString S;
                if (Val.IsValid() && Val->TryGetString(S)) { SP->SetPropertyValue(ValuePtr, S); }
                else { Skipped.Add(Key); }
            }
            else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
            {
                FString S;
                if (Val.IsValid() && Val->TryGetString(S)) { NP->SetPropertyValue(ValuePtr, FName(*S)); }
                else { Skipped.Add(Key); }
            }
            else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
            {
                double N = 0;
                if (Val.IsValid() && Val->TryGetNumber(N)) { IP->SetPropertyValue(ValuePtr, (int32)N); }
                else { Skipped.Add(Key); }
            }
            else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
            {
                double N = 0;
                if (Val.IsValid() && Val->TryGetNumber(N)) { FP->SetPropertyValue(ValuePtr, (float)N); }
                else { Skipped.Add(Key); }
            }
            else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
            {
                double N = 0;
                if (Val.IsValid() && Val->TryGetNumber(N)) { DP->SetPropertyValue(ValuePtr, N); }
                else { Skipped.Add(Key); }
            }
            else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
            {
                bool B = false;
                if (Val.IsValid() && Val->TryGetBool(B)) { BP->SetPropertyValue(ValuePtr, B); }
                else { Skipped.Add(Key); }
            }
            else if (FStructProperty* StP = CastField<FStructProperty>(Prop))
            {
                // FVector only for v1.
                if (StP->Struct == TBaseStructure<FVector>::Get())
                {
                    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
                    if (Val.IsValid() && Val->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
                    {
                        FVector V((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
                        *static_cast<FVector*>(ValuePtr) = V;
                    }
                    else { Skipped.Add(Key); }
                }
                else
                {
                    Skipped.Add(Key);
                }
            }
            else
            {
                Skipped.Add(Key);
            }
        }
        return Skipped;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FLevelCommands
// ─────────────────────────────────────────────────────────────────────────────

FLevelCommands::FLevelCommands()
{
}

TSharedPtr<FJsonObject> FLevelCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_level"))                 return HandleCreateLevel(Params);
    if (CommandType == TEXT("load_level"))                   return HandleLoadLevel(Params);
    if (CommandType == TEXT("save_level"))                   return HandleSaveLevel(Params);
    if (CommandType == TEXT("spawn_actor_in_level"))         return HandleSpawnActorInLevel(Params);
    if (CommandType == TEXT("remove_actor_from_level"))      return HandleRemoveActorFromLevel(Params);
    if (CommandType == TEXT("set_actor_transform_in_level")) return HandleSetActorTransformInLevel(Params);
    if (CommandType == TEXT("list_actors_in_level"))         return HandleListActorsInLevel(Params);

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_LEVEL_COMMAND"),
        FString::Printf(TEXT("Level category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// create_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleCreateLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString DestMapPath;
    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("destMapPath"), DestMapPath) ||
        DestMapPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_DEST_MAP_PATH"),
            TEXT("Required param 'destMapPath' is missing or empty"));
    }
    if (!FAssetCommonUtils::IsValidAssetPath(DestMapPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("destMapPath"), DestMapPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("destMapPath '%s' must start with /Game/"), *DestMapPath),
            D);
    }

    FString Template = TEXT("Empty");
    Params->TryGetStringField(TEXT("template"), Template);
    const FString TemplateUpper = Template.ToUpper();
    if (TemplateUpper != TEXT("EMPTY") && TemplateUpper != TEXT("DEFAULT"))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("template"), Template);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_LEVEL_TEMPLATE"),
            TEXT("Param 'template' must be 'Empty' or 'Default'"),
            D);
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(DestMapPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bOverwrite = (Decision.Action == TEXT("overwrite") || Decision.Action == TEXT("update"));

    FString PackagePath, AssetName;
    if (!SplitPackagePath(DestMapPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("destMapPath"), DestMapPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split '%s' into package+name"), *DestMapPath),
            D);
    }

    // On overwrite, clear the existing umap so AssetTools::CreateAsset can land a fresh one.
    if (bOverwrite && UEditorAssetLibrary::DoesAssetExist(DestMapPath))
    {
        UEditorAssetLibrary::DeleteAsset(DestMapPath);
    }

    // Build UWorldFactory with WP explicitly disabled.
    UWorldFactory* Factory = NewObject<UWorldFactory>();
    Factory->WorldType = EWorldType::Inactive;
    Factory->bInformEngineOfWorld = true;
    Factory->bCreateWorldPartition = false;
    Factory->bEnableWorldPartitionStreaming = false;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewWorldObj = AssetTools.CreateAsset(AssetName, PackagePath, UWorld::StaticClass(), Factory);
    UWorld* NewWorld = Cast<UWorld>(NewWorldObj);
    if (!NewWorld)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LEVEL_CREATE_FAILED"),
            FString::Printf(TEXT("AssetTools::CreateAsset returned null for '%s'"), *DestMapPath));
    }

    // Default template — populate with a minimum usable actor set.
    if (TemplateUpper == TEXT("DEFAULT"))
    {
        FActorSpawnParameters SP;
        SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        NewWorld->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0, 0, 1000), FRotator(-45, 0, 0), SP);
        NewWorld->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, SP);
        NewWorld->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, SP);
    }

    // Save.
    const FString PackageName = FPackageName::ObjectPathToPackageName(NewWorld->GetPathName());
    if (!UEditorLoadingAndSavingUtils::SaveMap(NewWorld, PackageName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("packageName"), PackageName);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LEVEL_SAVE_FAILED"),
            TEXT("SaveMap returned false after CreateAsset"),
            D);
    }

    // Dissociate the fresh world from the editor-world context and fully
    // unload its package from memory. AssetTools::CreateAsset makes the new
    // UWorld the active editor world AND leaves it rooted via RF_Standalone
    // on the saved asset. Just switching to a blank editor world isn't
    // enough — the next LoadMap(MAP_PATH) finds the old in-memory UWorld
    // instance still alive (standalone flag blocks normal GC) and trips
    // EditorServer.cpp:2524 "World Memory Leaks". UnloadPackages clears
    // the flags + triggers the garbage pass; the on-disk .umap survives
    // untouched.
    const FString CreatedPackageName = PackageName;
    NewWorld = nullptr;
    Factory = nullptr;
    UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap=*/false);
    if (UPackage* CreatedPackage = FindPackage(nullptr, *CreatedPackageName))
    {
        TArray<UPackage*> ToUnload;
        ToUnload.Add(CreatedPackage);
        FText UnloadErr;
        UPackageTools::UnloadPackages(ToUnload, UnloadErr);
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("template"), (TemplateUpper == TEXT("DEFAULT")) ? TEXT("Default") : TEXT("Empty"));
    Meta->SetBoolField(TEXT("worldPartition"), false);

    const FString Status = bOverwrite ? TEXT("overwritten") : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, DestMapPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// load_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleLoadLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString MapPath;
    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("mapPath"), MapPath) ||
        MapPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_MAP_PATH"),
            TEXT("Required param 'mapPath' is missing or empty"));
    }

    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("mapPath"), MapPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LEVEL_LOAD_FAILED"),
            Err,
            D);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("handle"), MapPath);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), MapPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// save_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleSaveLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString MapPath;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("mapPath"), MapPath);
    }

    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LEVEL_RESOLVE_FAILED"),
            Err);
    }

    const FString PackageName = FPackageName::ObjectPathToPackageName(World->GetPathName());
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, PackageName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("packageName"), PackageName);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LEVEL_SAVE_FAILED"),
            TEXT("SaveMap returned false"),
            D);
    }

    const FString ResolvedPath = MapPath.IsEmpty() ? PackageName : MapPath;
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("packageName"), PackageName);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), ResolvedPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// spawn_actor_in_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleSpawnActorInLevel(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString ActorClassPath;
    if (!Params->TryGetStringField(TEXT("actorClass"), ActorClassPath) || ActorClassPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_ACTOR_CLASS"),
            TEXT("Required param 'actorClass' is missing or empty"));
    }

    FString MapPath;
    Params->TryGetStringField(TEXT("mapPath"), MapPath);

    FString DesiredName;
    Params->TryGetStringField(TEXT("name"), DesiredName);

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);
    const FString IfExistsLower = IfExists.IsEmpty() ? TEXT("skip") : IfExists.ToLower();

    const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
    Params->TryGetObjectField(TEXT("transform"), TransformObjPtr);
    const FTransform Xform = ParseTransform(TransformObjPtr ? *TransformObjPtr : nullptr);

    const TSharedPtr<FJsonObject>* PropsObjPtr = nullptr;
    Params->TryGetObjectField(TEXT("properties"), PropsObjPtr);

    // Resolve class.
    FString ClassErr;
    UClass* ActorClass = ResolveActorClass(ActorClassPath, ClassErr);
    if (!ActorClass)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("actorClass"), ActorClassPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ACTOR_CLASS"),
            ClassErr,
            D);
    }

    // Resolve world.
    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("LEVEL_RESOLVE_FAILED"), Err);
    }

    // Idempotency on desired label.
    if (!DesiredName.IsEmpty())
    {
        if (AActor* Existing = FindActorInWorld(World, DesiredName))
        {
            if (IfExistsLower == TEXT("skip"))
            {
                TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
                Meta->SetStringField(TEXT("reason"), TEXT("actor_exists"));
                Meta->SetStringField(TEXT("actorLabel"), Existing->GetActorLabel());
                return FAssetCommonUtils::MakeSuccessResponse(TEXT("skipped"),
                    World->GetPathName() + TEXT(":PersistentLevel.") + Existing->GetName(), Meta);
            }
            if (IfExistsLower == TEXT("fail"))
            {
                TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
                D->SetStringField(TEXT("actorLabel"), DesiredName);
                return FAssetCommonUtils::MakeFailureResponse(
                    TEXT("user"),
                    TEXT("ACTOR_ALREADY_EXISTS"),
                    FString::Printf(TEXT("Actor labeled '%s' already exists"), *DesiredName),
                    D);
            }
            if (IfExistsLower == TEXT("overwrite"))
            {
                World->DestroyActor(Existing);
            }
        }
    }

    // Spawn.
    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Xform, SP);
    if (!NewActor)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("actorClass"), ActorClassPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("ACTOR_SPAWN_FAILED"),
            TEXT("SpawnActor returned null"),
            D);
    }

    // Apply label + rename (stable FName).
    if (!DesiredName.IsEmpty())
    {
        NewActor->SetActorLabel(DesiredName);
        // Rename may fail on name collision; non-fatal.
        NewActor->Rename(*DesiredName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
    }

    // Apply property overrides (v1 narrow).
    TArray<FString> Skipped;
    if (PropsObjPtr && PropsObjPtr->IsValid())
    {
        Skipped = ApplyPropertyOverrides(NewActor, *PropsObjPtr);
    }

    // Save if we opened a specific map.
    if (bOpened)
    {
        const FString PackageName = FPackageName::ObjectPathToPackageName(World->GetPathName());
        UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("actorLabel"), NewActor->GetActorLabel());
    Meta->SetStringField(TEXT("actorName"), NewActor->GetName());
    Meta->SetStringField(TEXT("class"), ActorClass->GetPathName());
    Meta->SetArrayField(TEXT("location"), VecToJson(NewActor->GetActorLocation()));
    Meta->SetArrayField(TEXT("rotation"), RotToJson(NewActor->GetActorRotation()));
    Meta->SetArrayField(TEXT("scale"),    VecToJson(NewActor->GetActorScale3D()));
    TArray<TSharedPtr<FJsonValue>> SkippedJson;
    for (const FString& S : Skipped) SkippedJson.Add(MakeShared<FJsonValueString>(S));
    Meta->SetArrayField(TEXT("skippedProperties"), SkippedJson);

    const FString AssetPathOut = World->GetPathName() + TEXT(":PersistentLevel.") + NewActor->GetName();
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("created"), AssetPathOut, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// remove_actor_from_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleRemoveActorFromLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("actorName"), ActorName) ||
        ActorName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_ACTOR_NAME"),
            TEXT("Required param 'actorName' is missing or empty"));
    }

    FString MapPath;
    Params->TryGetStringField(TEXT("mapPath"), MapPath);

    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("LEVEL_RESOLVE_FAILED"), Err);
    }

    AActor* Found = FindActorInWorld(World, ActorName);
    if (!Found)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("actorName"), ActorName);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("No actor with label/name '%s'"), *ActorName),
            D);
    }
    const FString RemovedName = Found->GetName();
    const FString RemovedLabel = Found->GetActorLabel();
    const bool bOk = World->DestroyActor(Found);
    if (!bOk)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("ACTOR_DESTROY_FAILED"),
            FString::Printf(TEXT("DestroyActor returned false for '%s'"), *ActorName));
    }

    if (bOpened)
    {
        const FString PackageName = FPackageName::ObjectPathToPackageName(World->GetPathName());
        UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("actorName"), RemovedName);
    Meta->SetStringField(TEXT("actorLabel"), RemovedLabel);
    const FString AssetPathOut = MapPath.IsEmpty() ? World->GetPathName() : MapPath;
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPathOut, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_actor_transform_in_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleSetActorTransformInLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params.IsValid() ||
        !Params->TryGetStringField(TEXT("actorName"), ActorName) ||
        ActorName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_ACTOR_NAME"),
            TEXT("Required param 'actorName' is missing or empty"));
    }

    const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("transform"), TransformObjPtr) || !TransformObjPtr || !TransformObjPtr->IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_TRANSFORM"),
            TEXT("Required param 'transform' is missing"));
    }
    const FTransform Xform = ParseTransform(*TransformObjPtr);

    FString MapPath;
    Params->TryGetStringField(TEXT("mapPath"), MapPath);

    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("LEVEL_RESOLVE_FAILED"), Err);
    }

    AActor* Found = FindActorInWorld(World, ActorName);
    if (!Found)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("actorName"), ActorName);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("ACTOR_NOT_FOUND"),
            FString::Printf(TEXT("No actor with label/name '%s'"), *ActorName),
            D);
    }
    Found->SetActorTransform(Xform);

    if (bOpened)
    {
        const FString PackageName = FPackageName::ObjectPathToPackageName(World->GetPathName());
        UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("actorName"), Found->GetName());
    Meta->SetStringField(TEXT("actorLabel"), Found->GetActorLabel());
    Meta->SetArrayField(TEXT("location"), VecToJson(Found->GetActorLocation()));
    Meta->SetArrayField(TEXT("rotation"), RotToJson(Found->GetActorRotation()));
    Meta->SetArrayField(TEXT("scale"),    VecToJson(Found->GetActorScale3D()));
    const FString AssetPathOut = MapPath.IsEmpty() ? World->GetPathName() : MapPath;
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPathOut, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// list_actors_in_level
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLevelCommands::HandleListActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString MapPath;
    FString ClassFilter;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("mapPath"), MapPath);
        Params->TryGetStringField(TEXT("classFilter"), ClassFilter);
    }

    UWorld* World = nullptr;
    bool bOpened = false;
    FString Err;
    if (!FAssetCommonUtils::ResolveLevelContext(MapPath, World, bOpened, Err))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("LEVEL_RESOLVE_FAILED"), Err);
    }

    UClass* FilterClass = nullptr;
    if (!ClassFilter.IsEmpty())
    {
        FString FilterErr;
        FilterClass = ResolveActorClass(ClassFilter, FilterErr);
        if (!FilterClass)
        {
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("classFilter"), ClassFilter);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("INVALID_CLASS_FILTER"),
                FilterErr,
                D);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Arr;
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!A) continue;
        if (FilterClass && !A->IsA(FilterClass)) continue;

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"),  A->GetName());
        Obj->SetStringField(TEXT("label"), A->GetActorLabel());
        Obj->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
        Obj->SetArrayField(TEXT("loc"),   VecToJson(A->GetActorLocation()));
        Obj->SetArrayField(TEXT("rot"),   RotToJson(A->GetActorRotation()));
        Obj->SetArrayField(TEXT("scale"), VecToJson(A->GetActorScale3D()));
        Arr.Add(MakeShared<FJsonValueObject>(Obj));
        ++Count;
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("count"), Count);
    Meta->SetArrayField(TEXT("actors"), Arr);

    const FString AssetPathOut = MapPath.IsEmpty() ? World->GetPathName() : MapPath;
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPathOut, Meta);
}
