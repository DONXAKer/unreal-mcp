// Header for creating extra flow-control K2Nodes (Phase 2B):
//   - Delay (CallFunction to KismetSystemLibrary::Delay)
//   - MultiGate (native UK2Node_MultiGate)
//   - Gate, DoOnce, FlipFlop (UK2Node_MacroInstance из /Engine/EditorBlueprintResources/StandardMacros)

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

class UK2Node;

/**
 * Creator for additional Blueprint flow-control nodes (Phase 2B).
 *
 * Все методы возвращают UK2Node* и не трогают outer-bridge / Python tools —
 * диспетчеризация выполняется в NodeManager::AddNode через node_type.
 */
class FFlowControlExtraNodeCreator
{
public:
	/**
	 * Создаёт Delay node как UK2Node_CallFunction → UKismetSystemLibrary::Delay.
	 * Параметры node_params:
	 *   - duration (double, optional, default 0.2): значение пина Duration
	 *   - pos_x / pos_y (double, optional)
	 */
	static UK2Node* CreateDelayNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт native UK2Node_MultiGate.
	 * Параметры node_params:
	 *   - num_outputs (int, optional, default 2): число output exec-пинов (>= 2)
	 *   - is_random (bool, optional, default false): DefaultValue на пине IsRandom
	 *   - loop (bool, optional, default false): DefaultValue на пине Loop
	 *   - pos_x / pos_y
	 */
	static UK2Node* CreateMultiGateNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт K2Node_MacroInstance, ссылающийся на макрос "Gate" из StandardMacros.
	 */
	static UK2Node* CreateGateMacroNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт K2Node_MacroInstance, ссылающийся на макрос "Do Once" из StandardMacros.
	 */
	static UK2Node* CreateDoOnceMacroNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт K2Node_MacroInstance, ссылающийся на макрос "FlipFlop" из StandardMacros.
	 */
	static UK2Node* CreateFlipFlopMacroNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
};
