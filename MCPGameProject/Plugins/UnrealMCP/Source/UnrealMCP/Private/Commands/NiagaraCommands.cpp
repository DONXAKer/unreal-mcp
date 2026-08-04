#include "Commands/NiagaraCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"

// Niagara headers — guarded so the plugin compiles even without the Niagara module.
#if WITH_EDITOR
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterStore.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraRendererProperties.h"
// NiagaraEditor — emitter authoring (v3.22.0).
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystemEditorData.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraph/EdGraphPin.h"
#endif // WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPNiagara, Log, All);

#if WITH_EDITOR
namespace UnrealMCPNiagara
{
    // ─────────────────────────────────────────────────────────────────────────
    // Small string helpers
    // ─────────────────────────────────────────────────────────────────────────

    /** lowercase, strip spaces / underscores — so "Loop Behavior" == "loopbehavior". */
    static FString NormalizeKey(const FString& In)
    {
        FString Out = In.ToLower();
        Out.ReplaceInline(TEXT(" "), TEXT(""));
        Out.ReplaceInline(TEXT("_"), TEXT(""));
        return Out;
    }

    static FString EnumToString(const UEnum* Enum, int64 Value)
    {
        if (!Enum)
        {
            return FString::Printf(TEXT("%lld"), Value);
        }
        const FString Full = Enum->GetNameStringByValue(Value);
        return Full.IsEmpty() ? FString::Printf(TEXT("%lld"), Value) : Full;
    }

    /** Case/space-insensitive enum lookup by short name or display name. Returns INDEX_NONE on miss. */
    static int64 EnumFromString(const UEnum* Enum, const FString& Name)
    {
        if (!Enum)
        {
            return INDEX_NONE;
        }
        const FString Wanted = NormalizeKey(Name);
        const int32 Num = Enum->NumEnums();
        for (int32 i = 0; i < Num; ++i)
        {
            if (NormalizeKey(Enum->GetNameStringByIndex(i)) == Wanted ||
                NormalizeKey(Enum->GetDisplayNameTextByIndex(i).ToString()) == Wanted)
            {
                return Enum->GetValueByIndex(i);
            }
        }
        return INDEX_NONE;
    }

