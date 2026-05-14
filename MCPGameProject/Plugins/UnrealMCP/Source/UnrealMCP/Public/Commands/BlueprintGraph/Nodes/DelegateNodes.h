// Header for creating delegate-related K2Nodes (Add/Remove/Call/Clear).
// All nodes inherit from UK2Node_BaseMCDelegate and use FMemberReference
// to point to a UMulticastDelegateProperty on either Self or an external class.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

class UK2Node;

/**
 * Creator for Unreal Blueprint multicast-delegate nodes.
 *
 * Все методы принимают единый набор параметров:
 *   - blueprint_name (используется outer-dispatcher'ом для загрузки BP)
 *   - target_property — имя FMulticastDelegateProperty (например "OnUnitDeployed")
 *   - target_class (optional) — путь класса, на котором живёт делегат
 *                                (по умолчанию — GeneratedClass самого Blueprint'а)
 *   - pos_x, pos_y — позиция узла
 */
class FDelegateNodeCreator
{
public:
	/**
	 * Создаёт ноду Add Delegate (UK2Node_AddDelegate) — bind функцию к multicast делегату.
	 */
	static UK2Node* CreateAddDelegateNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт ноду Remove Delegate (UK2Node_RemoveDelegate) — unbind функцию.
	 */
	static UK2Node* CreateRemoveDelegateNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт ноду Call Delegate (UK2Node_CallDelegate) — broadcast делегата.
	 */
	static UK2Node* CreateCallDelegateNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);

	/**
	 * Создаёт ноду Clear Delegate (UK2Node_ClearDelegate) — отвязать всех подписчиков.
	 */
	static UK2Node* CreateClearDelegateNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
};
