// WarCard project-specific commands — DeploymentSubsystem RPC через reflection (MCP-PLUGIN-006).

#include "Commands/WarCardGameCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/PIEUtils.h"

#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr const TCHAR* kDeploymentSubsystemPath    = TEXT("/Script/Client.DeploymentSubsystem");
    constexpr const TCHAR* kUnitSelectionSubsystemPath = TEXT("/Script/Client.UnitSelectionSubsystem");
    constexpr const TCHAR* kActionCardSubsystemPath    = TEXT("/Script/Client.ActionCardSubsystem");

    /** Установить значение FProperty из FJsonValue (int / string / bool). Возвращает false если type mismatch. */
    bool SetPropertyFromJson(FProperty* Prop, void* PropAddr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
    {
        if (!Prop || !PropAddr || !Value.IsValid())
        {
            OutError = TEXT("internal: null prop/value");
            return false;
        }

        if (FStrProperty* StrP = CastField<FStrProperty>(Prop))
        {
            FString S;
            if (!Value->TryGetString(S))
            {
                OutError = FString::Printf(TEXT("expected string for '%s'"), *Prop->GetName());
                return false;
            }
            StrP->SetPropertyValue(PropAddr, S);
            return true;
        }
        if (FIntProperty* IntP = CastField<FIntProperty>(Prop))
        {
            int32 N = 0;
            double D = 0.0;
            if (Value->TryGetNumber(D)) { N = static_cast<int32>(D); }
            else if (!Value->TryGetNumber(N))
            {
                OutError = FString::Printf(TEXT("expected int for '%s'"), *Prop->GetName());
                return false;
            }
            IntP->SetPropertyValue(PropAddr, N);
            return true;
        }
        if (FBoolProperty* BoolP = CastField<FBoolProperty>(Prop))
        {
            bool B = false;
            if (!Value->TryGetBool(B))
            {
                OutError = FString::Printf(TEXT("expected bool for '%s'"), *Prop->GetName());
                return false;
            }
            BoolP->SetPropertyValue(PropAddr, B);
            return true;
        }
        if (FFloatProperty* FloatP = CastField<FFloatProperty>(Prop))
        {
            double D = 0.0;
            if (!Value->TryGetNumber(D))
            {
                OutError = FString::Printf(TEXT("expected number for '%s'"), *Prop->GetName());
                return false;
            }
            FloatP->SetPropertyValue(PropAddr, static_cast<float>(D));
            return true;
        }

        OutError = FString::Printf(TEXT("unsupported property type for '%s'"), *Prop->GetName());
        return false;
    }

    /** Прочитать значение return property в FJsonValue. */
    TSharedPtr<FJsonValue> ReadReturnProperty(FProperty* RetProp, void* ParamsBuffer)
    {
        if (!RetProp || !ParamsBuffer) return MakeShared<FJsonValueNull>();
        void* RetAddr = RetProp->ContainerPtrToValuePtr<void>(ParamsBuffer);

        if (FBoolProperty* BoolP = CastField<FBoolProperty>(RetProp))
            return MakeShared<FJsonValueBoolean>(BoolP->GetPropertyValue(RetAddr));
        if (FIntProperty* IntP = CastField<FIntProperty>(RetProp))
            return MakeShared<FJsonValueNumber>(IntP->GetPropertyValue(RetAddr));
        if (FStrProperty* StrP = CastField<FStrProperty>(RetProp))
            return MakeShared<FJsonValueString>(StrP->GetPropertyValue(RetAddr));
        if (FFloatProperty* FloatP = CastField<FFloatProperty>(RetProp))
            return MakeShared<FJsonValueNumber>(FloatP->GetPropertyValue(RetAddr));

        return MakeShared<FJsonValueNull>();
    }

    /**
     * Вызвать UFunction через ProcessEvent с параметрами из JSON map.
     * Параметры подставляются по ИМЕНИ FProperty (не порядку), что устойчиво к
     * добавлению/перестановке аргументов в C++ signature.
     *
     * @return JSON { ok: bool, return: <value> } или error.
     */
    TSharedPtr<FJsonObject> InvokeFunction(
        UObject* Target,
        const FString& FunctionName,
        const TMap<FString, TSharedPtr<FJsonValue>>& JsonArgs)
    {
        UFunction* Func = Target->FindFunction(FName(*FunctionName));
        if (!Func)
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Function '%s' not found on %s"),
                    *FunctionName, *Target->GetClass()->GetName()));
        }

        // Аллоцируем буфер параметров. Для void-функций без аргументов
        // ParmsSize == 0 и Buffer.GetData() == nullptr — InitializeStruct/
        // DestroyStruct/ProcessEvent на nullptr роняют UE по assertion в
        // Class.cpp:1189 (Dest required). Обрабатываем void-кейс отдельно.
        TArray<uint8> Buffer;
        void* ParamsPtr = nullptr;
        if (Func->ParmsSize > 0)
        {
            Buffer.SetNumZeroed(Func->ParmsSize);
            ParamsPtr = Buffer.GetData();
            Func->InitializeStruct(ParamsPtr);

            FString SetErr;
            for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
            {
                FProperty* P = *It;
                if (P->PropertyFlags & CPF_ReturnParm) continue;

                const TSharedPtr<FJsonValue>* JsonVal = JsonArgs.Find(P->GetName());
                if (!JsonVal)
                {
                    // Параметр отсутствует — оставляем default (InitializeStruct).
                    continue;
                }

                void* PropAddr = P->ContainerPtrToValuePtr<void>(ParamsPtr);
                if (!SetPropertyFromJson(P, PropAddr, *JsonVal, SetErr))
                {
                    Func->DestroyStruct(ParamsPtr);
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(SetErr);
                }
            }
        }

        // Вызов — ProcessEvent OK с nullptr Params для функций без аргументов.
        Target->ProcessEvent(Func, ParamsPtr);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("function"), FunctionName);
        Result->SetStringField(TEXT("target_class"), Target->GetClass()->GetName());

        if (FProperty* RetProp = Func->GetReturnProperty(); RetProp && ParamsPtr)
        {
            Result->SetField(TEXT("return"), ReadReturnProperty(RetProp, ParamsPtr));
        }

        if (ParamsPtr)
        {
            Func->DestroyStruct(ParamsPtr);
        }
        return Result;
    }
}

