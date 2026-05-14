#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/BlueprintGraph/NodeManager.h"
#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/BlueprintGraph/EventManager.h"
#include "Commands/BlueprintGraph/NodeDeleter.h"
#include "Commands/BlueprintGraph/NodePropertyManager.h"
#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Commands/BlueprintGraph/Function/FunctionIO.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_Event.h"
#include "K2Node_InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"

FEpicUnrealMCPBlueprintGraphCommands::FEpicUnrealMCPBlueprintGraphCommands()
{
}

FEpicUnrealMCPBlueprintGraphCommands::~FEpicUnrealMCPBlueprintGraphCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_blueprint_node"))
    {
        return HandleAddBlueprintNode(Params);
    }
    else if (CommandType == TEXT("connect_nodes"))
    {
        return HandleConnectNodes(Params);
    }
    else if (CommandType == TEXT("create_variable"))
    {
        return HandleCreateVariable(Params);
    }
    else if (CommandType == TEXT("set_blueprint_variable_properties"))
    {
        return HandleSetVariableProperties(Params);
    }
    else if (CommandType == TEXT("add_event_node"))
    {
        return HandleAddEventNode(Params);
    }
    else if (CommandType == TEXT("add_component_bound_event"))
    {
        return FEventManager::AddComponentBoundEvent(Params);
    }
    else if (CommandType == TEXT("delete_node"))
    {
        return HandleDeleteNode(Params);
    }
    else if (CommandType == TEXT("set_node_property"))
    {
        return HandleSetNodeProperty(Params);
    }
    else if (CommandType == TEXT("create_function"))
    {
        return HandleCreateFunction(Params);
    }
    else if (CommandType == TEXT("add_function_input"))
    {
        return HandleAddFunctionInput(Params);
    }
    else if (CommandType == TEXT("add_function_output"))
    {
        return HandleAddFunctionOutput(Params);
    }
    else if (CommandType == TEXT("delete_function"))
    {
        return HandleDeleteFunction(Params);
    }
    else if (CommandType == TEXT("rename_function"))
    {
        return HandleRenameFunction(Params);
    }
    else if (CommandType == TEXT("add_input_action_node"))
    {
        return HandleAddInputActionNode(Params);
    }
    else if (CommandType == TEXT("find_blueprint_nodes"))
    {
        return HandleFindBlueprintNodes(Params);
    }
    // Phase 1B (v1.11.0) — Variable lifecycle
    else if (CommandType == TEXT("rename_blueprint_variable"))
    {
        return FBPVariables::RenameVariable(Params);
    }
    else if (CommandType == TEXT("delete_blueprint_variable"))
    {
        return FBPVariables::DeleteVariable(Params);
    }
    else if (CommandType == TEXT("set_variable_default_value"))
    {
        return FBPVariables::SetVariableDefaultValue(Params);
    }
    else if (CommandType == TEXT("list_blueprint_variables"))
    {
        return FBPVariables::ListVariables(Params);
    }
    else if (CommandType == TEXT("set_blueprint_variable_flags"))
    {
        return FBPVariables::SetVariableFlags(Params);
    }

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint graph command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeType;
    if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNode: Adding %s node to blueprint '%s'"), *NodeType, *BlueprintName);

    // Use the NodeManager to add the node
    return FBlueprintNodeManager::AddNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString SourceNodeId;
    if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
    }

    FString SourcePinName;
    if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin_name' parameter"));
    }

    FString TargetNodeId;
    if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
    }

    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodes: Connecting %s.%s to %s.%s in blueprint '%s'"),
        *SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName, *BlueprintName);

    // Use the BPConnector to connect the nodes
    return FBPConnector::ConnectNodes(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateVariable(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    FString VariableType;
    if (!Params->TryGetStringField(TEXT("variable_type"), VariableType))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_type' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleCreateVariable: Creating %s variable '%s' in blueprint '%s'"),
        *VariableType, *VariableName, *BlueprintName);

    // Use the BPVariables to create the variable
    return FBPVariables::CreateVariable(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetVariableProperties: Modifying variable '%s' in blueprint '%s'"),
        *VariableName, *BlueprintName);

    // Use the BPVariables to set the variable properties
    return FBPVariables::SetVariableProperties(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddEventNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString EventName;
    if (!Params->TryGetStringField(TEXT("event_name"), EventName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddEventNode: Adding event '%s' to blueprint '%s'"),
        *EventName, *BlueprintName);

    // Use the EventManager to add the event node
    return FEventManager::AddEventNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    UE_LOG(LogTemp, Display,
        TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteNode: Deleting node '%s' from blueprint '%s'"),
        *NodeID, *BlueprintName);

    return FNodeDeleter::DeleteNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeID;
    if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    }

    // Check if this is semantic mode (action parameter) or legacy mode (property_name)
    bool bHasAction = Params->HasField(TEXT("action"));

    if (bHasAction)
    {
        // Semantic mode - delegate directly to SetNodeProperty
        FString Action;
        Params->TryGetStringField(TEXT("action"), Action);
        UE_LOG(LogTemp, Display,
            TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty: Semantic mode - action '%s' on node '%s' in blueprint '%s'"),
            *Action, *NodeID, *BlueprintName);
    }
    else
    {
        // Legacy mode - require property_name
        FString PropertyName;
        if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
        }

        UE_LOG(LogTemp, Display,
            TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty: Legacy mode - Setting '%s' on node '%s' in blueprint '%s'"),
            *PropertyName, *NodeID, *BlueprintName);
    }

    return FNodePropertyManager::SetNodeProperty(Params);
}


TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleCreateFunction: Creating function '%s' in blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::CreateFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionInput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionInput: Adding input '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionInput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionOutput(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString ParamName;
    if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionOutput: Adding output '%s' to function '%s' in blueprint '%s'"),
        *ParamName, *FunctionName, *BlueprintName);

    return FFunctionIO::AddFunctionOutput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteFunction: Deleting function '%s' from blueprint '%s'"),
        *FunctionName, *BlueprintName);

    return FFunctionManager::DeleteFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleRenameFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString OldFunctionName;
    if (!Params->TryGetStringField(TEXT("old_function_name"), OldFunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'old_function_name' parameter"));
    }

    FString NewFunctionName;
    if (!Params->TryGetStringField(TEXT("new_function_name"), NewFunctionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_function_name' parameter"));
    }

    UE_LOG(LogTemp, Display, TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleRenameFunction: Renaming function '%s' to '%s' in blueprint '%s'"),
        *OldFunctionName, *NewFunctionName, *BlueprintName);

    return FFunctionManager::RenameFunction(Params);
}

// ---------------------------------------------------------------------------
// v1.9.0 — Add a K2Node_InputAction to the event graph
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddInputActionNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActionName;
    if (!Params->TryGetStringField(TEXT("action_name"), ActionName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));
    }

    UE_LOG(LogTemp, Display,
        TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleAddInputActionNode: Adding InputAction '%s' to blueprint '%s'"),
        *ActionName, *BlueprintName);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UEdGraph* Graph = FEpicUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!Graph)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to find or create event graph"));
    }

    // Optional position
    FVector2D Position(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
        if (Params->TryGetArrayField(TEXT("node_position"), PosArr) && PosArr && PosArr->Num() >= 2)
        {
            Position.X = (float)(*PosArr)[0]->AsNumber();
            Position.Y = (float)(*PosArr)[1]->AsNumber();
        }
    }

    UK2Node_InputAction* InputActionNode = FEpicUnrealMCPCommonUtils::CreateInputActionNode(Graph, ActionName, Position);
    if (!InputActionNode)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create InputAction node for '%s'"), *ActionName));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("node_id"), InputActionNode->NodeGuid.ToString());
    Result->SetStringField(TEXT("action_name"), ActionName);
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetNumberField(TEXT("node_pos_x"), Position.X);
    Result->SetNumberField(TEXT("node_pos_y"), Position.Y);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ---------------------------------------------------------------------------
// v1.9.0 — Find nodes in a Blueprint's UbergraphPages by optional type filters
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleFindBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeTypeFilter;
    Params->TryGetStringField(TEXT("node_type"), NodeTypeFilter); // optional, empty if missing

    FString EventTypeFilter;
    Params->TryGetStringField(TEXT("event_type"), EventTypeFilter); // optional, empty if missing

    UE_LOG(LogTemp, Display,
        TEXT("FEpicUnrealMCPBlueprintGraphCommands::HandleFindBlueprintNodes: BP='%s', node_type='%s', event_type='%s'"),
        *BlueprintName, *NodeTypeFilter, *EventTypeFilter);

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    TArray<TSharedPtr<FJsonValue>> NodesArray;

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            const FString ClassName = Node->GetClass()->GetName();

            // node_type filter: case-insensitive substring match against class name (with or without "K2Node_" prefix)
            if (!NodeTypeFilter.IsEmpty())
            {
                const bool bClassMatch = ClassName.Contains(NodeTypeFilter, ESearchCase::IgnoreCase);
                if (!bClassMatch)
                {
                    continue;
                }
            }

            // event_type filter: only meaningful for K2Node_Event — compare MemberName
            FString EventMemberName;
            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                EventMemberName = EventNode->EventReference.GetMemberName().ToString();
            }

            if (!EventTypeFilter.IsEmpty())
            {
                if (EventMemberName.IsEmpty() || !EventMemberName.Equals(EventTypeFilter, ESearchCase::IgnoreCase))
                {
                    continue;
                }
            }

            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
            NodeObj->SetStringField(TEXT("node_type"), ClassName);
            NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            NodeObj->SetNumberField(TEXT("node_pos_x"), Node->NodePosX);
            NodeObj->SetNumberField(TEXT("node_pos_y"), Node->NodePosY);
            NodeObj->SetStringField(TEXT("graph"), Graph->GetName());
            if (!EventMemberName.IsEmpty())
            {
                NodeObj->SetStringField(TEXT("event_name"), EventMemberName);
            }

            NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetArrayField(TEXT("nodes"), NodesArray);
    Result->SetNumberField(TEXT("count"), NodesArray.Num());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}
