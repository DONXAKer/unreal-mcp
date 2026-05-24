#include "Commands/BlueprintGraph/Nodes/DelegateNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "Engine/MemberReference.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Json.h"

namespace
{
	/**
	 * Разрешает целевой UClass для делегата.
	 *   - Если в Params задан target_class — резолвим через Find/LoadObject.
	 *   - Иначе — берём GeneratedClass Blueprint'а текущего графа (Self).
	 * Возвращает nullptr если ничего не найдено.
	 */
	UClass* ResolveDelegateOwnerClass(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params, bool& bOutIsSelfContext)
	{
		bOutIsSelfContext = false;

		FString TargetClassPath;
		if (Params->TryGetStringField(TEXT("target_class"), TargetClassPath) && !TargetClassPath.IsEmpty())
		{
			// UE 5.7: FindObject<UClass>(nullptr, ...) deprecated — для full path
			// используем LoadObject, для short name — FindFirstObject.
			UClass* Found = LoadObject<UClass>(nullptr, *TargetClassPath);
			if (!Found)
			{
				int32 DotIdx;
				if (TargetClassPath.FindLastChar(TEXT('.'), DotIdx))
				{
					FString ShortName = TargetClassPath.Mid(DotIdx + 1);
					Found = FindFirstObject<UClass>(*ShortName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP DelegateNode"));
				}
				else
				{
					Found = FindFirstObject<UClass>(*TargetClassPath, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP DelegateNode"));
				}
			}
			return Found;
		}

		// Self context — берём GeneratedClass текущего BP
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
		if (Blueprint)
		{
			bOutIsSelfContext = true;
			// SkeletonGeneratedClass предпочтительнее во время редактирования,
			// но GeneratedClass — стабильно для уже скомпилированного BP.
			UClass* OwnerClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass;
			if (!OwnerClass && Blueprint->ParentClass)
			{
				OwnerClass = Blueprint->ParentClass;
			}
			return OwnerClass;
		}
		return nullptr;
	}

	/**
	 * Общая конфигурация для всех 4 типов delegate-нод.
	 * Делает: resolve property → SetExternalMember/SetSelfMember на DelegateReference →
	 *         задаёт позицию → AddNode + Initialize.
	 * Возвращает true если property найден и привязан.
	 */
	bool ConfigureDelegateNode(UK2Node_BaseMCDelegate* DelegateNode, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params, const TCHAR* CallerTag)
	{
		FString TargetProperty;
		if (!Params->TryGetStringField(TEXT("target_property"), TargetProperty) || TargetProperty.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("%s: missing 'target_property' parameter"), CallerTag);
			return false;
		}

		bool bIsSelfContext = false;
		UClass* OwnerClass = ResolveDelegateOwnerClass(Graph, Params, bIsSelfContext);
		if (!OwnerClass)
		{
			UE_LOG(LogTemp, Error, TEXT("%s: failed to resolve owner class for delegate '%s'"), CallerTag, *TargetProperty);
			return false;
		}

		// Ищем именно multicast-делегатное свойство (broadcast-able)
		FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(OwnerClass, FName(*TargetProperty));
		if (!DelegateProp)
		{
			UE_LOG(LogTemp, Error, TEXT("%s: multicast delegate property '%s' not found on class '%s'"),
				CallerTag, *TargetProperty, *OwnerClass->GetName());
			return false;
		}

		// SetFromProperty корректно проставит и MemberName, и MemberParent, и bSelfContext
		DelegateNode->SetFromProperty(DelegateProp, bIsSelfContext, OwnerClass);

		UE_LOG(LogTemp, Display, TEXT("%s: bound to '%s::%s' (self=%s)"),
			CallerTag, *OwnerClass->GetName(), *TargetProperty, bIsSelfContext ? TEXT("true") : TEXT("false"));

		double PosX, PosY;
		FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
		DelegateNode->NodePosX = static_cast<int32>(PosX);
		DelegateNode->NodePosY = static_cast<int32>(PosY);

		Graph->AddNode(DelegateNode, true, false);
		FNodeCreatorUtils::InitializeK2Node(DelegateNode, Graph);
		return true;
	}
}

UK2Node* FDelegateNodeCreator::CreateAddDelegateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}
	UK2Node_AddDelegate* Node = NewObject<UK2Node_AddDelegate>(Graph);
	if (!Node || !ConfigureDelegateNode(Node, Graph, Params, TEXT("MCP AddDelegate")))
	{
		return nullptr;
	}
	return Node;
}

UK2Node* FDelegateNodeCreator::CreateRemoveDelegateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}
	UK2Node_RemoveDelegate* Node = NewObject<UK2Node_RemoveDelegate>(Graph);
	if (!Node || !ConfigureDelegateNode(Node, Graph, Params, TEXT("MCP RemoveDelegate")))
	{
		return nullptr;
	}
	return Node;
}

UK2Node* FDelegateNodeCreator::CreateCallDelegateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}
	UK2Node_CallDelegate* Node = NewObject<UK2Node_CallDelegate>(Graph);
	if (!Node || !ConfigureDelegateNode(Node, Graph, Params, TEXT("MCP CallDelegate")))
	{
		return nullptr;
	}
	return Node;
}

UK2Node* FDelegateNodeCreator::CreateClearDelegateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}
	UK2Node_ClearDelegate* Node = NewObject<UK2Node_ClearDelegate>(Graph);
	if (!Node || !ConfigureDelegateNode(Node, Graph, Params, TEXT("MCP ClearDelegate")))
	{
		return nullptr;
	}
	return Node;
}