    static TArray<TSharedPtr<FJsonValue>> ToJsonStringArray(const TArray<FString>& In, int32 MaxItems = 400)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (int32 i = 0; i < In.Num() && i < MaxItems; ++i)
        {
            Out.Add(MakeShared<FJsonValueString>(In[i]));
        }
        if (In.Num() > MaxItems)
        {
            Out.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("... (%d more truncated)"), In.Num() - MaxItems)));
        }
        return Out;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Niagara value <-> JSON
    // ─────────────────────────────────────────────────────────────────────────

    static bool IsSupportedValueType(const FNiagaraTypeDefinition& T)
    {
        return T == FNiagaraTypeDefinition::GetFloatDef()
            || T == FNiagaraTypeDefinition::GetIntDef()
            || T == FNiagaraTypeDefinition::GetBoolDef()
            || T == FNiagaraTypeDefinition::GetVec2Def()
            || T == FNiagaraTypeDefinition::GetVec3Def()
            || T == FNiagaraTypeDefinition::GetPositionDef()
            || T == FNiagaraTypeDefinition::GetVec4Def()
            || T == FNiagaraTypeDefinition::GetColorDef()
            || T == FNiagaraTypeDefinition::GetQuatDef()
            || T.IsEnum();
    }

    static TSharedPtr<FJsonValue> ValueToJson(const FNiagaraTypeDefinition& T, const uint8* Data)
    {
        if (!Data)
        {
            return MakeShared<FJsonValueNull>();
        }
        auto Floats = [Data](int32 Count) -> TSharedPtr<FJsonValue>
        {
            TArray<TSharedPtr<FJsonValue>> Arr;
            const float* F = reinterpret_cast<const float*>(Data);
            for (int32 i = 0; i < Count; ++i)
            {
                Arr.Add(MakeShared<FJsonValueNumber>(F[i]));
            }
            return MakeShared<FJsonValueArray>(Arr);
        };

        if (T == FNiagaraTypeDefinition::GetFloatDef()) return MakeShared<FJsonValueNumber>(*reinterpret_cast<const float*>(Data));
        if (T == FNiagaraTypeDefinition::GetIntDef())   return MakeShared<FJsonValueNumber>(*reinterpret_cast<const int32*>(Data));
        if (T == FNiagaraTypeDefinition::GetBoolDef())  return MakeShared<FJsonValueBoolean>(*reinterpret_cast<const int32*>(Data) != 0);
        if (T.IsEnum())                                 return MakeShared<FJsonValueString>(EnumToString(T.GetEnum(), *reinterpret_cast<const int32*>(Data)));
        if (T == FNiagaraTypeDefinition::GetVec2Def())  return Floats(2);
        if (T == FNiagaraTypeDefinition::GetVec3Def() ||
            T == FNiagaraTypeDefinition::GetPositionDef()) return Floats(3);
        if (T == FNiagaraTypeDefinition::GetVec4Def() ||
            T == FNiagaraTypeDefinition::GetColorDef() ||
            T == FNiagaraTypeDefinition::GetQuatDef())  return Floats(4);

        return MakeShared<FJsonValueString>(FString::Printf(TEXT("<unreadable:%s>"), *T.GetName()));
    }

    /**
     * Converts a JSON value into the raw byte representation Niagara expects for TypeDef.
     * Returns false (with OutError filled) instead of writing garbage — this is what
     * keeps "ok but nothing happened" from ever being reported.
     */
    static bool JsonToValueBytes(const FNiagaraTypeDefinition& T,
                                 const TSharedPtr<FJsonValue>& Val,
                                 TArray<uint8>& OutBytes,
                                 FString& OutError)
    {
        if (!Val.IsValid())
        {
            OutError = TEXT("value is null");
            return false;
        }
        if (!IsSupportedValueType(T))
        {
            OutError = FString::Printf(TEXT("type '%s' is not writable through MCP (supported: float,int,bool,enum,vec2,vec3,vec4,color,quat,position)"), *T.GetName());
            return false;
        }

        const int32 Size = T.GetSize();
        OutBytes.SetNumZeroed(Size);

        auto WriteFloats = [&](int32 Count) -> bool
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            if (!Val->TryGetArray(Arr) || !Arr)
            {
                OutError = FString::Printf(TEXT("expected an array of %d numbers for type '%s'"), Count, *T.GetName());
                return false;
            }
            if (Arr->Num() < Count)
            {
                OutError = FString::Printf(TEXT("expected %d numbers for type '%s', got %d"), Count, *T.GetName(), Arr->Num());
                return false;
            }
            float* F = reinterpret_cast<float*>(OutBytes.GetData());
            for (int32 i = 0; i < Count; ++i)
            {
                F[i] = static_cast<float>((*Arr)[i]->AsNumber());
            }
            return true;
        };

        if (T == FNiagaraTypeDefinition::GetFloatDef())
        {
            double N = 0.0;
            if (!Val->TryGetNumber(N)) { OutError = TEXT("expected a number for a float input"); return false; }
            *reinterpret_cast<float*>(OutBytes.GetData()) = static_cast<float>(N);
            return true;
        }
        if (T == FNiagaraTypeDefinition::GetIntDef())
        {
            double N = 0.0;
            if (!Val->TryGetNumber(N)) { OutError = TEXT("expected a number for an int input"); return false; }
            *reinterpret_cast<int32*>(OutBytes.GetData()) = static_cast<int32>(N);
            return true;
        }
        if (T == FNiagaraTypeDefinition::GetBoolDef())
        {
            bool B = false;
            if (!Val->TryGetBool(B))
            {
                double N = 0.0;
                if (!Val->TryGetNumber(N)) { OutError = TEXT("expected true/false for a bool input"); return false; }
                B = (N != 0.0);
            }
            // Niagara bools use -1 for true, 0 for false.
            *reinterpret_cast<int32*>(OutBytes.GetData()) = B ? static_cast<int32>(FNiagaraBool::True) : static_cast<int32>(FNiagaraBool::False);
            return true;
        }
        if (T.IsEnum())
        {
            const UEnum* Enum = T.GetEnum();
            FString S;
            int64 Resolved = INDEX_NONE;
            if (Val->TryGetString(S))
            {
                Resolved = EnumFromString(Enum, S);
                if (Resolved == INDEX_NONE)
                {
                    TArray<FString> Allowed;
                    if (Enum)
                    {
                        for (int32 i = 0; i + 1 < Enum->NumEnums(); ++i)
                        {
                            Allowed.Add(Enum->GetNameStringByIndex(i));
                        }
                    }
                    OutError = FString::Printf(TEXT("'%s' is not a valid value for enum '%s' (allowed: %s)"),
                        *S, *T.GetName(), *FString::Join(Allowed, TEXT(", ")));
                    return false;
                }
            }
            else
            {
                double N = 0.0;
                if (!Val->TryGetNumber(N)) { OutError = TEXT("expected an enum entry name (string) or an integer"); return false; }
                Resolved = static_cast<int64>(N);
            }
            *reinterpret_cast<int32*>(OutBytes.GetData()) = static_cast<int32>(Resolved);
            return true;
        }
        if (T == FNiagaraTypeDefinition::GetVec2Def())  return WriteFloats(2);
        if (T == FNiagaraTypeDefinition::GetVec3Def() ||
            T == FNiagaraTypeDefinition::GetPositionDef()) return WriteFloats(3);
        if (T == FNiagaraTypeDefinition::GetVec4Def() ||
            T == FNiagaraTypeDefinition::GetColorDef() ||
            T == FNiagaraTypeDefinition::GetQuatDef())  return WriteFloats(4);

        OutError = FString::Printf(TEXT("unhandled type '%s'"), *T.GetName());
        return false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // System / emitter lookup
    // ─────────────────────────────────────────────────────────────────────────

    static UNiagaraSystem* LoadSystem(const FString& AssetPath, TSharedPtr<FJsonObject>& OutFailure)
    {
        UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
        if (!Obj)
        {
            Obj = UEditorAssetLibrary::LoadAsset(AssetPath);
        }
        UNiagaraSystem* System = Cast<UNiagaraSystem>(Obj);
        if (!System)
        {
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("assetPath"), AssetPath);
            D->SetStringField(TEXT("resolvedClass"), Obj ? Obj->GetClass()->GetName() : TEXT("<null>"));
            OutFailure = FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("NIAGARA_SYSTEM_NOT_FOUND"),
                FString::Printf(TEXT("No UNiagaraSystem at '%s'"), *AssetPath), D);
            return nullptr;
        }
        return System;
    }

    static FNiagaraEmitterHandle* FindEmitterHandle(UNiagaraSystem* System, const FString& EmitterName, TArray<FString>& OutAvailable)
    {
        FNiagaraEmitterHandle* Found = nullptr;
        const FString Wanted = NormalizeKey(EmitterName);
        for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
        {
            const FString Name = Handle.GetName().ToString();
            OutAvailable.Add(Name);
            if (!Found && NormalizeKey(Name) == Wanted)
            {
                Found = &Handle;
            }
        }
        return Found;
    }

    static UNiagaraGraph* GetEmitterGraph(const FVersionedNiagaraEmitterData* Data)
    {
#if WITH_EDITORONLY_DATA
        if (!Data) return nullptr;
        UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
        return Source ? Source->NodeGraph : nullptr;
#else
        return nullptr;
#endif
    }

    static UEdGraphPin* FindParameterMapInputPin(UEdGraphNode* Node)
    {
        if (!Node) return nullptr;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input &&
                UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
            {
                return Pin;
            }
        }
        return nullptr;
    }

    /**
     * Walks the parameter-map chain backwards from the output node of the requested
     * usage and collects the module function-call nodes in execution order.
     * Implemented locally because FNiagaraStackGraphUtilities::GetOrderedModuleNodes
     * is not exported from NiagaraEditor.
     */
    static void CollectModuleNodes(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, TArray<UNiagaraNodeFunctionCall*>& Out)
    {
        if (!Graph) return;

        UNiagaraNodeOutput* OutputNode = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UNiagaraNodeOutput* Candidate = Cast<UNiagaraNodeOutput>(Node);
            if (Candidate && Candidate->GetUsage() == Usage)
            {
                OutputNode = Candidate;
                break;
            }
        }
        if (!OutputNode) return;

        TSet<UEdGraphNode*> Visited;
        UEdGraphNode* Current = OutputNode;
        while (Current && !Visited.Contains(Current))
        {
            Visited.Add(Current);
            UEdGraphPin* MapIn = FindParameterMapInputPin(Current);
            if (!MapIn || MapIn->LinkedTo.Num() == 0 || MapIn->LinkedTo[0] == nullptr) break;

            UEdGraphNode* Prev = MapIn->LinkedTo[0]->GetOwningNode();
            if (UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Prev))
            {
                Out.Insert(FunctionCall, 0);
            }
            Current = Prev;
        }
    }

    static TArray<UNiagaraNodeFunctionCall*> CollectAllModuleNodes(UNiagaraGraph* Graph)
    {
        TArray<UNiagaraNodeFunctionCall*> All;
        if (!Graph) return All;
        static const ENiagaraScriptUsage Usages[] = {
            ENiagaraScriptUsage::EmitterSpawnScript,
            ENiagaraScriptUsage::EmitterUpdateScript,
            ENiagaraScriptUsage::ParticleSpawnScript,
            ENiagaraScriptUsage::ParticleUpdateScript,
        };
        for (ENiagaraScriptUsage Usage : Usages)
        {
            TArray<UNiagaraNodeFunctionCall*> Stage;
            CollectModuleNodes(Graph, Usage, Stage);
            for (UNiagaraNodeFunctionCall* Node : Stage)
            {
                All.AddUnique(Node);
            }
        }
        return All;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Module input resolution (rapid-iteration parameter OR static-switch pin)
    // ─────────────────────────────────────────────────────────────────────────

    struct FModuleInputTarget
    {
        FString Kind;                                  // "rapidIteration" | "staticSwitch"
        FString ResolvedName;                          // full parameter / pin name
        UNiagaraScript* Script = nullptr;              // rapidIteration
        FNiagaraVariable Variable;                     // rapidIteration
        UNiagaraNodeFunctionCall* Node = nullptr;      // staticSwitch
        UEdGraphPin* Pin = nullptr;                    // staticSwitch

        bool IsValid() const { return (Script != nullptr) || (Pin != nullptr); }
    };

    /**
     * Splits a rapid-iteration parameter name into module + input parts.
     * Name shape: "Constants.<EmitterUniqueName>.<Module>.<Input[.SubMember]>".
     * Returns false when the name doesn't look like a module input.
     */
    static bool SplitRapidIterationName(const FString& FullName, const FString& EmitterUniqueName,
                                        FString& OutModule, FString& OutInput)
    {
        FString Rest = FullName;
        if (Rest.StartsWith(TEXT("Constants.")))
        {
            Rest = Rest.RightChop(10);
        }
        const FString EmitterPrefix = EmitterUniqueName + TEXT(".");
        if (!EmitterUniqueName.IsEmpty() && Rest.StartsWith(EmitterPrefix))
        {
            Rest = Rest.RightChop(EmitterPrefix.Len());
        }
        int32 Dot = INDEX_NONE;
        if (!Rest.FindChar(TEXT('.'), Dot))
        {
            return false;
        }
        OutModule = Rest.Left(Dot);
        OutInput = Rest.RightChop(Dot + 1);
        return !OutModule.IsEmpty() && !OutInput.IsEmpty();
    }

    /**
     * Resolves <moduleKey>.<inputKey> for an emitter. ModuleKey may be empty to
     * match any module. Always fills OutModules / OutInputs so the caller can
     * report actionable alternatives on a miss.
     */
    static bool ResolveModuleInput(FVersionedNiagaraEmitterData* Data,
                                   const FString& EmitterUniqueName,
                                   const FString& ModuleKey,
                                   const FString& InputKey,
                                   FModuleInputTarget& Out,
                                   TArray<FString>& OutModules,
                                   TArray<FString>& OutInputs)
    {
        if (!Data) return false;

        // 1. Rapid-iteration parameters.
        TArray<UNiagaraScript*> Scripts;
        Data->GetScripts(Scripts, /*bCompilableOnly=*/false, /*bEnabledOnly=*/false);
        for (UNiagaraScript* Script : Scripts)
        {
            if (!Script) continue;
            for (const FNiagaraVariableWithOffset& Var : Script->RapidIterationParameters.ReadParameterVariables())
            {
                const FString FullName = Var.GetName().ToString();
                FString Module, Input;
                if (!SplitRapidIterationName(FullName, EmitterUniqueName, Module, Input))
                {
                    continue;
                }
                OutModules.AddUnique(Module);
                OutInputs.AddUnique(Module + TEXT(".") + Input);

                if (Out.IsValid()) continue;
                if (!ModuleKey.IsEmpty() && NormalizeKey(Module) != ModuleKey) continue;

                FString LastSegment = Input;
                int32 LastDot = INDEX_NONE;
                if (Input.FindLastChar(TEXT('.'), LastDot))
                {
                    LastSegment = Input.RightChop(LastDot + 1);
                }
                if (NormalizeKey(Input) == InputKey || NormalizeKey(LastSegment) == InputKey)
                {
                    Out.Kind = TEXT("rapidIteration");
                    Out.ResolvedName = FullName;
                    Out.Script = Script;
                    Out.Variable = FNiagaraVariable(Var.GetType(), Var.GetName());
                }
            }
        }

        // 2. Static-switch pins on module nodes.
        UNiagaraGraph* Graph = GetEmitterGraph(Data);
        for (UNiagaraNodeFunctionCall* Node : CollectAllModuleNodes(Graph))
        {
            if (!Node) continue;
            const FString ModuleName = Node->GetFunctionName();
            OutModules.AddUnique(ModuleName);

            const bool bModuleMatches = ModuleKey.IsEmpty() || NormalizeKey(ModuleName) == ModuleKey;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Input) continue;
                if (!UEdGraphSchema_Niagara::IsStaticPin(Pin)) continue;

                const FString PinName = Pin->PinName.ToString();
                OutInputs.AddUnique(ModuleName + TEXT(".") + PinName + TEXT(" [static]"));

                if (Out.IsValid() || !bModuleMatches) continue;
                if (NormalizeKey(PinName) == InputKey)
                {
                    Out.Kind = TEXT("staticSwitch");
                    Out.ResolvedName = ModuleName + TEXT(".") + PinName;
                    Out.Node = Node;
                    Out.Pin = Pin;
                }
            }
        }

        OutModules.Sort();
        OutInputs.Sort();
        return Out.IsValid();
    }

    /**
     * Applies a JSON value to a resolved target and VERIFIES the write by
     * reading the value back. Returns false with OutError on any failure —
     * never reports success for a no-op.
     */
    static bool ApplyModuleInput(const FModuleInputTarget& Target,
                                 const TSharedPtr<FJsonValue>& Value,
                                 TSharedPtr<FJsonValue>& OutNewValue,
                                 FString& OutError)
    {
        if (Target.Kind == TEXT("rapidIteration"))
        {
            const FNiagaraTypeDefinition& T = Target.Variable.GetType();
            TArray<uint8> Bytes;
            if (!JsonToValueBytes(T, Value, Bytes, OutError))
            {
                return false;
            }

            Target.Script->Modify();
            FNiagaraParameterStore& Store = Target.Script->RapidIterationParameters;
            Store.SetParameterData(Bytes.GetData(), Target.Variable, /*bAdd=*/false);
            Store.MarkParametersDirty();

            const uint8* ReadBack = Store.GetParameterData(Target.Variable);
            if (!ReadBack)
            {
                OutError = FString::Printf(TEXT("parameter '%s' vanished from the store after write"), *Target.ResolvedName);
                return false;
            }
            if (FMemory::Memcmp(ReadBack, Bytes.GetData(), T.GetSize()) != 0)
            {
                OutError = FString::Printf(TEXT("read-back mismatch for '%s' — Niagara rejected the value"), *Target.ResolvedName);
                return false;
            }
            Target.Script->MarkPackageDirty();
            OutNewValue = ValueToJson(T, ReadBack);
            return true;
        }

        if (Target.Kind == TEXT("staticSwitch"))
        {
            if (Target.Pin->LinkedTo.Num() > 0)
            {
                OutError = FString::Printf(
                    TEXT("pin '%s' is driven by a connected node (dynamic input / linked parameter); its default value would be ignored"),
                    *Target.ResolvedName);
                return false;
            }

            const FNiagaraTypeDefinition T = UEdGraphSchema_Niagara::PinToTypeDefinition(Target.Pin);
            TArray<uint8> Bytes;
            if (!JsonToValueBytes(T, Value, Bytes, OutError))
            {
                return false;
            }

            FNiagaraVariable Var(T, Target.Pin->PinName);
            Var.AllocateData();
            FMemory::Memcpy(Var.GetData(), Bytes.GetData(), T.GetSize());

            FString NewDefault;
            const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
            if (!Schema || !Schema->TryGetPinDefaultValueFromNiagaraVariable(Var, NewDefault))
            {
                OutError = FString::Printf(TEXT("could not serialize a pin default for type '%s'"), *T.GetName());
                return false;
            }

            Target.Node->Modify();
            Target.Pin->Modify();
            Target.Pin->DefaultValue = NewDefault;

            if (Target.Pin->DefaultValue != NewDefault)
            {
                OutError = FString::Printf(TEXT("pin '%s' did not accept the new default value"), *Target.ResolvedName);
                return false;
            }

            Target.Node->MarkNodeRequiresSynchronization(TEXT("MCP niagara_set_module_input"), /*bRaiseGraphNeedsRecompile=*/true);
            Target.Node->MarkPackageDirty();
            OutNewValue = MakeShared<FJsonValueString>(NewDefault);
            return true;
        }

        OutError = TEXT("internal: unresolved module input target");
        return false;
    }

    /** RequestCompile + wait. Returns false when the system still reports pending compilation. */
    static bool CompileSystem(UNiagaraSystem* System, FString& OutError)
    {
        System->RequestCompile(/*bForce=*/false);
        System->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false, /*bShowProgress=*/false);
        if (System->HasOutstandingCompilationRequests())
        {
            OutError = TEXT("Niagara compilation is still pending after WaitForCompilationComplete");
            return false;
        }
        return true;
    }

    /** Marks dirty + saves; returns false when the package could not be written. */
    static bool SaveSystem(UNiagaraSystem* System, const FString& AssetPath, FString& OutError)
    {
        System->MarkPackageDirty();
        const FString PackageName = System->GetPackage() ? System->GetPackage()->GetName() : AssetPath;
        if (!UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false))
        {
            OutError = FString::Printf(TEXT("SaveAsset('%s') returned false"), *PackageName);
            return false;
        }
        return true;
    }
} // namespace UnrealMCPNiagara
#endif // WITH_EDITOR

