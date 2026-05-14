// Header for creating data nodes (Variable Get/Set, MakeArray)

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

class UK2Node;

/**
 * Creator for Unreal Blueprint data management nodes
 */
class FDataNodeCreator
{
public:
	/**
	 * Creates a Variable Get node (K2Node_VariableGet)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, variable_name
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateVariableGetNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Variable Set node (K2Node_VariableSet)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, variable_name
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateVariableSetNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Creates a Make Array node (K2Node_MakeArray)
	 * @param Graph - The graph to add the node to
	 * @param Params - JSON parameters containing pos_x, pos_y, element_type
	 * @return The created node or nullptr on error
	 */
	static UK2Node* CreateMakeArrayNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт ноду Make Map (K2Node_MakeMap) — builds TMap<Key, Value>.
	 * После создания ноды задаёт типы ключа/значения через PinType output-пина
	 * (UE5 распространяет тип через PropagatePinType).
	 * @param Graph - граф для добавления ноды
	 * @param Params - JSON параметры: pos_x, pos_y, key_type, value_type
	 *                 (например "int", "string", "/Game/Foo.Foo" для структур)
	 * @return Созданная нода или nullptr при ошибке
	 */
	static UK2Node* CreateMakeMapNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт ноду Make Set (K2Node_MakeSet) — builds TSet<Element>.
	 * @param Graph - граф для добавления ноды
	 * @param Params - JSON параметры: pos_x, pos_y, element_type
	 * @return Созданная нода или nullptr при ошибке
	 */
	static UK2Node* CreateMakeSetNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
};
