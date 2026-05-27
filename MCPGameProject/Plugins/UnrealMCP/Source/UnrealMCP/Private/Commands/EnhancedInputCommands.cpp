// Реализация Enhanced Input команд (MCP-PLUGIN-004).

#include "Commands/EnhancedInputCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/BlueprintGraph/PinResolver.h"

#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "EnhancedActionKeyMapping.h"
#include "K2Node_EnhancedInputAction.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "InputCoreTypes.h"

FEnhancedInputCommands::FEnhancedInputCommands()
{
}

TSharedPtr<FJsonObject> FEnhancedInputCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_input_action"))                  return HandleCreateInputAction(Params);
    if (CommandType == TEXT("create_input_mapping_context"))         return HandleCreateInputMappingContext(Params);
    if (CommandType == TEXT("add_input_action_mapping"))             return HandleAddInputActionMapping(Params);
    if (CommandType == TEXT("add_enhanced_input_action_event_node")) return HandleAddEnhancedInputActionEventNode(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown Enhanced-Input command: %s"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// helpers
// ─────────────────────────────────────────────────────────────────────────────

UObject* FEnhancedInputCommands::LoadAssetByPath(const FString& Path)
{
    if (Path.IsEmpty())
    {
        return nullptr;
    }
    if (UEditorAssetLibrary::DoesAssetExist(Path))
    {
        return UEditorAssetLibrary::LoadAsset(Path);
    }
    return LoadObject<UObject>(nullptr, *Path);
}

void FEnhancedInputCommands::SaveAssetPackage(UObject* Asset)
{
    if (!Asset)
    {
        return;
    }
    if (UPackage* Pkg = Asset->GetPackage())
    {
        Pkg->MarkPackageDirty();
        const FString PackageName = Pkg->GetName();
        UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);
    }
}

