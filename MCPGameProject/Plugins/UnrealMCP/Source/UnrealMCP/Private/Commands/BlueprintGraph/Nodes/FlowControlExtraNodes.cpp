#include "Commands/BlueprintGraph/Nodes/FlowControlExtraNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MultiGate.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ExecutionSequence.h" // для InsertPinIntoExecutionNode (parent API MultiGate)
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Json.h"

// =============================================================================
// Delay — convenience wrapper для CallFunction(KismetSystemLibrary::Delay)
// =============================================================================
UK2Node* FFlowControlExtraNodeCreator::CreateDelayNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UFunction* DelayFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
	if (!DelayFunc)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowControlExtraNodes: UKismetSystemLibrary::Delay UFunction не найден"));
		return nullptr;
	}

	UK2Node_CallFunction* DelayNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!DelayNode)
	{
		return nullptr;
	}

	DelayNode->SetFromFunction(DelayFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	DelayNode->NodePosX = static_cast<int32>(PosX);
	DelayNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(DelayNode, false, false);
	DelayNode->CreateNewGuid();
	DelayNode->PostPlacedNewNode();

	// Инициализируем пины ПЕРЕД установкой DefaultValue
	FNodeCreatorUtils::InitializeK2Node(DelayNode, Graph);

	// Optional duration (default 0.2 как в стандартном Delay)
	double Duration = 0.2;
	if (Params->TryGetNumberField(TEXT("duration"), Duration))
	{
		UEdGraphPin* DurationPin = DelayNode->FindPin(TEXT("Duration"));
		if (DurationPin)
		{
			DurationPin->DefaultValue = FString::SanitizeFloat(Duration);
		}
	}

	return DelayNode;
}

// =============================================================================
// MultiGate — native UK2Node_MultiGate (наследник UK2Node_ExecutionSequence)
// =============================================================================
UK2Node* FFlowControlExtraNodeCreator::CreateMultiGateNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_MultiGate* MultiGateNode = NewObject<UK2Node_MultiGate>(Graph);
	if (!MultiGateNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MultiGateNode->NodePosX = static_cast<int32>(PosX);
	MultiGateNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MultiGateNode, false, false);
	MultiGateNode->CreateNewGuid();
	MultiGateNode->PostPlacedNewNode();
	FNodeCreatorUtils::InitializeK2Node(MultiGateNode, Graph);

	// num_outputs (default 2). MultiGate стартует с базовым числом output pins,
	// мы добавляем недостающие через унаследованный InsertPinIntoExecutionNode.
	int32 RequestedOutputs = 2;
	int32 NumOutputsParam = 0;
	if (Params->TryGetNumberField(TEXT("num_outputs"), NumOutputsParam))
	{
		RequestedOutputs = FMath::Max(NumOutputsParam, 2);
	}

	// Считаем текущие output exec-пины через индексный доступ родителя
	int32 CurrentCount = 0;
	while (UEdGraphPin* P = MultiGateNode->GetThenPinGivenIndex(CurrentCount))
	{
		CurrentCount++;
		if (CurrentCount > 256) // safety
		{
			break;
		}
	}

	while (CurrentCount < RequestedOutputs)
	{
		UEdGraphPin* LastThenPin = MultiGateNode->GetThenPinGivenIndex(CurrentCount - 1);
		if (!LastThenPin)
		{
			break;
		}
		MultiGateNode->InsertPinIntoExecutionNode(LastThenPin, EPinInsertPosition::After);
		CurrentCount++;
	}

	// IsRandom / Loop — это пины. UK2Node_MultiGate's GetIsRandomPin/GetLoopPin
	// declared in header but NOT exported from BlueprintGraph module (no BLUEPRINTGRAPH_API).
	// Look up by pin name directly — the typical UE 5.x naming is "bRandom"/"bLoop".
	bool bIsRandom = false;
	if (Params->TryGetBoolField(TEXT("is_random"), bIsRandom))
	{
		UEdGraphPin* RandomPin = MultiGateNode->FindPin(TEXT("bRandom"), EGPD_Input);
		if (!RandomPin) { RandomPin = MultiGateNode->FindPin(TEXT("IsRandom"), EGPD_Input); }
		if (RandomPin)
		{
			RandomPin->DefaultValue = bIsRandom ? TEXT("true") : TEXT("false");
		}
	}

	bool bLoop = false;
	if (Params->TryGetBoolField(TEXT("loop"), bLoop))
	{
		UEdGraphPin* LoopPin = MultiGateNode->FindPin(TEXT("bLoop"), EGPD_Input);
		if (!LoopPin) { LoopPin = MultiGateNode->FindPin(TEXT("Loop"), EGPD_Input); }
		if (LoopPin)
		{
			LoopPin->DefaultValue = bLoop ? TEXT("true") : TEXT("false");
		}
	}

	// Реконструируем после правок DefaultValue, чтобы UI обновился
	MultiGateNode->ReconstructNode();
	Graph->NotifyGraphChanged();

	return MultiGateNode;
}

// =============================================================================
// Internal helper: создание K2Node_MacroInstance по имени макрографа из StandardMacros
// =============================================================================
static UK2Node* CreateStandardMacroInstance(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params, const FName& MacroGraphName)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UBlueprint* StandardMacros = LoadObject<UBlueprint>(nullptr,
		TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	if (!StandardMacros)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowControlExtraNodes: StandardMacros library not available"));
		return nullptr;
	}

	UEdGraph* TargetMacroGraph = nullptr;
	for (UEdGraph* MacroGraph : StandardMacros->MacroGraphs)
	{
		if (MacroGraph && MacroGraph->GetFName() == MacroGraphName)
		{
			TargetMacroGraph = MacroGraph;
			break;
		}
	}

	if (!TargetMacroGraph)
	{
		UE_LOG(LogTemp, Warning, TEXT("FlowControlExtraNodes: макрос '%s' не найден в StandardMacros"),
			*MacroGraphName.ToString());
		return nullptr;
	}

	UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
	if (!MacroNode)
	{
		return nullptr;
	}

	// SetMacroGraph должен быть вызван ДО AllocateDefaultPins, иначе пины не сгенерируются
	MacroNode->SetMacroGraph(TargetMacroGraph);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	MacroNode->NodePosX = static_cast<int32>(PosX);
	MacroNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(MacroNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(MacroNode, Graph);

	return MacroNode;
}

// =============================================================================
// Gate macro
// =============================================================================
UK2Node* FFlowControlExtraNodeCreator::CreateGateMacroNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	return CreateStandardMacroInstance(Graph, Params, FName(TEXT("Gate")));
}

// =============================================================================
// DoOnce macro (имя графа: "Do Once" — с пробелом)
// =============================================================================
UK2Node* FFlowControlExtraNodeCreator::CreateDoOnceMacroNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	return CreateStandardMacroInstance(Graph, Params, FName(TEXT("Do Once")));
}

// =============================================================================
// FlipFlop macro
// =============================================================================
UK2Node* FFlowControlExtraNodeCreator::CreateFlipFlopMacroNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	return CreateStandardMacroInstance(Graph, Params, FName(TEXT("FlipFlop")));
}
