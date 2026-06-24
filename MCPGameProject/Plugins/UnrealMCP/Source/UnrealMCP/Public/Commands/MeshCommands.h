#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Mesh-pipeline MCP commands (MCP-CONTENT-003a).
 *
 * Primitives:
 *   - import_static_mesh       : imports an on-disk FBX/OBJ file as a UStaticMesh asset,
 *                                optionally generating simple/complex collision and
 *                                applying a material-slot override map.
 *   - generate_box_static_mesh : programmatically creates a box UStaticMesh via
 *                                FMeshDescription API — no file import needed, avoids
 *                                Interchange GameThread deadlock present with OBJ in UE 5.7.
 *   - set_nanite_enabled       : enables or disables Nanite on an existing UStaticMesh.
 *   - import_skeletal_mesh     : imports an FBX file with armature as a USkeletalMesh
 *                                (with skeleton and animations).
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

    /**
     * generate_box_static_mesh — programmatically builds a box UStaticMesh via
     * FMeshDescription + FStaticMeshAttributes. No file I/O required; avoids
     * Interchange GameThread deadlock (UE 5.7 OBJ import crash).
     *
     * Params:
     *   - assetPath    (string, required): destination /Game/... path (no ext).
     *   - halfExtentX  (float, optional): half-size along X in UE units (cm). Default 50.
     *   - halfExtentY  (float, optional): half-size along Y in UE units (cm). Default 50.
     *   - halfExtentZ  (float, optional): half-size along Z in UE units (cm). Default 50.
     *   - ifExists     (string, optional): skip | overwrite | update | fail; default skip.
     */
    TSharedPtr<FJsonObject> HandleGenerateBoxStaticMesh(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_nanite_enabled — toggles Nanite on an existing UStaticMesh asset.
     *
     * Params:
     *   - assetPath  (string, required): /Game/... path to an existing UStaticMesh.
     *   - enabled    (bool, optional): true to enable Nanite, false to disable. Default true.
     */
    TSharedPtr<FJsonObject> HandleSetNaniteEnabled(const TSharedPtr<FJsonObject>& Params);

    /**
     * import_skeletal_mesh — imports an FBX with armature/skeleton as a USkeletalMesh.
     * Uses the same Interchange-bypass CVar as import_static_mesh to prevent
     * GameThread TaskGraph re-entry in UE 5.8.
     *
     * Params:
     *   - assetPath   (string, required): destination /Game/... path (no ext).
     *   - sourcePath  (string, required): absolute path to the .fbx file with armature.
     *   - ifExists    (string, optional): skip | overwrite | fail | update. Default skip.
     */
    TSharedPtr<FJsonObject> HandleImportSkeletalMesh(const TSharedPtr<FJsonObject>& Params);
};
