#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Mesh-pipeline MCP commands (MCP-CONTENT-003a).
 *
 * Primitives:
 *   - import_static_mesh : imports an on-disk FBX/OBJ file as a UStaticMesh asset,
 *                          optionally generating simple/complex collision and
 *                          applying a material-slot override map.
 *
 * Honors the unified MCP Content Pipeline response contract from
 * FAssetCommonUtils and the `ifExists` idempotency policy.
 */
class UNREALMCP_API FMeshCommands
{
public:
    FMeshCommands();

    /**
     * Routes an incoming mesh command to the appropriate handler.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * import_static_mesh — imports an FBX/OBJ from disk via AssetTools automated import.
     *
     * Params:
     *   - sourcePath         (string, required): absolute path to a .fbx or .obj file.
     *   - assetPath          (string, required): destination /Game/... path (no ext).
     *   - generateCollision  (string, optional): "None" | "Simple" | "Complex"; default "Simple".
     *   - materialOverrides  (object, optional): { slotName: "/Game/.../MI_X" } map.
     *                         Unknown slot names are recorded in meta.skippedOverrides.
     *   - ifExists           (string, optional): skip | overwrite | update | fail; default skip.
     */
    TSharedPtr<FJsonObject> HandleImportStaticMesh(const TSharedPtr<FJsonObject>& Params);
};
