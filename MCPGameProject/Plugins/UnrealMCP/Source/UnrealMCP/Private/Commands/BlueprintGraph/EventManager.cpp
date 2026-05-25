#include "Commands/BlueprintGraph/EventManager.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"

TSharedPtr<FJsonObject> FEventManager::AddEventNode(const TSharedPtr<FJsonObject>& Params)
{
	// Validate parameters
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
	}

	// Get optional position parameters — поддерживаем оба формата:
	//  - pos_x / pos_y (legacy, плоские числа)
	//  - node_position: [X, Y] (Python tool как раз шлёт массивом)
	FVector2D Position(0.0f, 0.0f);
	double PosX = 0.0, PosY = 0.0;
	if (Params->TryGetNumberField(TEXT("pos_x"), PosX))
	{
		Position.X = static_cast<float>(PosX);
	}
	if (Params->TryGetNumberField(TEXT("pos_y"), PosY))
	{
		Position.Y = static_cast<float>(PosY);
	}
	if (Params->HasField(TEXT("node_position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
		if (Params->TryGetArrayField(TEXT("node_position"), PosArr) && PosArr && PosArr->Num() >= 2)
		{
			Position.X = static_cast<float>((*PosArr)[0]->AsNumber());
			Position.Y = static_cast<float>((*PosArr)[1]->AsNumber());
		}
	}

	// Опциональный флаг "as_override": создать именно override BlueprintImplementableEvent /
	// BlueprintNativeEvent из ParentClass (с bOverrideFunction=true и EventReference на parent).
	// Без этого C++ VM не маршрутизирует вызов BIE в Blueprint — нужна именно Override-нода.
	bool bAsOverride = false;
	Params->TryGetBoolField(TEXT("as_override"), bAsOverride);

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Get the event graph (events can only exist in the event graph)
	if (Blueprint->UbergraphPages.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Blueprint has no event graph"));
	}

	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Failed to get Blueprint event graph"));
	}

	FString FallbackWarning;
	UK2Node_Event* EventNode = nullptr;

	if (bAsOverride)
	{
		// Явный путь: найти UFunction в ParentClass и создать настоящий override.
		EventNode = CreateOverrideEventNode(Graph, EventName, Position, FallbackWarning);
		if (!EventNode)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Failed to create override event node: %s"), *EventName));
		}
	}
	else
	{
		// Legacy путь — старое поведение (через GeneratedClass + GetSuperClass), оставляем как было.
		EventNode = CreateEventNode(Graph, EventName, Position);
		if (!EventNode)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to create event node: %s"), *EventName));
		}
	}

	// Notify changes
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	TSharedPtr<FJsonObject> Response = CreateSuccessResponse(EventNode);
	Response->SetBoolField(TEXT("is_override"), EventNode->bOverrideFunction);
	if (!FallbackWarning.IsEmpty())
	{
		Response->SetStringField(TEXT("warning"), FallbackWarning);
	}
	return Response;
}

UK2Node_Event* FEventManager::CreateOverrideEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position, FString& OutWarning)
{
	if (!Graph)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return nullptr;
	}

	// Дубликат-guard — повторный override той же функции даст BP-ошибку компиляции.
	if (UK2Node_Event* Existing = FindExistingEventNode(Graph, EventName))
	{
		UE_LOG(LogTemp, Display, TEXT("FEventManager::CreateOverrideEventNode: Existing event node '%s' reused (ID: %s)"),
			*EventName, *Existing->NodeGuid.ToString());
		return Existing;
	}

	const FName EventFName(*EventName);

	// Ищем UFunction строго в ParentClass и выше — это и есть критерий "override".
	UClass* ParentClass = Blueprint->ParentClass;
	UFunction* OverriddenFunc = nullptr;
	UClass* DeclaringClass = nullptr;
	for (UClass* Class = ParentClass; Class; Class = Class->GetSuperClass())
	{
		if (UFunction* Func = Class->FindFunctionByName(EventFName, EIncludeSuperFlag::ExcludeSuper))
		{
			OverriddenFunc = Func;
			DeclaringClass = Class;
			break;
		}
	}

	if (!OverriddenFunc || !DeclaringClass)
	{
		// Fallback на старое поведение — создаём как раньше, но с понятным предупреждением.
		OutWarning = FString::Printf(
			TEXT("Function '%s' not found in parent class — created as custom/event node (NOT a true override)"),
			*EventName);
		UE_LOG(LogTemp, Warning, TEXT("FEventManager::CreateOverrideEventNode: %s"), *OutWarning);
		return CreateEventNode(Graph, EventName, Position);
	}

	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
	if (!EventNode)
	{
		return nullptr;
	}

	// Каноничный набор флагов K2Node_Event для override-узла.
	EventNode->EventReference.SetExternalMember(EventFName, DeclaringClass);
	EventNode->bOverrideFunction = true;
	EventNode->bInternalEvent = false;
	EventNode->NodePosX = static_cast<int32>(Position.X);
	EventNode->NodePosY = static_cast<int32>(Position.Y);
	EventNode->SetFlags(RF_Transactional);

	Graph->Modify();
	Graph->AddNode(EventNode, /*bFromUI*/ true, /*bSelectNewNode*/ false);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	UE_LOG(LogTemp, Display, TEXT("FEventManager::CreateOverrideEventNode: Override event '%s' from class '%s' created (ID: %s)"),
		*EventName, *DeclaringClass->GetName(), *EventNode->NodeGuid.ToString());

	return EventNode;
}

