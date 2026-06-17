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

    // ─── Master Material graph commands (FEAT-MAT-001 / v3.8.0) ─────────────

    /**
     * material_create — creates a new UMaterial (master material) asset.
     *
     * Params:
     *   - assetPath (string, required): /Game/... destination path.
     *   - domain    (string, optional): "Surface" (default) | "UserInterface".
     *   - ifExists  (string, optional): "skip" | "overwrite" | "fail" (default).
     */
    TSharedPtr<FJsonObject> HandleMaterialCreate(const TSharedPtr<FJsonObject>& Params);

    /**
     * material_add_node — adds an expression node to a master material graph.
     *
     * Params:
     *   - assetPath (string, required): /Game/... path to UMaterial.
     *   - nodeType  (string, required): Constant | Constant3Vector | ScalarParameter |
     *                                   VectorParameter | TextureSample |
     *                                   TextureSampleParameter2D | Lerp | Multiply |
     *                                   Add | Panner | Noise.
     *   - nodeId    (string, required): unique ID stored in Desc field.
     *   - posX      (number, optional): graph X position (default 0).
     *   - posY      (number, optional): graph Y position (default 0).
     */
    TSharedPtr<FJsonObject> HandleMaterialAddNode(const TSharedPtr<FJsonObject>& Params);

    /**
     * material_connect — connects expression output → expression input or root property.
     *
     * Params:
     *   - assetPath  (string, required): /Game/... path to UMaterial.
     *   - fromNodeId (string, required): Desc of source expression.
     *   - fromOutput (string, optional): output pin name; empty = first output.
     *   - toNodeId   (string, optional): Desc of target expression; omit → connect to root.
     *   - toInput    (string, optional): input pin name or root property
     *                                    (BaseColor|EmissiveColor|Opacity|Roughness|Metallic|Normal).
     */
    TSharedPtr<FJsonObject> HandleMaterialConnect(const TSharedPtr<FJsonObject>& Params);

    /**
     * material_set_node_param — sets parameters on a material expression node.
     *
     * Params:
     *   - assetPath (string, required): /Game/... path to UMaterial.
     *   - nodeId    (string, required): Desc of target expression.
     *   - params    (object, required): { paramName: value } — type-dependent.
     */
    TSharedPtr<FJsonObject> HandleMaterialSetNodeParam(const TSharedPtr<FJsonObject>& Params);

    /** Find a UMaterialExpression in Mat->GetExpressions() by its Desc field. */
    static UMaterialExpression* FindExprByDesc(UMaterial* Mat, const FString& Desc);
};
