#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Niagara VFX MCP commands.
 *
 * v2 scope (plugin 3.22.0) — introspection + emitter graph authoring.
 *
 * Commands (6):
 *   - copy_niagara_system          : duplicate an existing UNiagaraSystem asset.
 *   - set_niagara_parameters       : overwrite exposed User.* parameters.
 *   - niagara_get_info             : read back emitters / modules / renderers (read-only).
 *   - niagara_add_emitter          : add an emitter asset into a system.
 *   - niagara_set_emitter_properties : local space / sim target / enabled / loop.
 *   - niagara_set_module_input     : write a module input (rapid-iteration param
 *                                    or static-switch pin).
 *
 * If the Niagara module is not available, all commands return a graceful
 * failure with code "NIAGARA_UNAVAILABLE".
 *
 * DESIGN RULE — no silent success. Every mutating handler re-reads the value it
 * just wrote (or the container it just mutated) and returns ok:false with a
 * specific `code` when the effect did not land. `niagara_get_info` exists
 * precisely so callers can verify a system is not empty before shipping it.
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

    /**
     * niagara_get_info — read-only introspection of a UNiagaraSystem.
     *
     * Params:
     *   - assetPath           (string, required)
     *   - includeModuleInputs (bool, optional, default false): also dump every
     *     rapid-iteration parameter (name/type/value) per emitter. Use this to
     *     discover the exact `moduleName` / `inputName` accepted by
     *     niagara_set_module_input.
     *
     * Returns meta:
     *   { emitterCount, isEmpty, warnings[], exposedParameters[],
     *     warmupTime, fixedBounds,
     *     emitters: [ { index, name, enabled, mode, localSpace, simTarget,
     *                   determinism, randomSeed, boundsMode, rendererCount,
     *                   renderers[], moduleCount,
     *                   modules: { emitterSpawn[], emitterUpdate[],
     *                              particleSpawn[], particleUpdate[] },
     *                   loopBehavior, loopDuration,
     *                   moduleInputs[] (only with includeModuleInputs) } ] }
     */
    TSharedPtr<FJsonObject> HandleNiagaraGetInfo(const TSharedPtr<FJsonObject>& Params);

    /**
     * niagara_add_emitter — add an emitter asset into an existing system.
     *
     * Params:
     *   - systemPath       (string, required): /Game/... UNiagaraSystem.
     *   - emitterAssetPath (string, required): /Game/... or /Niagara/... UNiagaraEmitter.
     *   - emitterName      (string, optional): rename the new handle after adding.
     *   - compile          (bool, optional, default true): RequestCompile + wait.
     *
     * Verifies the emitter handle count actually increased; returns
     * NIAGARA_ADD_EMITTER_FAILED otherwise.
     */
    TSharedPtr<FJsonObject> HandleNiagaraAddEmitter(const TSharedPtr<FJsonObject>& Params);

    /**
     * niagara_set_emitter_properties — mutate one emitter handle in a system.
     *
     * Params:
     *   - systemPath   (string, required)
     *   - emitterName  (string, required): handle display name (case-insensitive).
     *   - enabled      (bool,   optional)
     *   - localSpace   (bool,   optional)
     *   - simTarget    (string, optional): "CPU" | "GPU"
     *   - determinism  (bool,   optional)
     *   - randomSeed   (int,    optional)
     *   - boundsMode   (string, optional): "Dynamic" | "Fixed" | "ProgrammaticOnly"
     *   - fixedBounds  (array,  optional): [minX,minY,minZ,maxX,maxY,maxZ]
     *   - loopBehavior (string, optional): "Once" | "Multiple" | "Infinite"
     *                  (routed to the Emitter State module input)
     *   - loopDuration (float,  optional)
     *   - loopCount    (int,    optional)
     *   - compile      (bool,   optional, default true)
     *
     * Every requested field is verified by read-back. If ANY requested field
     * could not be applied the response is ok:false / NIAGARA_PROPERTY_NOT_APPLIED
     * with details.applied + details.unapplied so the caller never mistakes a
     * partial write for a success.
     */
    TSharedPtr<FJsonObject> HandleNiagaraSetEmitterProperties(const TSharedPtr<FJsonObject>& Params);

    /**
     * niagara_set_module_input — best-effort write of a single module input.
     *
     * Params:
     *   - systemPath  (string, required)
     *   - emitterName (string, required)
     *   - moduleName  (string, required): e.g. "SpawnRate", "Emitter State",
     *                 "Initialize Particle". Spaces and case are ignored.
     *   - inputName   (string, required): e.g. "SpawnRate", "Lifetime",
     *                 "Loop Behavior". Spaces and case are ignored.
     *   - value       (any,    required): number / bool / string (enum entry) /
     *                 [x,y] / [x,y,z] / [r,g,b,a].
     *   - compile     (bool,   optional, default true)
     *
     * Resolution order:
     *   1. Rapid-iteration parameters of the emitter's scripts
     *      ("Constants.<Emitter>.<Module>.<Input>").
     *   2. Static-switch input pins on the module's UNiagaraNodeFunctionCall.
     *
     * When neither matches, returns NIAGARA_MODULE_INPUT_NOT_FOUND with
     * details.availableModules and details.availableInputs so the caller can
     * retry with a real name instead of guessing.
     */
    TSharedPtr<FJsonObject> HandleNiagaraSetModuleInput(const TSharedPtr<FJsonObject>& Params);
};