UK2Node_Event* FEventManager::CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position)
{
	if (!Graph)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return nullptr;
	}

	// Check for existing event node to avoid duplicates
	UK2Node_Event* ExistingNode = FindExistingEventNode(Graph, EventName);
	if (ExistingNode)
	{
		UE_LOG(LogTemp, Display, TEXT("F18: Using existing event node '%s' (ID: %s)"),
			*EventName, *ExistingNode->NodeGuid.ToString());
		return ExistingNode;
	}

	// Create new event node
	UK2Node_Event* EventNode = nullptr;
	UClass* BlueprintClass = Blueprint->GeneratedClass;

	if (!BlueprintClass)
	{
		UE_LOG(LogTemp, Error, TEXT("F18: Blueprint has no generated class"));
		return nullptr;
	}

	// Ищем класс, непосредственно объявляющий функцию (не генерированный Blueprint-класс).
	// Это критично для BlueprintImplementableEvent из C++: если использовать GeneratedClass,
	// AllocateDefaultPins() не создаёт параметрные пины (например, TArray<FUnitTemplate> Units).
	UClass* DeclaringClass = nullptr;
	UFunction* EventFunction = nullptr;
	for (UClass* Class = BlueprintClass; Class; Class = Class->GetSuperClass())
	{
		UFunction* Func = Class->FindFunctionByName(FName(*EventName), EIncludeSuperFlag::ExcludeSuper);
		if (Func)
		{
			EventFunction = Func;
			DeclaringClass = Class;
			break;
		}
	}

	// Запасной вариант: функция найдена через наследование, но декларирующий класс не определён
	if (!EventFunction)
	{
		EventFunction = BlueprintClass->FindFunctionByName(FName(*EventName));
		DeclaringClass = BlueprintClass;
	}

	if (EventFunction)
	{
		EventNode = NewObject<UK2Node_Event>(Graph);
		EventNode->EventReference.SetExternalMember(FName(*EventName), DeclaringClass);
		EventNode->NodePosX = static_cast<int32>(Position.X);
		EventNode->NodePosY = static_cast<int32>(Position.Y);
		Graph->AddNode(EventNode, true);
		EventNode->PostPlacedNewNode();
		EventNode->AllocateDefaultPins();

		UE_LOG(LogTemp, Display, TEXT("F18: Created new event node '%s' (ID: %s)"),
			*EventName, *EventNode->NodeGuid.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("F18: Failed to find function for event name: %s"), *EventName);
	}

	return EventNode;
}

UK2Node_Event* FEventManager::FindExistingEventNode(UEdGraph* Graph, const FString& EventName)
{
	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
		if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
		{
			return EventNode;
		}
	}

	return nullptr;
}

UBlueprint* FEventManager::LoadBlueprint(const FString& BlueprintName)
{
	// Try direct path first
	FString BlueprintPath = BlueprintName;

	// If no path prefix, assume /Game/Blueprints/
	if (!BlueprintPath.StartsWith(TEXT("/")))
	{
		BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
	}

	// Add .Blueprint suffix if not present
	if (!BlueprintPath.Contains(TEXT(".")))
	{
		BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
	}

	// Try to load the Blueprint
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

	// If not found, try with UEditorAssetLibrary
	if (!BP)
	{
		FString AssetPath = BlueprintPath;
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
			BP = Cast<UBlueprint>(Asset);
		}
	}

	return BP;
}

