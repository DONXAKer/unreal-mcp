#include "Commands/LandscapeCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"            // TActorIterator
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

// Отложенное выполнение вне контекста задачи TaskGraph
#include "Containers/Ticker.h"

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

    if (CommandType == TEXT("landscape_set_properties"))
        return HandleLandscapeSetProperties(Params);

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

    // Данные уже в НАТИВНОМ порядке байт: PNG хранит 16-битные сэмплы big-endian,
    // но IImageWrapper при декодировании приводит их к порядку платформы. Прежний
    // ручной (Hi << 8) | Lo делал ВТОРОЙ swap — младший байт становился старшим,
    // и любой нефлэтовый heightmap превращался в частокол игл высотой во весь
    // диапазон. На плоских тестовых картах баг был незаметен, поэтому дожил до
    // первой карты с рельефом.
    TArray<uint16> HeightData;
    HeightData.SetNumUninitialized(Width * Height);
    FMemory::Memcpy(HeightData.GetData(), UncompressedData.GetData(), ExpectedBytes);

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
    // Центрирование делается ОДИН раз — диапазоном квадов в Import() ниже
    // (MinX/MinY = OffsetX/OffsetY, то есть -(Width-1)/2). Актор поэтому стоит в
    // начале координат.
    //
    // v3.21.0 fix: раньше тот же офсет дополнительно применялся к позиции актора,
    // и смещение срабатывало ДВАЖДЫ — ландшафт уезжал на половину своего размера
    // (для 6300 UU карта занимала [-6300..0] вместо [-3150..+3150] и не совпадала
    // с игровой сеткой). Баг не замечали, пока масштаб перетирался движком:
    // при квадах по 800 UU никто не сверял положение с сеткой.
    const int32 OffsetX = -(Width  - 1) / 2;
    const int32 OffsetY = -(Height - 1) / 2;

    const FVector Location = FVector::ZeroVector;
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
    // UE 5.8 ALandscapeProxy::Import() requires:
    //  (a) InGuid (1st arg) must be a valid FGuid — it's the landscape actor GUID
    //      and must satisfy check(InGuid.IsValid()).  After SpawnActor, the actor's
    //      own LandscapeGuid is still FGuid() (unset), so we generate+assign it.
    //  (b) HeightDataPerLayer and MaterialLayerInfo must be keyed by FGuid() (empty),
    //      NOT by the landscape GUID.  Inside Import(), line 3229:
    //          const FGuid FinalLayerGuid = FGuid();
    //          HeightData = InImportHeightData.FindChecked(FinalLayerGuid);
    //      So the data map key is always the empty GUID — it means "base/final layer".
    FGuid LandscapeGuid = FGuid::NewGuid();
    Landscape->SetLandscapeGuid(LandscapeGuid);

    TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
    HeightDataPerLayer.Add(FGuid(), HeightData);   // key = empty GUID (final layer)

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerInfo;
    MaterialLayerInfo.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());  // same

    Landscape->Import(
        LandscapeGuid,
        OffsetX,                                   // MinX (quads)
        OffsetY,                                   // MinY (quads)
        OffsetX + Width  - 1,                      // MaxX (quads)
        OffsetY + Height - 1,                      // MaxY (quads)
        SectionsPerComponent,                      // InNumSubsections
        SectionSize,                               // InSubsectionSizeQuads
        HeightDataPerLayer,
        nullptr,                                   // InHeightmapFileName (display only)
        MaterialLayerInfo,
        ELandscapeImportAlphamapType::Additive,
        TArrayView<const FLandscapeLayer>{}        // InImportLayers — UE 5.8 added param
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

    // ── Re-assert the requested scale (v3.21.0 fix) ───────────────────────────
    // Import()/CreateLandscapeInfo() overwrite the spawn transform's scale: a
    // request for 6.25 cm per quad came back as 800, and 0.75 Z came back as 192
    // (x128 on XY, x256 on Z). Location survives, so only the scale needs
    // restoring. Without this the command silently ignores scale_* and builds a
    // landscape two orders of magnitude larger than asked for.
    const FVector ActualScale = Landscape->GetActorScale3D();
    if (!ActualScale.Equals(Scale, 1e-4))
    {
        Landscape->SetActorScale3D(Scale);
        Landscape->PostEditChange();
        UE_LOG(LogTemp, Log,
            TEXT("create_landscape: scale re-asserted %s -> %s (engine overrode the spawn transform)"),
            *ActualScale.ToString(), *Scale.ToString());
    }

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

// ─────────────────────────────────────────────────────────────────────────────
// landscape_set_properties (v3.21.0)
// ─────────────────────────────────────────────────────────────────────────────
//
// Зачем отдельно от create_landscape: смена материала или включение Nanite не
// должны стоить переимпорта heightmap'а. Пересоздание ландшафта роняет уже
// расставленный по нему декор (Z берётся трейсом) и заново пишет уровень —
// в итерации лукдева это неприемлемо дорого.

TSharedPtr<FJsonObject> FLandscapeCommands::HandleLandscapeSetProperties(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    if (!GEditor)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NO_EDITOR"),
            TEXT("GEditor is null — is the plugin running in editor context?"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NO_WORLD"),
            TEXT("Active editor world is null — open a level first"));
    }

    // ── Найти целевой ландшафт ────────────────────────────────────────────────
    // Без actor_label берём единственный ландшафт уровня. Если их несколько —
    // отказываемся угадывать и перечисляем найденные метки.
    FString ActorLabel;
    Params->TryGetStringField(TEXT("actor_label"), ActorLabel);

    ALandscape* Target = nullptr;
    TArray<TSharedPtr<FJsonValue>> Found;
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        ALandscape* Candidate = *It;
        if (!Candidate)
        {
            continue;
        }
        Found.Add(MakeShared<FJsonValueString>(Candidate->GetActorLabel()));

        if (ActorLabel.IsEmpty())
        {
            // Первый найденный — кандидат; уникальность проверим после цикла.
            if (!Target)
            {
                Target = Candidate;
            }
        }
        else if (Candidate->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) ||
                 Candidate->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
        {
            Target = Candidate;
            break;
        }
    }

    if (!Target)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetArrayField(TEXT("available"), Found);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("LANDSCAPE_NOT_FOUND"),
            ActorLabel.IsEmpty()
                ? TEXT("No ALandscape actor in the current level")
                : FString::Printf(TEXT("No ALandscape labelled '%s' in the current level"), *ActorLabel),
            Details);
    }

    if (ActorLabel.IsEmpty() && Found.Num() > 1)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetArrayField(TEXT("available"), Found);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("AMBIGUOUS_LANDSCAPE"),
            TEXT("Level has more than one ALandscape — pass 'actor_label' explicitly"),
            Details);
    }

    Target->Modify();
    TArray<TSharedPtr<FJsonValue>> Changed;
    bool bMaterialChanged = false;

    // ── Материал ──────────────────────────────────────────────────────────────
    FString MaterialPath;
    if (Params->TryGetStringField(TEXT("material_path"), MaterialPath) && !MaterialPath.IsEmpty())
    {
        UMaterialInterface* Mat = Cast<UMaterialInterface>(
            FAssetCommonUtils::LoadAssetObject(MaterialPath));
        if (!Mat)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("materialPath"), MaterialPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("MATERIAL_NOT_FOUND"),
                FString::Printf(TEXT("No UMaterialInterface found at '%s'"), *MaterialPath), Details);
        }
        Target->LandscapeMaterial = Mat;
        bMaterialChanged = true;
        Changed.Add(MakeShared<FJsonValueString>(TEXT("material_path")));
    }

    // ── Nanite ────────────────────────────────────────────────────────────────
    // bEnableNanite и NaniteLODIndex объявлены protected, публичных сеттеров у
    // ALandscapeProxy нет — пишем через рефлексию: это UPROPERTY(EditAnywhere),
    // и модификатор доступа C++ на неё не распространяется.
    bool bEnableNanite = false;
    bool bNaniteTouched = false;
    if (Params->TryGetBoolField(TEXT("enable_nanite"), bEnableNanite))
    {
        if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(
                ALandscapeProxy::StaticClass(), TEXT("bEnableNanite")))
        {
            Prop->SetPropertyValue_InContainer(Target, bEnableNanite);
            bNaniteTouched = true;
            Changed.Add(MakeShared<FJsonValueString>(TEXT("enable_nanite")));
        }
        else
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"), TEXT("NANITE_PROPERTY_MISSING"),
                TEXT("ALandscapeProxy::bEnableNanite not found via reflection — engine API changed"));
        }
    }

    int32 NaniteLod = 0;
    if (Params->TryGetNumberField(TEXT("nanite_lod"), NaniteLod))
    {
        if (FIntProperty* Prop = FindFProperty<FIntProperty>(
                ALandscapeProxy::StaticClass(), TEXT("NaniteLODIndex")))
        {
            Prop->SetPropertyValue_InContainer(Target, NaniteLod);
            bNaniteTouched = true;
            Changed.Add(MakeShared<FJsonValueString>(TEXT("nanite_lod")));
        }
    }

    // ── Положение ─────────────────────────────────────────────────────────────
    // Масштаб намеренно не редактируем: он связан с разрешением heightmap'а, и
    // его правка молча рассинхронизирует террейн с игровой сеткой.
    const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
    if (Params->TryGetArrayField(TEXT("location"), LocArray) && LocArray && LocArray->Num() == 3)
    {
        const FVector NewLocation(
            (*LocArray)[0]->AsNumber(),
            (*LocArray)[1]->AsNumber(),
            (*LocArray)[2]->AsNumber());
        Target->SetActorLocation(NewLocation);
        Changed.Add(MakeShared<FJsonValueString>(TEXT("location")));
    }

    if (Changed.Num() == 0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NOTHING_TO_SET"),
            TEXT("Provide at least one of: material_path, enable_nanite, nanite_lod, location"));
    }

    Target->MarkPackageDirty();
    World->MarkPackageDirty();

    bool bSave = true;
    Params->TryGetBoolField(TEXT("save"), bSave);

    // ── Тяжёлую перестройку — на следующий тик ────────────────────────────────
    // UpdateAllComponentMaterialInstances (компиляция шейдеров) и
    // UpdateNaniteRepresentation качают TaskGraph. Вызванные прямо из обработчика
    // команды — то есть уже изнутри задачи — они роняют редактор ассертом
    // `++Queue(QueueIndex).RecursionGuard == 1` (Async/TaskGraph.cpp:705). Это тот
    // же класс краха, что у import_asset_tasks() через Interchange.
    // FTSTicker выполняет лямбду из главного цикла, вне контекста задачи.
    TWeakObjectPtr<ALandscape> WeakTarget(Target);
    TWeakObjectPtr<UWorld> WeakWorld(World);
    const bool bDoMaterial = bMaterialChanged;
    const bool bDoNanite = bNaniteTouched;

    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakTarget, WeakWorld, bDoMaterial, bDoNanite, bSave](float) -> bool
        {
            ALandscape* Landscape = WeakTarget.Get();
            if (!Landscape)
            {
                return false;   // одноразовый тикер
            }

            Landscape->PostEditChange();

            if (bDoMaterial)
            {
                // Присваивание LandscapeMaterial само по себе кадр не меняет: у
                // каждого LandscapeComponent свой производный MaterialInstance.
                Landscape->UpdateAllComponentMaterialInstances(
                    /*bInInvalidateCombinationMaterials=*/true);
            }
            if (bDoNanite)
            {
                Landscape->UpdateNaniteRepresentation(/*InTargetPlatform=*/nullptr);
            }

            if (bSave)
            {
                if (UWorld* SaveWorld = WeakWorld.Get())
                {
                    const FString PackageName =
                        FPackageName::ObjectPathToPackageName(SaveWorld->GetPathName());
                    UEditorLoadingAndSavingUtils::SaveMap(SaveWorld, PackageName);
                }
            }

            UE_LOG(LogTemp, Log, TEXT("landscape_set_properties: отложенная перестройка выполнена"));
            return false;
        }), /*InDelay=*/0.0f);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("rebuild"), TEXT("deferred_next_tick"));
    Meta->SetArrayField(TEXT("changed"), Changed);
    Meta->SetStringField(TEXT("actor_label"), Target->GetActorLabel());
    Meta->SetBoolField(TEXT("nanite_enabled"), Target->IsNaniteEnabled());
    Meta->SetStringField(TEXT("material"),
        Target->LandscapeMaterial ? Target->LandscapeMaterial->GetPathName() : TEXT(""));
    const FVector Loc = Target->GetActorLocation();
    Meta->SetArrayField(TEXT("location"), VecToJson(Loc.X, Loc.Y, Loc.Z));
    Meta->SetBoolField(TEXT("saved"), bSave);

    const FString ActorPath = World->GetPathName()
        + TEXT(":PersistentLevel.") + Target->GetName();
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), ActorPath, Meta);
}