// ─────────────────────────────────────────────────────────────────────────────
// FNiagaraCommands
// ─────────────────────────────────────────────────────────────────────────────

FNiagaraCommands::FNiagaraCommands()
{
}

TSharedPtr<FJsonObject> FNiagaraCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("copy_niagara_system"))              return HandleCopyNiagaraSystem(Params);
    if (CommandType == TEXT("set_niagara_parameters"))           return HandleSetNiagaraParameters(Params);
    if (CommandType == TEXT("niagara_get_info"))                 return HandleNiagaraGetInfo(Params);
    if (CommandType == TEXT("niagara_add_emitter"))              return HandleNiagaraAddEmitter(Params);
    if (CommandType == TEXT("niagara_set_emitter_properties"))   return HandleNiagaraSetEmitterProperties(Params);
    if (CommandType == TEXT("niagara_set_module_input"))         return HandleNiagaraSetModuleInput(Params);

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_NIAGARA_COMMAND"),
        FString::Printf(TEXT("Niagara category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// copy_niagara_system
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleCopyNiagaraSystem(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else

    FString SourcePath;
    if (!Params->TryGetStringField(TEXT("sourcePath"), SourcePath) || SourcePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SOURCE_PATH"),
            TEXT("Required param 'sourcePath' is missing or empty"));
    }

    FString DestPath;
    if (!Params->TryGetStringField(TEXT("destPath"), DestPath) || DestPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_DEST_PATH"),
            TEXT("Required param 'destPath' is missing or empty"));
    }

    if (!FAssetCommonUtils::IsValidAssetPath(SourcePath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("sourcePath"), SourcePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("sourcePath '%s' must start with /Game/"), *SourcePath), D);
    }
    if (!FAssetCommonUtils::IsValidAssetPath(DestPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("destPath"), DestPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("destPath '%s' must start with /Game/"), *DestPath), D);
    }

    // Verify source exists and is a UNiagaraSystem.
    if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("sourcePath"), SourcePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SOURCE_ASSET_NOT_FOUND"),
            FString::Printf(TEXT("Source asset not found: '%s'"), *SourcePath), D);
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(DestPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));

    if (bOverwrite && UEditorAssetLibrary::DoesAssetExist(DestPath))
    {
        UEditorAssetLibrary::DeleteAsset(DestPath);
    }

    // DuplicateAsset takes a full path and returns the duplicated object.
    // The UEditorAssetLibrary version expects SourceAssetPath, DestinationAssetPath
    // where destination must NOT already exist.
    UObject* DupObj = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
    if (!DupObj)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_DUPLICATE_FAILED"),
            FString::Printf(TEXT("DuplicateAsset returned null (src='%s' dest='%s')"), *SourcePath, *DestPath));
    }

    // Verify it really is a NiagaraSystem.
    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(DupObj);
    if (!NiagaraSystem)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SOURCE_NOT_NIAGARA_SYSTEM"),
            FString::Printf(TEXT("Source asset at '%s' is not a UNiagaraSystem"), *SourcePath));
    }

    // Save the duplicate.
    UEditorAssetLibrary::SaveAsset(DestPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("sourcePath"), SourcePath);

    const FString Status = bOverwrite ? TEXT("overwritten") : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, DestPath, Meta);