TSharedPtr<FJsonObject> FEventManager::CreateSuccessResponse(const UK2Node_Event* EventNode)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	Response->SetStringField(TEXT("event_name"), EventNode->EventReference.GetMemberName().ToString());
	Response->SetNumberField(TEXT("pos_x"), EventNode->NodePosX);
	Response->SetNumberField(TEXT("pos_y"), EventNode->NodePosY);
	return Response;
}

TSharedPtr<FJsonObject> FEventManager::AddComponentBoundEvent(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString ComponentPropertyName;
	if (!Params->TryGetStringField(TEXT("component_property_name"), ComponentPropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'component_property_name' parameter"));
	}

	FString DelegateName;
	if (!Params->TryGetStringField(TEXT("delegate_name"), DelegateName))
	{
		return CreateErrorResponse(TEXT("Missing 'delegate_name' parameter"));
	}

	FString DelegateClass;
	if (!Params->TryGetStringField(TEXT("delegate_class"), DelegateClass))
	{
		return CreateErrorResponse(TEXT("Missing 'delegate_class' parameter"));
	}

	double PosX = 0.0, PosY = 0.0;
	Params->TryGetNumberField(TEXT("pos_x"), PosX);
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	if (Blueprint->UbergraphPages.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Blueprint has no event graph"));
	}

	UEdGraph* Graph = Blueprint->UbergraphPages[0];

	// Найти класс-владелец делегата.
	// UE 5.7: FindObject<UClass>(nullptr, ShortName) deprecated (ANY_PACKAGE
	// удалён) и всегда возвращал null на 5.7 — поэтому используем
	// FindFirstObject для коротких имён + LoadObject для полных path'ов.
	UClass* OwnerClass = FindFirstObject<UClass>(*DelegateClass, EFindFirstObjectOptions::None,
		ELogVerbosity::Warning, TEXT("MCP EventManager"));
	if (!OwnerClass)
	{
		OwnerClass = LoadObject<UClass>(nullptr, *DelegateClass);
	}
	if (!OwnerClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Delegate owner class not found: %s"), *DelegateClass));
	}

	// Найти FMulticastDelegateProperty на классе-владельце
	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(OwnerClass, FName(*DelegateName));
	if (!DelegateProp)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Delegate '%s' not found on class '%s'"), *DelegateName, *DelegateClass));
	}

	// Найти FObjectProperty компонента на Blueprint-классе (включая родительские классы)
	UClass* BlueprintClass = Blueprint->GeneratedClass;
	if (!BlueprintClass)
	{
		return CreateErrorResponse(TEXT("Blueprint has no generated class"));
	}

	FObjectProperty* ComponentProp = nullptr;
	for (TFieldIterator<FObjectProperty> PropIt(BlueprintClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		if (PropIt->GetName() == ComponentPropertyName)
		{
			ComponentProp = *PropIt;
			break;
		}
	}

	if (!ComponentProp)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Component property '%s' not found on Blueprint class"), *ComponentPropertyName));
	}

	// Создать K2Node_ComponentBoundEvent
	UK2Node_ComponentBoundEvent* BoundEventNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
	if (!BoundEventNode)
	{
		return CreateErrorResponse(TEXT("Failed to create ComponentBoundEvent node"));
	}

	BoundEventNode->NodePosX = static_cast<int32>(PosX);
	BoundEventNode->NodePosY = static_cast<int32>(PosY);

	// Инициализировать параметры узла через UE5 API
	BoundEventNode->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);

	Graph->AddNode(BoundEventNode, true, false);
	BoundEventNode->PostPlacedNewNode();
	BoundEventNode->AllocateDefaultPins();

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("node_id"), BoundEventNode->NodeGuid.ToString());
	Response->SetStringField(TEXT("delegate_name"), DelegateName);
	Response->SetStringField(TEXT("component_property_name"), ComponentPropertyName);
	Response->SetNumberField(TEXT("pos_x"), BoundEventNode->NodePosX);
	Response->SetNumberField(TEXT("pos_y"), BoundEventNode->NodePosY);
	return Response;
}

TSharedPtr<FJsonObject> FEventManager::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}

// ── Phase 1D (v1.12.0) — Custom events ───────────────────────────────────────

namespace
{
	// Locate a K2Node_CustomEvent across all Ubergraph pages by CustomFunctionName.
	static UK2Node_CustomEvent* FindCustomEventByName(UBlueprint* Blueprint, const FString& EventName)
	{
		if (!Blueprint) return nullptr;
		const FName TargetName(*EventName);
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
				{
					if (CE->CustomFunctionName == TargetName)
					{
						return CE;
					}
				}
			}
		}
		return nullptr;
	}
}

