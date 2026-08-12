#include "Commands/MeshCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AutomatedAssetImportData.h"
#include "Factories/FbxFactory.h"
#include "HAL/IConsoleManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "SceneTypes.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "EditorAssetLibrary.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "StaticMeshCompiler.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Generates a single FKBoxElem sized to the mesh bounds and adds it to the
    // BodySetup's aggregate geometry. Mirrors the core behavior of
    // GenerateBoxAsSimpleCollision (which lives in a Private engine header and
    // is therefore not includable from an external module). Returns true on
    // success; false if mesh has no valid bounds.
    bool AddBoxCollisionFromBounds(UStaticMesh* Mesh)
    {
        if (!Mesh) return false;
        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (!BodySetup)
        {
            Mesh->CreateBodySetup();
            BodySetup = Mesh->GetBodySetup();
        }
        if (!BodySetup) return false;

        const FBox Bounds = Mesh->GetBoundingBox();
        if (!Bounds.IsValid) return false;

        const FVector Center = Bounds.GetCenter();
        const FVector Extents = Bounds.GetExtent(); // half-size per axis

        BodySetup->Modify();
        BodySetup->RemoveSimpleCollision();

        FKBoxElem BoxElem;
        BoxElem.Center = Center;
        BoxElem.Rotation = FRotator::ZeroRotator;
        BoxElem.X = Extents.X * 2.0f;
        BoxElem.Y = Extents.Y * 2.0f;
        BoxElem.Z = Extents.Z * 2.0f;
        BodySetup->AggGeom.BoxElems.Add(BoxElem);

        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FMeshCommands
// ─────────────────────────────────────────────────────────────────────────────

FMeshCommands::FMeshCommands()
{
}

TSharedPtr<FJsonObject> FMeshCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("import_static_mesh"))
    {
        return HandleImportStaticMesh(Params);
    }
    if (CommandType == TEXT("generate_box_static_mesh"))
    {
        return HandleGenerateBoxStaticMesh(Params);
    }
    if (CommandType == TEXT("set_nanite_enabled"))
    {
        return HandleSetNaniteEnabled(Params);
    }
    if (CommandType == TEXT("import_skeletal_mesh"))
    {
        return HandleImportSkeletalMesh(Params);
    }
    if (CommandType == TEXT("mesh_get_stats"))
    {
        return HandleMeshGetStats(Params);
    }
    if (CommandType == TEXT("mesh_set_material"))
    {
        return HandleMeshSetMaterial(Params);
    }
    if (CommandType == TEXT("mesh_set_build_scale"))
    {
        return HandleMeshSetBuildScale(Params);
    }

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_MESH_COMMAND"),
        FString::Printf(TEXT("Mesh category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// import_static_mesh
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMeshCommands::HandleImportStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
    // Required params.
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString SourcePath;
    if (!Params->TryGetStringField(TEXT("sourcePath"), SourcePath) || SourcePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_SOURCE_PATH"),
            TEXT("Required param 'sourcePath' is missing or empty"));
    }

    if (!FPaths::FileExists(SourcePath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("SOURCE_FILE_NOT_FOUND"),
            FString::Printf(TEXT("No file at source path '%s'"), *SourcePath),
            Details);
    }

    // Extension check — only FBX and OBJ are supported for the static-mesh primitive.
    const FString Ext = FPaths::GetExtension(SourcePath, /*bIncludeDot=*/false).ToLower();
    if (Ext != TEXT("fbx") && Ext != TEXT("obj"))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        Details->SetStringField(TEXT("extension"), Ext);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("UNSUPPORTED_MESH_FORMAT"),
            FString::Printf(TEXT("Expected .fbx or .obj; got '.%s'"), *Ext),
            Details);
    }

    // Optional params.
    FString CollisionStr = TEXT("Simple");
    Params->TryGetStringField(TEXT("generateCollision"), CollisionStr);
    const FString CollisionUpper = CollisionStr.ToUpper();
    if (CollisionUpper != TEXT("NONE") && CollisionUpper != TEXT("SIMPLE") && CollisionUpper != TEXT("COMPLEX"))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("generateCollision"), CollisionStr);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_COLLISION_MODE"),
            TEXT("Param 'generateCollision' must be one of: None, Simple, Complex"),
            Details);
    }

    const TSharedPtr<FJsonObject>* MaterialOverridesObj = nullptr;
    Params->TryGetObjectField(TEXT("materialOverrides"), MaterialOverridesObj);

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    // Idempotency.
    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));
    const bool bUpdate = (Decision.Action == TEXT("update"));
    const bool bReplaceExisting = bOverwrite || bUpdate;

    // Split asset path.
    FString PackagePath, AssetName;
    if (!FAssetCommonUtils::SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split asset path '%s' into package + name"), *AssetPath),
            Details);
    }

    // UE 5.8: ImportAssetsAutomated routes FBX through Interchange even when
    // UFbxFactory is pinned in ImportData. Interchange pumps the game-thread
    // TaskGraph, re-entering the queue the bridge already dispatched on
    // (AsyncTask GameThread) and triggering RecursionGuard in TaskGraph.cpp.
    //
    // Fix: for FBX call UFbxFactory::ImportObject directly — fully synchronous
    // legacy FBX pipeline, no Interchange involvement.
    // For OBJ fall back to ImportAssetsAutomated (Interchange only path in 5.8).
    // Disable Interchange for FBX to stay on the synchronous legacy path.
    // In UE 5.8, Interchange re-enters the GameThread TaskGraph from inside
    // the bridge's own GameThread dispatch → RecursionGuard assertion in
    // TaskGraph.cpp:705. Setting the CVar to 0 forces ImportAssetsAutomated
    // through the classic UFbxFactory pipeline (fully synchronous, no TaskGraph
    // re-entry). RAII guard (FIX-ANIM-MCP-001): a manual "restore after the
    // call" line — the old shape here — does not run on an early return or an
    // exception; the destructor does, on every path.
    FScopedInterchangeFbxImportDisable InterchangeGuard;

    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->DestinationPath = PackagePath;
    ImportData->bReplaceExisting = bReplaceExisting;
    ImportData->bSkipReadOnly = false;
    ImportData->Filenames.Add(SourcePath);
    if (Ext == TEXT("fbx"))
    {
        ImportData->Factory = NewObject<UFbxFactory>();
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);

    if (Imported.Num() == 0 || !Imported[0])
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        Details->SetStringField(TEXT("destPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MESH_IMPORT_FAILED"),
            TEXT("AssetTools::ImportAssetsAutomated returned no asset"),
            Details);
    }

    // Find a UStaticMesh among imported assets (FBX may also spawn textures /
    // materials — we only care about the mesh for our result).
    UStaticMesh* Mesh = nullptr;
    for (UObject* Obj : Imported)
    {
        if (UStaticMesh* Candidate = Cast<UStaticMesh>(Obj))
        {
            Mesh = Candidate;
            break;
        }
    }
    if (!Mesh)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MESH_IMPORT_NO_STATIC_MESH"),
            FString::Printf(TEXT("Import at '%s' produced no UStaticMesh"), *AssetPath));
    }

    // Rename / relocate so asset name matches requested AssetName.
    if (Mesh->GetName() != AssetName)
    {
        const FString NewObjPath = PackagePath / AssetName;
        // Pre-delete any stale asset at the target path — RenameAsset refuses
        // to overwrite, so on the overwrite/update path we must clear it
        // ourselves. The import always lands under the source-file stem
        // (e.g. /Game/.../cube for cube.fbx), so this delete never touches
        // the freshly imported mesh.
        if (bReplaceExisting && UEditorAssetLibrary::DoesAssetExist(NewObjPath))
        {
            UEditorAssetLibrary::DeleteAsset(NewObjPath);
        }
        if (!UEditorAssetLibrary::RenameAsset(Mesh->GetPathName(), NewObjPath))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("from"), Mesh->GetPathName());
            Details->SetStringField(TEXT("to"), NewObjPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("MESH_RENAME_FAILED"),
                TEXT("Imported static mesh could not be renamed to match assetPath"),
                Details);
        }
        Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
        if (!Mesh)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("MESH_RELOAD_FAILED"),
                FString::Printf(TEXT("Could not reload renamed static mesh at '%s'"), *AssetPath));
        }
    }

    // ── Collision ───────────────────────────────────────────────────────────
    Mesh->Modify();
    if (CollisionUpper == TEXT("SIMPLE"))
    {
        AddBoxCollisionFromBounds(Mesh);
    }
    else if (CollisionUpper == TEXT("COMPLEX"))
    {
        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (!BodySetup)
        {
            Mesh->CreateBodySetup();
            BodySetup = Mesh->GetBodySetup();
        }
        if (BodySetup)
        {
            BodySetup->Modify();
            BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
            BodySetup->InvalidatePhysicsData();
            BodySetup->CreatePhysicsMeshes();
        }
    }
    // "None" → skip.

    // ── Material overrides ──────────────────────────────────────────────────
    TArray<TSharedPtr<FJsonValue>> SkippedOverrides;
    if (MaterialOverridesObj && MaterialOverridesObj->IsValid())
    {
        TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
        for (const auto& Pair : (*MaterialOverridesObj)->Values)
        {
            const FString SlotName = *Pair.Key;
            FString MatPath;
            if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(MatPath) || MatPath.IsEmpty())
            {
                SkippedOverrides.Add(MakeShared<FJsonValueString>(SlotName));
                continue;
            }

            int32 FoundIdx = INDEX_NONE;
            for (int32 i = 0; i < Materials.Num(); ++i)
            {
                if (Materials[i].MaterialSlotName.ToString() == SlotName)
                {
                    FoundIdx = i;
                    break;
                }
            }
            if (FoundIdx == INDEX_NONE)
            {
                SkippedOverrides.Add(MakeShared<FJsonValueString>(SlotName));
                continue;
            }

            UMaterialInterface* Loaded = Cast<UMaterialInterface>(FAssetCommonUtils::LoadAssetObject(MatPath));
            if (!Loaded)
            {
                SkippedOverrides.Add(MakeShared<FJsonValueString>(SlotName));
                continue;
            }
            Mesh->SetMaterial(FoundIdx, Loaded);
        }
    }

    // Finalize + save.
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    // ── Meta response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();

    const int32 NumLODs = Mesh->GetNumLODs();
    const int32 Tris = (NumLODs > 0) ? Mesh->GetNumTriangles(0) : 0;
    const int32 Verts = (NumLODs > 0) ? Mesh->GetNumVertices(0) : 0;
    Meta->SetNumberField(TEXT("trianglesCount"), Tris);
    Meta->SetNumberField(TEXT("verticesCount"), Verts);

    TArray<TSharedPtr<FJsonValue>> SlotArr;
    for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
    {
        SlotArr.Add(MakeShared<FJsonValueString>(M.MaterialSlotName.ToString()));
    }
    Meta->SetArrayField(TEXT("materialSlots"), SlotArr);

    // Normalize collision string to the canonical input spelling.
    FString CollisionOut = TEXT("None");
    if (CollisionUpper == TEXT("SIMPLE"))  CollisionOut = TEXT("Simple");
    if (CollisionUpper == TEXT("COMPLEX")) CollisionOut = TEXT("Complex");
    Meta->SetStringField(TEXT("collision"), CollisionOut);

    Meta->SetArrayField(TEXT("skippedOverrides"), SkippedOverrides);
    Meta->SetStringField(TEXT("sourcePath"), SourcePath);

    const FString Status = bReplaceExisting
        ? (bUpdate ? TEXT("updated") : TEXT("overwritten"))
        : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// generate_box_static_mesh
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMeshCommands::HandleGenerateBoxStaticMesh(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    // ── Optional half-extents (cm) ───────────────────────────────────────────
    double HalfX = 50.0, HalfY = 50.0, HalfZ = 50.0;
    Params->TryGetNumberField(TEXT("halfExtentX"), HalfX);
    Params->TryGetNumberField(TEXT("halfExtentY"), HalfY);
    Params->TryGetNumberField(TEXT("halfExtentZ"), HalfZ);

    const float X = static_cast<float>(HalfX);
    const float Y = static_cast<float>(HalfY);
    const float Z = static_cast<float>(HalfZ);

    if (X <= 0.f || Y <= 0.f || Z <= 0.f)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("halfExtentX"), HalfX);
        Details->SetNumberField(TEXT("halfExtentY"), HalfY);
        Details->SetNumberField(TEXT("halfExtentZ"), HalfZ);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_EXTENTS"),
            TEXT("halfExtentX/Y/Z must all be positive values"),
            Details);
    }

    // ── ifExists idempotency ─────────────────────────────────────────────────
    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bReplaceExisting = (Decision.Action == TEXT("overwrite") || Decision.Action == TEXT("update"));

    // ── Split asset path ─────────────────────────────────────────────────────
    FString PackagePath, AssetName;
    if (!FAssetCommonUtils::SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split asset path '%s' into package + name"), *AssetPath),
            Details);
    }

    // ── Delete existing asset if overwriting ─────────────────────────────────
    if (bReplaceExisting && UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        UEditorAssetLibrary::DeleteAsset(AssetPath);
    }

    // ── Create package + UStaticMesh object ──────────────────────────────────
    const FString PackageName = PackagePath / AssetName;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("PACKAGE_CREATE_FAILED"),
            FString::Printf(TEXT("CreatePackage failed for '%s'"), *PackageName));
    }
    Package->FullyLoad();

    UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
    if (!Mesh)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MESH_CREATE_FAILED"),
            TEXT("NewObject<UStaticMesh> returned null"));
    }

    // ── Source model must exist before CreateMeshDescription ────────────────
    // AddSourceModel allocates LOD 0 which CreateMeshDescription(0) then populates.
    // If AddSourceModel is called after CreateMeshDescription, slot 0 doesn't exist
    // and CreateMeshDescription returns null.
    FStaticMeshSourceModel& SrcModel = Mesh->AddSourceModel();
    SrcModel.BuildSettings.bRecomputeNormals    = false;
    SrcModel.BuildSettings.bRecomputeTangents   = true;
    SrcModel.BuildSettings.bGenerateLightmapUVs = true;

    // ── Build FMeshDescription for LOD 0 ────────────────────────────────────
    FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
    if (!MeshDesc)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MESHDESC_CREATE_FAILED"),
            TEXT("UStaticMesh::CreateMeshDescription(0) returned null after AddSourceModel"));
    }

    FStaticMeshAttributes Attributes(*MeshDesc);
    Attributes.Register();

    // Single polygon group (material slot 0)
    FPolygonGroupID PolyGroup = MeshDesc->CreatePolygonGroup();

    // 8 vertices of a centred box (±X, ±Y, ±Z)
    //   0:(-X,-Y,-Z)  1:(+X,-Y,-Z)  2:(+X,+Y,-Z)  3:(-X,+Y,-Z)   — bottom ring
    //   4:(-X,-Y,+Z)  5:(+X,-Y,+Z)  6:(+X,+Y,+Z)  7:(-X,+Y,+Z)   — top ring
    const float RawVerts[8][3] = {
        {-X, -Y, -Z}, { X, -Y, -Z}, { X,  Y, -Z}, {-X,  Y, -Z},
        {-X, -Y,  Z}, { X, -Y,  Z}, { X,  Y,  Z}, {-X,  Y,  Z}
    };

    auto VertexPositions = Attributes.GetVertexPositions();

    TArray<FVertexID> VIDs;
    VIDs.Reserve(8);
    for (const auto& V : RawVerts)
    {
        FVertexID VID = MeshDesc->CreateVertex();
        VertexPositions[VID] = FVector3f(V[0], V[1], V[2]);
        VIDs.Add(VID);
    }

    // 6 faces defined as quads (CCW winding when viewed from outside)
    // Each quad is split into 2 triangles: (0,1,2) + (0,2,3)
    const int32 Faces[6][4] = {
        {0, 3, 2, 1},  // bottom  (-Z face, normal pointing -Z)
        {4, 5, 6, 7},  // top     (+Z face, normal pointing +Z)
        {0, 1, 5, 4},  // front   (-Y face)
        {1, 2, 6, 5},  // right   (+X face)
        {2, 3, 7, 6},  // back    (+Y face)
        {3, 0, 4, 7}   // left    (-X face)
    };

    auto UVs     = Attributes.GetVertexInstanceUVs();
    auto Normals = Attributes.GetVertexInstanceNormals();

    // Simple per-face UV layout: each quad maps [0..1] x [0..1]
    const FVector2f FaceUVs[4] = {
        {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}
    };

    for (const auto& Face : Faces)
    {
        TArray<FVertexInstanceID> Instances;
        Instances.Reserve(4);
        for (int32 i = 0; i < 4; ++i)
        {
            FVertexInstanceID VI = MeshDesc->CreateVertexInstance(VIDs[Face[i]]);
            UVs.Set(VI, 0, FaceUVs[i]);
            // Normals will be computed by ComputeTriangleTangentsAndNormals below
            Normals[VI] = FVector3f::ZeroVector;
            Instances.Add(VI);
        }
        // Quad → 2 triangles
        MeshDesc->CreatePolygon(PolyGroup, TArrayView<const FVertexInstanceID>({Instances[0], Instances[1], Instances[2]}));
        MeshDesc->CreatePolygon(PolyGroup, TArrayView<const FVertexInstanceID>({Instances[0], Instances[2], Instances[3]}));
    }

    FStaticMeshOperations::ComputeTriangleTangentsAndNormals(*MeshDesc);

    Mesh->CommitMeshDescription(0);

    // ── Material slot ────────────────────────────────────────────────────────
    Mesh->GetStaticMaterials().Add(FStaticMaterial());

    // ── Simple box collision matching the extents ────────────────────────────
    Mesh->CreateBodySetup();
    UBodySetup* BodySetup = Mesh->GetBodySetup();
    if (BodySetup)
    {
        BodySetup->Modify();
        BodySetup->RemoveSimpleCollision();

        FKBoxElem BoxElem;
        BoxElem.Center   = FVector::ZeroVector;
        BoxElem.Rotation = FRotator::ZeroRotator;
        BoxElem.X        = X * 2.f;
        BoxElem.Y        = Y * 2.f;
        BoxElem.Z        = Z * 2.f;
        BodySetup->AggGeom.BoxElems.Add(BoxElem);
        BodySetup->InvalidatePhysicsData();
        BodySetup->CreatePhysicsMeshes();
    }

    // ── Build mesh (generates render data) ──────────────────────────────────
    Mesh->Build(/*bSilent=*/true);
    Mesh->PostEditChange();

    // ── Register + save ──────────────────────────────────────────────────────
    FAssetRegistryModule::AssetCreated(Mesh);
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    // ── Response ─────────────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    const int32 NumLODs = Mesh->GetNumLODs();
    Meta->SetNumberField(TEXT("trianglesCount"), (NumLODs > 0) ? Mesh->GetNumTriangles(0) : 12);
    Meta->SetNumberField(TEXT("verticesCount"),  (NumLODs > 0) ? Mesh->GetNumVertices(0)  : 8);
    Meta->SetNumberField(TEXT("halfExtentX"), static_cast<double>(X));
    Meta->SetNumberField(TEXT("halfExtentY"), static_cast<double>(Y));
    Meta->SetNumberField(TEXT("halfExtentZ"), static_cast<double>(Z));
    Meta->SetStringField(TEXT("collision"), TEXT("Simple"));

    TArray<TSharedPtr<FJsonValue>> SlotArr;
    for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
    {
        SlotArr.Add(MakeShared<FJsonValueString>(M.MaterialSlotName.ToString()));
    }
    Meta->SetArrayField(TEXT("materialSlots"), SlotArr);

    const FString StatusStr = bReplaceExisting ? TEXT("overwritten") : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(StatusStr, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_nanite_enabled
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMeshCommands::HandleSetNaniteEnabled(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    // ── Optional: enabled flag (default true) ────────────────────────────────
    bool bEnabled = true;
    Params->TryGetBoolField(TEXT("enabled"), bEnabled);

    // ── Load the mesh ────────────────────────────────────────────────────────
    UStaticMesh* Mesh = Cast<UStaticMesh>(FAssetCommonUtils::LoadAssetObject(AssetPath));
    if (!Mesh)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MESH_NOT_FOUND"),
            FString::Printf(TEXT("No UStaticMesh found at '%s'"), *AssetPath),
            Details);
    }

    // ── Toggle Nanite ────────────────────────────────────────────────────────
    Mesh->Modify();
    FMeshNaniteSettings NaniteSettings = Mesh->GetNaniteSettings();
    NaniteSettings.bEnabled = bEnabled;
    Mesh->SetNaniteSettings(NaniteSettings);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    // ── Meta response ────────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetBoolField(TEXT("naniteEnabled"), bEnabled);
    const int32 NumLODs = Mesh->GetNumLODs();
    Meta->SetNumberField(TEXT("trianglesCount"), (NumLODs > 0) ? Mesh->GetNumTriangles(0) : 0);

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// import_skeletal_mesh
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMeshCommands::HandleImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString SourcePath;
    if (!Params->TryGetStringField(TEXT("sourcePath"), SourcePath) || SourcePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_SOURCE_PATH"),
            TEXT("Required param 'sourcePath' is missing or empty"));
    }

    if (!FPaths::FileExists(SourcePath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("SOURCE_FILE_NOT_FOUND"),
            FString::Printf(TEXT("No file at source path '%s'"), *SourcePath),
            Details);
    }

    const FString Ext = FPaths::GetExtension(SourcePath, /*bIncludeDot=*/false).ToLower();
    if (Ext != TEXT("fbx"))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        Details->SetStringField(TEXT("extension"), Ext);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("UNSUPPORTED_MESH_FORMAT"),
            FString::Printf(TEXT("import_skeletal_mesh expects .fbx; got '.%s'"), *Ext),
            Details);
    }

    // ── Optional params ──────────────────────────────────────────────────────
    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    // ── Idempotency ──────────────────────────────────────────────────────────
    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));
    const bool bUpdate    = (Decision.Action == TEXT("update"));
    const bool bReplaceExisting = bOverwrite || bUpdate;

    // ── Split asset path ─────────────────────────────────────────────────────
    FString PackagePath, AssetName;
    if (!FAssetCommonUtils::SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split asset path '%s' into package + name"), *AssetPath),
            Details);
    }

    // ── Disable Interchange for FBX (same fix as import_static_mesh) ─────────
    // In UE 5.8, Interchange re-enters the GameThread TaskGraph → RecursionGuard
    // assertion in TaskGraph.cpp:705. Forcing CVar=0 keeps the classic UFbxFactory
    // pipeline (fully synchronous, no TaskGraph re-entry). RAII guard restores it
    // in its destructor on every path (early return / exception included) — see
    // FScopedInterchangeFbxImportDisable in AssetCommonUtils.h.
    FScopedInterchangeFbxImportDisable InterchangeGuard;

    // ── Import via UFbxFactory (auto-detects skeletal from armature) ──────────
    // We let UFbxFactory auto-detect the mesh type from the FBX content.
    // An FBX with a skeleton/armature will be imported as USkeletalMesh by
    // the legacy factory without explicitly setting MeshTypeToImport, which
    // avoids a dependency on UFbxImportUI (an Editor-only internal header).
    UFbxFactory* FbxFactory = NewObject<UFbxFactory>();

    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->DestinationPath  = PackagePath;
    ImportData->bReplaceExisting = bReplaceExisting;
    ImportData->bSkipReadOnly    = false;
    ImportData->Filenames.Add(SourcePath);
    ImportData->Factory = FbxFactory;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);

    if (Imported.Num() == 0 || !Imported[0])
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        Details->SetStringField(TEXT("destPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("SKELETAL_MESH_IMPORT_FAILED"),
            TEXT("AssetTools::ImportAssetsAutomated returned no asset"),
            Details);
    }

    // ── Find USkeletalMesh among imported objects ─────────────────────────────
    USkeletalMesh* SkelMesh = nullptr;
    for (UObject* Obj : Imported)
    {
        if (USkeletalMesh* Candidate = Cast<USkeletalMesh>(Obj))
        {
            SkelMesh = Candidate;
            break;
        }
    }
    if (!SkelMesh)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("SKELETAL_MESH_IMPORT_NO_SKEL_MESH"),
            FString::Printf(
                TEXT("Import of '%s' produced no USkeletalMesh — FBX may lack an armature/skeleton"),
                *SourcePath));
    }

    // ── Rename to match requested AssetName ──────────────────────────────────
    if (SkelMesh->GetName() != AssetName)
    {
        const FString NewObjPath = PackagePath / AssetName;
        if (bReplaceExisting && UEditorAssetLibrary::DoesAssetExist(NewObjPath))
        {
            UEditorAssetLibrary::DeleteAsset(NewObjPath);
        }
        if (!UEditorAssetLibrary::RenameAsset(SkelMesh->GetPathName(), NewObjPath))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("from"), SkelMesh->GetPathName());
            Details->SetStringField(TEXT("to"), NewObjPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("SKELETAL_MESH_RENAME_FAILED"),
                TEXT("Imported skeletal mesh could not be renamed to match assetPath"),
                Details);
        }
        SkelMesh = Cast<USkeletalMesh>(FAssetCommonUtils::LoadAssetObject(AssetPath));
        if (!SkelMesh)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("SKELETAL_MESH_RELOAD_FAILED"),
                FString::Printf(TEXT("Could not reload renamed skeletal mesh at '%s'"), *AssetPath));
        }
    }

    // ── Finalize + save ───────────────────────────────────────────────────────
    SkelMesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    // ── Meta response ─────────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();

    // Rendering data is not streamed in at import time, so vertex count
    // is not available here. Return 0; caller can query after the mesh loads.
    Meta->SetNumberField(TEXT("verticesCount"), 0);

    const int32 BoneCount = SkelMesh->GetRefSkeleton().GetNum();
    Meta->SetNumberField(TEXT("boneCount"), BoneCount);

    TArray<TSharedPtr<FJsonValue>> MatSlotArr;
    for (const FSkeletalMaterial& M : SkelMesh->GetMaterials())
    {
        MatSlotArr.Add(MakeShared<FJsonValueString>(M.MaterialSlotName.ToString()));
    }
    Meta->SetArrayField(TEXT("materialSlots"), MatSlotArr);
    Meta->SetStringField(TEXT("sourcePath"), SourcePath);

    const FString Status = bReplaceExisting
        ? (bUpdate ? TEXT("updated") : TEXT("overwritten"))
        : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// mesh_get_stats (v3.14.0) — read-only аудит габаритов и качества
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMeshCommands::HandleMeshGetStats(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    // Размер тайла поля — база для пересчёта габаритов в "тайлы". Юнит шире
    // одного тайла почти всегда означает ошибку масштаба при импорте.
    double TileSize = 100.0;
    Params->TryGetNumberField(TEXT("tileSize"), TileSize);
    if (TileSize <= 0.0)
    {
        TileSize = 100.0;
    }

    UStaticMesh* Mesh = Cast<UStaticMesh>(FAssetCommonUtils::LoadAssetObject(AssetPath));
    if (!Mesh)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MESH_NOT_FOUND"),
            FString::Printf(TEXT("No UStaticMesh found at '%s'"), *AssetPath),
            Details);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();

    // ── Габариты ─────────────────────────────────────────────────────────────
    const FBox Bounds = Mesh->GetBoundingBox();
    const FVector Size = Bounds.IsValid ? Bounds.GetSize() : FVector::ZeroVector;

    TArray<TSharedPtr<FJsonValue>> SizeArr;
    SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
    SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
    SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Z));
    Meta->SetArrayField(TEXT("sizeUU"), SizeArr);

    TArray<TSharedPtr<FJsonValue>> TileArr;
    TileArr.Add(MakeShared<FJsonValueNumber>(Size.X / TileSize));
    TileArr.Add(MakeShared<FJsonValueNumber>(Size.Y / TileSize));
    TileArr.Add(MakeShared<FJsonValueNumber>(Size.Z / TileSize));
    Meta->SetArrayField(TEXT("sizeInTiles"), TileArr);
    Meta->SetNumberField(TEXT("tileSize"), TileSize);

    Meta->SetNumberField(TEXT("longestAxisUU"), FMath::Max3(Size.X, Size.Y, Size.Z));

    // ── Геометрия ────────────────────────────────────────────────────────────
    const int32 NumLODs = Mesh->GetNumLODs();
    Meta->SetNumberField(TEXT("lodCount"), NumLODs);
    Meta->SetNumberField(TEXT("trianglesLOD0"), (NumLODs > 0) ? Mesh->GetNumTriangles(0) : 0);
    Meta->SetNumberField(TEXT("verticesLOD0"), (NumLODs > 0) ? Mesh->GetNumVertices(0) : 0);
    Meta->SetBoolField(TEXT("naniteEnabled"), Mesh->GetNaniteSettings().bEnabled);

    // ── Материалы и текстуры ────────────────────────────────────────────────
    // Планка стайл-гайда: текстуры юнитов >= 2K, у техники 4K. Считаем
    // максимальное разрешение среди всех использованных текстур.
    TArray<TSharedPtr<FJsonValue>> MatArr;
    int32 MaxTextureDim = 0;
    int32 TextureCount = 0;

    for (const FStaticMaterial& StaticMat : Mesh->GetStaticMaterials())
    {
        TSharedPtr<FJsonObject> MatObj = MakeShared<FJsonObject>();
        MatObj->SetStringField(TEXT("slot"), StaticMat.MaterialSlotName.ToString());

        UMaterialInterface* MatIface = StaticMat.MaterialInterface;
        if (!MatIface)
        {
            MatObj->SetStringField(TEXT("material"), TEXT(""));
            MatArr.Add(MakeShared<FJsonValueObject>(MatObj));
            continue;
        }

        MatObj->SetStringField(TEXT("material"), MatIface->GetPathName());

        TArray<UTexture*> UsedTextures;
        // UE 5.8: перегрузка с (QualityLevel, bAllQualityLevels, FeatureLevel,
        // bAllFeatureLevels) помечена final/deprecated. Без аргументов —
        // поиск по всем quality level и shader platform, что нам и нужно.
        MatIface->GetUsedTextures(UsedTextures);

        int32 SlotMaxDim = 0;
        for (UTexture* Tex : UsedTextures)
        {
            UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
            if (!Tex2D)
            {
                continue;
            }
            ++TextureCount;
            const int32 Dim = FMath::Max(static_cast<int32>(Tex2D->GetSizeX()), static_cast<int32>(Tex2D->GetSizeY()));
            SlotMaxDim = FMath::Max(SlotMaxDim, Dim);
        }
        MatObj->SetNumberField(TEXT("maxTextureDim"), SlotMaxDim);
        MaxTextureDim = FMath::Max(MaxTextureDim, SlotMaxDim);

        MatArr.Add(MakeShared<FJsonValueObject>(MatObj));
    }

    Meta->SetArrayField(TEXT("materials"), MatArr);
    Meta->SetNumberField(TEXT("maxTextureDim"), MaxTextureDim);
    Meta->SetNumberField(TEXT("textureCount"), TextureCount);

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("ok"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// mesh_set_material (v3.16.0) — назначение материала на слот UStaticMesh-АССЕТА
// ─────────────────────────────────────────────────────────────────────────────
//
// apply_material_to_actor меняет материал у актора НА УРОВНЕ, но юниты WarCard
// спавнятся в рантайме, поэтому правку надо вносить в сам ассет — иначе она
// не доживёт до игры.

TSharedPtr<FJsonObject> FMeshCommands::HandleMeshSetMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_MATERIAL"),
            TEXT("Required param 'materialPath' is missing or empty"), nullptr);
    }

    UStaticMesh* Mesh = Cast<UStaticMesh>(FAssetCommonUtils::LoadAssetObject(AssetPath));
    if (!Mesh)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MESH_NOT_FOUND"),
            FString::Printf(TEXT("No UStaticMesh found at '%s'"), *AssetPath), Details);
    }

    UMaterialInterface* Material = Cast<UMaterialInterface>(FAssetCommonUtils::LoadAssetObject(MaterialPath));
    if (!Material)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("materialPath"), MaterialPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("No UMaterialInterface found at '%s'"), *MaterialPath), Details);
    }

    // slot < 0 (по умолчанию) — назначить во ВСЕ слоты. У AI-сгенерированных
    // мешей слот обычно один, но имя его непредсказуемо (tripo_mat_XXXX),
    // поэтому адресация по индексу надёжнее, чем по имени.
    int32 Slot = -1;
    Params->TryGetNumberField(TEXT("slot"), Slot);

    TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();
    if (StaticMaterials.Num() == 0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NO_MATERIAL_SLOTS"),
            FString::Printf(TEXT("Mesh '%s' has no material slots"), *AssetPath), nullptr);
    }

    if (Slot >= StaticMaterials.Num())
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("slotCount"), StaticMaterials.Num());
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SLOT_OUT_OF_RANGE"),
            FString::Printf(TEXT("Slot %d is out of range (mesh has %d)"), Slot, StaticMaterials.Num()),
            Details);
    }

    Mesh->Modify();

    TArray<TSharedPtr<FJsonValue>> Assigned;
    for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
    {
        if (Slot >= 0 && Index != Slot)
        {
            continue;
        }
        StaticMaterials[Index].MaterialInterface = Material;
        Assigned.Add(MakeShared<FJsonValueString>(StaticMaterials[Index].MaterialSlotName.ToString()));
    }

    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("materialPath"), MaterialPath);
    Meta->SetArrayField(TEXT("assignedSlots"), Assigned);
    Meta->SetNumberField(TEXT("slotCount"), StaticMaterials.Num());
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// mesh_set_build_scale (v3.17.0) — исправление масштаба В САМОМ АССЕТЕ
// ─────────────────────────────────────────────────────────────────────────────