#endif // WITH_EDITOR
}

// ─────────────────────────────────────────────────────────────────────────────
// set_niagara_parameters
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleSetNiagaraParameters(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    const TSharedPtr<FJsonObject>* ParamsObjPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("params"), ParamsObjPtr) || !ParamsObjPtr || !ParamsObjPtr->IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_NIAGARA_PARAMS"),
            TEXT("Required param 'params' (object) is missing"));
    }
    const TSharedPtr<FJsonObject>& ParamsObj = *ParamsObjPtr;

    // Load the NiagaraSystem.
    UObject* AssetObj = UEditorAssetLibrary::LoadAsset(AssetPath);
    UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(AssetObj);
    if (!NiagaraSystem)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NIAGARA_SYSTEM_NOT_FOUND"),
            FString::Printf(TEXT("No UNiagaraSystem at '%s'"), *AssetPath), D);
    }

    // Collect the exposed user parameters so we can look them up by name.
    FNiagaraUserRedirectionParameterStore& ExposedParams = NiagaraSystem->GetExposedParameters();

    int32 ParamsSet = 0;
    TArray<FString> Skipped;

    for (const auto& Pair : ParamsObj->Values)
    {
        const FString RawParamName = *Pair.Key;
        const TSharedPtr<FJsonValue>& Val = Pair.Value;
        if (!Val.IsValid())
        {
            Skipped.Add(RawParamName);
            continue;
        }

        // Build a FNiagaraVariable key to search. Niagara user params use the
        // "User." namespace prefix internally; allow callers to pass with or without it.
        FString QualifiedName = RawParamName;
        if (!QualifiedName.StartsWith(TEXT("User.")))
        {
            QualifiedName = TEXT("User.") + QualifiedName;
        }

        bool bApplied = false;

        // Try bool first (JSON bool is distinct type).
        bool BoolVal = false;
        if (Val->Type == EJson::Boolean)
        {
            Val->TryGetBool(BoolVal);
            FNiagaraVariable BoolVar(FNiagaraTypeDefinition::GetBoolDef(), FName(*QualifiedName));
            if (ExposedParams.FindParameterOffset(BoolVar) != nullptr)
            {
                FNiagaraBool NiagaraBool;
                NiagaraBool.SetValue(BoolVal);
                ExposedParams.SetParameterValue(NiagaraBool, BoolVar);
                bApplied = true;
            }
        }
        // Try array → FVector / FVector4.
        else if (Val->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
            Val->TryGetArray(Arr);
            if (Arr && Arr->Num() >= 3)
            {
                const float X = (float)(*Arr)[0]->AsNumber();
                const float Y = (float)(*Arr)[1]->AsNumber();
                const float Z = (float)(*Arr)[2]->AsNumber();

                if (Arr->Num() >= 4)
                {
                    // FVector4
                    const float W = (float)(*Arr)[3]->AsNumber();
                    FNiagaraVariable Vec4Var(FNiagaraTypeDefinition::GetVec4Def(), FName(*QualifiedName));
                    if (ExposedParams.FindParameterOffset(Vec4Var) != nullptr)
                    {
                        ExposedParams.SetParameterValue(FVector4f(X, Y, Z, W), Vec4Var);
                        bApplied = true;
                    }
                }
                if (!bApplied)
                {
                    FNiagaraVariable Vec3Var(FNiagaraTypeDefinition::GetVec3Def(), FName(*QualifiedName));
                    if (ExposedParams.FindParameterOffset(Vec3Var) != nullptr)
                    {
                        ExposedParams.SetParameterValue(FVector3f(X, Y, Z), Vec3Var);
                        bApplied = true;
                    }
                }
            }
        }
        // Try number → float or int.
        else if (Val->Type == EJson::Number)
        {
            double NumVal = 0.0;
            Val->TryGetNumber(NumVal);

            // Prefer float.
            FNiagaraVariable FloatVar(FNiagaraTypeDefinition::GetFloatDef(), FName(*QualifiedName));
            if (ExposedParams.FindParameterOffset(FloatVar) != nullptr)
            {
                ExposedParams.SetParameterValue((float)NumVal, FloatVar);
                bApplied = true;
            }
            else
            {
                FNiagaraVariable IntVar(FNiagaraTypeDefinition::GetIntDef(), FName(*QualifiedName));
                if (ExposedParams.FindParameterOffset(IntVar) != nullptr)
                {
                    ExposedParams.SetParameterValue((int32)NumVal, IntVar);
                    bApplied = true;
                }
            }
        }

        if (bApplied)
        {
            ++ParamsSet;
        }
        else
        {
            Skipped.Add(RawParamName);
        }
    }

    // Mark dirty and save.
    NiagaraSystem->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("paramsSet"), ParamsSet);
    TArray<TSharedPtr<FJsonValue>> SkippedArr;
    for (const FString& S : Skipped) SkippedArr.Add(MakeShared<FJsonValueString>(S));
    Meta->SetArrayField(TEXT("skippedParams"), SkippedArr);

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);

