#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"

TSharedPtr<FJsonObject> FFunctionManager::CreateFunction(const TSharedPtr<FJsonObject>& Params)
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

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	// Get optional return type (default: void)
	FString ReturnType = TEXT("void");
	Params->TryGetStringField(TEXT("return_type"), ReturnType);

	// Validate function name
	if (!ValidateFunctionName(FunctionName))
	{
		return CreateErrorResponse(TEXT("Invalid function name: contains spaces or special characters"));
	}

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Check if function already exists
	if (FunctionExists(Blueprint, FunctionName))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function already exists: %s"), *FunctionName));
	}

	// Store current graph count to track which graph was created
	int32 GraphCountBefore = Blueprint->FunctionGraphs.Num();

	// Create the function using FBlueprintEditorUtils
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		return CreateErrorResponse(TEXT("Failed to create function graph"));
	}

	// Add the function to the Blueprint (bIsUserCreated = true)
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, true, nullptr);

	// Verify that FunctionEntry and FunctionResult nodes were created correctly
	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;

	// Find nodes directly in the graph (more reliable than TObjectIterator)
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (Node)
		{
			if (Node->IsA<UK2Node_FunctionEntry>())
			{
				EntryNode = Cast<UK2Node_FunctionEntry>(Node);
			}
			else if (Node->IsA<UK2Node_FunctionResult>())
			{
				ResultNode = Cast<UK2Node_FunctionResult>(Node);
			}
		}
	}

	if (!ResultNode)
	{
		// FunctionResult node doesn't exist - this will be created when output parameters are added
		if (EntryNode)
		{
			// Create FunctionResult node by adding a dummy output parameter then removing it
			// This forces Unreal to create the FunctionResult node
			FEdGraphPinType DummyType;
			DummyType.PinCategory = UEdGraphSchema_K2::PC_Boolean;

			UEdGraphPin* DummyPin = EntryNode->CreateUserDefinedPin(TEXT("__DummyOutput"), DummyType, EGPD_Input);

			if (DummyPin)
			{
				// Find the UserDefinedPin info and remove it
				for (int32 i = EntryNode->UserDefinedPins.Num() - 1; i >= 0; --i)
				{
					if (EntryNode->UserDefinedPins[i]->PinName == TEXT("__DummyOutput"))
					{
						EntryNode->RemoveUserDefinedPin(EntryNode->UserDefinedPins[i]);
						UE_LOG(LogTemp, Display, TEXT("FunctionResult node created successfully"));
						break;
					}
				}
			}
		}
	}

	// Mark Blueprint as modified
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// Compile the Blueprint AFTER verifying nodes (like GenBlueprintUtils does)
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Get the actual graph name that was created
	FString ActualGraphName = NewGraph->GetFName().ToString();
	if (Blueprint->FunctionGraphs.Num() > GraphCountBefore)
	{
		// Use the newly added graph's name
		UEdGraph* CreatedGraph = Blueprint->FunctionGraphs[Blueprint->FunctionGraphs.Num() - 1];
		if (CreatedGraph)
		{
			ActualGraphName = CreatedGraph->GetFName().ToString();
		}
	}

	UE_LOG(LogTemp, Display, TEXT("Successfully created function '%s' with internal name '%s' in %s"), *FunctionName, *ActualGraphName, *BlueprintName);

	return CreateSuccessResponse(FunctionName, ActualGraphName);
}

TSharedPtr<FJsonObject> FFunctionManager::DeleteFunction(const TSharedPtr<FJsonObject>& Params)
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

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Check if function exists
	if (!FunctionExists(Blueprint, FunctionName))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	// Prevent deletion of system functions
	if (FunctionName == TEXT("Construction Script") || FunctionName == TEXT("EventGraph"))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Cannot delete system function: %s"), *FunctionName));
	}

	// Find and remove the function
	UEdGraph* FunctionGraph = FindFunctionGraph(Blueprint, FunctionName);
	if (FunctionGraph)
	{
		FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

		UE_LOG(LogTemp, Display, TEXT("Successfully deleted function '%s' from %s"), *FunctionName, *BlueprintName);

		return CreateSuccessResponse(FunctionName);
	}

	return CreateErrorResponse(FString::Printf(TEXT("Failed to delete function: %s"), *FunctionName));
}