TSharedPtr<FJsonObject> FMeshCommands::HandleMeshSetBuildScale(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    UStaticMesh* Mesh = Cast<UStaticMesh>(FAssetCommonUtils::LoadAssetObject(AssetPath));
    if (!Mesh)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MESH_NOT_FOUND"),
            FString::Printf(TEXT("No UStaticMesh found at '%s'"), *AssetPath), Details);
    }

    if (Mesh->GetNumSourceModels() == 0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NO_SOURCE_MODEL"),
            FString::Printf(TEXT("Mesh '%s' has no source model"), *AssetPath), nullptr);
    }

    // ── Габариты ИСХОДНОЙ геометрии ─────────────────────────────────────────
    //
    // Ключевой момент этой команды. Считать масштаб от GetBoundingBox() нельзя:
    // это границы СОБРАННОГО меша, то есть уже умноженные на текущий
    // BuildScale3D, и вдобавок обновляются асинхронно. Прошлая реализация
    // делала именно так и работала как множитель — при неудачной пересборке
    // множители НАКАПЛИВАЛИСЬ (наблюдался BuildScale3D = 124 117 817).
    //
    // MeshDescription хранит позиции вершин в единицах ИСТОЧНИКА и от
    // BuildScale3D не зависит вовсе. Поэтому команда идемпотентна: сколько раз
    // её ни вызови с одним targetLongestAxisUU, результат один и тот же.
    const FMeshDescription* SourceDesc = Mesh->GetMeshDescription(0);
    if (!SourceDesc)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NO_MESH_DESCRIPTION"),
            FString::Printf(TEXT("Mesh '%s' has no MeshDescription for LOD0"), *AssetPath), nullptr);
    }

    FBox SourceBox(ForceInit);
    {
        FStaticMeshConstAttributes Attributes(*SourceDesc);
        TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
        for (const FVertexID VertexID : SourceDesc->Vertices().GetElementIDs())
        {
            SourceBox += FVector(Positions[VertexID]);
        }
    }

    if (!SourceBox.IsValid)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DEGENERATE_SOURCE"),
            TEXT("Source geometry has no valid bounds"), nullptr);
    }

    const FVector SourceSize = SourceBox.GetSize();
    const double SourceLongest = FMath::Max3(SourceSize.X, SourceSize.Y, SourceSize.Z);
    if (SourceLongest <= KINDA_SMALL_NUMBER)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DEGENERATE_SOURCE"),
            TEXT("Source geometry is degenerate"), nullptr);
    }

    // ── Целевой АБСОЛЮТНЫЙ масштаб ──────────────────────────────────────────
    // Только absolute: параметра-множителя больше нет by design.
    double AbsoluteScale = 0.0;
    double TargetLongest = 0.0;
    if (Params->TryGetNumberField(TEXT("targetLongestAxisUU"), TargetLongest) && TargetLongest > 0.0)
    {
        AbsoluteScale = TargetLongest / SourceLongest;
    }
    else if (!Params->TryGetNumberField(TEXT("buildScale"), AbsoluteScale) || AbsoluteScale <= 0.0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SCALE"),
            TEXT("Provide 'targetLongestAxisUU' or absolute 'buildScale' (relative multipliers are not supported)"),
            nullptr);
    }

    // Защита от повторения прежней катастрофы: осмысленный масштаб для игровых
    // ассетов лежит далеко внутри этих границ.
    if (AbsoluteScale < 1e-4 || AbsoluteScale > 1e4)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("requestedScale"), AbsoluteScale);
        Details->SetNumberField(TEXT("sourceLongestUU"), SourceLongest);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SCALE_OUT_OF_SANE_RANGE"),
            FString::Printf(TEXT("Refusing BuildScale3D %g (sane range 1e-4..1e4)"), AbsoluteScale),
            Details);
    }

    FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(0);
    const FVector PreviousBuildScale = SourceModel.BuildSettings.BuildScale3D;

    // Масштаб ЗАПЕКАЕТСЯ В ГЕОМЕТРИЮ, а не в BuildSettings.
    //
    // Почему так: проверено на SM_t72_red — BuildScale3D = 105.55 корректно
    // сохраняется и переживает перезапуск редактора, но габариты меша остаются
    // прежними (1.9 UU). Для этих ассетов (все с включённым Nanite) движок
    // BuildScale3D к результату не применяет. Правка позиций вершин в
    // MeshDescription работает независимо от пути сборки.
    Mesh->Modify();
    Mesh->ModifyMeshDescription(0);
    if (FMeshDescription* EditDesc = Mesh->GetMeshDescription(0))
    {
        FStaticMeshAttributes EditAttributes(*EditDesc);
        TVertexAttributesRef<FVector3f> EditPositions = EditAttributes.GetVertexPositions();
        for (const FVertexID VertexID : EditDesc->Vertices().GetElementIDs())
        {
            EditPositions[VertexID] *= static_cast<float>(AbsoluteScale);
        }
    }
    else
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("MESH_DESCRIPTION_NOT_EDITABLE"),
            TEXT("Could not obtain an editable MeshDescription for LOD0"), nullptr);
    }
    Mesh->CommitMeshDescription(0);

    // Геометрия уже в нужных единицах — BuildScale3D возвращаем к 1, иначе
    // прежнее значение умножилось бы поверх запечённого масштаба.
    SourceModel.BuildSettings.BuildScale3D = FVector::OneVector;

    Mesh->Build(/*bInSilent=*/true);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    const FVector ExpectedSize = SourceSize * AbsoluteScale;

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("appliedFactor"), AbsoluteScale);
    Meta->SetStringField(TEXT("method"), TEXT("baked into MeshDescription vertices; BuildScale3D reset to 1"));
    Meta->SetNumberField(TEXT("previousBuildScale"), PreviousBuildScale.X);
    Meta->SetNumberField(TEXT("sourceLongestUU"), SourceLongest);
    Meta->SetNumberField(TEXT("expectedLongestUU"),
        FMath::Max3(ExpectedSize.X, ExpectedSize.Y, ExpectedSize.Z));

    TArray<TSharedPtr<FJsonValue>> SrcArr;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        SrcArr.Add(MakeShared<FJsonValueNumber>(SourceSize[Axis]));
    }
    Meta->SetArrayField(TEXT("sourceSizeUU"), SrcArr);

    // Границы собранного меша сразу после Build ещё устаревшие, поэтому в ответ
    // отдаётся ОЖИДАЕМЫЙ размер (source x scale). Фактический проверять
    // отдельным mesh_get_stats.
    Meta->SetStringField(TEXT("note"),
        TEXT("expectedLongestUU is source*scale; verify actual size with mesh_get_stats"));
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}
