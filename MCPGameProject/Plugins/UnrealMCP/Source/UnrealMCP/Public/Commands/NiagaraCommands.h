#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Niagara VFX MCP commands.
 *
 * v1 scope — copy / parameterize only. Authoring from scratch is out of scope.
 *
 * Commands (2):
 *   - copy_niagara_system     : duplicate an existing UNiagaraSystem asset.
 *   - set_niagara_parameters  : overwrite exposed parameters on a UNiagaraSystem.
 *
 * If the Niagara module is not loaded at runtime, all commands return a
 * graceful failure with code "NIAGARA_UNAVAILABLE".
 *
 * All responses follow the unified MCP Content Pipeline contract defined in
 * FAssetCommonUtils (ok, status, assetPath, meta / error).
 */
class UNREALMCP_API FNiagaraCommands
{
public:
    FNiagaraCommands();

    /** Routes an incoming Niagara command to the appropriate handler. */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * copy_niagara_system — DuplicateAsset from sourcePath → destPath.
     * Params:
     *   - sourcePath (string, required): /Game/... path to an existing UNiagaraSystem.
     *   - destPath   (string, required): /Game/... destination path (no ext).
     *   - ifExists   (string, optional): "skip" (default) | "overwrite".
     */
    TSharedPtr<FJsonObject> HandleCopyNiagaraSystem(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_niagara_parameters — update exposed float/int/bool/vector parameters.
     * Params:
     *   - assetPath (string, required): /Game/... path to a UNiagaraSystem.
     *   - params    (object, required): map of paramName → scalar or [x,y,z[,w]] array.
     *               Supported types: float, int, bool, FVector (3-element array),
     *               FVector4 (4-element array).
     * Returns meta: { paramsSet:N }.
     */
    TSharedPtr<FJsonObject> HandleSetNiagaraParameters(const TSharedPtr<FJsonObject>& Params);
};