TSharedPtr<FJsonObject> FEventManager::AddCustomEventNode(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
	}

	if (EventName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Event name must not be empty"));
	}

	FVector2D Position(0.0f, 0.0f);
	if (Params->HasField(TEXT("node_position")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
		if (Params->TryGetArrayField(TEXT("node_position"), PosArr) && PosArr && PosArr->Num() >= 2)
		{
			Position.X = (float)(*PosArr)[0]->AsNumber();
			Position.Y = (float)(*PosArr)[1]->AsNumber();
		}
	}

	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	if (Blueprint->UbergraphPages.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Blueprint has no event graph"));
	}

	if (UK2Node_CustomEvent* Existing = FindCustomEventByName(Blueprint, EventName))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Custom event '%s' already exists"), *EventName));
	}

	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Failed to get Blueprint event graph"));
	}

	UK2Node_CustomEvent* CustomEvent = NewObject<UK2Node_CustomEvent>(Graph);
	if (!CustomEvent)
	{
		return CreateErrorResponse(TEXT("Failed to allocate K2Node_CustomEvent"));
	}

	// Mirror UK2Node_CustomEvent::CreateFromFunction exactly — the canonical engine flow.
	CustomEvent->CustomFunctionName = FName(*EventName);
	CustomEvent->bIsEditable = true;
	CustomEvent->NodePosX = static_cast<int32>(Position.X);
	CustomEvent->NodePosY = static_cast<int32>(Position.Y);
	CustomEvent->SetFlags(RF_Transactional);

	Graph->Modify();
	Graph->AddNode(CustomEvent, /*bFromUI*/ true, /*bSelectNewNode*/ false);
	CustomEvent->CreateNewGuid();
	CustomEvent->PostPlacedNewNode();
	CustomEvent->AllocateDefaultPins();

	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("node_id"), CustomEvent->NodeGuid.ToString());
	Response->SetStringField(TEXT("event_name"), CustomEvent->CustomFunctionName.ToString());
	Response->SetStringField(TEXT("blueprint_name"), BlueprintName);
	Response->SetNumberField(TEXT("node_pos_x"), CustomEvent->NodePosX);
	Response->SetNumberField(TEXT("node_pos_y"), CustomEvent->NodePosY);
	return Response;
}

TSharedPtr<FJsonObject> FEventManager::AddCustomEventInput(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
	}

	FString ParameterName;
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName))
	{
		return CreateErrorResponse(TEXT("Missing 'parameter_name' parameter"));
	}

	FString ParameterType;
	if (!Params->TryGetStringField(TEXT("parameter_type"), ParameterType))
	{
		return CreateErrorResponse(TEXT("Missing 'parameter_type' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	UK2Node_CustomEvent* CustomEvent = FindCustomEventByName(Blueprint, EventName);
	if (!CustomEvent)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Custom event '%s' not found"), *EventName));
	}

	// Duplicate-pin guard: K2Node_CustomEvent stores its params in UserDefinedPins (same as FunctionEntry).
	const FName NewPinName(*ParameterName);
	for (const TSharedPtr<FUserPinInfo>& Existing : CustomEvent->UserDefinedPins)
	{
		if (Existing.IsValid() && Existing->PinName == NewPinName)
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Parameter '%s' already exists on custom event '%s'"), *ParameterName, *EventName));
		}
	}

	const FEdGraphPinType PinType = FBPVariables::GetPinTypeFromString(ParameterType);

	// Inputs on a CustomEvent flow OUT of the node (downstream consumers read them),
	// so the user-defined pin direction is EGPD_Output — matches FunctionEntry behaviour.
	UEdGraphPin* NewPin = CustomEvent->CreateUserDefinedPin(NewPinName, PinType, EGPD_Output);
	if (!NewPin)
	{
		return CreateErrorResponse(FString::Printf(
			TEXT("Failed to create pin '%s' on custom event '%s'"), *ParameterName, *EventName));
	}

	if (UEdGraph* OwningGraph = CustomEvent->GetGraph())
	{
		OwningGraph->NotifyGraphChanged();
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("blueprint_name"), BlueprintName);
	Response->SetStringField(TEXT("event_name"), EventName);
	Response->SetStringField(TEXT("parameter_name"), ParameterName);
	Response->SetStringField(TEXT("parameter_type"), ParameterType);
	Response->SetStringField(TEXT("node_id"), CustomEvent->NodeGuid.ToString());
	return Response;
}
