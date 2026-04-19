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