TSharedPtr<FJsonObject> FFunctionManager::RenameFunction(const TSharedPtr<FJsonObject>& Params)
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

	FString OldFunctionName;
	if (!Params->TryGetStringField(TEXT("old_function_name"), OldFunctionName))
	{
		return CreateErrorResponse(TEXT("Missing 'old_function_name' parameter"));
	}

	FString NewFunctionName;
	if (!Params->TryGetStringField(TEXT("new_function_name"), NewFunctionName))
	{
		return CreateErrorResponse(TEXT("Missing 'new_function_name' parameter"));
	}

	// Validate new function name
	if (!ValidateFunctionName(NewFunctionName))
	{
		return CreateErrorResponse(TEXT("Invalid function name: contains spaces or special characters"));
	}

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Check if old function exists
	if (!FunctionExists(Blueprint, OldFunctionName))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *OldFunctionName));
	}

	// Check if new name already exists
	if (FunctionExists(Blueprint, NewFunctionName))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function already exists: %s"), *NewFunctionName));
	}

	// Find the function graph
	UEdGraph* FunctionGraph = FindFunctionGraph(Blueprint, OldFunctionName);
	if (!FunctionGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function graph not found: %s"), *OldFunctionName));
	}

	// Rename using FBlueprintEditorUtils
	FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewFunctionName);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogTemp, Display, TEXT("Successfully renamed function '%s' to '%s' in %s"), *OldFunctionName, *NewFunctionName, *BlueprintName);

	return CreateSuccessResponse(NewFunctionName);
}

UBlueprint* FFunctionManager::LoadBlueprint(const FString& BlueprintName)
{
	// Try direct load with _C suffix first (most reliable for Blueprint assets)
	FString ClassPath = BlueprintName + TEXT("_C");
	UClass* BlueprintClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *ClassPath));
	if (BlueprintClass)
	{
		// Get the Blueprint asset from the class
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			if (It->GetPathName().Contains(BlueprintName))
			{
				return *It;
			}
		}
	}

	// Try EditorAssetLibrary as fallback
	if (UEditorAssetLibrary::DoesAssetExist(BlueprintName))
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintName);
		return Cast<UBlueprint>(Asset);
	}

	return nullptr;
}

bool FFunctionManager::ValidateFunctionName(const FString& FunctionName)
{
	if (FunctionName.IsEmpty())
	{
		return false;
	}

	// Check for spaces or invalid characters
	for (TCHAR Char : FunctionName)
	{
		if (FChar::IsWhitespace(Char) || (!FChar::IsAlnum(Char) && Char != TEXT('_')))
		{
			return false;
		}
	}

	// Must start with letter or underscore
	if (!FChar::IsAlpha(FunctionName[0]) && FunctionName[0] != TEXT('_'))
	{
		return false;
	}

	return true;
}

bool FFunctionManager::FunctionExists(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint)
	{
		return false;
	}

	return FindFunctionGraph(Blueprint, FunctionName) != nullptr;
}

UEdGraph* FFunctionManager::FindFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return nullptr;
	}

	// Strategy 1: Exact name match
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName().ToString() == FunctionName)
		{
			return Graph;
		}
	}

	// Strategy 2: Search by outer object name
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetOuter() && Graph->GetOuter()->GetFName().ToString() == FunctionName)
		{
			return Graph;
		}
	}

	// Strategy 3: Partial match (for auto-generated names like EdGraph_N)
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName().ToString().Contains(FunctionName))
		{
			return Graph;
		}
	}

	return nullptr;
}

// ── Phase 1C (v1.12.0) — Function lifecycle ──────────────────────────────────

namespace
{
	// Map a function entry's ExtraFlags + name into one of "Public"/"Protected"/"Private"
	static FString AccessSpecifierFromFlags(int32 ExtraFlags)
	{
		if ((ExtraFlags & FUNC_Private) != 0)   return TEXT("Private");
		if ((ExtraFlags & FUNC_Protected) != 0) return TEXT("Protected");
		// Default: Public (most Blueprint functions are public).
		return TEXT("Public");
	}
}

