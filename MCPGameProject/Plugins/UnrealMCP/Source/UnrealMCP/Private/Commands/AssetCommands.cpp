#include "Commands/AssetCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FAssetCommands::FAssetCommands()
{
}

TSharedPtr<FJsonObject> FAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("asset_exists"))
    {
        return HandleAssetExists(Params);
    }
    if (CommandType == TEXT("delete_asset"))
    {
        return HandleDeleteAsset(Params);
    }
    if (CommandType == TEXT("get_asset_references"))
    {
        return HandleGetAssetReferences(Params);
    }
    if (CommandType == TEXT("rename_asset"))
    {
        return HandleRenameAsset(Params);
    }

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_ASSET_COMMAND"),
        FString::Printf(TEXT("Asset category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// asset_exists
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAssetCommands::HandleAssetExists(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    const bool bExists = FAssetCommonUtils::AssetExistsInRegistry(AssetPath);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetBoolField(TEXT("exists"), bExists);

    if (bExists)
    {
        const FString ClassName = FAssetCommonUtils::GetAssetClassName(AssetPath);
        if (!ClassName.IsEmpty())
        {
            Meta->SetStringField(TEXT("class"), ClassName);
        }
    }

    // Query commands always report "updated" per unified pipeline contract.
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_asset
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAssetCommands::HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    // Optional ifMissing ("skip" | "fail"), default "skip".
    FString IfMissing = TEXT("skip");
    if (Params.IsValid())
    {
        FString Tmp;
        if (Params->TryGetStringField(TEXT("ifMissing"), Tmp) && !Tmp.IsEmpty())
        {
            IfMissing = Tmp.ToLower();
        }
    }
    if (IfMissing != TEXT("skip") && IfMissing != TEXT("fail"))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("ifMissing"), IfMissing);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_IF_MISSING"),
            TEXT("Param 'ifMissing' must be 'skip' or 'fail'"),
            Details);
    }

    const bool bExists = FAssetCommonUtils::AssetExistsInRegistry(AssetPath);
    if (!bExists)
    {
        if (IfMissing == TEXT("fail"))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("assetPath"), AssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("ASSET_NOT_FOUND"),
                FString::Printf(TEXT("No asset exists at '%s'"), *AssetPath),
                Details);
        }

        // ifMissing == "skip": idempotent no-op.
        TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
        Meta->SetBoolField(TEXT("deleted"), false);
        Meta->SetStringField(TEXT("reason"), TEXT("not_found"));
        return FAssetCommonUtils::MakeSuccessResponse(TEXT("skipped"), AssetPath, Meta);
    }

    // Asset exists — attempt deletion. UEditorAssetLibrary::DeleteAsset
    // handles package unload + dirty-state cleanup.
    const bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);
    if (!bDeleted)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("DELETE_FAILED"),
            FString::Printf(TEXT("UEditorAssetLibrary::DeleteAsset returned false for '%s' (asset may be referenced or locked)"), *AssetPath),
            Details);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetBoolField(TEXT("deleted"), true);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_asset_references
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAssetCommands::HandleGetAssetReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    if (!FAssetCommonUtils::AssetExistsInRegistry(AssetPath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("No asset exists at '%s'"), *AssetPath),
            Details);
    }

    // Prefer EditorAssetLibrary — same surface Content Browser uses for
    // "Reference Viewer" package list (hard + soft package deps on this asset).
    TArray<FString> Referencers = UEditorAssetLibrary::FindPackageReferencersForAsset(AssetPath, /*bLoadAssetsToConfirm=*/false);

    TArray<TSharedPtr<FJsonValue>> RefValues;
    RefValues.Reserve(Referencers.Num());
    for (const FString& Ref : Referencers)
    {
        RefValues.Add(MakeShared<FJsonValueString>(Ref));
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetArrayField(TEXT("references"), RefValues);
    Meta->SetNumberField(TEXT("count"), Referencers.Num());
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// rename_asset
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAssetCommands::HandleRenameAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString NewAssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("newAssetPath"), NewAssetPath) || NewAssetPath.IsEmpty())
    {
        // Also accept snake_case for Python callers.
        if (!Params.IsValid() || !Params->TryGetStringField(TEXT("new_asset_path"), NewAssetPath) || NewAssetPath.IsEmpty())
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("MISSING_NEW_ASSET_PATH"),
                TEXT("Param 'newAssetPath' (or 'new_asset_path') is required"));
        }
    }

    if (!FAssetCommonUtils::AssetExistsInRegistry(AssetPath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("No asset exists at '%s'"), *AssetPath),
            Details);
    }

    if (FAssetCommonUtils::AssetExistsInRegistry(NewAssetPath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("newAssetPath"), NewAssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("DESTINATION_EXISTS"),
            FString::Printf(TEXT("Destination already exists: '%s'"), *NewAssetPath),
            Details);
    }

    const bool bRenamed = UEditorAssetLibrary::RenameAsset(AssetPath, NewAssetPath);
    if (!bRenamed)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        Details->SetStringField(TEXT("newAssetPath"), NewAssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("RENAME_FAILED"),
            FString::Printf(TEXT("UEditorAssetLibrary::RenameAsset failed for '%s' → '%s'"), *AssetPath, *NewAssetPath),
            Details);
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("oldAssetPath"), AssetPath);
    Meta->SetStringField(TEXT("newAssetPath"), NewAssetPath);
    Meta->SetBoolField(TEXT("renamed"), true);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), NewAssetPath, Meta);
}