#endif // WITH_EDITOR
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara_get_info  (v3.22.0)
//
// Read-only. Exists so callers can answer "is this system actually going to
// spawn anything?" without diffing .uasset byte sizes. Reports emitters,
// per-stage module lists, renderers and — with includeModuleInputs — every
// rapid-iteration parameter, which is the vocabulary accepted by
// niagara_set_module_input.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleNiagaraGetInfo(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else
    using namespace UnrealMCPNiagara;

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    bool bIncludeModuleInputs = false;
    Params->TryGetBoolField(TEXT("includeModuleInputs"), bIncludeModuleInputs);

    TSharedPtr<FJsonObject> LoadFailure;
    UNiagaraSystem* System = LoadSystem(AssetPath, LoadFailure);
    if (!System)
    {
        return LoadFailure;
    }

    TArray<FString> Warnings;
    TArray<TSharedPtr<FJsonValue>> EmitterArr;

    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    int32 EnabledCount = 0;
    int32 TotalModules = 0;
    int32 TotalRenderers = 0;

    for (int32 Index = 0; Index < Handles.Num(); ++Index)
    {
        const FNiagaraEmitterHandle& Handle = Handles[Index];
        TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();

        const FString EmitterName = Handle.GetName().ToString();
        E->SetNumberField(TEXT("index"), Index);
        E->SetStringField(TEXT("name"), EmitterName);
        E->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
        E->SetStringField(TEXT("mode"),
            EnumToString(StaticEnum<ENiagaraEmitterMode>(), static_cast<int64>(Handle.GetEmitterMode())));

        if (Handle.GetIsEnabled())
        {
            ++EnabledCount;
        }
        else
        {
            Warnings.Add(FString::Printf(TEXT("emitter '%s' is DISABLED - it will not simulate"), *EmitterName));
        }

        FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
        if (!Data)
        {
            E->SetStringField(TEXT("error"), TEXT("emitter handle has no versioned emitter data"));
            Warnings.Add(FString::Printf(TEXT("emitter '%s' has no emitter data (broken reference?)"), *EmitterName));
            EmitterArr.Add(MakeShared<FJsonValueObject>(E));
            continue;
        }

        const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
        const FString UniqueName = Instance.Emitter ? Instance.Emitter->GetUniqueEmitterName() : FString();
        E->SetStringField(TEXT("uniqueName"), UniqueName);
        E->SetStringField(TEXT("sourceAsset"), Instance.Emitter ? Instance.Emitter->GetPathName() : TEXT(""));

        E->SetBoolField(TEXT("localSpace"), Data->bLocalSpace);
        E->SetStringField(TEXT("simTarget"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"));
        E->SetBoolField(TEXT("determinism"), Data->bDeterminism);
        E->SetNumberField(TEXT("randomSeed"), Data->RandomSeed);
        E->SetStringField(TEXT("boundsMode"),
            EnumToString(StaticEnum<ENiagaraEmitterCalculateBoundMode>(), static_cast<int64>(Data->CalculateBoundsMode)));

        // Renderers - an emitter with zero renderers produces zero visible particles.
        TArray<TSharedPtr<FJsonValue>> RendererArr;
        for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
        {
            if (!Renderer) continue;
            TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
            R->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
            R->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
            RendererArr.Add(MakeShared<FJsonValueObject>(R));
        }
        TotalRenderers += RendererArr.Num();
        E->SetNumberField(TEXT("rendererCount"), RendererArr.Num());
        E->SetArrayField(TEXT("renderers"), RendererArr);
        if (RendererArr.Num() == 0)
        {
            Warnings.Add(FString::Printf(TEXT("emitter '%s' has NO renderers - particles will be invisible"), *EmitterName));
        }

        // Modules per stage.
        UNiagaraGraph* Graph = GetEmitterGraph(Data);
        if (!Graph)
        {
            Warnings.Add(FString::Printf(TEXT("emitter '%s' has no editor graph - module info unavailable"), *EmitterName));
        }

        struct FStageSpec { const TCHAR* Key; ENiagaraScriptUsage Usage; };
        static const FStageSpec Stages[] = {
            { TEXT("emitterSpawn"),   ENiagaraScriptUsage::EmitterSpawnScript   },
            { TEXT("emitterUpdate"),  ENiagaraScriptUsage::EmitterUpdateScript  },
            { TEXT("particleSpawn"),  ENiagaraScriptUsage::ParticleSpawnScript  },
            { TEXT("particleUpdate"), ENiagaraScriptUsage::ParticleUpdateScript },
        };

        TSharedPtr<FJsonObject> Modules = MakeShared<FJsonObject>();
        int32 EmitterModuleCount = 0;
        for (const FStageSpec& Stage : Stages)
        {
            TArray<UNiagaraNodeFunctionCall*> Nodes;
            CollectModuleNodes(Graph, Stage.Usage, Nodes);
            TArray<TSharedPtr<FJsonValue>> NameArr;
            for (UNiagaraNodeFunctionCall* Node : Nodes)
            {
                if (Node)
                {
                    NameArr.Add(MakeShared<FJsonValueString>(Node->GetFunctionName()));
                }
            }
            EmitterModuleCount += NameArr.Num();
            Modules->SetArrayField(Stage.Key, NameArr);
        }
        TotalModules += EmitterModuleCount;
        E->SetNumberField(TEXT("moduleCount"), EmitterModuleCount);
        E->SetObjectField(TEXT("modules"), Modules);
        if (EmitterModuleCount == 0)
        {
            Warnings.Add(FString::Printf(TEXT("emitter '%s' has NO modules - nothing will spawn"), *EmitterName));
        }

        // Loop behaviour / duration - best effort, they live as module inputs.
        {
            TArray<FString> AvailableModules, AvailableInputs;
            FModuleInputTarget LoopTarget;
            ResolveModuleInput(Data, UniqueName, FString(), TEXT("loopbehavior"), LoopTarget, AvailableModules, AvailableInputs);
            if (LoopTarget.Kind == TEXT("rapidIteration"))
            {
                const uint8* Raw = LoopTarget.Script->RapidIterationParameters.GetParameterData(LoopTarget.Variable);
                E->SetField(TEXT("loopBehavior"), ValueToJson(LoopTarget.Variable.GetType(), Raw));
            }
            else if (LoopTarget.Kind == TEXT("staticSwitch"))
            {
                E->SetStringField(TEXT("loopBehavior"), LoopTarget.Pin->DefaultValue);
            }
            else
            {
                E->SetStringField(TEXT("loopBehavior"), TEXT("unknown"));
            }

            AvailableModules.Reset();
            AvailableInputs.Reset();
            FModuleInputTarget DurTarget;
            ResolveModuleInput(Data, UniqueName, FString(), TEXT("loopduration"), DurTarget, AvailableModules, AvailableInputs);
            if (DurTarget.Kind == TEXT("rapidIteration"))
            {
                const uint8* Raw = DurTarget.Script->RapidIterationParameters.GetParameterData(DurTarget.Variable);
                E->SetField(TEXT("loopDuration"), ValueToJson(DurTarget.Variable.GetType(), Raw));
            }

            if (bIncludeModuleInputs)
            {
                AvailableModules.Reset();
                AvailableInputs.Reset();
                FModuleInputTarget Unused;
                // Sentinel keys that cannot match anything - we only want the catalogue.
                ResolveModuleInput(Data, UniqueName, TEXT("\x1"), TEXT("\x1"), Unused, AvailableModules, AvailableInputs);

                TArray<TSharedPtr<FJsonValue>> InputArr;
                TArray<UNiagaraScript*> Scripts;
                Data->GetScripts(Scripts, /*bCompilableOnly=*/false, /*bEnabledOnly=*/false);
                for (UNiagaraScript* Script : Scripts)
                {
                    if (!Script) continue;
                    for (const FNiagaraVariableWithOffset& Var : Script->RapidIterationParameters.ReadParameterVariables())
                    {
                        TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
                        P->SetStringField(TEXT("name"), Var.GetName().ToString());
                        P->SetStringField(TEXT("type"), Var.GetType().GetName());
                        P->SetField(TEXT("value"),
                            ValueToJson(Var.GetType(), Script->RapidIterationParameters.GetParameterData(Var)));
                        InputArr.Add(MakeShared<FJsonValueObject>(P));
                    }
                }
                E->SetArrayField(TEXT("moduleInputs"), InputArr);
                E->SetArrayField(TEXT("staticSwitchInputs"),
                    ToJsonStringArray(AvailableInputs.FilterByPredicate(
                        [](const FString& S) { return S.EndsWith(TEXT("[static]")); })));
            }
        }

        EmitterArr.Add(MakeShared<FJsonValueObject>(E));
    }

    if (Handles.Num() == 0)
    {
        Warnings.Add(TEXT("system has ZERO emitters - SpawnSystemAtLocation returns a valid component that renders nothing"));
    }
    else if (EnabledCount == 0)
    {
        Warnings.Add(TEXT("system has emitters but NONE are enabled"));
    }

    // Exposed (User.*) parameters.
    TArray<TSharedPtr<FJsonValue>> ExposedArr;
    {
        const FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
        for (const FNiagaraVariableWithOffset& Var : Exposed.ReadParameterVariables())
        {
            TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
            P->SetStringField(TEXT("name"), Var.GetName().ToString());
            P->SetStringField(TEXT("type"), Var.GetType().GetName());
            ExposedArr.Add(MakeShared<FJsonValueObject>(P));
        }
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("emitterCount"), Handles.Num());
    Meta->SetNumberField(TEXT("enabledEmitterCount"), EnabledCount);
    Meta->SetNumberField(TEXT("totalModuleCount"), TotalModules);
    Meta->SetNumberField(TEXT("totalRendererCount"), TotalRenderers);
    Meta->SetBoolField(TEXT("isEmpty"), Handles.Num() == 0 || TotalModules == 0 || TotalRenderers == 0);
    Meta->SetNumberField(TEXT("warmupTime"), System->GetWarmupTime());
    Meta->SetArrayField(TEXT("emitters"), EmitterArr);
    Meta->SetArrayField(TEXT("exposedParameters"), ExposedArr);
    Meta->SetArrayField(TEXT("warnings"), ToJsonStringArray(Warnings));

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("ok"), AssetPath, Meta);
#endif // WITH_EDITOR
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara_add_emitter  (v3.22.0)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleNiagaraAddEmitter(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else
    using namespace UnrealMCPNiagara;

    FString SystemPath;
    if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath) || SystemPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SYSTEM_PATH"), TEXT("Required param 'systemPath' is missing or empty"));
    }

    FString EmitterAssetPath;
    if (!Params->TryGetStringField(TEXT("emitterAssetPath"), EmitterAssetPath) || EmitterAssetPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_EMITTER_ASSET_PATH"),
            TEXT("Required param 'emitterAssetPath' is missing or empty"));
    }

    FString DesiredName;
    Params->TryGetStringField(TEXT("emitterName"), DesiredName);

    bool bCompile = true;
    Params->TryGetBoolField(TEXT("compile"), bCompile);

    TSharedPtr<FJsonObject> LoadFailure;
    UNiagaraSystem* System = LoadSystem(SystemPath, LoadFailure);
    if (!System)
    {
        return LoadFailure;
    }

    UObject* EmitterObj = FAssetCommonUtils::LoadAssetObject(EmitterAssetPath);
    if (!EmitterObj)
    {
        EmitterObj = UEditorAssetLibrary::LoadAsset(EmitterAssetPath);
    }
    UNiagaraEmitter* SourceEmitter = Cast<UNiagaraEmitter>(EmitterObj);
    if (!SourceEmitter)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("emitterAssetPath"), EmitterAssetPath);
        D->SetStringField(TEXT("resolvedClass"), EmitterObj ? EmitterObj->GetClass()->GetName() : TEXT("<null>"));
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NIAGARA_EMITTER_NOT_FOUND"),
            FString::Printf(TEXT("No UNiagaraEmitter at '%s'"), *EmitterAssetPath), D);
    }

    // FNiagaraEditorUtilities::AddEmitterToSystem does a CastChecked on the system
    // editor data - bail out politely instead of crashing the editor.
    if (!Cast<UNiagaraSystemEditorData>(System->GetEditorData()))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_SYSTEM_EDITOR_DATA_MISSING"),
            FString::Printf(TEXT("System '%s' has no UNiagaraSystemEditorData; it cannot be edited (cooked or corrupt asset?)"), *SystemPath));
    }

    if (!FModuleManager::Get().LoadModule(TEXT("NiagaraEditor")))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_EDITOR_MODULE_UNAVAILABLE"),
            TEXT("NiagaraEditor module could not be loaded"));
    }

    const int32 CountBefore = System->GetEmitterHandles().Num();

    System->Modify();
    const FGuid NewHandleId = FNiagaraEditorUtilities::AddEmitterToSystem(
        *System, *SourceEmitter, SourceEmitter->GetExposedVersion().VersionGuid, /*bCreateCopy=*/true);

    const int32 CountAfter = System->GetEmitterHandles().Num();
    if (!NewHandleId.IsValid() || CountAfter != CountBefore + 1)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetNumberField(TEXT("emitterCountBefore"), CountBefore);
        D->SetNumberField(TEXT("emitterCountAfter"), CountAfter);
        D->SetBoolField(TEXT("handleIdValid"), NewHandleId.IsValid());
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_ADD_EMITTER_FAILED"),
            FString::Printf(TEXT("AddEmitterToSystem did not add a handle to '%s' (before=%d after=%d)"),
                *SystemPath, CountBefore, CountAfter), D);
    }

    // Locate the new handle and optionally rename it.
    FNiagaraEmitterHandle* NewHandle = nullptr;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetId() == NewHandleId)
        {
            NewHandle = &Handle;
            break;
        }
    }
    if (!NewHandle)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_ADD_EMITTER_FAILED"),
            TEXT("AddEmitterToSystem returned a handle id that is not present in the system"));
    }

    if (!DesiredName.IsEmpty())
    {
        NewHandle->SetName(FName(*DesiredName), *System);
    }
    const FString FinalName = NewHandle->GetName().ToString();

    // Sanity: the freshly added emitter must expose emitter data.
    if (!NewHandle->GetEmitterData())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_ADD_EMITTER_FAILED"),
            FString::Printf(TEXT("Emitter '%s' was added but exposes no versioned emitter data"), *FinalName));
    }

    FString CompileError;
    bool bCompiled = true;
    if (bCompile)
    {
        bCompiled = CompileSystem(System, CompileError);
    }

    FString SaveError;
    if (!SaveSystem(System, SystemPath, SaveError))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("NIAGARA_SAVE_FAILED"),
            FString::Printf(TEXT("Emitter '%s' was added but the system could not be saved: %s"), *FinalName, *SaveError));
    }

    UE_LOG(LogUnrealMCPNiagara, Log, TEXT("niagara_add_emitter: '%s' + '%s' -> handle '%s' (%d emitters)"),
        *SystemPath, *EmitterAssetPath, *FinalName, CountAfter);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("emitterName"), FinalName);
    Meta->SetStringField(TEXT("emitterHandleId"), NewHandleId.ToString());
    Meta->SetStringField(TEXT("sourceEmitter"), SourceEmitter->GetPathName());
    Meta->SetNumberField(TEXT("emitterCount"), CountAfter);
    Meta->SetBoolField(TEXT("compiled"), bCompile && bCompiled);
    if (bCompile && !bCompiled)
    {
        Meta->SetStringField(TEXT("compileWarning"), CompileError);
    }

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), SystemPath, Meta);
#endif // WITH_EDITOR
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara_set_emitter_properties  (v3.22.0)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleNiagaraSetEmitterProperties(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else
    using namespace UnrealMCPNiagara;

    FString SystemPath;
    if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath) || SystemPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SYSTEM_PATH"), TEXT("Required param 'systemPath' is missing or empty"));
    }

    FString EmitterName;
    if (!Params->TryGetStringField(TEXT("emitterName"), EmitterName) || EmitterName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_EMITTER_NAME"), TEXT("Required param 'emitterName' is missing or empty"));
    }

    bool bCompile = true;
    Params->TryGetBoolField(TEXT("compile"), bCompile);

    TSharedPtr<FJsonObject> LoadFailure;
    UNiagaraSystem* System = LoadSystem(SystemPath, LoadFailure);
    if (!System)
    {
        return LoadFailure;
    }

    TArray<FString> AvailableEmitters;
    FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, AvailableEmitters);
    if (!Handle)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetArrayField(TEXT("availableEmitters"), ToJsonStringArray(AvailableEmitters));
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NIAGARA_EMITTER_HANDLE_NOT_FOUND"),
            FString::Printf(TEXT("System '%s' has no emitter named '%s'"), *SystemPath, *EmitterName), D);
    }

    FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
    if (!Data)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_EMITTER_DATA_MISSING"),
            FString::Printf(TEXT("Emitter '%s' exposes no versioned emitter data"), *EmitterName));
    }

    const FVersionedNiagaraEmitter Instance = Handle->GetInstance();
    const FString UniqueName = Instance.Emitter ? Instance.Emitter->GetUniqueEmitterName() : FString();

    TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
    TArray<FString> Unapplied;
    int32 RequestedCount = 0;

    if (Instance.Emitter)
    {
        Instance.Emitter->Modify();
    }
    System->Modify();

    // -- Direct emitter-data properties (verified by read-back) --------------
    bool bTmp = false;
    if (Params->TryGetBoolField(TEXT("localSpace"), bTmp))
    {
        ++RequestedCount;
        Data->bLocalSpace = bTmp;
        if (Data->bLocalSpace == bTmp) Applied->SetBoolField(TEXT("localSpace"), bTmp);
        else Unapplied.Add(TEXT("localSpace: value did not stick after write"));
    }
    if (Params->TryGetBoolField(TEXT("determinism"), bTmp))
    {
        ++RequestedCount;
        Data->bDeterminism = bTmp;
        if (Data->bDeterminism == bTmp) Applied->SetBoolField(TEXT("determinism"), bTmp);
        else Unapplied.Add(TEXT("determinism: value did not stick after write"));
    }
    if (Params->TryGetBoolField(TEXT("enabled"), bTmp))
    {
        ++RequestedCount;
        Handle->SetIsEnabled(bTmp, *System, /*bRecompileIfChanged=*/false);
        if (Handle->GetIsEnabled() == bTmp) Applied->SetBoolField(TEXT("enabled"), bTmp);
        else Unapplied.Add(TEXT("enabled: SetIsEnabled did not change the handle state"));
    }

    double NumTmp = 0.0;
    if (Params->TryGetNumberField(TEXT("randomSeed"), NumTmp))
    {
        ++RequestedCount;
        Data->RandomSeed = static_cast<int32>(NumTmp);
        Applied->SetNumberField(TEXT("randomSeed"), Data->RandomSeed);
    }

    FString StrTmp;
    if (Params->TryGetStringField(TEXT("simTarget"), StrTmp))
    {
        ++RequestedCount;
        const FString Key = NormalizeKey(StrTmp);
        if (Key == TEXT("cpu") || Key == TEXT("cpusim"))
        {
            Data->SimTarget = ENiagaraSimTarget::CPUSim;
            Applied->SetStringField(TEXT("simTarget"), TEXT("CPU"));
        }
        else if (Key == TEXT("gpu") || Key == TEXT("gpucomputesim"))
        {
            Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
            Applied->SetStringField(TEXT("simTarget"), TEXT("GPU"));
        }
        else
        {
            Unapplied.Add(FString::Printf(TEXT("simTarget: '%s' is not one of CPU|GPU"), *StrTmp));
        }
    }

    if (Params->TryGetStringField(TEXT("boundsMode"), StrTmp))
    {
        ++RequestedCount;
        const int64 Resolved = EnumFromString(StaticEnum<ENiagaraEmitterCalculateBoundMode>(), StrTmp);
        if (Resolved == INDEX_NONE)
        {
            Unapplied.Add(FString::Printf(TEXT("boundsMode: '%s' is not a valid ENiagaraEmitterCalculateBoundMode entry"), *StrTmp));
        }
        else
        {
            Data->CalculateBoundsMode = static_cast<ENiagaraEmitterCalculateBoundMode>(Resolved);
            Applied->SetStringField(TEXT("boundsMode"),
                EnumToString(StaticEnum<ENiagaraEmitterCalculateBoundMode>(), static_cast<int64>(Data->CalculateBoundsMode)));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* BoundsArr = nullptr;
    if (Params->TryGetArrayField(TEXT("fixedBounds"), BoundsArr) && BoundsArr)
    {
        ++RequestedCount;
        if (BoundsArr->Num() < 6)
        {
            Unapplied.Add(TEXT("fixedBounds: expected [minX,minY,minZ,maxX,maxY,maxZ]"));
        }
        else
        {
            const FVector Min((*BoundsArr)[0]->AsNumber(), (*BoundsArr)[1]->AsNumber(), (*BoundsArr)[2]->AsNumber());
            const FVector Max((*BoundsArr)[3]->AsNumber(), (*BoundsArr)[4]->AsNumber(), (*BoundsArr)[5]->AsNumber());
            Data->FixedBounds = FBox(Min, Max);
            Applied->SetStringField(TEXT("fixedBounds"), Data->FixedBounds.ToString());
        }
    }

    // -- Loop settings - these live as module inputs on "Emitter State" ------
    struct FLoopSpec { const TCHAR* ParamName; const TCHAR* InputKey; };
    static const FLoopSpec LoopSpecs[] = {
        { TEXT("loopBehavior"), TEXT("loopbehavior") },
        { TEXT("loopDuration"), TEXT("loopduration") },
        { TEXT("loopCount"),    TEXT("loopcount")    },
    };

    bool bTouchedGraph = false;
    for (const FLoopSpec& Spec : LoopSpecs)
    {
        if (!Params->HasField(Spec.ParamName))
        {
            continue;
        }
        ++RequestedCount;
        const TSharedPtr<FJsonValue> Value = Params->TryGetField(Spec.ParamName);

        TArray<FString> AvailableModules, AvailableInputs;
        FModuleInputTarget Target;
        if (!ResolveModuleInput(Data, UniqueName, FString(), Spec.InputKey, Target, AvailableModules, AvailableInputs))
        {
            Unapplied.Add(FString::Printf(
                TEXT("%s: no module input matching '%s' on emitter '%s' (does it have an Emitter State module?)"),
                Spec.ParamName, Spec.InputKey, *EmitterName));
            continue;
        }

        TSharedPtr<FJsonValue> NewValue;
        FString ApplyError;
        if (!ApplyModuleInput(Target, Value, NewValue, ApplyError))
        {
            Unapplied.Add(FString::Printf(TEXT("%s (%s): %s"), Spec.ParamName, *Target.ResolvedName, *ApplyError));
            continue;
        }
        if (Target.Kind == TEXT("staticSwitch"))
        {
            bTouchedGraph = true;
        }
        Applied->SetField(Spec.ParamName, NewValue);
    }

    if (RequestedCount == 0)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NO_PROPERTIES_REQUESTED"),
            TEXT("No mutable property was supplied. Provide at least one of: enabled, localSpace, simTarget, determinism, randomSeed, boundsMode, fixedBounds, loopBehavior, loopDuration, loopCount"));
    }

    // Persist whatever DID apply so the asset on disk matches the report.
    FString CompileError;
    bool bCompiled = true;
    if (bCompile && (bTouchedGraph || Applied->Values.Num() > 0))
    {
        bCompiled = CompileSystem(System, CompileError);
    }

    FString SaveError;
    const bool bSaved = SaveSystem(System, SystemPath, SaveError);

    if (Unapplied.Num() > 0)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetObjectField(TEXT("applied"), Applied);
        D->SetArrayField(TEXT("unapplied"), ToJsonStringArray(Unapplied));
        D->SetBoolField(TEXT("savedAppliedChanges"), bSaved);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("validation"), TEXT("NIAGARA_PROPERTY_NOT_APPLIED"),
            FString::Printf(TEXT("%d of %d requested properties could not be applied to emitter '%s'"),
                Unapplied.Num(), RequestedCount, *EmitterName), D);
    }

    if (!bSaved)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("NIAGARA_SAVE_FAILED"),
            FString::Printf(TEXT("Properties applied to '%s' but the system could not be saved: %s"), *EmitterName, *SaveError));
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("emitterName"), Handle->GetName().ToString());
    Meta->SetObjectField(TEXT("applied"), Applied);
    Meta->SetNumberField(TEXT("propertiesApplied"), Applied->Values.Num());
    Meta->SetBoolField(TEXT("compiled"), bCompile && bCompiled);
    if (bCompile && !bCompiled)
    {
        Meta->SetStringField(TEXT("compileWarning"), CompileError);
    }

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), SystemPath, Meta);
#endif // WITH_EDITOR
}

