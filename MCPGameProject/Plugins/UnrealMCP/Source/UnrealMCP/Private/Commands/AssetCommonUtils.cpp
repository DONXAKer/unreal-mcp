#include "Commands/AssetCommonUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "EditorAssetLibrary.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Asset Registry / lookup
// ─────────────────────────────────────────────────────────────────────────────

UObject* FAssetCommonUtils::FindAssetByPath(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return nullptr;
    }
    // UEditorAssetLibrary::LoadAsset is the standard editor-side loader; it
    // returns nullptr silently when the asset does not exist.
    return UEditorAssetLibrary::LoadAsset(AssetPath);
}

bool FAssetCommonUtils::AssetExistsInRegistry(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return false;
    }

    // UEditorAssetLibrary::DoesAssetExist hits the Asset Registry without
    // forcing a load, which is cheaper than FindAssetByPath for pure checks.
    return UEditorAssetLibrary::DoesAssetExist(AssetPath);
}

FString FAssetCommonUtils::GetAssetClassName(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return FString();
    }

    // Try a registry-only lookup first (no load).
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = AssetRegistryModule.Get();

    FSoftObjectPath SoftPath(AssetPath);
    FAssetData Data = Registry.GetAssetByObjectPath(SoftPath);
    if (Data.IsValid())
    {
        // AssetClassPath.GetAssetName() returns just the class short name
        // (e.g., "Texture2D") without package qualification.
        return Data.AssetClassPath.GetAssetName().ToString();
    }

    // Fallback: load the object and report its class.
    if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath))
    {
        return Loaded->GetClass()->GetName();
    }

    return FString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Unified response builders
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAssetCommonUtils::MakeSuccessResponse(
    const FString& Status,
    const FString& AssetPath,
    const TSharedPtr<FJsonObject>& Meta)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetBoolField(TEXT("ok"), true);
    Response->SetStringField(TEXT("status"), Status);
    Response->SetStringField(TEXT("assetPath"), AssetPath);
    Response->SetObjectField(TEXT("meta"),
        Meta.IsValid() ? Meta : MakeShared<FJsonObject>());
    return Response;
}

TSharedPtr<FJsonObject> FAssetCommonUtils::MakeFailureResponse(
    const FString& Category,
    const FString& Code,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Details)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("category"), Category);
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    Error->SetObjectField(TEXT("details"),
        Details.IsValid() ? Details : MakeShared<FJsonObject>());

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetBoolField(TEXT("ok"), false);
    Response->SetObjectField(TEXT("error"), Error);
    return Response;
}

// ─────────────────────────────────────────────────────────────────────────────
// Idempotency resolver
// ─────────────────────────────────────────────────────────────────────────────

FAssetCommonUtils::FIdempotencyDecision FAssetCommonUtils::ResolveIdempotency(
    const FString& AssetPath,
    const FString& IfExists)
{
    FIdempotencyDecision Decision;
    Decision.ExistingAsset = nullptr;

    // Default policy: when unspecified/unknown, prefer safe "skip".
    FString Policy = IfExists.IsEmpty() ? TEXT("skip") : IfExists.ToLower();

    const bool bExists = AssetExistsInRegistry(AssetPath);

    if (!bExists)
    {
        Decision.Action = TEXT("create");
        return Decision;
    }

    // Asset exists — load it so callers can operate on it.
    Decision.ExistingAsset = FindAssetByPath(AssetPath);

    if (Policy == TEXT("overwrite"))
    {
        Decision.Action = TEXT("overwrite");
        return Decision;
    }
    if (Policy == TEXT("update"))
    {
        Decision.Action = TEXT("update");
        return Decision;
    }
    if (Policy == TEXT("fail"))
    {
        Decision.Action = TEXT("fail");

        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("ifExists"), TEXT("fail"));
        Decision.SkipResponse = MakeFailureResponse(
            TEXT("user"),
            TEXT("ASSET_ALREADY_EXISTS"),
            FString::Printf(TEXT("Asset already exists at '%s' and ifExists='fail'"), *AssetPath),
            Details);
        return Decision;
    }

    // Default: skip.
    Decision.Action = TEXT("skip");

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("reason"), TEXT("asset_exists"));
    Decision.SkipResponse = MakeSuccessResponse(TEXT("skipped"), AssetPath, Meta);
    return Decision;
}

// ─────────────────────────────────────────────────────────────────────────────
// Param helpers
// ─────────────────────────────────────────────────────────────────────────────

bool FAssetCommonUtils::IsValidAssetPath(const FString& AssetPath)
{
    if (AssetPath.IsEmpty()) return false;
    // Unreal convention: editable content paths start with "/Game/" (or other
    // mount points like "/Engine/", but pipeline is scoped to /Game/).
    return AssetPath.StartsWith(TEXT("/Game/"));
}

bool FAssetCommonUtils::RequireAssetPath(
    const TSharedPtr<FJsonObject>& Params,
    FString& OutAssetPath,
    TSharedPtr<FJsonObject>& OutFailureResponse)
{
    if (!Params.IsValid())
    {
        OutFailureResponse = MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_PARAMS"),
            TEXT("Request params object is missing"));
        return false;
    }

    if (!Params->TryGetStringField(TEXT("assetPath"), OutAssetPath) || OutAssetPath.IsEmpty())
    {
        OutFailureResponse = MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_ASSET_PATH"),
            TEXT("Required param 'assetPath' is missing or empty"));
        return false;
    }

    if (!IsValidAssetPath(OutAssetPath))
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), OutAssetPath);
        OutFailureResponse = MakeFailureResponse(
            TEXT("user"),
            TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Asset path '%s' is not a valid /Game/ path"), *OutAssetPath),
            Details);
        return false;
    }

    return true;
}
