// Pin-level operations for Blueprint graphs (Phase 3C — v1.15.0)
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Pin-level operations on Blueprint graph nodes:
 *   - split_struct_pin      (UEdGraphSchema_K2::SplitPin)
 *   - recombine_struct_pin  (UEdGraphSchema_K2::RecombinePin)
 *   - set_pin_default_value (UEdGraphSchema_K2::TrySetDefaultValue)
 *   - get_pin_info          (read-only)
 *   - disconnect_pin        (UEdGraphSchema_K2::BreakPinLinks)
 *
 * Node lookup is by NodeGuid string (case-insensitive) across all UbergraphPages
 * + FunctionGraphs + MacroGraphs of the Blueprint.
 */
class UNREALMCP_API FPinManager
{
public:
    static TSharedPtr<FJsonObject> SplitStructPin(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RecombineStructPin(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetPinDefaultValue(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetPinInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> DisconnectPin(const TSharedPtr<FJsonObject>& Params);

private:
    /** Find node by GUID/Name across all graphs of the Blueprint. Also returns the graph the node lives in. */
    static UEdGraphNode* FindNodeAcrossGraphs(UBlueprint* Blueprint, const FString& NodeId, UEdGraph*& OutOwningGraph);

    /** Find pin by name (case-insensitive). Returns first match regardless of direction. */
    static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName);

    /** Convert a pin to a JSON descriptor (used by get_pin_info). */
    static TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin);
};
