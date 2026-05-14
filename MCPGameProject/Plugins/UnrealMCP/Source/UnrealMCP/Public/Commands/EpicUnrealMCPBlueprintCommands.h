#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Blueprint-related MCP commands
 */
class FEpicUnrealMCPBlueprintCommands
{
public:
    	FEpicUnrealMCPBlueprintCommands();

    // Handle blueprint commands
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    // Specific blueprint command handlers (only used functions)
    TSharedPtr<FJsonObject> HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateBlueprintFromTemplate(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReparentBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMeshMaterialColor(const TSharedPtr<FJsonObject>& Params);
    
    // Material management functions
    TSharedPtr<FJsonObject> HandleGetAvailableMaterials(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyMaterialToActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyMaterialToBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetActorMaterialInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintMaterialInfo(const TSharedPtr<FJsonObject>& Params);

    // Blueprint analysis functions
    TSharedPtr<FJsonObject> HandleReadBlueprintContent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAnalyzeBlueprintGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintVariableDetails(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintFunctionDetails(const TSharedPtr<FJsonObject>& Params);

    // CDO property setter
    TSharedPtr<FJsonObject> HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params);

    // Component property setter (v1.9.0)
    TSharedPtr<FJsonObject> HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params);

    // Component lifecycle (Phase 1A — v1.11.0)
    TSharedPtr<FJsonObject> HandleDeleteComponentFromBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameComponent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListComponents(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetComponentTransform(const TSharedPtr<FJsonObject>& Params);

    // Helper: load blueprint by short name or full /Game/... path
    static UBlueprint* LoadBlueprintByName(const FString& BlueprintName);
};