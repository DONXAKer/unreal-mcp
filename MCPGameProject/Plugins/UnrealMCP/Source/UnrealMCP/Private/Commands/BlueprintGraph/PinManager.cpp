// Pin-level operations for Blueprint graphs (Phase 3C — v1.15.0)

#include "Commands/BlueprintGraph/PinManager.h"
#include "Commands/BlueprintGraph/PinResolver.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static UBlueprint* LoadBlueprintForPinOps(const FString& BlueprintName)
{
    UBlueprint* BP = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!BP && BlueprintName.StartsWith(TEXT("/")))
    {
        BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName));
    }
    return BP;
}

UEdGraphNode* FPinManager::FindNodeAcrossGraphs(UBlueprint* Blueprint, const FString& NodeId, UEdGraph*& OutOwningGraph)
{
    OutOwningGraph = nullptr;
    if (!Blueprint || NodeId.IsEmpty())
    {
        return nullptr;
    }

    auto SearchGraphs = [&](const TArray<UEdGraph*>& Graphs) -> UEdGraphNode*
    {
        for (UEdGraph* G : Graphs)
        {
            if (!G) continue;
            for (UEdGraphNode* N : G->Nodes)
            {
                if (!N) continue;
                if (N->NodeGuid.ToString().Equals(NodeId, ESearchCase::IgnoreCase)
                    || N->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
                {
                    OutOwningGraph = G;
                    return N;
                }
            }
        }
        return nullptr;
    };

    if (UEdGraphNode* Found = SearchGraphs(Blueprint->UbergraphPages)) return Found;
    if (UEdGraphNode* Found = SearchGraphs(Blueprint->FunctionGraphs)) return Found;
    if (UEdGraphNode* Found = SearchGraphs(Blueprint->MacroGraphs)) return Found;
    if (UEdGraphNode* Found = SearchGraphs(Blueprint->DelegateSignatureGraphs)) return Found;
    return nullptr;
}

UEdGraphPin* FPinManager::FindPin(UEdGraphNode* Node, const FString& PinName)
{
    if (!Node) return nullptr;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON conversion
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::PinToJson(UEdGraphPin* Pin)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Pin) return Obj;

    Obj->SetStringField(TEXT("name"), Pin->PinName.ToString());
    Obj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    Obj->SetStringField(TEXT("pin_category"), Pin->PinType.PinCategory.ToString());
    Obj->SetStringField(TEXT("pin_sub_category"), Pin->PinType.PinSubCategory.ToString());

    // Sub-category object: e.g. UScriptStruct* for PC_Struct pins.
    if (Pin->PinType.PinSubCategoryObject.IsValid())
    {
        Obj->SetStringField(TEXT("pin_sub_category_object"),
            Pin->PinType.PinSubCategoryObject->GetPathName());
    }

    Obj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    if (Pin->DefaultObject)
    {
        Obj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
    }
    if (!Pin->DefaultTextValue.IsEmpty())
    {
        Obj->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
    }

    Obj->SetBoolField(TEXT("is_split"), Pin->ParentPin != nullptr);
    Obj->SetBoolField(TEXT("is_orphaned"), Pin->bOrphanedPin);
    Obj->SetBoolField(TEXT("is_hidden"), Pin->bHidden);
    Obj->SetNumberField(TEXT("num_connections"), Pin->LinkedTo.Num());

    TArray<TSharedPtr<FJsonValue>> Targets;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
    {
        if (!Linked) continue;
        TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
        T->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
        if (Linked->GetOwningNode())
        {
            T->SetStringField(TEXT("node_id"), Linked->GetOwningNode()->NodeGuid.ToString());
            T->SetStringField(TEXT("node_title"),
                Linked->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        }
        Targets.Add(MakeShared<FJsonValueObject>(T));
    }
    Obj->SetArrayField(TEXT("connection_targets"), Targets);

    return Obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// split_struct_pin
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::SplitStructPin(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName, NodeId, PinName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));

    UBlueprint* Blueprint = LoadBlueprintForPinOps(BlueprintName);
    if (!Blueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

    UEdGraph* OwningGraph = nullptr;
    UEdGraphNode* Node = FindNodeAcrossGraphs(Blueprint, NodeId, OwningGraph);
    if (!Node)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));

    UEdGraphPin* Pin = FindPin(Node, PinName);
    if (!Pin)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId));

    if (Pin->SubPins.Num() > 0)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Pin '%s' is already split"), *PinName));
    }

    const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Pin->GetSchema());
    if (!K2Schema)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Pin schema is not UEdGraphSchema_K2"));

    K2Schema->SplitPin(Pin, /*bNotify=*/true);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("node_id"), NodeId);
    Result->SetStringField(TEXT("pin_name"), PinName);
    Result->SetNumberField(TEXT("num_sub_pins"), Pin->SubPins.Num());

    TArray<TSharedPtr<FJsonValue>> SubPinNames;
    for (UEdGraphPin* Sub : Pin->SubPins)
    {
        if (Sub) SubPinNames.Add(MakeShared<FJsonValueString>(Sub->PinName.ToString()));
    }
    Result->SetArrayField(TEXT("sub_pin_names"), SubPinNames);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// recombine_struct_pin
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::RecombineStructPin(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName, NodeId, PinName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));

    UBlueprint* Blueprint = LoadBlueprintForPinOps(BlueprintName);
    if (!Blueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

    UEdGraph* OwningGraph = nullptr;
    UEdGraphNode* Node = FindNodeAcrossGraphs(Blueprint, NodeId, OwningGraph);
    if (!Node)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));

    UEdGraphPin* Pin = FindPin(Node, PinName);
    if (!Pin)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId));

    // Recombine works on a parent pin OR on one of its sub-pins. We accept both.
    UEdGraphPin* TargetForRecombine = Pin;
    if (Pin->SubPins.Num() == 0 && Pin->ParentPin == nullptr)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Pin '%s' is neither split nor a sub-pin"), *PinName));
    }

    const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(TargetForRecombine->GetSchema());
    if (!K2Schema)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Pin schema is not UEdGraphSchema_K2"));

    K2Schema->RecombinePin(TargetForRecombine);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("node_id"), NodeId);
    Result->SetStringField(TEXT("pin_name"), PinName);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_pin_default_value
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::SetPinDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName, NodeId, PinName, DefaultValue;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
    if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'default_value' parameter"));

    UBlueprint* Blueprint = LoadBlueprintForPinOps(BlueprintName);
    if (!Blueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

    UEdGraph* OwningGraph = nullptr;
    UEdGraphNode* Node = FindNodeAcrossGraphs(Blueprint, NodeId, OwningGraph);
    if (!Node)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));

    // PinResolver (MCP-PLUGIN-001) — fuzzy match, sub-pins, auto-expand.
    FPinResolutionError ResolveErr;
    UEdGraphPin* Pin = FUnrealMCPPinResolver::ResolvePinAny(Node, PinName, ResolveErr);
    if (!Pin)
    {
        return FUnrealMCPPinResolver::MakeErrorResponse(
            FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId),
            ResolveErr);
    }

    // For Object pins, try to resolve the value as a UObject path.
    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
        Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
    {
        if (DefaultValue.IsEmpty() || DefaultValue.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            Pin->DefaultObject = nullptr;
            Pin->DefaultValue = TEXT("None");
        }
        else
        {
            UObject* Resolved = LoadObject<UObject>(nullptr, *DefaultValue);
            if (!Resolved)
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Could not load object for pin default: %s"), *DefaultValue));
            }
            Pin->DefaultObject = Resolved;
            Pin->DefaultValue = FString();
        }
    }
    else
    {
        const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Pin->GetSchema());
        if (K2Schema)
        {
            K2Schema->TrySetDefaultValue(*Pin, DefaultValue, /*bMarkAsModified=*/true);
        }
        else
        {
            Pin->DefaultValue = DefaultValue;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("node_id"), NodeId);
    Result->SetStringField(TEXT("pin_name"), PinName);
    Result->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    if (Pin->DefaultObject)
    {
        Result->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
    }
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// get_pin_info — read-only
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::GetPinInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName, NodeId;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));

    // Optional: pin_name → return info for a single pin. Omitted → return all pins.
    FString TargetPinName;
    const bool bSinglePin = Params->TryGetStringField(TEXT("pin_name"), TargetPinName) && !TargetPinName.IsEmpty();

    UBlueprint* Blueprint = LoadBlueprintForPinOps(BlueprintName);
    if (!Blueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

    UEdGraph* OwningGraph = nullptr;
    UEdGraphNode* Node = FindNodeAcrossGraphs(Blueprint, NodeId, OwningGraph);
    if (!Node)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("node_id"), NodeId);
    Result->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    if (OwningGraph)
    {
        Result->SetStringField(TEXT("graph"), OwningGraph->GetFName().ToString());
    }

    if (bSinglePin)
    {
        // PinResolver (MCP-PLUGIN-001).
        FPinResolutionError ResolveErr;
        UEdGraphPin* Pin = FUnrealMCPPinResolver::ResolvePinAny(Node, TargetPinName, ResolveErr);
        if (!Pin)
        {
            return FUnrealMCPPinResolver::MakeErrorResponse(
                FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *TargetPinName, *NodeId),
                ResolveErr);
        }
        Result->SetObjectField(TEXT("pin"), PinToJson(Pin));
    }
    else
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin) Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
        }
        Result->SetArrayField(TEXT("pins"), Pins);
        Result->SetNumberField(TEXT("count"), Pins.Num());
    }

    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// disconnect_pin
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinManager::DisconnectPin(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName, NodeId, PinName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    if (!Params->TryGetStringField(TEXT("node_id"), NodeId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));

    UBlueprint* Blueprint = LoadBlueprintForPinOps(BlueprintName);
    if (!Blueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

    UEdGraph* OwningGraph = nullptr;
    UEdGraphNode* Node = FindNodeAcrossGraphs(Blueprint, NodeId, OwningGraph);
    if (!Node)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeId));

    // PinResolver (MCP-PLUGIN-001).
    FPinResolutionError ResolveErr;
    UEdGraphPin* Pin = FUnrealMCPPinResolver::ResolvePinAny(Node, PinName, ResolveErr);
    if (!Pin)
    {
        return FUnrealMCPPinResolver::MakeErrorResponse(
            FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId),
            ResolveErr);
    }

    const int32 NumLinksBefore = Pin->LinkedTo.Num();

    const UEdGraphSchema_K2* K2Schema = Cast<UEdGraphSchema_K2>(Pin->GetSchema());
    if (K2Schema)
    {
        K2Schema->BreakPinLinks(*Pin, /*bSendsNodeNotifcation=*/true);
    }
    else
    {
        Pin->BreakAllPinLinks();
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("node_id"), NodeId);
    Result->SetStringField(TEXT("pin_name"), PinName);
    Result->SetNumberField(TEXT("num_links_broken"), NumLinksBefore);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}
