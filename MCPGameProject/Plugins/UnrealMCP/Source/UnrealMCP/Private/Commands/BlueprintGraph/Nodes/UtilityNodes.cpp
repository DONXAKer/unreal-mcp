// v3.12.4 fix: ReconstructNode fallback for non-static component methods (AddRelativeRotation etc.)
#include "Commands/BlueprintGraph/Nodes/UtilityNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Json.h"

UK2Node* FUtilityNodeCreator::CreatePrintNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_CallFunction* PrintNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!PrintNode)
	{
		return nullptr;
	}

	UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)
	);

	if (!PrintFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	PrintNode->SetFromFunction(PrintFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	PrintNode->NodePosX = static_cast<int32>(PosX);
	PrintNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(PrintNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(PrintNode, Graph);

	// Set message if provided AFTER initialization
	FString Message;
	if (Params->TryGetStringField(TEXT("message"), Message))
	{
		UEdGraphPin* InStringPin = PrintNode->FindPin(TEXT("InString"));
		if (InStringPin)
		{
			InStringPin->DefaultValue = Message;
		}
	}

	return PrintNode;
}

UK2Node* FUtilityNodeCreator::CreateCallFunctionNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Get target function name
	FString TargetFunction;
	if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
	{
		return nullptr;
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!CallNode)
	{
		return nullptr;
	}

	// Find the function to call
	UFunction* TargetFunc = nullptr;
	FString ClassName;
	bool bIsNonStaticComponentCall = false;  // v3.12.4: track if this is a component instance method

	if (Params->TryGetStringField(TEXT("target_class"), ClassName))
	{
		UClass* TargetClass = nullptr;

		// 0. Прямое сравнение с известными библиотечными классами по короткому имени.
		//    Это надёжнее чем поиск через StaticFindObject, который в UE 5.7/5.8 устарел
		//    и часто возвращает nullptr для Script-классов.
		if (ClassName.Equals(TEXT("KismetMathLibrary"), ESearchCase::IgnoreCase) ||
		    ClassName.Equals(TEXT("UKismetMathLibrary"), ESearchCase::IgnoreCase))
		{
			TargetClass = UKismetMathLibrary::StaticClass();
		}
		else if (ClassName.Equals(TEXT("KismetSystemLibrary"), ESearchCase::IgnoreCase) ||
		         ClassName.Equals(TEXT("UKismetSystemLibrary"), ESearchCase::IgnoreCase))
		{
			TargetClass = UKismetSystemLibrary::StaticClass();
		}

		// 1. Полный путь вида /Script/Engine.KismetMathLibrary – LoadObject надёжнее StaticFindObject
		if (!TargetClass && ClassName.StartsWith(TEXT("/")))
		{
			TargetClass = LoadObject<UClass>(nullptr, *ClassName);
			if (!TargetClass)
			{
				// Fallback для путей без суффикса класса
				TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP CallFunction full-path lookup"));
			}
		}

		// 2. Короткое имя – ищем через LoadObject по популярным пакетам (UE 5.7/5.8 safe)
		if (!TargetClass)
		{
			const FString PackagePrefixes[] = {
				TEXT("/Script/Engine."),
				TEXT("/Script/UMG."),
				TEXT("/Script/SlateCore."),
				TEXT("/Script/Slate."),
				TEXT("/Script/CoreUObject."),
				TEXT("/Script/AnimGraphRuntime."),
				TEXT("/Script/GameplayAbilities.")
			};
			for (const FString& Prefix : PackagePrefixes)
			{
				TargetClass = LoadObject<UClass>(nullptr, *(Prefix + ClassName));
				if (TargetClass)
				{
					break;
				}
			}
		}

		// 3. FindFirstObject – поиск по короткому имени через глобальный реестр объектов (UE5 fallback)
		if (!TargetClass)
		{
			TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("MCP CallFunction class lookup"));
		}

		// 4. Компонент по имени в Blueprint – если ClassName не является UClass,
		//    пробуем найти компонент с таким именем в SimpleConstructionScript и взять его класс.
		//    Это позволяет передавать имя компонента (например "MainRotorMesh") как target.
		//    v3.12.4: помечаем bIsNonStaticComponentCall = true для смены стратегии инициализации.
		if (!TargetClass)
		{
			UBlueprint* OwnerBP = Graph->GetTypedOuter<UBlueprint>();
			if (OwnerBP && OwnerBP->SimpleConstructionScript)
			{
				for (USCS_Node* SCSNode : OwnerBP->SimpleConstructionScript->GetAllNodes())
				{
					if (SCSNode && SCSNode->ComponentTemplate)
					{
						FString VarName = SCSNode->GetVariableName().ToString();
						// SCS variable name может не совпадать точно – сравниваем без суффикса _GEN_VARIABLE
						FString CleanVarName = VarName.Replace(TEXT("_GEN_VARIABLE"), TEXT(""));
						if (CleanVarName.Equals(ClassName, ESearchCase::IgnoreCase) ||
						    VarName.Equals(ClassName, ESearchCase::IgnoreCase))
						{
							TargetClass = SCSNode->ComponentTemplate->GetClass();
							bIsNonStaticComponentCall = true;  // v3.12.4: component instance call
							UE_LOG(LogTemp, Display, TEXT("CreateCallFunctionNode: found component '%s' -> class '%s'"),
								*ClassName, *TargetClass->GetName());
							break;
						}
					}
				}
			}
		}

		if (TargetClass)
		{
			TargetFunc = TargetClass->FindFunctionByName(FName(*TargetFunction), EIncludeSuperFlag::IncludeSuper);
			if (TargetFunc)
			{
				// v3.12.4: если это не UBlueprintFunctionLibrary (не static utility),
				// то это instance method – пометить как компонентный вызов.
				if (!TargetFunc->HasAnyFunctionFlags(FUNC_Static))
				{
					bIsNonStaticComponentCall = true;
				}
				UE_LOG(LogTemp, Display, TEXT("CreateCallFunctionNode: found UFunction '%s' on class '%s' (static=%d)"),
					*TargetFunction, *TargetClass->GetName(), (int32)TargetFunc->HasAnyFunctionFlags(FUNC_Static));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CreateCallFunctionNode: UFunction '%s' NOT found on class '%s'"),
					*TargetFunction, *TargetClass->GetName());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateCallFunctionNode: class or component '%s' NOT resolved"), *ClassName);
		}
	}
	else
	{
		// Нет target_class – ищем по популярным Unreal-классам в порядке приоритета
		TargetFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(*TargetFunction));
		if (!TargetFunc)
		{
			TargetFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*TargetFunction));
		}
		// UMG-классы: UTextBlock (SetText), UWidget (SetIsEnabled, SetVisibility), UButton, UPanelWidget (AddChild, ClearChildren)
		if (!TargetFunc)
		{
			static const TCHAR* UMGClassPaths[] = {
				TEXT("/Script/UMG.TextBlock"),
				TEXT("/Script/UMG.Widget"),
				TEXT("/Script/UMG.Button"),
				TEXT("/Script/UMG.PanelWidget"),
				TEXT("/Script/UMG.UserWidget"),
				TEXT("/Script/UMG.WidgetBlueprintLibrary"),
			};
			for (const TCHAR* ClassPath : UMGClassPaths)
			{
				UClass* UMGClass = LoadObject<UClass>(nullptr, ClassPath);
				if (UMGClass)
				{
					TargetFunc = UMGClass->FindFunctionByName(FName(*TargetFunction));
					if (TargetFunc)
					{
						break;
					}
				}
			}
		}
	}

	if (TargetFunc)
	{
		// SetFromFunction may resolve to SetSelfMember for actor-based BPs even for external
		// library functions, causing GetTargetFunction() to return nullptr in AllocateDefaultPins()
		// → zero pins. Fix: call SetFromFunction first (copies bIsPureFunc/bIsConstFunc flags),
		// then explicitly override FunctionReference via SetExternalMember.
		CallNode->SetFromFunction(TargetFunc);
		UClass* FuncOwnerClass = TargetFunc->GetOwnerClass();
		if (FuncOwnerClass)
		{
			CallNode->FunctionReference.SetExternalMember(TargetFunc->GetFName(), FuncOwnerClass);
		}
	}
	else
	{
		// Fall back to self-member call (for local Blueprint functions)
		CallNode->FunctionReference.SetSelfMember(FName(*TargetFunction));
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	CallNode->NodePosX = static_cast<int32>(PosX);
	CallNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(CallNode, true, false);

	// v3.12.4: Two-phase initialization strategy:
	//   Phase A (static/pure library functions): PostPlacedNewNode + AllocateDefaultPins only.
	//     ReconstructNode() on a freshly-created pure-function node can flush the Pins array
	//     before UE has had a chance to bind the FunctionReference, resulting in zero pins.
	//   Phase B (non-static instance methods, e.g. AddRelativeRotation on SceneComponent):
	//     AllocateDefaultPins alone is insufficient — the node needs ReconstructNode()
	//     to properly set up the Target pin and exec pins via the Blueprint class context.
	if (bIsNonStaticComponentCall)
	{
		// Phase B: instance method — use full InitializeK2Node (AllocateDefaultPins + ReconstructNode)
		FNodeCreatorUtils::InitializeK2Node(CallNode, Graph);

		// Safety: if ReconstructNode zeroed the pins (UE5 edge case), fall back to Phase A.
		if (CallNode->Pins.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateCallFunctionNode: ReconstructNode produced 0 pins for '%s', trying PostPlacedNewNode fallback"), *TargetFunction);
			CallNode->PostPlacedNewNode();
			CallNode->AllocateDefaultPins();
		}
	}
	else
	{
		// Phase A: static/pure function — PostPlacedNewNode + AllocateDefaultPins only.
		CallNode->PostPlacedNewNode();
		CallNode->AllocateDefaultPins();

		// Safety: if AllocateDefaultPins produced 0 pins (e.g. TargetFunc was null → SetSelfMember fallback),
		// try ReconstructNode as a last resort.
		if (CallNode->Pins.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("CreateCallFunctionNode: AllocateDefaultPins produced 0 pins for '%s', trying ReconstructNode fallback"), *TargetFunction);
			CallNode->ReconstructNode();
		}
	}

	return CallNode;
}

UK2Node* FUtilityNodeCreator::CreateSelectNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
	if (!SelectNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SelectNode->NodePosX = static_cast<int32>(PosX);
	SelectNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SelectNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SelectNode, Graph);

	return SelectNode;
}

UK2Node* FUtilityNodeCreator::CreateSpawnActorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_SpawnActorFromClass* SpawnActorNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
	if (!SpawnActorNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SpawnActorNode->NodePosX = static_cast<int32>(PosX);
	SpawnActorNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SpawnActorNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SpawnActorNode, Graph);

	return SpawnActorNode;
}
