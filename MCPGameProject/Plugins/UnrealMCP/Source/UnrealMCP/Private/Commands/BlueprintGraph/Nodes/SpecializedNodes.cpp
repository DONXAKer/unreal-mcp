#include "Commands/BlueprintGraph/Nodes/SpecializedNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_BreakStruct.h"
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

UK2Node* FSpecializedNodeCreator::CreateBreakStructNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Получаем тип структуры — принимаем struct_type или target_class как альтернативу
	FString StructTypePath;
	if (!Params->TryGetStringField(TEXT("struct_type"), StructTypePath))
	{
		if (!Params->TryGetStringField(TEXT("target_class"), StructTypePath))
		{
			return nullptr;
		}
	}

	// Ищем UScriptStruct по пути — пробуем несколько вариантов написания
	UScriptStruct* StructType = FindObject<UScriptStruct>(nullptr, *StructTypePath);

	// UE5 регистрирует структуры без F-префикса: FUnitTemplate → UnitTemplate
	if (!StructType && StructTypePath.Contains(TEXT(".")))
	{
		int32 DotIdx;
		StructTypePath.FindLastChar(TEXT('.'), DotIdx);
		FString Package = StructTypePath.Left(DotIdx + 1);
		FString StructName = StructTypePath.Mid(DotIdx + 1);
		// Убираем F-префикс если есть
		if (StructName.StartsWith(TEXT("F")) && StructName.Len() > 1 && FChar::IsUpper(StructName[1]))
		{
			FString WithoutF = Package + StructName.Mid(1);
			StructType = FindObject<UScriptStruct>(nullptr, *WithoutF);
			UE_LOG(LogTemp, Display, TEXT("BreakStruct: trying without F-prefix: %s"), *WithoutF);
		}
	}

	if (!StructType)
	{
		StructType = LoadObject<UScriptStruct>(nullptr, *StructTypePath);
	}

	// Fallback: поиск по короткому имени через FindFirstObject
	if (!StructType)
	{
		int32 DotIdx;
		if (StructTypePath.FindLastChar(TEXT('.'), DotIdx))
		{
			FString ShortName = StructTypePath.Mid(DotIdx + 1);
			// Пробуем с F-префиксом и без
			StructType = FindFirstObject<UScriptStruct>(*ShortName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP BreakStruct"));
			if (!StructType && ShortName.StartsWith(TEXT("F")))
			{
				StructType = FindFirstObject<UScriptStruct>(*ShortName.Mid(1), EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP BreakStruct"));
			}
		}
	}

	if (!StructType)
	{
		UE_LOG(LogTemp, Error, TEXT("BreakStruct: struct type not found: %s"), *StructTypePath);
		return nullptr;
	}
	UE_LOG(LogTemp, Display, TEXT("BreakStruct: found struct '%s'"), *StructType->GetName());

	UK2Node_BreakStruct* BreakNode = NewObject<UK2Node_BreakStruct>(Graph);
	if (!BreakNode)
	{
		return nullptr;
	}

	BreakNode->StructType = StructType;

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	BreakNode->NodePosX = static_cast<int32>(PosX);
	BreakNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(BreakNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(BreakNode, Graph);

	return BreakNode;
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

	Graph->AddNode(CreateWidgetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(CreateWidgetNode, Graph);

	// Устанавливаем класс виджета через Class-пин после AllocateDefaultPins (UE5.7 API)
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
			UEdGraphPin* ClassPin = CreateWidgetNode->GetClassPin();
			if (ClassPin)
			{
				ClassPin->DefaultObject = WidgetClass;
				CreateWidgetNode->ReconstructNode();
			}
		}
	}

	return CreateWidgetNode;
}
