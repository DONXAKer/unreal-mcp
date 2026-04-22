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
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h" // UEditorLoadingAndSavingUtils

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
    return AssetPath.StartsWith(TEXT("/Game/"));
}

bool FAssetCommonUtils::SplitAssetPath(const FString& InAssetPath, FString& OutPackagePath, FString& OutAssetName)
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

// ─────────────────────────────────────────────────────────────────────────────
// Level context resolver (MCP-CONTENT-003b)
// ─────────────────────────────────────────────────────────────────────────────

bool FAssetCommonUtils::ResolveLevelContext(
    const FString& MapPath,
    UWorld*& OutWorld,
    bool& bOutOpened,
    FString& OutError)
{
    OutWorld = nullptr;
    bOutOpened = false;
    OutError.Reset();

    if (!GEditor)
    {
        OutError = TEXT("GEditor is null; cannot resolve level context");
        return false;
    }

    if (MapPath.IsEmpty())
    {
        // Active-editor-world path. No load, no save.
        UWorld* Active = GEditor->GetEditorWorldContext().World();
        if (!Active)
        {
            OutError = TEXT("No active editor world");
            return false;
        }
        OutWorld = Active;
        bOutOpened = false;
        return true;
    }

    // If the requested map is already the active editor world, short-circuit.
    // Calling LoadMap on the current world triggers a "World Memory Leaks"
    // fatal in EditorServer.cpp line 2524 — the live world is still rooted
    // as the editor-world context, and LoadMap's own sanity check refuses
    // to reload the same map while its old instance is alive.
    UWorld* Active = GEditor->GetEditorWorldContext().World();
    if (Active && Active->GetOutermost() &&
        Active->GetOutermost()->GetName() == MapPath)
    {
        OutWorld = Active;
        bOutOpened = false; // Caller shouldn't auto-save as if we opened it here.
        return true;
    }

    // Force a GC pass to release any stale refs before switching the editor
    // world — guards against leaks from prior create_level / spawn work
    // that can otherwise surface as the same EditorServer.cpp:2524 fatal.
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    // Explicit map path — blocking load. LoadMap sets the loaded world as
    // the active editor world on success, so after this call GEditor's
    // editor-world context reflects the freshly loaded map.
    UWorld* Loaded = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
    if (!Loaded)
    {
        OutError = FString::Printf(TEXT("LoadMap failed for '%s'"), *MapPath);
        return false;
    }

    OutWorld = Loaded;
    bOutOpened = true;
    return true;
}