FWarCardGameCommands::FWarCardGameCommands() {}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("wc_select_unit"))           return HandleSelectUnit(Params);
    if (CommandType == TEXT("wc_deselect_unit"))         return HandleDeselectUnit(Params);
    if (CommandType == TEXT("wc_confirm_selection"))     return HandleConfirmSelection(Params);
    if (CommandType == TEXT("wc_get_selection_state"))   return HandleGetSelectionState(Params);
    if (CommandType == TEXT("wc_deploy_unit"))           return HandleDeployUnit(Params);
    if (CommandType == TEXT("wc_confirm_deployment"))    return HandleConfirmDeployment(Params);
    if (CommandType == TEXT("wc_get_deployment_state"))  return HandleGetDeploymentState(Params);
    if (CommandType == TEXT("wc_surrender"))             return HandleSurrender(Params);
    if (CommandType == TEXT("wc_end_turn"))              return HandleEndTurn(Params);
    if (CommandType == TEXT("wc_get_battle_state"))      return HandleGetBattleState(Params);
    if (CommandType == TEXT("wc_free_move"))             return HandleFreeMove(Params);
    if (CommandType == TEXT("wc_attack"))                return HandleAttack(Params);
    if (CommandType == TEXT("wc_get_battle_units"))       return HandleGetBattleUnits(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown WarCard command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleSelectUnit(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString UnitId;
    if (!Params->TryGetStringField(TEXT("unit_id"), UnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'unit_id' parameter"));

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kUnitSelectionSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> Args;
    Args.Add(TEXT("UnitId"), MakeShared<FJsonValueString>(UnitId));
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("AddUnitToComposition"), Args);
    if (Result.IsValid())
    {
        bool bAdded = false;
        Result->TryGetBoolField(TEXT("return"), bAdded);
        Result->SetBoolField(TEXT("selected"), bAdded);
        Result->SetStringField(TEXT("unit_id"), UnitId);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleDeselectUnit(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString UnitId;
    if (!Params->TryGetStringField(TEXT("unit_id"), UnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'unit_id' parameter"));

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kUnitSelectionSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> Args;
    Args.Add(TEXT("UnitId"), MakeShared<FJsonValueString>(UnitId));
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("RemoveUnitFromComposition"), Args);
    if (Result.IsValid())
    {
        Result->SetStringField(TEXT("unit_id"), UnitId);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleConfirmSelection(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kUnitSelectionSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("SendCompositionToServer"), NoArgs);
    if (Result.IsValid())
    {
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleGetSelectionState(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kUnitSelectionSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> CountR = InvokeFunction(Sub, TEXT("GetCompositionCount"), NoArgs);
    TSharedPtr<FJsonObject> ReadyR = InvokeFunction(Sub, TEXT("IsCompositionReady"), NoArgs);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

    int32 Count = 0;
    if (CountR.IsValid())
    {
        const TSharedPtr<FJsonValue>* Ret = CountR->Values.Find(TEXT("return"));
        if (Ret && Ret->IsValid()) (*Ret)->TryGetNumber(Count);
    }
    Result->SetNumberField(TEXT("selected_count"), Count);

    bool bReady = false;
    if (ReadyR.IsValid())
    {
        ReadyR->TryGetBoolField(TEXT("return"), bReady);
    }
    Result->SetBoolField(TEXT("ready"), bReady);

    return Result;
}

UObject* FWarCardGameCommands::ResolveSubsystem(int32 ControllerIndex, const FString& SubsystemClassPath, FString& OutError)
{
    UWorld* World = FUnrealMCPPIEUtils::GetPIEWorldForClient(ControllerIndex);
    if (!World)
    {
        OutError = TEXT("No active PIE world for controller_index");
        return nullptr;
    }

    // StaticLoadClass — резолвит class по string path. Без include клиентских .h.
    UClass* SubsystemClass = StaticLoadClass(UWorldSubsystem::StaticClass(), nullptr, *SubsystemClassPath);
    if (!SubsystemClass)
    {
        OutError = FString::Printf(TEXT("Class not found: %s"), *SubsystemClassPath);
        return nullptr;
    }

    UObject* Subsystem = World->GetSubsystemBase(SubsystemClass);
    if (!Subsystem)
    {
        OutError = FString::Printf(TEXT("Subsystem '%s' not initialized in PIE world"), *SubsystemClassPath);
        return nullptr;
    }
    return Subsystem;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleDeployUnit(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString UnitId;
    if (!Params->TryGetStringField(TEXT("unit_id"), UnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'unit_id' parameter"));

    int32 GridX = -1, GridY = -1;
    if (!Params->TryGetNumberField(TEXT("grid_x"), GridX))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'grid_x' parameter"));
    if (!Params->TryGetNumberField(TEXT("grid_y"), GridY))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'grid_y' parameter"));

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kDeploymentSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Имена параметров должны matchать FProperty.GetName() метода DeployUnit
    // на стороне клиента (DeploymentSubsystem.h): UnitId, GridX, GridY.
    TMap<FString, TSharedPtr<FJsonValue>> Args;
    Args.Add(TEXT("UnitId"), MakeShared<FJsonValueString>(UnitId));
    Args.Add(TEXT("GridX"), MakeShared<FJsonValueNumber>(GridX));
    Args.Add(TEXT("GridY"), MakeShared<FJsonValueNumber>(GridY));

    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("DeployUnit"), Args);
    if (Result.IsValid() && Result->HasField(TEXT("return")))
    {
        // Удобство — продублировать return в верхний уровень как 'deployed'.
        bool bDeployed = false;
        Result->TryGetBoolField(TEXT("return"), bDeployed);
        Result->SetBoolField(TEXT("deployed"), bDeployed);
    }
    if (Result.IsValid())
    {
        Result->SetStringField(TEXT("unit_id"), UnitId);
        Result->SetNumberField(TEXT("grid_x"), GridX);
        Result->SetNumberField(TEXT("grid_y"), GridY);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleConfirmDeployment(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kDeploymentSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> Args;
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("ConfirmDeployment"), Args);
    if (Result.IsValid())
    {
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleGetDeploymentState(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kDeploymentSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Зовём 3 простых getter'а: GetDeployedUnitCount, IsDeploymentReady, GetDeployedUnits.
    // GetDeployedUnits возвращает TArray<FUnitDeploymentInfo> — структуры, их
    // мы пока не сериализуем (был бы слишком зависимый код); ограничимся count + ready.
    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;

    TSharedPtr<FJsonObject> CountResult = InvokeFunction(Sub, TEXT("GetDeployedUnitCount"), NoArgs);
    TSharedPtr<FJsonObject> ReadyResult = InvokeFunction(Sub, TEXT("IsDeploymentReady"), NoArgs);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

    int32 Count = 0;
    if (CountResult.IsValid())
    {
        const TSharedPtr<FJsonValue>* Ret = CountResult->Values.Find(TEXT("return"));
        if (Ret && Ret->IsValid()) (*Ret)->TryGetNumber(Count);
    }
    Result->SetNumberField(TEXT("deployed_count"), Count);

    bool bReady = false;
    if (ReadyResult.IsValid())
    {
        ReadyResult->TryGetBoolField(TEXT("return"), bReady);
    }
    Result->SetBoolField(TEXT("ready"), bReady);

    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleSurrender(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Surrender — void, без аргументов. Возврата нет, поэтому "return" в Result
    // может отсутствовать; выставляем явные success-флаги.
    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("Surrender"), NoArgs);
    if (Result.IsValid())
    {
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetBoolField(TEXT("surrendered"), true);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleEndTurn(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("EndTurn"), NoArgs);
    if (Result.IsValid())
    {
        bool bEnded = false;
        Result->TryGetBoolField(TEXT("return"), bEnded);
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetBoolField(TEXT("ended"), bEnded);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleGetBattleState(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Три getter'а: IsMyTurn → my_turn, GetCurrentAP → ap, GetMaxAP → max_ap.
    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> TurnR  = InvokeFunction(Sub, TEXT("IsMyTurn"), NoArgs);
    TSharedPtr<FJsonObject> ApR    = InvokeFunction(Sub, TEXT("GetCurrentAP"), NoArgs);
    TSharedPtr<FJsonObject> MaxApR = InvokeFunction(Sub, TEXT("GetMaxAP"), NoArgs);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

    bool bMyTurn = false;
    if (TurnR.IsValid())
    {
        TurnR->TryGetBoolField(TEXT("return"), bMyTurn);
    }
    Result->SetBoolField(TEXT("my_turn"), bMyTurn);

    int32 AP = 0;
    if (ApR.IsValid())
    {
        const TSharedPtr<FJsonValue>* Ret = ApR->Values.Find(TEXT("return"));
        if (Ret && Ret->IsValid()) (*Ret)->TryGetNumber(AP);
    }
    Result->SetNumberField(TEXT("ap"), AP);

    int32 MaxAP = 0;
    if (MaxApR.IsValid())
    {
        const TSharedPtr<FJsonValue>* Ret = MaxApR->Values.Find(TEXT("return"));
        if (Ret && Ret->IsValid()) (*Ret)->TryGetNumber(MaxAP);
    }
    Result->SetNumberField(TEXT("max_ap"), MaxAP);

    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleFreeMove(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString UnitId;
    if (!Params->TryGetStringField(TEXT("unit_id"), UnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'unit_id' parameter"));

    int32 X = -1, Y = -1;
    if (!Params->TryGetNumberField(TEXT("x"), X))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'x' parameter"));
    if (!Params->TryGetNumberField(TEXT("y"), Y))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'y' parameter"));

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Имена параметров должны matchать FProperty.GetName() метода FreeMove
    // на стороне клиента (ActionCardSubsystem.h): UnitId, TargetX, TargetY.
    // FreeMove — void, без возврата.
    TMap<FString, TSharedPtr<FJsonValue>> Args;
    Args.Add(TEXT("UnitId"), MakeShared<FJsonValueString>(UnitId));
    Args.Add(TEXT("TargetX"), MakeShared<FJsonValueNumber>(X));
    Args.Add(TEXT("TargetY"), MakeShared<FJsonValueNumber>(Y));

    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("FreeMove"), Args);
    if (Result.IsValid())
    {
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("unit_id"), UnitId);
        Result->SetNumberField(TEXT("x"), X);
        Result->SetNumberField(TEXT("y"), Y);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleAttack(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString AttackerUnitId;
    if (!Params->TryGetStringField(TEXT("attacker_unit_id"), AttackerUnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'attacker_unit_id' parameter"));

    FString TargetUnitId;
    if (!Params->TryGetStringField(TEXT("target_unit_id"), TargetUnitId))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_unit_id' parameter"));

    int32 X = -1, Y = -1;
    if (!Params->TryGetNumberField(TEXT("x"), X))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'x' parameter"));
    if (!Params->TryGetNumberField(TEXT("y"), Y))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'y' parameter"));

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // Имена параметров должны matchать FProperty.GetName() метода AttackUnit
    // на стороне клиента (ActionCardSubsystem.h): AttackerUnitId, TargetUnitId,
    // TargetX, TargetY. AttackUnit — void, без возврата.
    TMap<FString, TSharedPtr<FJsonValue>> Args;
    Args.Add(TEXT("AttackerUnitId"), MakeShared<FJsonValueString>(AttackerUnitId));
    Args.Add(TEXT("TargetUnitId"), MakeShared<FJsonValueString>(TargetUnitId));
    Args.Add(TEXT("TargetX"), MakeShared<FJsonValueNumber>(X));
    Args.Add(TEXT("TargetY"), MakeShared<FJsonValueNumber>(Y));

    TSharedPtr<FJsonObject> Result = InvokeFunction(Sub, TEXT("AttackUnit"), Args);
    if (Result.IsValid())
    {
        Result->SetBoolField(TEXT("ok"), true);
        Result->SetStringField(TEXT("attacker_unit_id"), AttackerUnitId);
        Result->SetStringField(TEXT("target_unit_id"), TargetUnitId);
        Result->SetNumberField(TEXT("x"), X);
        Result->SetNumberField(TEXT("y"), Y);
        Result->SetNumberField(TEXT("controller_index"), ControllerIndex);
    }
    return Result;
}

TSharedPtr<FJsonObject> FWarCardGameCommands::HandleGetBattleUnits(const TSharedPtr<FJsonObject>& Params)
{
    int32 ControllerIndex = 0;
    Params->TryGetNumberField(TEXT("controller_index"), ControllerIndex);

    FString Err;
    UObject* Sub = ResolveSubsystem(ControllerIndex, kActionCardSubsystemPath, Err);
    if (!Sub) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

    // GetBattleUnitsJson возвращает FString с JSON-объектом
    // {"units":[{unitId,gridX,gridY,hp,playerId,alive}]}. Отдаём raw-строкой —
    // Python сам распарсит (envelope требует dict, поэтому оборачиваем).
    TMap<FString, TSharedPtr<FJsonValue>> NoArgs;
    TSharedPtr<FJsonObject> Inner = InvokeFunction(Sub, TEXT("GetBattleUnitsJson"), NoArgs);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

    FString UnitsJson;
    if (Inner.IsValid())
    {
        const TSharedPtr<FJsonValue>* Ret = Inner->Values.Find(TEXT("return"));
        if (Ret && Ret->IsValid()) (*Ret)->TryGetString(UnitsJson);
    }
    Result->SetStringField(TEXT("units_json"), UnitsJson);

    return Result;
}
