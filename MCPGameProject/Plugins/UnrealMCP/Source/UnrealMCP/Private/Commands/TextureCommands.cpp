#include "Commands/TextureCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AutomatedAssetImportData.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "PixelFormat.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Enum mappers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    TextureCompressionSettings MapCompression(const FString& InName)
    {
        // Map the small, pipeline-relevant subset. Unknown names fall back to BC7.
        const FString N = InName.ToUpper();
        if (N == TEXT("BC7"))          return TC_BC7;
        if (N == TEXT("DEFAULT"))      return TC_Default;
        if (N == TEXT("NORMALMAP"))    return TC_Normalmap;
        if (N == TEXT("GRAYSCALE"))    return TC_Grayscale;
        if (N == TEXT("HDR"))          return TC_HDR;
        if (N == TEXT("USERINTERFACE2D")) return TC_EditorIcon; // UI textures
        if (N == TEXT("EDITORICON"))   return TC_EditorIcon;
        return TC_BC7;
    }

    TextureMipGenSettings MapMipGen(const FString& InName)
    {
        const FString N = InName.ToUpper();
        if (N == TEXT("FROMTEXTURE"))  return TMGS_FromTextureGroup;
        if (N == TEXT("NOMIPMAPS"))    return TMGS_NoMipmaps;
        if (N == TEXT("SHARPEN0"))     return TMGS_Sharpen0;
        if (N == TEXT("SIMPLEAVG"))    return TMGS_SimpleAverage;
        return TMGS_FromTextureGroup;
    }

    // Split "/Game/Path/Name" into ("/Game/Path", "Name"). Returns false when
    // the shape is unexpected.
    bool SplitAssetPath(const FString& InAssetPath, FString& OutPackagePath, FString& OutAssetName)
    {
        int32 LastSlash = INDEX_NONE;
        if (!InAssetPath.FindLastChar(TCHAR('/'), LastSlash) || LastSlash <= 0)
        {
            return false;
        }
        OutPackagePath = InAssetPath.Left(LastSlash);
        OutAssetName = InAssetPath.Mid(LastSlash + 1);
        return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5x7 bitmap font for placeholder labels (uppercase letters, digits, '#', ' ').
// Each glyph is 5 columns x 7 rows. Bit 4 = leftmost column. This keeps the
// plugin independent of any editor-time font system.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    struct FGlyph { TCHAR Ch; uint8 Rows[7]; };

    static const FGlyph GlyphTable[] = {
        {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
        {'#', {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}},
        {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
        {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
        {'.', {0x00,0x00,0x00,0x00,0x00,0x06,0x06}},
        {':', {0x00,0x06,0x06,0x00,0x06,0x06,0x00}},
        {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
        {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
        {'2', {0x0E,0x11,0x01,0x0E,0x10,0x10,0x1F}},
        {'3', {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}},
        {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
        {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
        {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
        {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
        {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
        {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
        {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
        {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
        {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
        {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
        {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
        {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
        {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
        {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
        {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
        {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
        {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
        {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
        {'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
        {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
        {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
        {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
        {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
        {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
        {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
        {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
        {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
        {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
        {'Y', {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}},
        {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    };

    const FGlyph* LookupGlyph(TCHAR Ch)
    {
        const TCHAR Upper = FChar::ToUpper(Ch);
        for (const FGlyph& G : GlyphTable)
        {
            if (G.Ch == Upper) return &G;
        }
        return nullptr; // unknown glyphs are rendered blank
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FTextureCommands
// ─────────────────────────────────────────────────────────────────────────────

FTextureCommands::FTextureCommands()
{
}

TSharedPtr<FJsonObject> FTextureCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("import_texture"))
    {
        return HandleImportTexture(Params);
    }
    if (CommandType == TEXT("generate_placeholder_texture"))
    {
        return HandleGeneratePlaceholderTexture(Params);
    }

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_TEXTURE_COMMAND"),
        FString::Printf(TEXT("Texture category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// import_texture
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FTextureCommands::HandleImportTexture(const TSharedPtr<FJsonObject>& Params)
{
    // Required: assetPath, sourcePath.
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

    // Validate file exists on disk (ASCII-only paths per project convention).
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

    // Optional params.
    bool bSRGB = true;
    if (Params->HasField(TEXT("sRGB")))
    {
        bSRGB = Params->GetBoolField(TEXT("sRGB"));
    }

    FString CompressionStr = TEXT("BC7");
    Params->TryGetStringField(TEXT("compression"), CompressionStr);

    FString MipGenStr = TEXT("FromTexture");
    Params->TryGetStringField(TEXT("mipGen"), MipGenStr);

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
    // "update" semantics on a texture is equivalent to overwrite (re-import).
    const bool bUpdate = (Decision.Action == TEXT("update"));
    const bool bReplaceExisting = bOverwrite || bUpdate;

    // Split asset path.
    FString PackagePath, AssetName;
    if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
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
    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->DestinationPath = PackagePath;
    ImportData->bReplaceExisting = bReplaceExisting;
    ImportData->bSkipReadOnly = false;
    ImportData->Filenames.Add(SourcePath);

    UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
    UTextureFactory::SuppressImportOverwriteDialog();
    ImportData->Factory = TextureFactory;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);
    if (Imported.Num() == 0 || !Imported[0])
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("sourcePath"), SourcePath);
        Details->SetStringField(TEXT("destPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("TEXTURE_IMPORT_FAILED"),
            TEXT("AssetTools::ImportAssetsAutomated returned no asset"),
            Details);
    }

    UTexture2D* Texture = Cast<UTexture2D>(Imported[0]);
    if (!Texture)
    {
        // Imported as something else (shouldn't happen for TextureFactory but cover it).
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("TEXTURE_IMPORT_UNEXPECTED_CLASS"),
            FString::Printf(TEXT("Imported asset '%s' is not a UTexture2D"), *AssetPath));
    }

    // Imported asset may be named after the source file; rename or relocate if
    // the user asked for a different AssetName.
    if (Texture->GetName() != AssetName)
    {
        const FString NewObjPath = PackagePath / AssetName;
        if (!UEditorAssetLibrary::RenameAsset(Texture->GetPathName(), NewObjPath))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("from"), Texture->GetPathName());
            Details->SetStringField(TEXT("to"), NewObjPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("TEXTURE_RENAME_FAILED"),
                TEXT("Imported texture could not be renamed to match assetPath"),
                Details);
        }
        Texture = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(AssetPath));
    }

    // Apply texture settings.
    if (Texture)
    {
        Texture->SRGB = bSRGB;
        Texture->CompressionSettings = MapCompression(CompressionStr);
        Texture->MipGenSettings = MapMipGen(MipGenStr);
        Texture->UpdateResource();
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("sourcePath"), SourcePath);
    Meta->SetBoolField(TEXT("sRGB"), bSRGB);
    Meta->SetStringField(TEXT("compression"), CompressionStr);
    Meta->SetStringField(TEXT("mipGen"), MipGenStr);

    const FString Status = bReplaceExisting
        ? (bUpdate ? TEXT("updated") : TEXT("overwritten"))
        : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// generate_placeholder_texture
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FTextureCommands::HandleGeneratePlaceholderTexture(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    int32 Size = 512;
    if (Params->HasField(TEXT("size")))
    {
        Size = static_cast<int32>(Params->GetIntegerField(TEXT("size")));
    }
    if (Size <= 0 || Size > 4096)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("size"), Size);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_SIZE"),
            TEXT("Param 'size' must be in [1, 4096]"),
            Details);
    }

    // Color RGBA [0..1].
    float R = 0.2f, G = 0.2f, B = 0.3f, A = 1.0f;
    if (Params->HasField(TEXT("color")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Arr = Params->GetArrayField(TEXT("color"));
        if (Arr.Num() >= 1) R = static_cast<float>(Arr[0]->AsNumber());
        if (Arr.Num() >= 2) G = static_cast<float>(Arr[1]->AsNumber());
        if (Arr.Num() >= 3) B = static_cast<float>(Arr[2]->AsNumber());
        if (Arr.Num() >= 4) A = static_cast<float>(Arr[3]->AsNumber());
    }

    FString Label;
    Params->TryGetStringField(TEXT("label"), Label);

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

    // If an asset already exists and we have to replace it, delete the old asset
    // first — simpler than reusing the package for a newly-created transient.
    if ((bOverwrite || bUpdate) && FAssetCommonUtils::AssetExistsInRegistry(AssetPath))
    {
        UEditorAssetLibrary::DeleteAsset(AssetPath);
    }

    // Split /Game/Path/Name.
    FString PackagePath, AssetName;
    if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split asset path '%s'"), *AssetPath),
            Details);
    }

    // Create package + UTexture2D.
    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("PACKAGE_CREATE_FAILED"),
            FString::Printf(TEXT("CreatePackage returned null for '%s'"), *AssetPath));
    }
    Package->FullyLoad();

    UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
    if (!Texture)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("TEXTURE_ALLOC_FAILED"),
            TEXT("NewObject<UTexture2D> returned null"));
    }

    // Build source pixel buffer in BGRA8 (the format UTexture::Source expects
    // for TSF_BGRA8). Fill with base color.
    const uint8 RByte = static_cast<uint8>(FMath::Clamp(R, 0.f, 1.f) * 255.0f);
    const uint8 GByte = static_cast<uint8>(FMath::Clamp(G, 0.f, 1.f) * 255.0f);
    const uint8 BByte = static_cast<uint8>(FMath::Clamp(B, 0.f, 1.f) * 255.0f);
    const uint8 AByte = static_cast<uint8>(FMath::Clamp(A, 0.f, 1.f) * 255.0f);

    const int32 NumPixels = Size * Size;
    TArray<uint8> Pixels;
    Pixels.SetNumUninitialized(NumPixels * 4);
    for (int32 i = 0; i < NumPixels; ++i)
    {
        Pixels[i * 4 + 0] = BByte; // B
        Pixels[i * 4 + 1] = GByte; // G
        Pixels[i * 4 + 2] = RByte; // R
        Pixels[i * 4 + 3] = AByte; // A
    }

    // Draw label in a contrasting color (white if background is dark, black otherwise).
    const float Luma = 0.299f * R + 0.587f * G + 0.114f * B;
    const uint8 TextByte = (Luma < 0.5f) ? 255 : 0;

    if (!Label.IsEmpty())
    {
        // Scale text so label height ~ Size / 8, min 2px per glyph pixel.
        const int32 GlyphPxSize = FMath::Max(2, Size / 64);
        const int32 GlyphW = 5 * GlyphPxSize;
        const int32 GlyphH = 7 * GlyphPxSize;
        const int32 SpacingPx = GlyphPxSize; // 1 glyph-pixel between chars

        const int32 OriginX = FMath::Max(4, Size / 16);
        const int32 OriginY = FMath::Max(4, Size / 16);

        int32 PenX = OriginX;
        const int32 PenY = OriginY;

        for (int32 i = 0; i < Label.Len(); ++i)
        {
            if (PenX + GlyphW > Size - OriginX) break; // stop at right margin

            const FGlyph* G_Ptr = LookupGlyph(Label[i]);
            if (G_Ptr)
            {
                for (int32 Row = 0; Row < 7; ++Row)
                {
                    const uint8 Bits = G_Ptr->Rows[Row];
                    for (int32 Col = 0; Col < 5; ++Col)
                    {
                        if ((Bits >> (4 - Col)) & 0x1)
                        {
                            // Paint a GlyphPxSize x GlyphPxSize block.
                            for (int32 dy = 0; dy < GlyphPxSize; ++dy)
                            {
                                for (int32 dx = 0; dx < GlyphPxSize; ++dx)
                                {
                                    const int32 Px = PenX + Col * GlyphPxSize + dx;
                                    const int32 Py = PenY + Row * GlyphPxSize + dy;
                                    if (Px < 0 || Py < 0 || Px >= Size || Py >= Size) continue;
                                    const int32 Idx = (Py * Size + Px) * 4;
                                    Pixels[Idx + 0] = TextByte;
                                    Pixels[Idx + 1] = TextByte;
                                    Pixels[Idx + 2] = TextByte;
                                    Pixels[Idx + 3] = 255;
                                }
                            }
                        }
                    }
                }
            }
            PenX += GlyphW + SpacingPx;
        }
    }

    // Populate texture source.
    Texture->Source.Init(Size, Size, /*NumSlices=*/1, /*NumMips=*/1, TSF_BGRA8, Pixels.GetData());
    Texture->SRGB = true;
    Texture->CompressionSettings = TC_Default; // Placeholders don't need BC7 fidelity.
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->PostEditChange();
    Texture->UpdateResource();

    // Register with Asset Registry and save.
    FAssetRegistryModule::AssetCreated(Texture);
    Texture->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);

    if (!bSaved)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("PACKAGE_SAVE_FAILED"),
            FString::Printf(TEXT("UPackage::SavePackage returned false for '%s'"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("size"), Size);
    Meta->SetStringField(TEXT("label"), Label);
    TArray<TSharedPtr<FJsonValue>> ColorArr;
    ColorArr.Add(MakeShared<FJsonValueNumber>(R));
    ColorArr.Add(MakeShared<FJsonValueNumber>(G));
    ColorArr.Add(MakeShared<FJsonValueNumber>(B));
    ColorArr.Add(MakeShared<FJsonValueNumber>(A));
    Meta->SetArrayField(TEXT("color"), ColorArr);

    const FString Status = bOverwrite ? TEXT("overwritten") : (bUpdate ? TEXT("updated") : TEXT("created"));
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}
