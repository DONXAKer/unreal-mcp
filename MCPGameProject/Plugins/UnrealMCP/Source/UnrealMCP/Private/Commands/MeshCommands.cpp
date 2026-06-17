#include "Commands/MeshCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AutomatedAssetImportData.h"
#include "Factories/FbxFactory.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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

    // Build FAutomatedAssetImportData.
    //
    // For .fbx we force the legacy UFbxFactory. Without an explicit factory,
    // UE 5.7 routes FBX through the Interchange framework, whose translation
    // pipeline pumps the game thread via TaskGraph — and we're already on
    // GameThread (bridge dispatch uses AsyncTask(ENamedThreads::GameThread)),
    // so Interchange re-enters the same TaskGraph queue and hits the
    // RecursionGuard assertion in TaskGraph.cpp. Pinning UFbxFactory keeps
    // the import fully synchronous.
    //
    // For .obj we leave Factory null — 5.7 has no legacy OBJ factory, Interchange
    // is the only path, and it may have the same issue. If it crashes we'll
    // add an explicit fail or follow-up fix.
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
            const FString& SlotName = Pair.Key;
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

    // ── Build FMeshDescription for LOD 0 ────────────────────────────────────
    FMeshDescription* MeshDesc = Mesh->CreateMeshDescription(0);
    if (!MeshDesc)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MESHDESC_CREATE_FAILED"),
            TEXT("UStaticMesh::CreateMeshDescription(0) returned null"));
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

    // ── Source model build settings ──────────────────────────────────────────
    FStaticMeshSourceModel& SrcModel = Mesh->AddSourceModel();
    SrcModel.BuildSettings.bRecomputeNormals   = false;
    SrcModel.BuildSettings.bRecomputeTangents  = true;
    SrcModel.BuildSettings.bGenerateLightmapUVs = true;

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