TSharedPtr<FJsonObject> FFunctionManager::ListFunctions(const TSharedPtr<FJsonObject>& Params)
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

	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	TArray<TSharedPtr<FJsonValue>> FuncArr;

	for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
	{
		if (!FuncGraph)
		{
			continue;
		}

		// Locate the FunctionEntry node — the source of truth for I/O pins, flags, category.
		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node && Node->IsA<UK2Node_FunctionEntry>())
			{
				EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				break;
			}
		}

		// Locate the FunctionResult node (may be absent for void/no-return functions).
		UK2Node_FunctionResult* ResultNode = nullptr;
		for (UEdGraphNode* Node : FuncGraph->Nodes)
		{
			if (Node && Node->IsA<UK2Node_FunctionResult>())
			{
				ResultNode = Cast<UK2Node_FunctionResult>(Node);
				break;
			}
		}

		const int32 NumInputs  = EntryNode  ? EntryNode->UserDefinedPins.Num()  : 0;
		const int32 NumOutputs = ResultNode ? ResultNode->UserDefinedPins.Num() : 0;

		int32 ExtraFlags = 0;
		FString Category;
		if (EntryNode)
		{
			ExtraFlags = EntryNode->GetExtraFlags();
			Category = EntryNode->MetaData.Category.ToString();
		}

		const bool bIsPure  = (ExtraFlags & FUNC_BlueprintPure) != 0;
		const bool bIsConst = (ExtraFlags & FUNC_Const) != 0;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), FuncGraph->GetFName().ToString());
		Obj->SetNumberField(TEXT("num_inputs"), NumInputs);
		Obj->SetNumberField(TEXT("num_outputs"), NumOutputs);
		Obj->SetBoolField(TEXT("is_pure"), bIsPure);
		Obj->SetBoolField(TEXT("is_const"), bIsConst);
		Obj->SetStringField(TEXT("access_specifier"), AccessSpecifierFromFlags(ExtraFlags));
		Obj->SetStringField(TEXT("category"), Category);

		FuncArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("blueprint_name"), BlueprintName);
	Response->SetArrayField(TEXT("functions"), FuncArr);
	Response->SetNumberField(TEXT("count"), FuncArr.Num());
	return Response;
}

TSharedPtr<FJsonObject> FFunctionManager::SetFunctionFlags(const TSharedPtr<FJsonObject>& Params)
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

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	UEdGraph* FuncGraph = FindFunctionGraph(Blueprint, FunctionName);
	if (!FuncGraph)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName));
	}

	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : FuncGraph->Nodes)
	{
		if (Node && Node->IsA<UK2Node_FunctionEntry>())
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(Node);
			break;
		}
	}

	if (!EntryNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("FunctionEntry node not found in '%s'"), *FunctionName));
	}

	int32 ExtraFlags = EntryNode->GetExtraFlags();

	bool bIsPure = false;
	if (Params->TryGetBoolField(TEXT("is_pure"), bIsPure))
	{
		if (bIsPure)
		{
			ExtraFlags |= FUNC_BlueprintPure;
			// Pure functions are inherently const in BP terms.
			ExtraFlags |= FUNC_Const;
		}
		else
		{
			ExtraFlags &= ~FUNC_BlueprintPure;
		}
	}

	bool bIsConst = false;
	if (Params->TryGetBoolField(TEXT("is_const"), bIsConst))
	{
		if (bIsConst) ExtraFlags |= FUNC_Const;
		else          ExtraFlags &= ~FUNC_Const;
	}

	FString Access;
	if (Params->TryGetStringField(TEXT("access"), Access))
	{
		// Clear all access bits first, then set requested one.
		ExtraFlags &= ~(FUNC_Public | FUNC_Protected | FUNC_Private);
		if (Access == TEXT("Public"))         ExtraFlags |= FUNC_Public;
		else if (Access == TEXT("Protected")) ExtraFlags |= FUNC_Protected;
		else if (Access == TEXT("Private"))   ExtraFlags |= FUNC_Private;
		else
		{
			return CreateErrorResponse(FString::Printf(
				TEXT("Invalid access specifier '%s': expected Public/Protected/Private"), *Access));
		}
	}

	EntryNode->SetExtraFlags(ExtraFlags);

	FString CategoryStr;
	if (Params->TryGetStringField(TEXT("category"), CategoryStr))
	{
		EntryNode->MetaData.Category = FText::FromString(CategoryStr);
	}

	// Persist + propagate
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("blueprint_name"), BlueprintName);
	Response->SetStringField(TEXT("function_name"), FunctionName);
	Response->SetNumberField(TEXT("extra_flags"), static_cast<double>(EntryNode->GetExtraFlags()));
	Response->SetBoolField(TEXT("is_pure"),  (EntryNode->GetExtraFlags() & FUNC_BlueprintPure) != 0);
	Response->SetBoolField(TEXT("is_const"), (EntryNode->GetExtraFlags() & FUNC_Const) != 0);
	Response->SetStringField(TEXT("access_specifier"), AccessSpecifierFromFlags(EntryNode->GetExtraFlags()));
	Response->SetStringField(TEXT("category"), EntryNode->MetaData.Category.ToString());
	return Response;
}

TSharedPtr<FJsonObject> FFunctionManager::CreateSuccessResponse(const FString& FunctionName, const FString& GraphID)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("function_name"), FunctionName);
	if (!GraphID.IsEmpty())
	{
		Response->SetStringField(TEXT("graph_id"), GraphID);
	}
	return Response;
}

TSharedPtr<FJsonObject> FFunctionManager::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}
