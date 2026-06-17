#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/BlueprintGraph/PinResolver.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"

TSharedPtr<FJsonObject> FBPConnector::ConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    // Extraire paramètres
    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
    FString SourcePinName = Params->GetStringField(TEXT("source_pin_name"));
    FString TargetNodeId = Params->GetStringField(TEXT("target_node_id"));
    FString TargetPinName = Params->GetStringField(TEXT("target_pin_name"));

    FString FunctionName;
    Params->TryGetStringField(TEXT("function_name"), FunctionName);

    // Charger Blueprint - handle both full paths and simple names
    UBlueprint* Blueprint = nullptr;
    FString BlueprintPath = BlueprintName;

    // If no path prefix, assume /Game/Blueprints/
    if (!BlueprintPath.StartsWith(TEXT("/")))
    {
        BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
    }

    // Add .Blueprint suffix if not present
    if (!BlueprintPath.Contains(TEXT(".")))
    {
        BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
    }

    // Try to load the Blueprint
    Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

    // If not found, try with UEditorAssetLibrary
    if (!Blueprint)
    {
        FString AssetPath = BlueprintPath;
        if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            Blueprint = Cast<UBlueprint>(Asset);
        }
    }

    if (!Blueprint)
    {
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), "Blueprint not found");
        return Result;
    }

    // Get graph
    UEdGraph* Graph = nullptr;

    if (!FunctionName.IsEmpty())
    {
        // Strategy 1: Try exact name match with GetFName()
        for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
        {
            if (FuncGraph && (FuncGraph->GetFName().ToString() == FunctionName ||
                              (FuncGraph->GetOuter() && FuncGraph->GetOuter()->GetFName().ToString() == FunctionName)))
            {
                Graph = FuncGraph;
                break;
            }
        }

        // Strategy 2: Fallback - partial match for auto-generated names
        if (!Graph)
        {
            for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
            {
                if (FuncGraph && FuncGraph->GetFName().ToString().Contains(FunctionName))
                {
                    Graph = FuncGraph;
                    break;
                }
            }
        }

        if (!Graph)
        {
            Result->SetBoolField(TEXT("success"), false);
            Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
            return Result;
        }
    }
    else
    {
        // Use event graph if no function specified
        if (Blueprint->UbergraphPages.Num() == 0)
        {
            Result->SetBoolField(TEXT("success"), false);
            Result->SetStringField(TEXT("error"), "Blueprint has no event graph");
            return Result;
        }

        Graph = Blueprint->UbergraphPages[0];
    }

    if (!Graph)
    {
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), "Graph not found");
        return Result;
    }

    // Find nodes
    UK2Node* SourceNode = FindNodeById(Graph, SourceNodeId);
    UK2Node* TargetNode = FindNodeById(Graph, TargetNodeId);

    if (!SourceNode || !TargetNode)
    {
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), "Node not found");
        return Result;
    }

    // Pins — через PinResolver (MCP-PLUGIN-001): fuzzy, sub-pins, auto-expand.
    FPinResolutionError SourceErr, TargetErr;
    UEdGraphPin* SourcePin = FUnrealMCPPinResolver::ResolvePin(SourceNode, SourcePinName, EGPD_Output, SourceErr);
    UEdGraphPin* TargetPin = FUnrealMCPPinResolver::ResolvePin(TargetNode, TargetPinName, EGPD_Input, TargetErr);

    if (!SourcePin)
    {
        return FUnrealMCPPinResolver::MakeErrorResponse(
            FString::Printf(TEXT("Source pin '%s' not found on node '%s'"), *SourcePinName, *SourceNodeId),
            SourceErr);
    }
    if (!TargetPin)
    {
        return FUnrealMCPPinResolver::MakeErrorResponse(
            FString::Printf(TEXT("Target pin '%s' not found on node '%s'"), *TargetPinName, *TargetNodeId),
            TargetErr);
    }

    // Delegate compatibility check AND connection to the K2 schema so that
    // UE5's automatic type-coercion nodes (float→int, int→float, etc.) are
    // inserted exactly as the Blueprint editor would.  The old pre-validation
    // was too strict: it rejected numeric promotions that the engine allows.
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    if (Schema)
    {
        FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
        if (Response.Response == CONNECT_RESPONSE_DISALLOW)
        {
            Result->SetBoolField(TEXT("success"), false);
            Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Pins not compatible: %s"), *Response.Message.ToString()));
            return Result;
        }
        Schema->TryCreateConnection(SourcePin, TargetPin);
    }
    else
    {
        // Fallback for non-K2 graphs: direct link without coercion nodes
        SourcePin->MakeLinkTo(TargetPin);
    }

    // Recompile
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    // Return
    Result->SetBoolField(TEXT("success"), true);

    TSharedPtr<FJsonObject> ConnectionInfo = MakeShared<FJsonObject>();
    ConnectionInfo->SetStringField(TEXT("source_node"), SourceNodeId);
    ConnectionInfo->SetStringField(TEXT("source_pin"), SourcePinName);
    ConnectionInfo->SetStringField(TEXT("target_node"), TargetNodeId);
    ConnectionInfo->SetStringField(TEXT("target_pin"), TargetPinName);
    ConnectionInfo->SetStringField(TEXT("connection_type"), SourcePin->PinType.PinCategory.ToString());

    Result->SetObjectField(TEXT("connection"), ConnectionInfo);

    return Result;
}

UK2Node* FBPConnector::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
    if (!Graph)
    {
        return nullptr;
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        // Try matching by NodeGuid first
        if (Node->NodeGuid.ToString().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            UK2Node* K2Node = Cast<UK2Node>(Node);
            return K2Node;  // Return even if nullptr (caller will handle)
        }

        // Try matching by GetName()
        if (Node->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            UK2Node* K2Node = Cast<UK2Node>(Node);
            return K2Node;  // Return even if nullptr (caller will handle)
        }
    }

    return nullptr;
}

UEdGraphPin* FBPConnector::FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString() == PinName && Pin->Direction == Direction)
        {
            return Pin;
        }
    }
    return nullptr;
}

bool FBPConnector::ArePinsCompatible(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
{
    if (SourcePin->Direction != EGPD_Output || TargetPin->Direction != EGPD_Input)
    {
        return false;
    }

    // Wildcard pins accept any type
    if (SourcePin->PinType.PinCategory == FName("wildcard") ||
        TargetPin->PinType.PinCategory == FName("wildcard"))
    {
        return true;
    }

    return SourcePin->PinType.PinCategory == TargetPin->PinType.PinCategory;
}