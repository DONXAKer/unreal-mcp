// Manages Blueprint event node creation
#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UK2Node_Event;
class UK2Node_ComponentBoundEvent;
class UEdGraph;

/**
 * Manages Blueprint event node creation
 * F18: add_event_node - Create specialized event nodes
 *
 * This class handles creation of event nodes in Blueprint graphs,
 * supporting all UE5 native events (ReceiveBeginPlay, ReceiveTick, etc.)
 * and custom events.
 */
class UNREALMCP_API FEventManager
{
public:
	/**
	 * Add an event node to a Blueprint graph
	 * @param Params JSON parameters containing:
	 *   - blueprint_name (string): Name of the Blueprint
	 *   - event_name (string): Name of the event (e.g., "ReceiveBeginPlay", "ReceiveTick")
	 *   - pos_x (number, optional): X position in graph (default: 0.0)
	 *   - pos_y (number, optional): Y position in graph (default: 0.0)
	 * @return JSON response with node_id or error
	 */
	static TSharedPtr<FJsonObject> AddEventNode(const TSharedPtr<FJsonObject>& Params);

	/**
	 * Создать узел привязанного события компонента (K2Node_ComponentBoundEvent).
	 * Используется для привязки делегатов UMG-виджетов (например, Button.OnClicked).
	 * @param Params JSON параметры:
	 *   - blueprint_name (string): Имя Blueprint
	 *   - component_property_name (string): Имя UPROPERTY компонента (например, "ConfirmButton")
	 *   - delegate_name (string): Имя делегата на компоненте (например, "OnClicked")
	 *   - delegate_class (string): Полный путь класса-владельца делегата (например, "/Script/UMG.Button")
	 *   - pos_x (number, optional): X позиция в графе
	 *   - pos_y (number, optional): Y позиция в графе
	 * @return JSON ответ с node_id или ошибкой
	 */
	static TSharedPtr<FJsonObject> AddComponentBoundEvent(const TSharedPtr<FJsonObject>& Params);

	// ── Phase 1D (v1.12.0) — Custom events ───────────────────────────────────

	/**
	 * Create a K2Node_CustomEvent in the Blueprint's Ubergraph.
	 * Unlike AddEventNode (which overrides BlueprintImplementableEvent/builtin events from C++),
	 * this creates an entirely new BP-only event that can later be CallFunction'd or bound.
	 * @param Params JSON parameters:
	 *   - blueprint_name (string)
	 *   - event_name (string): Must be a valid identifier; must not collide with an existing custom event.
	 *   - node_position ([X, Y], optional): Graph coordinates (default [0,0]).
	 */
	static TSharedPtr<FJsonObject> AddCustomEventNode(const TSharedPtr<FJsonObject>& Params);

	/**
	 * Add an input parameter pin to an existing K2Node_CustomEvent.
	 * @param Params JSON parameters:
	 *   - blueprint_name (string)
	 *   - event_name (string): Custom event to mutate (matched by CustomFunctionName).
	 *   - parameter_name (string): New pin name.
	 *   - parameter_type (string): Type string (same vocabulary as FBPVariables::GetPinTypeFromString).
	 */
	static TSharedPtr<FJsonObject> AddCustomEventInput(const TSharedPtr<FJsonObject>& Params);

private:
	/**
	 * Create an event node in the specified graph
	 * Inspired by chongdashu's implementation
	 * @param Graph The Blueprint event graph
	 * @param EventName Name of the event function
	 * @param Position Position in the graph
	 * @return Created event node or nullptr on failure
	 */
	static UK2Node_Event* CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position);

	/**
	 * Find existing event node with the same name (to avoid duplicates)
	 * @param Graph The Blueprint event graph
	 * @param EventName Name of the event to find
	 * @return Existing event node or nullptr if not found
	 */
	static UK2Node_Event* FindExistingEventNode(UEdGraph* Graph, const FString& EventName);

	/**
	 * Load a Blueprint by name
	 * @param BlueprintName Name or path of the Blueprint
	 * @return Loaded Blueprint or nullptr
	 */
	static UBlueprint* LoadBlueprint(const FString& BlueprintName);

	// Helper functions
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const UK2Node_Event* EventNode);
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage);
};
