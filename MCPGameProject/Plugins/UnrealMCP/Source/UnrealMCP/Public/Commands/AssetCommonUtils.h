#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWorld;

/**
 * Shared helpers for MCP Asset Pipeline commands (Texture / Material / Mesh / Level / ...).
 *
 * These utilities implement the unified MCP Content Pipeline response contract:
 *
 *   Success: { ok:true, status:"created|skipped|overwritten|updated",
 *              assetPath:"/Game/...", meta:{ ... } }
 *   Failure: { ok:false, error:{ category, code, message, details } }
 *
 * Status semantics:
 *   - "created"      — a new asset was created.
 *   - "skipped"      — existing asset left untouched (idempotent no-op).
 *   - "overwritten"  — existing asset replaced in place.
 *   - "updated"      — world state mutated (e.g., asset deleted, registry query).
 *
 * All helpers are static. No logging occurs on expected misses (keeps query spam down).
 */
class UNREALMCP_API FAssetCommonUtils
{
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Asset Registry / lookup
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Loads and returns the UObject at AssetPath, or nullptr if missing.
     * Does NOT log on miss. Accepts short paths (e.g. "/Game/Cards/T_Foo").
     */
    static UObject* FindAssetByPath(const FString& AssetPath);

    /**
     * Cheap registry-only check (no asset load). Returns true if an asset is
     * registered at AssetPath.
     */
    static bool AssetExistsInRegistry(const FString& AssetPath);

    /**
     * Returns the short UClass name of an asset at AssetPath (e.g. "Texture2D"),
     * or empty string if the asset is missing.
     */
    static FString GetAssetClassName(const FString& AssetPath);

    // ─────────────────────────────────────────────────────────────────────────
    // Unified response builders
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Builds a success response:
     *   { ok:true, status:<Status>, assetPath:<AssetPath>, meta:<Meta or {}> }
     */
    static TSharedPtr<FJsonObject> MakeSuccessResponse(
        const FString& Status,
        const FString& AssetPath,
        const TSharedPtr<FJsonObject>& Meta = nullptr);

    /**
     * Builds a failure response:
     *   { ok:false, error:{ category, code, message, details:<Details or {}> } }
     *
     * Category is one of: "user" | "io" | "ue_internal" | "validation" | "config".
     * Code is a stable machine-readable SCREAMING_SNAKE_CASE identifier
     * (e.g., "INVALID_ASSET_PATH", "ASSET_NOT_FOUND").
     */
    static TSharedPtr<FJsonObject> MakeFailureResponse(
        const FString& Category,
        const FString& Code,
        const FString& Message,
        const TSharedPtr<FJsonObject>& Details = nullptr);

    // ─────────────────────────────────────────────────────────────────────────
    // Idempotency resolver
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Result of ResolveIdempotency: tells the caller whether to create / skip /
     * overwrite / update / fail a pending operation based on current asset state.
     *
     *  - Action == "create"     → no asset at path; caller should create new.
     *  - Action == "overwrite"  → asset exists and IfExists=="overwrite"; caller
     *                             should proceed replacing ExistingAsset.
     *  - Action == "update"     → asset exists and IfExists=="update"; caller
     *                             should mutate ExistingAsset in place.
     *  - Action == "skip"       → caller MUST return SkipResponse as-is.
     *  - Action == "fail"       → caller MUST return SkipResponse (a pre-built
     *                             failure response) as-is.
     *
     * ExistingAsset is only populated when an asset was found at AssetPath.
     */
    struct FIdempotencyDecision
    {
        FString Action;
        UObject* ExistingAsset = nullptr;
        TSharedPtr<FJsonObject> SkipResponse; // pre-built response for skip / fail
    };

    /**
     * Inspects AssetPath and decides what action the caller should take based on
     * IfExists ("skip" | "overwrite" | "update" | "fail"). Default policy if
     * IfExists is empty or unknown is "skip".
     */
    static FIdempotencyDecision ResolveIdempotency(
        const FString& AssetPath,
        const FString& IfExists);

    // ─────────────────────────────────────────────────────────────────────────
    // Param helpers (convenience)
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Extracts a non-empty "assetPath" string param. On failure, fills
     * OutFailureResponse with a ready-to-return failure and returns false.
     */
    static bool RequireAssetPath(
        const TSharedPtr<FJsonObject>& Params,
        FString& OutAssetPath,
        TSharedPtr<FJsonObject>& OutFailureResponse);

    /**
     * Validates asset path shape ("/Game/..."). Returns true if acceptable.
     */
    static bool IsValidAssetPath(const FString& AssetPath);

    /**
     * Splits "/Game/Path/AssetName" into ("/Game/Path", "AssetName").
     * Returns false when the path shape is unexpected (no slash or empty parts).
     * Centralized to avoid ODR violations in unity builds.
     */
    static bool SplitAssetPath(const FString& InAssetPath, FString& OutPackagePath, FString& OutAssetName);

    // ─────────────────────────────────────────────────────────────────────────
    // Level context resolver (MCP-CONTENT-003b)
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Resolves the target UWorld for a level-pipeline operation.
     *
     * If MapPath is empty, the caller targets the active editor world:
     *   OutWorld      = GEditor->GetEditorWorldContext().World()
     *   bOutOpened    = false     (no load happened; caller should NOT SaveMap)
     *
     * If MapPath is non-empty, the level is loaded via
     * UEditorLoadingAndSavingUtils::LoadMap (blocking):
     *   OutWorld      = GEditor world after load (LoadMap makes it active)
     *   bOutOpened    = true      (caller is expected to SaveMap after mutation)
     *
     * On any failure (no editor, no world, LoadMap returned null), returns false
     * and writes a one-line diagnostic into OutError.
     *
     * Note: LoadMap is synchronous. There is no Interchange / TaskGraph pump
     * involved, so this is safe from the bridge's GameThread AsyncTask handler.
     */
    static bool ResolveLevelContext(
        const FString& MapPath,
        UWorld*& OutWorld,
        bool& bOutOpened,
        FString& OutError);
};
