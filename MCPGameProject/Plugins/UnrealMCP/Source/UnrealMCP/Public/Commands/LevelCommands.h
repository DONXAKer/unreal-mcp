#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Level-pipeline MCP commands (MCP-CONTENT-003b).
 *
 * Primitives (7):
 *   - create_level                 : create a new non-WP UWorld at /Game/... and save.
 *   - load_level                   : blocking open of a .umap in the editor.
 *   - save_level                   : save the active or named world.
 *   - spawn_actor_in_level         : spawn an actor (C++ or BP class) with transform,
 *                                    optional CDO-style property overrides, unique label.
 *   - remove_actor_from_level      : destroy actor by label (fallback: FName).
 *   - set_actor_transform_in_level : update actor transform.
 *   - list_actors_in_level         : enumerate actors, optional classFilter.
 *
 * All primitives honor the unified MCP Content Pipeline response contract from
 * FAssetCommonUtils. For level targeting all primitives use
 * FAssetCommonUtils::ResolveLevelContext — mapPath=null operates on the active
 * editor world (no save), mapPath=<path> opens + operates + saves.
 *
 * API choices (UE 5.7):
 *   - Create world: UWorldFactory with bCreateWorldPartition=false. The 5.7
 *     factory does NOT use FWorldFactoryCreationParams directly — the fields
 *     are set on the factory instance. "Empty" vs "Default" template: "Empty"
 *     produces a bare UWorld (no default actors); "Default" additionally spawns
 *     a DirectionalLight + SkyLight + PlayerStart so the level is lightable.
 *   - Load/Save level: UEditorLoadingAndSavingUtils::LoadMap / SaveMap — both
 *     fully synchronous, no Interchange / TaskGraph re-entry.
 *   - Actor class resolution:
 *       * /Script/... → LoadClass<AActor>(nullptr, *Path)
 *       * /Game/...   → LoadObject<UBlueprintGeneratedClass>(nullptr, *Path),
 *                       appending "_C" if missing.
 *   - Property writes on spawn use FProperty reflection and are intentionally
 *     narrow: string / int / float / bool / FVector only. Other property types
 *     are reported via meta.skippedProperties.
 */
class UNREALMCP_API FLevelCommands
{
public:
    FLevelCommands();

    /** Routes an incoming level command to the appropriate handler. */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * create_level — create a new (non-WP) UWorld at the given /Game/... path.
     * Params:
     *   - destMapPath (string, required): "/Game/Maps/Arena" style path (no ext).
     *   - template    (string, optional): "Empty" (default) | "Default".
     *   - ifExists    (string, optional): "skip" | "overwrite" | "fail"; default "skip".
     */
    TSharedPtr<FJsonObject> HandleCreateLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * load_level — blocking LoadMap of a /Game/... map path.
     * Params:
     *   - mapPath (string, required)
     */
    TSharedPtr<FJsonObject> HandleLoadLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * save_level — SaveMap of active world (if mapPath empty) or of the world
     * resolved from mapPath.
     * Params:
     *   - mapPath (string, optional)
     */
    TSharedPtr<FJsonObject> HandleSaveLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * spawn_actor_in_level — spawn an actor in active/target world.
     * Params:
     *   - mapPath    (string, optional): null → active level.
     *   - actorClass (string, required): "/Script/Engine.DirectionalLight" OR
     *                                    "/Game/.../BP_Foo" (appends "_C" if missing).
     *   - transform  (object, optional): { loc:[x,y,z], rot:[pitch,yaw,roll], scale:[x,y,z] }
     *   - properties (object, optional): JSON map of FProperty overrides.
     *                                    v1 supports string/int/float/bool/FVector only;
     *                                    others are skipped into meta.skippedProperties.
     *   - name       (string, optional): actor label (SetActorLabel) + Rename FName.
     *                                    If omitted, UE-default name is used.
     *   - ifExists   (string, optional): behavior when an actor with `name` label
     *                                    already exists. "skip" | "overwrite" | "fail".
     */
    TSharedPtr<FJsonObject> HandleSpawnActorInLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * remove_actor_from_level — destroy actor by label (fallback: GetName()).
     * Params:
     *   - mapPath   (string, optional)
     *   - actorName (string, required)
     */
    TSharedPtr<FJsonObject> HandleRemoveActorFromLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_actor_transform_in_level — SetActorTransform on existing actor.
     * Params:
     *   - mapPath   (string, optional)
     *   - actorName (string, required)
     *   - transform (object, required) { loc, rot, scale }  — same format as spawn.
     */
    TSharedPtr<FJsonObject> HandleSetActorTransformInLevel(const TSharedPtr<FJsonObject>& Params);

    /**
     * list_actors_in_level — enumerate actors in target world. Read-only.
     * Params:
     *   - mapPath     (string, optional)
     *   - classFilter (string, optional): full class path for IsA filtering.
     */
    TSharedPtr<FJsonObject> HandleListActorsInLevel(const TSharedPtr<FJsonObject>& Params);
};
