#include "Commands/BlueprintGraph/Nodes/SpecializedNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_AddComponentByClass.h"
#include "K2Node_Self.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_Knot.h"
#include "K2Node_CreateWidget.h"
#include "Engine/Blueprint.h"
#include "Blueprint/UserWidget.h"
#include "EdGraphSchema_K2.h"
#include "Json.h"

UK2Node* FSpecializedNodeCreator::CreateGetDataTableRowNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_GetDataTableRow* DataTableRowNode = NewObject<UK2Node_GetDataTableRow>(Graph);
	if (!DataTableRowNode)
	{
		return nullptr;
	}

	// Note: DataTable property not available in UE5.5 API
	// Parameter ignored - node created without data table reference

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	DataTableRowNode->NodePosX = static_cast<int32>(PosX);
	DataTableRowNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(DataTableRowNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(DataTableRowNode, Graph);

	return DataTableRowNode;
}

UK2Node* FSpecializedNodeCreator::CreateAddComponentByClassNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_AddComponentByClass* AddComponentNode = NewObject<UK2Node_AddComponentByClass>(Graph);
	if (!AddComponentNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	AddComponentNode->NodePosX = static_cast<int32>(PosX);
	AddComponentNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(AddComponentNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(AddComponentNode, Graph);

	return AddComponentNode;
}

UK2Node* FSpecializedNodeCreator::CreateSelfNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
	if (!SelfNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SelfNode->NodePosX = static_cast<int32>(PosX);
	SelfNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SelfNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SelfNode, Graph);

	return SelfNode;
}

UK2Node* FSpecializedNodeCreator::CreateConstructObjectNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_ConstructObjectFromClass* ConstructObjNode = NewObject<UK2Node_ConstructObjectFromClass>(Graph);
	if (!ConstructObjNode)
	{
		return nullptr;
	}

	// Note: TargetClass property not available in UE5.5 API
	// Parameter ignored - node created without class reference

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	ConstructObjNode->NodePosX = static_cast<int32>(PosX);
	ConstructObjNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(ConstructObjNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(ConstructObjNode, Graph);

	return ConstructObjNode;
}

UK2Node* FSpecializedNodeCreator::CreateKnotNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Knot* KnotNode = NewObject<UK2Node_Knot>(Graph);
	if (!KnotNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	KnotNode->NodePosX = static_cast<int32>(PosX);
	KnotNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(KnotNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(KnotNode, Graph);

	return KnotNode;
}

UK2Node* FSpecializedNodeCreator::CreateWidgetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_CreateWidget* CreateWidgetNode = NewObject<UK2Node_CreateWidget>(Graph);
	if (!CreateWidgetNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	CreateWidgetNode->NodePosX = static_cast<int32>(PosX);
	CreateWidgetNode->NodePosY = static_cast<int32>(PosY);

	// Устанавливаем класс виджета если передан
	FString WidgetClassName;
	if (Params->TryGetStringField(TEXT("widget_class"), WidgetClassName))
	{
		UClass* WidgetClass = nullptr;

		if (WidgetClassName.StartsWith(TEXT("/")))
		{
			// Полный путь к ассету Blueprint: /Game/UI/WBP_MyWidget
			// Сначала пробуем загрузить сгенерированный класс (_C суффикс)
			WidgetClass = LoadObject<UClass>(nullptr, *(WidgetClassName + TEXT("_C")));
			if (!WidgetClass)
			{
				// Загружаем Blueprint и берём GeneratedClass
				UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *WidgetClassName);
				if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetClass = BP->GeneratedClass;
				}
			}
		}

		if (!WidgetClass)
		{
			WidgetClass = FindFirstObject<UClass>(*WidgetClassName, EFindFirstObjectOptions::None,
				ELogVerbosity::Warning, TEXT("MCP CreateWidget class lookup"));
		}

		if (WidgetClass && WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			CreateWidgetNode->WidgetType = TSubclassOf<UUserWidget>(WidgetClass);
		}
	}

	Graph->AddNode(CreateWidgetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(CreateWidgetNode, Graph);

	return CreateWidgetNode;
}
