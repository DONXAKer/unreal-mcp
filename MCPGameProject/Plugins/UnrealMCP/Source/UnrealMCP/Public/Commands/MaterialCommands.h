#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Material-pipeline MCP commands (MCP-CONTENT-002).
 *
 * Primitives:
 *   - create_material_instance   : creates a UMaterialInstanceConstant from a parent
 *                                  material and applies a `params` map.
 *   - set_material_instance_params: point-updates parameters on an existing MI.
 *
 * Param value-type detection is performed against the parent material's declared
 * parameter set (scalar / vector / texture). Unknown parameter names are returned
 * in meta.skippedParams (non-fatal).
 */
class UNREALMCP_API FMaterialCommands
{
public:
    FMaterialCommands();

    /**
     * Routes an incoming material command to the appropriate handler.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * create_material_instance — creates a new UMaterialInstanceConstant.
     *
     * Params:
     *   - parentMaterial (string, required): /Game/... path to a parent material or MI.
     *   - assetPath      (string, required): /Game/... destination for the new MI.
     *   - params         (object, optional): { paramName: value } applied to the MI.
     *                     Values may be:
     *                       - number            → scalar parameter.
     *                       - [r,g,b] or [r,g,b,a] → vector / linear color parameter.
     *                       - "/Game/..."       → texture parameter (loaded by path).
     *   - ifExists       (string, optional): skip | overwrite | update | fail.
     */
    TSharedPtr<FJsonObject> HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_material_instance_params — updates parameters on an existing MI.
     *
     * Params:
     *   - assetPath (string, required): existing MI path.
     *   - params    (object, required): same shape as in create_material_instance.
     *   - ifMissing (string, optional): skip | fail, default fail.
     */
    TSharedPtr<FJsonObject> HandleSetMaterialInstanceParams(const TSharedPtr<FJsonObject>& Params);
};