// ─────────────────────────────────────────────────────────────────────────────
// niagara_set_module_input  (v3.22.0)
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNiagaraCommands::HandleNiagaraSetModuleInput(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"), TEXT("NIAGARA_UNAVAILABLE"),
        TEXT("Niagara is not available in this build configuration"));
#else
    using namespace UnrealMCPNiagara;

    FString SystemPath, EmitterName, ModuleName, InputName;
    if (!Params->TryGetStringField(TEXT("systemPath"), SystemPath) || SystemPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SYSTEM_PATH"), TEXT("Required param 'systemPath' is missing or empty"));
    }
    if (!Params->TryGetStringField(TEXT("emitterName"), EmitterName) || EmitterName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_EMITTER_NAME"), TEXT("Required param 'emitterName' is missing or empty"));
    }
    if (!Params->TryGetStringField(TEXT("inputName"), InputName) || InputName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_INPUT_NAME"), TEXT("Required param 'inputName' is missing or empty"));
    }
    Params->TryGetStringField(TEXT("moduleName"), ModuleName); // optional - empty matches any module

    if (!Params->HasField(TEXT("value")))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_VALUE"), TEXT("Required param 'value' is missing"));
    }
    const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));

    bool bCompile = true;
    Params->TryGetBoolField(TEXT("compile"), bCompile);

    TSharedPtr<FJsonObject> LoadFailure;
    UNiagaraSystem* System = LoadSystem(SystemPath, LoadFailure);
    if (!System)
    {
        return LoadFailure;
    }

    TArray<FString> AvailableEmitters;
    FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, AvailableEmitters);
    if (!Handle)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetArrayField(TEXT("availableEmitters"), ToJsonStringArray(AvailableEmitters));
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NIAGARA_EMITTER_HANDLE_NOT_FOUND"),
            FString::Printf(TEXT("System '%s' has no emitter named '%s'"), *SystemPath, *EmitterName), D);
    }

    FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
    if (!Data)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("NIAGARA_EMITTER_DATA_MISSING"),
            FString::Printf(TEXT("Emitter '%s' exposes no versioned emitter data"), *EmitterName));
    }

    const FVersionedNiagaraEmitter Instance = Handle->GetInstance();
    const FString UniqueName = Instance.Emitter ? Instance.Emitter->GetUniqueEmitterName() : FString();

    TArray<FString> AvailableModules, AvailableInputs;
    FModuleInputTarget Target;
    const bool bResolved = ResolveModuleInput(
        Data, UniqueName, NormalizeKey(ModuleName), NormalizeKey(InputName),
        Target, AvailableModules, AvailableInputs);

    if (!bResolved)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("emitterName"), EmitterName);
        D->SetStringField(TEXT("moduleName"), ModuleName);
        D->SetStringField(TEXT("inputName"), InputName);
        D->SetArrayField(TEXT("availableModules"), ToJsonStringArray(AvailableModules));
        D->SetArrayField(TEXT("availableInputs"), ToJsonStringArray(AvailableInputs));
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("NIAGARA_MODULE_INPUT_NOT_FOUND"),
            FString::Printf(TEXT("No input '%s' on module '%s' of emitter '%s'. Call niagara_get_info(includeModuleInputs=true) or read details.availableInputs to pick a real name."),
                *InputName, ModuleName.IsEmpty() ? TEXT("<any>") : *ModuleName, *EmitterName), D);
    }

    if (Instance.Emitter)
    {
        Instance.Emitter->Modify();
    }
    System->Modify();

    TSharedPtr<FJsonValue> NewValue;
    FString ApplyError;
    if (!ApplyModuleInput(Target, Value, NewValue, ApplyError))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("resolvedName"), Target.ResolvedName);
        D->SetStringField(TEXT("kind"), Target.Kind);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("validation"), TEXT("NIAGARA_MODULE_INPUT_WRITE_FAILED"),
            FString::Printf(TEXT("Could not write '%s': %s"), *Target.ResolvedName, *ApplyError), D);
    }

    FString CompileError;
    bool bCompiled = true;
    if (bCompile)
    {
        bCompiled = CompileSystem(System, CompileError);
    }

    FString SaveError;
    if (!SaveSystem(System, SystemPath, SaveError))
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("NIAGARA_SAVE_FAILED"),
            FString::Printf(TEXT("Input '%s' was written but the system could not be saved: %s"),
                *Target.ResolvedName, *SaveError));
    }

    UE_LOG(LogUnrealMCPNiagara, Log, TEXT("niagara_set_module_input: %s / %s / %s = (%s)"),
        *SystemPath, *EmitterName, *Target.ResolvedName, *Target.Kind);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("emitterName"), Handle->GetName().ToString());
    Meta->SetStringField(TEXT("resolvedName"), Target.ResolvedName);
    Meta->SetStringField(TEXT("kind"), Target.Kind);
    Meta->SetField(TEXT("newValue"), NewValue);
    Meta->SetBoolField(TEXT("compiled"), bCompile && bCompiled);
    if (bCompile && !bCompiled)
    {
        Meta->SetStringField(TEXT("compileWarning"), CompileError);
    }

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), SystemPath, Meta);
#endif // WITH_EDITOR
}
