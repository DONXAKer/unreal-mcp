#include "Commands/BlueprintGraph/Nodes/DataNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"
#include "Json.h"

namespace
{
	/**
	 * Превращает строковое имя типа в FEdGraphPinType.
	 * Поддерживает примитивы (int, float/double/real, string, bool, name, text, byte, vector, rotator, transform)
	 * и фолбэк на UScriptStruct/UClass по пути для произвольных user-типов.
	 * Возвращает true если категория успешно установлена.
	 */
	bool ResolvePinTypeFromString(const FString& TypeName, FEdGraphPinType& OutPinType)
	{
		// Сбрасываем контейнер-категорию — caller потом устанавливает Map/Set/Array
		OutPinType.ContainerType = EPinContainerType::None;
		OutPinType.PinSubCategory = NAME_None;
		OutPinType.PinSubCategoryObject = nullptr;

		if (TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (TypeName.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("real"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (TypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (TypeName.Equals(TEXT("text"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}
		if (TypeName.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			return true;
		}
		if (TypeName.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			return true;
		}
		if (TypeName.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			return true;
		}
		if (TypeName.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			return true;
		}

		// Fallback: пробуем UScriptStruct по пути
		if (UScriptStruct* AsStruct = FindObject<UScriptStruct>(nullptr, *TypeName))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = AsStruct;
			return true;
		}
		// Fallback: UClass-ссылка (PC_Object)
		if (UClass* AsClass = FindObject<UClass>(nullptr, *TypeName))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = AsClass;
			return true;
		}
		if (UClass* AsClassLoaded = LoadObject<UClass>(nullptr, *TypeName))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = AsClassLoaded;
			return true;
		}

		UE_LOG(LogTemp, Warning, TEXT("ResolvePinTypeFromString: unknown type '%s', pin will remain wildcard"), *TypeName);
		return false;
	}
}

UK2Node* FDataNodeCreator::CreateVariableGetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return nullptr;
	}

	UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(Graph);
	if (!VarGetNode)
	{
		return nullptr;
	}

	// Если указан target_class — обращаемся к UPROPERTY внешнего C++ класса
	FString TargetClassName;
	if (Params->TryGetStringField(TEXT("target_class"), TargetClassName) && !TargetClassName.IsEmpty())
	{
		UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassName);
		if (!TargetClass)
		{
			TargetClass = LoadObject<UClass>(nullptr, *TargetClassName);
		}

		if (TargetClass)
		{
			VarGetNode->VariableReference.SetExternalMember(FName(*VariableName), TargetClass);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("DataNodes: target_class '%s' not found, falling back to self member"), *TargetClassName);
			VarGetNode->VariableReference.SetSelfMember(FName(*VariableName));
		}
	}
	else
	{
		VarGetNode->VariableReference.SetSelfMember(FName(*VariableName));
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	VarGetNode->NodePosX = static_cast<int32>(PosX);
	VarGetNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(VarGetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(VarGetNode, Graph);

	return VarGetNode;
}

UK2Node* FDataNodeCreator::CreateVariableSetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return nullptr;
	}

	UK2Node_VariableSet* VarSetNode = NewObject<UK2Node_VariableSet>(Graph);
	if (!VarSetNode)
	{
		return nullptr;
	}

	VarSetNode->VariableReference.SetSelfMember(FName(*VariableName));

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	VarSetNode->NodePosX = static_cast<int32>(PosX);
	VarSetNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(VarSetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(VarSetNode, Graph);

	return VarSetNode;
}


UK2Node* FDataNodeCreator::CreateMakeArrayNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_MakeArray* MakeArrayNode = NewObject<UK2Node_MakeArray>(Graph);
	if (!MakeArrayNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MakeArrayNode->NodePosX = static_cast<int32>(PosX);
	MakeArrayNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MakeArrayNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MakeArrayNode, Graph);

	return MakeArrayNode;
}

UK2Node* FDataNodeCreator::CreateMakeMapNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_MakeMap* MakeMapNode = NewObject<UK2Node_MakeMap>(Graph);
	if (!MakeMapNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MakeMapNode->NodePosX = static_cast<int32>(PosX);
	MakeMapNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MakeMapNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MakeMapNode, Graph);

	// Применяем типы ключа/значения через output-пин:
	// UK2Node_MakeContainer хранит тип в output PinType (PinValueType для Map),
	// PropagatePinType() распространяет на input-пины.
	FString KeyTypeStr;
	FString ValueTypeStr;
	const bool bHasKey = Params->TryGetStringField(TEXT("key_type"), KeyTypeStr) && !KeyTypeStr.IsEmpty();
	const bool bHasValue = Params->TryGetStringField(TEXT("value_type"), ValueTypeStr) && !ValueTypeStr.IsEmpty();

	if (bHasKey || bHasValue)
	{
		UEdGraphPin* OutputPin = MakeMapNode->GetOutputPin();
		if (OutputPin)
		{
			if (bHasKey)
			{
				FEdGraphPinType KeyType = OutputPin->PinType;
				if (ResolvePinTypeFromString(KeyTypeStr, KeyType))
				{
					// Применяем категорию ключа на саму PinType (output это TMap<Key,Value> — Key хранится в PinCategory)
					OutputPin->PinType.PinCategory = KeyType.PinCategory;
					OutputPin->PinType.PinSubCategory = KeyType.PinSubCategory;
					OutputPin->PinType.PinSubCategoryObject = KeyType.PinSubCategoryObject;
				}
			}
			if (bHasValue)
			{
				FEdGraphPinType ValueType;
				if (ResolvePinTypeFromString(ValueTypeStr, ValueType))
				{
					OutputPin->PinType.PinValueType.TerminalCategory = ValueType.PinCategory;
					OutputPin->PinType.PinValueType.TerminalSubCategory = ValueType.PinSubCategory;
					OutputPin->PinType.PinValueType.TerminalSubCategoryObject = ValueType.PinSubCategoryObject;
				}
			}
			OutputPin->PinType.ContainerType = EPinContainerType::Map;
			MakeMapNode->PropagatePinType();
			MakeMapNode->ReconstructNode();
		}
	}

	return MakeMapNode;
}

UK2Node* FDataNodeCreator::CreateMakeSetNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_MakeSet* MakeSetNode = NewObject<UK2Node_MakeSet>(Graph);
	if (!MakeSetNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MakeSetNode->NodePosX = static_cast<int32>(PosX);
	MakeSetNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MakeSetNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MakeSetNode, Graph);

	// Тип элемента — через output-пин (TSet<Element>)
	FString ElementTypeStr;
	if (Params->TryGetStringField(TEXT("element_type"), ElementTypeStr) && !ElementTypeStr.IsEmpty())
	{
		UEdGraphPin* OutputPin = MakeSetNode->GetOutputPin();
		if (OutputPin)
		{
			FEdGraphPinType ElementType;
			if (ResolvePinTypeFromString(ElementTypeStr, ElementType))
			{
				OutputPin->PinType.PinCategory = ElementType.PinCategory;
				OutputPin->PinType.PinSubCategory = ElementType.PinSubCategory;
				OutputPin->PinType.PinSubCategoryObject = ElementType.PinSubCategoryObject;
				OutputPin->PinType.ContainerType = EPinContainerType::Set;
				MakeSetNode->PropagatePinType();
				MakeSetNode->ReconstructNode();
			}
		}
	}

	return MakeSetNode;
}