bool FEnhancedInputCommands::ParseValueType(const FString& Str, uint8& OutValueType)
{
    if (Str.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Digital"), ESearchCase::IgnoreCase))
    {
        OutValueType = static_cast<uint8>(EInputActionValueType::Boolean);
        return true;
    }
    if (Str.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
    {
        OutValueType = static_cast<uint8>(EInputActionValueType::Axis1D);
        return true;
    }
    if (Str.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
    {
        OutValueType = static_cast<uint8>(EInputActionValueType::Axis2D);
        return true;
    }
    if (Str.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("Vector3"), ESearchCase::IgnoreCase))
    {
        OutValueType = static_cast<uint8>(EInputActionValueType::Axis3D);
        return true;
    }
    return false;
}

UInputModifier* FEnhancedInputCommands::CreateModifierByName(UObject* Outer, const FString& Name)
{
    // Поддерживаем основные встроенные модификаторы. Имена сравниваем
    // case-insensitive и допускаем как "Negate", так и "InputModifierNegate".
    auto Match = [&Name](const TCHAR* Want)
    {
        return Name.Equals(Want, ESearchCase::IgnoreCase) ||
               Name.Equals(FString::Printf(TEXT("InputModifier%s"), Want), ESearchCase::IgnoreCase);
    };

    if (Match(TEXT("Negate")))
    {
        return NewObject<UInputModifierNegate>(Outer);
    }
    if (Match(TEXT("DeadZone")))
    {
        return NewObject<UInputModifierDeadZone>(Outer);
    }
    if (Match(TEXT("Scalar")))
    {
        return NewObject<UInputModifierScalar>(Outer);
    }
    if (Match(TEXT("Smooth")))
    {
        return NewObject<UInputModifierSmooth>(Outer);
    }
    if (Match(TEXT("ScaleByDeltaTime")))
    {
        return NewObject<UInputModifierScaleByDeltaTime>(Outer);
    }
    if (Match(TEXT("FOVScaling")))
    {
        return NewObject<UInputModifierFOVScaling>(Outer);
    }
    if (Match(TEXT("ToWorldSpace")))
    {
        return NewObject<UInputModifierToWorldSpace>(Outer);
    }
    // Swizzle: "Swizzle" сам по себе → дефолтный YXZ; "Swizzle.YXZ"/"Swizzle.ZYX" — выбор оси.
    if (Name.StartsWith(TEXT("Swizzle"), ESearchCase::IgnoreCase) ||
        Name.StartsWith(TEXT("InputModifierSwizzleAxis"), ESearchCase::IgnoreCase))
    {
        UInputModifierSwizzleAxis* Sw = NewObject<UInputModifierSwizzleAxis>(Outer);
        if (Sw)
        {
            // Парсим суффикс ".YXZ" / ".XZY" если указан.
            FString Order;
            int32 DotIdx = INDEX_NONE;
            if (Name.FindChar(TEXT('.'), DotIdx))
            {
                Order = Name.Mid(DotIdx + 1);
            }
            if (Order.Equals(TEXT("XZY"), ESearchCase::IgnoreCase)) Sw->Order = EInputAxisSwizzle::XZY;
            else if (Order.Equals(TEXT("YXZ"), ESearchCase::IgnoreCase)) Sw->Order = EInputAxisSwizzle::YXZ;
            else if (Order.Equals(TEXT("YZX"), ESearchCase::IgnoreCase)) Sw->Order = EInputAxisSwizzle::YZX;
            else if (Order.Equals(TEXT("ZXY"), ESearchCase::IgnoreCase)) Sw->Order = EInputAxisSwizzle::ZXY;
            else if (Order.Equals(TEXT("ZYX"), ESearchCase::IgnoreCase)) Sw->Order = EInputAxisSwizzle::ZYX;
            // default — оставляем YXZ (заводское) если не указан.
        }
        return Sw;
    }

    return nullptr;
}

UInputTrigger* FEnhancedInputCommands::CreateTriggerByName(UObject* Outer, const FString& Name)
{
    auto Match = [&Name](const TCHAR* Want)
    {
        return Name.Equals(Want, ESearchCase::IgnoreCase) ||
               Name.Equals(FString::Printf(TEXT("InputTrigger%s"), Want), ESearchCase::IgnoreCase);
    };

    if (Match(TEXT("Down")))            return NewObject<UInputTriggerDown>(Outer);
    if (Match(TEXT("Pressed")))         return NewObject<UInputTriggerPressed>(Outer);
    if (Match(TEXT("Released")))        return NewObject<UInputTriggerReleased>(Outer);
    if (Match(TEXT("Hold")))            return NewObject<UInputTriggerHold>(Outer);
    if (Match(TEXT("HoldAndRelease")))  return NewObject<UInputTriggerHoldAndRelease>(Outer);
    if (Match(TEXT("Tap")))             return NewObject<UInputTriggerTap>(Outer);
    if (Match(TEXT("Pulse")))           return NewObject<UInputTriggerPulse>(Outer);
    if (Match(TEXT("RepeatedTap")))     return NewObject<UInputTriggerRepeatedTap>(Outer);
    if (Match(TEXT("ChordAction")))     return NewObject<UInputTriggerChordAction>(Outer);

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// create_input_action
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEnhancedInputCommands::HandleCreateInputAction(const TSharedPtr<FJsonObject>& Params)
{
    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));

    FString ValueTypeStr;
    if (!Params->TryGetStringField(TEXT("value_type"), ValueTypeStr))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value_type' parameter"));

    uint8 ValueType = 0;
    if (!ParseValueType(ValueTypeStr, ValueType))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown value_type: '%s' (expected Bool|Axis1D|Axis2D|Axis3D)"), *ValueTypeStr));

    FString Path = TEXT("/Game/Input/Actions");
    Params->TryGetStringField(TEXT("path"), Path);

    // Авто-префикс IA_.
    if (!Name.StartsWith(TEXT("IA_")))
    {
        Name = TEXT("IA_") + Name;
    }

    // Идемпотентность: уже есть ассет с этим путём?
    const FString AssetPath = FString::Printf(TEXT("%s/%s"), *Path, *Name);
    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("status"), TEXT("skipped"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("note"), TEXT("Asset already exists"));
        return Result;
    }

    // Создаём пакет.
    const FString PackageName = FString::Printf(TEXT("%s/%s"), *Path, *Name);
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
    }
    Package->FullyLoad();

    UInputAction* Action = NewObject<UInputAction>(Package, *Name, RF_Public | RF_Standalone);
    if (!Action)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UInputAction> returned null"));
    }
    Action->ValueType = static_cast<EInputActionValueType>(ValueType);
    FAssetRegistryModule::AssetCreated(Action);
    SaveAssetPackage(Action);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("status"), TEXT("created"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("value_type"), ValueTypeStr);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// create_input_mapping_context
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEnhancedInputCommands::HandleCreateInputMappingContext(const TSharedPtr<FJsonObject>& Params)
{
    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));

    FString Path = TEXT("/Game/Input/Contexts");
    Params->TryGetStringField(TEXT("path"), Path);

    if (!Name.StartsWith(TEXT("IMC_")))
    {
        Name = TEXT("IMC_") + Name;
    }

    const FString AssetPath = FString::Printf(TEXT("%s/%s"), *Path, *Name);
    if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("status"), TEXT("skipped"));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("note"), TEXT("Asset already exists"));
        return Result;
    }

    const FString PackageName = AssetPath;
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
    }
    Package->FullyLoad();

    UInputMappingContext* IMC = NewObject<UInputMappingContext>(Package, *Name, RF_Public | RF_Standalone);
    if (!IMC)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UInputMappingContext> returned null"));
    }
    FAssetRegistryModule::AssetCreated(IMC);
    SaveAssetPackage(IMC);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("status"), TEXT("created"));
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_input_action_mapping
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEnhancedInputCommands::HandleAddInputActionMapping(const TSharedPtr<FJsonObject>& Params)
{
    FString ContextPath, ActionPath, KeyName;
    if (!Params->TryGetStringField(TEXT("context_path"), ContextPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'context_path' parameter"));
    if (!Params->TryGetStringField(TEXT("action_path"), ActionPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_path' parameter"));
    if (!Params->TryGetStringField(TEXT("key"), KeyName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));

    UInputMappingContext* IMC = Cast<UInputMappingContext>(LoadAssetByPath(ContextPath));
    if (!IMC)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputMappingContext not found: %s"), *ContextPath));

    UInputAction* IA = Cast<UInputAction>(LoadAssetByPath(ActionPath));
    if (!IA)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));

    const FKey Key(*KeyName);
    if (!Key.IsValid())
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown engine key: '%s'"), *KeyName));

    // Создаём mapping (он сохраняется в IMC->DefaultKeyMappings.Mappings).
    FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA, Key);

    // Modifiers.
    int32 ModifiersApplied = 0;
    if (Params->HasField(TEXT("modifiers")))
    {
        const TArray<TSharedPtr<FJsonValue>>& ModArr = Params->GetArrayField(TEXT("modifiers"));
        for (const TSharedPtr<FJsonValue>& Val : ModArr)
        {
            if (!Val.IsValid()) continue;
            const FString ModName = Val->AsString();
            if (UInputModifier* Mod = CreateModifierByName(IMC, ModName))
            {
                Mapping.Modifiers.Add(Mod);
                ++ModifiersApplied;
            }
        }
    }

    // Triggers.
    int32 TriggersApplied = 0;
    if (Params->HasField(TEXT("triggers")))
    {
        const TArray<TSharedPtr<FJsonValue>>& TrigArr = Params->GetArrayField(TEXT("triggers"));
        for (const TSharedPtr<FJsonValue>& Val : TrigArr)
        {
            if (!Val.IsValid()) continue;
            const FString TName = Val->AsString();
            if (UInputTrigger* T = CreateTriggerByName(IMC, TName))
            {
                Mapping.Triggers.Add(T);
                ++TriggersApplied;
            }
        }
    }

    SaveAssetPackage(IMC);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("context_path"), ContextPath);
    Result->SetStringField(TEXT("action_path"), ActionPath);
    Result->SetStringField(TEXT("key"), KeyName);
    Result->SetNumberField(TEXT("modifiers_applied"), ModifiersApplied);
    Result->SetNumberField(TEXT("triggers_applied"), TriggersApplied);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_enhanced_input_action_event_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEnhancedInputCommands::HandleAddEnhancedInputActionEventNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, ActionPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("action_path"), ActionPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_path' parameter"));

    FString TriggerEvent = TEXT("Triggered");
    Params->TryGetStringField(TEXT("trigger_event"), TriggerEvent);

    // location
    FVector2D Location(0, 0);
    if (Params->HasField(TEXT("location")))
    {
        const TArray<TSharedPtr<FJsonValue>>& LocArr = Params->GetArrayField(TEXT("location"));
        if (LocArr.Num() >= 2)
        {
            Location.X = LocArr[0]->AsNumber();
            Location.Y = LocArr[1]->AsNumber();
        }
    }

    UBlueprint* BP = Cast<UBlueprint>(LoadAssetByPath(BlueprintPath));
    if (!BP)
    {
        UBlueprint* Found = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintPath);
        BP = Found;
    }
    if (!BP)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    UInputAction* IA = Cast<UInputAction>(LoadAssetByPath(ActionPath));
    if (!IA)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));

    if (BP->UbergraphPages.Num() == 0)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint has no UbergraphPages (EventGraph)"));

    UEdGraph* EventGraph = BP->UbergraphPages[0];

    // Создаём ноду напрямую через NewObject + AllocateDefaultPins (стандартный паттерн для K2Node).
    UK2Node_EnhancedInputAction* Node = NewObject<UK2Node_EnhancedInputAction>(EventGraph);
    if (!Node)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UK2Node_EnhancedInputAction> returned null"));
    }
    Node->InputAction = IA;
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    Node->SetFlags(RF_Transactional);
    Node->NodePosX = static_cast<int32>(Location.X);
    Node->NodePosY = static_cast<int32>(Location.Y);

    EventGraph->AddNode(Node, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Node->AllocateDefaultPins();
    // Reconstruct гарантирует что output-пины (Value, Elapsed Time и т.п.) появятся.
    Node->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    // Собираем pin descriptors через PinResolver (T001) для ответа.
    TArray<FPinDescriptor> Descriptors = FUnrealMCPPinResolver::CollectPins(Node);

    TArray<TSharedPtr<FJsonValue>> PinsArr;
    for (const FPinDescriptor& Desc : Descriptors)
    {
        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("name"), Desc.Name);
        PinObj->SetStringField(TEXT("friendlyName"), Desc.FriendlyName);
        PinObj->SetStringField(TEXT("direction"), Desc.Direction);
        PinObj->SetStringField(TEXT("pinCategory"), Desc.PinCategory);
        PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
    Result->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    Result->SetStringField(TEXT("trigger_event"), TriggerEvent);
    Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    Result->SetStringField(TEXT("action_path"), ActionPath);
    Result->SetArrayField(TEXT("pins"), PinsArr);
    return Result;
}
