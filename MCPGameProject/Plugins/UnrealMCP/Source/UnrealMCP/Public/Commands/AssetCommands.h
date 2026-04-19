#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for generic Asset-pipeline commands.
 *
 * Current primitives:
 *   - asset_exists  : registry query, returns { exists, class } in meta.
 *   - delete_asset  : deletes an asset at /Game/... path, idempotent via ifMissing.
 *
 * Future Texture / Material / Mesh / Level primitives will live alongside these
 * and reuse FAssetCommonUtils for the unified response contract.
 */
class UNREALMCP_API FAssetCommands
{
public:
    FAssetCommands();

    /**
     * Routes an incoming Asset command to the appropriate handler.
     * @param CommandType  Command name ("asset_exists" | "delete_asset").
     * @param Params       JSON parameters.
     * @return Unified response JSON (see FAssetCommonUtils).
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * asset_exists — checks Asset Registry for an asset at params.assetPath.
     * Response status is always "updated". meta.exists is bool. meta.class is
     * populated only when exists=true.
     */
    TSharedPtr<FJsonObject> HandleAssetExists(const TSharedPtr<FJsonObject>& Params);

    /**
     * delete_asset — deletes an asset, honoring params.ifMissing ("skip"|"fail").
     * On success: status="updated", meta.deleted=true.
     * On miss + ifMissing="skip": status="skipped", meta.deleted=false.
     * On miss + ifMissing="fail": failure with ASSET_NOT_FOUND.
     */
    TSharedPtr<FJsonObject> HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params);
};
