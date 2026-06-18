#include "Commands/LandscapeCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// Landscape core
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeEdit.h"         // ALandscapeProxy::Import
#include "LandscapeImportHelper.h" // FLandscapeImportLayerInfo

// PNG reading
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

// File helpers
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

// UMaterialInterface
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Editor save
#include "FileHelpers.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    TArray<TSharedPtr<FJsonValue>> VecToJson(double X, double Y, double Z)
    {
        TArray<TSharedPtr<FJsonValue>> A;
        A.Add(MakeShared<FJsonValueNumber>(X));
        A.Add(MakeShared<FJsonValueNumber>(Y));
        A.Add(MakeShared<FJsonValueNumber>(Z));
        return A;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FLandscapeCommands
// ─────────────────────────────────────────────────────────────────────────────

FLandscapeCommands::FLandscapeCommands()
{
}

TSharedPtr<FJsonObject> FLandscapeCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_landscape"))
        return HandleCreateLandscape(Params);

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_LANDSCAPE_COMMAND"),
        FString::Printf(TEXT("Landscape category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// create_landscape
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FLandscapeCommands::HandleCreateLandscape(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    // ── Required param: heightmap_path ────────────────────────────────────────
    FString HeightmapPath;
    if (!Params->TryGetStringField(TEXT("heightmap_path"), HeightmapPath) || HeightmapPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_HEIGHTMAP_PATH"),
            TEXT("Required param 'heightmap_path' is missing or empty"));
    }

    // Normalize path separators (Python may send forward slashes)
    HeightmapPath = HeightmapPath.Replace(TEXT("/"), TEXT("\\"));

    if (!IFileManager::Get().FileExists(*HeightmapPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("heightmap_path"), HeightmapPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("HEIGHTMAP_NOT_FOUND"),
            FString::Printf(TEXT("Heightmap file does not exist: %s"), *HeightmapPath),
            D);
    }

    // ── Optional params with defaults ─────────────────────────────────────────
    int32 SectionSize           = 63;
    int32 SectionsPerComponent  = 1;
    int32 ComponentsX           = 32;
    int32 ComponentsY           = 32;
    double ScaleX               = 100.0;
    double ScaleY               = 100.0;
    double ScaleZ               = 100.0;
    FString MaterialPath;
    FString ActorLabel          = TEXT("Landscape");

    {
        double TmpD = 0;
        int32  TmpI = 0;

        if (Params->TryGetNumberField(TEXT("section_size"), TmpD))           SectionSize           = (int32)TmpD;
        if (Params->TryGetNumberField(TEXT("sections_per_component"), TmpD)) SectionsPerComponent  = (int32)TmpD;
        if (Params->TryGetNumberField(TEXT("components_x"), TmpD))           ComponentsX           = (int32)TmpD;
        if (Params->TryGetNumberField(TEXT("components_y"), TmpD))           ComponentsY           = (int32)TmpD;
        if (Params->TryGetNumberField(TEXT("scale_x"), TmpD))                ScaleX                = TmpD;
        if (Params->TryGetNumberField(TEXT("scale_y"), TmpD))                ScaleY                = TmpD;
        if (Params->TryGetNumberField(TEXT("scale_z"), TmpD))                ScaleZ                = TmpD;
        Params->TryGetStringField(TEXT("material_path"), MaterialPath);
        Params->TryGetStringField(TEXT("actor_label"),   ActorLabel);
    }

    // ── Validate component params ─────────────────────────────────────────────
    // SectionSize must be one of: 7, 15, 31, 63, 127, 255
    const int32 ValidSizes[] = {7, 15, 31, 63, 127, 255};
    bool bValidSize = false;
    for (int32 VS : ValidSizes) { if (SectionSize == VS) { bValidSize = true; break; } }
    if (!bValidSize)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_SECTION_SIZE"),
            FString::Printf(TEXT("section_size %d is not valid. Must be one of: 7,15,31,63,127,255"), SectionSize));
    }
    if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_SECTIONS_PER_COMPONENT"),
            TEXT("sections_per_component must be 1 or 2"));
    }
    if (ComponentsX <= 0 || ComponentsY <= 0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_COMPONENT_COUNT"),
            TEXT("components_x and components_y must be positive"));
    }

    // ── Derive expected dimensions ────────────────────────────────────────────
    const int32 Width  = ComponentsX * SectionsPerComponent * SectionSize + 1;
    const int32 Height = ComponentsY * SectionsPerComponent * SectionSize + 1;

    // ── Read PNG and decode to uint16 height data ─────────────────────────────
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *HeightmapPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("heightmap_path"), HeightmapPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("HEIGHTMAP_READ_FAILED"),
            FString::Printf(TEXT("Failed to read file: %s"), *HeightmapPath),
            D);
    }

    IImageWrapperModule& ImageWrapperModule =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper =
        ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid() ||
        !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("HEIGHTMAP_DECODE_FAILED"),
            TEXT("Failed to parse PNG heightmap — is the file a valid PNG?"));
    }

    const int32 ImgWidth  = ImageWrapper->GetWidth();
    const int32 ImgHeight = ImageWrapper->GetHeight();
    if (ImgWidth != Width || ImgHeight != Height)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetNumberField(TEXT("expected_width"),  Width);
        D->SetNumberField(TEXT("expected_height"), Height);
        D->SetNumberField(TEXT("actual_width"),    ImgWidth);
        D->SetNumberField(TEXT("actual_height"),   ImgHeight);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("HEIGHTMAP_SIZE_MISMATCH"),
            FString::Printf(
                TEXT("PNG size %dx%d does not match expected %dx%d "
                     "(components_x=%d, components_y=%d, sections_per_component=%d, section_size=%d)"),
                ImgWidth, ImgHeight, Width, Height,
                ComponentsX, ComponentsY, SectionsPerComponent, SectionSize),
            D);
    }

    TArray<uint8> UncompressedData;
    if (!ImageWrapper->GetRaw(ERGBFormat::Gray, 16, UncompressedData))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("HEIGHTMAP_DECODE_FAILED"),
            TEXT("GetRaw(Gray,16) failed — PNG may not be 16-bit grayscale. "
                 "Convert to 16-bit grayscale PNG before calling create_landscape."));
    }

    const int32 ExpectedBytes = Width * Height * 2;
    if (UncompressedData.Num() != ExpectedBytes)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetNumberField(TEXT("expected_bytes"), ExpectedBytes);
        D->SetNumberField(TEXT("actual_bytes"),   UncompressedData.Num());
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"),
            TEXT("HEIGHTMAP_DATA_SIZE_MISMATCH"),
            FString::Printf(TEXT("Decoded PNG yielded %d bytes, expected %d (width=%d, height=%d, 2 bytes/px)"),
                            UncompressedData.Num(), ExpectedBytes, Width, Height),
            D);
    }

    // Convert big-endian byte pairs → native uint16
    TArray<uint16> HeightData;
    HeightData.SetNumUninitialized(Width * Height);
    for (int32 i = 0; i < Width * Height; ++i)
    {
        const uint16 Hi = static_cast<uint16>(UncompressedData[i * 2]);
        const uint16 Lo = static_cast<uint16>(UncompressedData[i * 2 + 1]);
        HeightData[i] = static_cast<uint16>((Hi << 8) | Lo);
    }

    // ── Resolve editor world ──────────────────────────────────────────────────
    if (!GEditor)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("NO_EDITOR"),
            TEXT("GEditor is null — is the plugin running in editor context?"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("NO_WORLD"),
            TEXT("Active editor world is null — open a level before calling create_landscape"));
    }

    // ── Spawn ALandscape actor ────────────────────────────────────────────────
    // UE Landscape origin = bottom-left corner; center the terrain on (0,0).
    const int32 OffsetX = -(Width  - 1) / 2;
    const int32 OffsetY = -(Height - 1) / 2;

    const FVector Location(
        static_cast<double>(OffsetX) * ScaleX,
        static_cast<double>(OffsetY) * ScaleY,
        0.0
    );
    const FVector Scale(ScaleX, ScaleY, ScaleZ);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = FName(*ActorLabel);
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

    ALandscape* Landscape = World->SpawnActor<ALandscape>(
        ALandscape::StaticClass(),
        FTransform(FRotator::ZeroRotator, Location, Scale),
        SpawnParams);

    if (!Landscape)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("LANDSCAPE_SPAWN_FAILED"),
            TEXT("World::SpawnActor<ALandscape> returned null"));
    }

    // ── Import heightmap data ─────────────────────────────────────────────────
    const FGuid LandscapeGuid = FGuid::NewGuid();

    TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
    HeightDataPerLayer.Add(LandscapeGuid, HeightData);

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerInfo;
    MaterialLayerInfo.Add(LandscapeGuid, TArray<FLandscapeImportLayerInfo>());

    Landscape->Import(
        LandscapeGuid,
        OffsetX,                         // MinX (quads)
        OffsetY,                         // MinY (quads)
        OffsetX + Width  - 1,            // MaxX (quads)
        OffsetY + Height - 1,            // MaxY (quads)
        SectionsPerComponent,            // InNumSubsections
        SectionSize,                     // InSubsectionSizeQuads
        HeightDataPerLayer,
        nullptr,                         // HeightmapFileName (optional display name)
        MaterialLayerInfo,
        ELandscapeImportAlphamapType::Additive
    );

    // ── Optional material assignment ──────────────────────────────────────────
    if (!MaterialPath.IsEmpty())
    {
        UMaterialInterface* Mat = Cast<UMaterialInterface>(
            FAssetCommonUtils::LoadAssetObject(MaterialPath));
        if (Mat)
        {
            Landscape->LandscapeMaterial = Mat;
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("create_landscape: Material not found at '%s' — skipping material assignment"),
                *MaterialPath);
        }
    }

    // ── Set actor label ───────────────────────────────────────────────────────
    Landscape->SetActorLabel(ActorLabel);

    // ── Create / refresh LandscapeInfo ───────────────────────────────────────
    Landscape->CreateLandscapeInfo();

    // ── Mark level dirty and save ─────────────────────────────────────────────
    World->MarkPackageDirty();
    const FString PackageName = FPackageName::ObjectPathToPackageName(World->GetPathName());
    UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);

    // ── Build success response ────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("actor_label"),  Landscape->GetActorLabel());
    Meta->SetNumberField(TEXT("width"),        Width);
    Meta->SetNumberField(TEXT("height"),       Height);
    Meta->SetStringField(TEXT("components"),   FString::Printf(TEXT("%dx%d"), ComponentsX, ComponentsY));
    Meta->SetNumberField(TEXT("section_size"), SectionSize);
    Meta->SetArrayField(TEXT("scale"),    VecToJson(ScaleX, ScaleY, ScaleZ));
    Meta->SetArrayField(TEXT("location"), VecToJson(Location.X, Location.Y, Location.Z));

    const FString ActorPath = World->GetPathName()
        + TEXT(":PersistentLevel.") + Landscape->GetName();
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("created"), ActorPath, Meta);
}
