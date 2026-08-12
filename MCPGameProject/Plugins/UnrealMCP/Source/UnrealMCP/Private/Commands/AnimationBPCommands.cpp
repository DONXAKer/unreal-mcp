#include "Commands/AnimationBPCommands.h"
#include "Commands/AssetCommonUtils.h"

// Animation runtime
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"

// Animation editor (AnimGraph module)
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"

// Factories / asset tools
#include "Factories/AnimBlueprintFactory.h"
// MCP-ANIM-001: импорт AnimSequence из FBX
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "AutomatedAssetImportData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h" // GEditor->PlayWorld (FIX-ANIM-MCP-001 PIE guard)

// JSON / engine
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

// ─────────────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Split "/Game/Path/AssetName" into ("/Game/Path", "AssetName").
    bool SplitAnimAssetPath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
    {
        int32 LastSlash = INDEX_NONE;
        if (!InPath.FindLastChar(TCHAR('/'), LastSlash) || LastSlash <= 0)
        {
            return false;
        }
        OutPackagePath = InPath.Left(LastSlash);
        OutAssetName = InPath.Mid(LastSlash + 1);
        return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
    }

    // Load an Animation Blueprint by short name or by /Game/... path.
    UAnimBlueprint* LoadAnimBlueprintByName(const FString& BlueprintName)
    {
        // Full path first.
        if (BlueprintName.StartsWith(TEXT("/")))
        {
            if (UAnimBlueprint* BP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintName)))
            {
                return BP;
            }
        }

        // Common search directories for AnimBPs.
        static const TArray<FString> SearchDirs = {
            TEXT("/Game/Animations/"),
            TEXT("/Game/Animation/"),
            TEXT("/Game/Characters/"),
            TEXT("/Game/Blueprints/"),
            TEXT("/Game/"),
        };

        for (const FString& Dir : SearchDirs)
        {
            const FString ObjectPath = Dir + BlueprintName + TEXT(".") + BlueprintName;
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
            {
                if (UAnimBlueprint* BP = Cast<UAnimBlueprint>(Asset))
                {
                    return BP;
                }
            }
        }

        // Direct path attempt without "." duplication.
        if (UObject* Asset = UEditorAssetLibrary::LoadAsset(BlueprintName))
        {
            if (UAnimBlueprint* BP = Cast<UAnimBlueprint>(Asset))
            {
                return BP;
            }
        }
        return nullptr;
    }

    // Find the (single) AnimGraph inside an Animation Blueprint.
    UAnimationGraph* FindAnimGraph(UAnimBlueprint* AnimBP)
    {
        if (!AnimBP) return nullptr;
        for (UEdGraph* Graph : AnimBP->FunctionGraphs)
        {
            if (UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph))
            {
                return AnimGraph;
            }
        }
        return nullptr;
    }

    // Find a state-machine sub-graph by name inside an AnimBP.
    UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBP, const FString& StateMachineName)
    {
        UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
        if (!AnimGraph) return nullptr;

        for (UEdGraphNode* Node : AnimGraph->Nodes)
        {
            if (UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node))
            {
                if (SM->EditorStateMachineGraph &&
                    SM->EditorStateMachineGraph->GetFName() == FName(*StateMachineName))
                {
                    return SM->EditorStateMachineGraph;
                }
            }
        }
        return nullptr;
    }

    // Find a UAnimStateNode by name inside a state machine.
    UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
    {
        if (!SMGraph) return nullptr;
        for (UEdGraphNode* Node : SMGraph->Nodes)
        {
            if (UAnimStateNode* SN = Cast<UAnimStateNode>(Node))
            {
                if (SN->GetStateName() == StateName)
                {
                    return SN;
                }
            }
        }
        return nullptr;
    }

    // Find the first pose pin of a given direction on an AnimGraph node.
    //
    // Pose pins are struct pins of type FPoseLink / FComponentSpacePoseLink; the
    // schema knows how to recognise them, so we ask it instead of matching pin
    // names (those differ between node classes: "Pose", "Result", "Output Animation Pose").
    UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        if (!Node) return nullptr;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType))
            {
                return Pin;
            }
        }
        return nullptr;
    }

    // Connect a pose-producing node to the state's "Output Animation Pose".
    //
    // MCP-ANIM-001: без этого состояние выглядело наполненным, но отдавало ПУСТУЮ
    // позу, а команда рапортовала успех — silent success, прямо запрещённый
    // design-rule соседнего NiagaraCommands.h. Результат перечитывается по
    // LinkedTo: «схема не отказала» и «связь есть» — разные вещи.
    //
    // OutReplacedPreviousLink: "Output Animation Pose" — single-link входной пин;
    // TryCreateConnection на занятом single-link пине молча РВЁТ существующую
    // связь и создаёт новую, оставляя прежний узел-плеер висячим в графе. Вызов
    // репортует это явно, а не тихо — второй add_play_anim_node/add_anim_state
    // на то же состояние раньше молча съедал первый плеер.
    bool WirePoseToStateResult(UEdGraph* BoundGraph, UEdGraphNode* PoseNode, FString& OutErrorCode,
                                bool& OutReplacedPreviousLink)
    {
        OutReplacedPreviousLink = false;

        if (!BoundGraph || !PoseNode)
        {
            OutErrorCode = TEXT("STATE_RESULT_WIRE_NO_GRAPH");
            return false;
        }

        UAnimGraphNode_StateResult* ResultNode = nullptr;
        for (UEdGraphNode* Node : BoundGraph->Nodes)
        {
            if (UAnimGraphNode_StateResult* Candidate = Cast<UAnimGraphNode_StateResult>(Node))
            {
                ResultNode = Candidate;
                break;
            }
        }
        if (!ResultNode)
        {
            OutErrorCode = TEXT("STATE_RESULT_NODE_NOT_FOUND");
            return false;
        }

        UEdGraphPin* OutPose = FindPosePin(PoseNode, EGPD_Output);
        UEdGraphPin* InPose  = FindPosePin(ResultNode, EGPD_Input);
        if (!OutPose || !InPose)
        {
            OutErrorCode = TEXT("POSE_PIN_NOT_FOUND");
            return false;
        }

        OutReplacedPreviousLink = (InPose->LinkedTo.Num() > 0) && !InPose->LinkedTo.Contains(OutPose);

        const UEdGraphSchema* Schema = BoundGraph->GetSchema();
        if (!Schema || !Schema->TryCreateConnection(OutPose, InPose))
        {
            OutErrorCode = TEXT("POSE_CONNECTION_REFUSED");
            return false;
        }

        if (!OutPose->LinkedTo.Contains(InPose))
        {
            OutErrorCode = TEXT("POSE_CONNECTION_NOT_PERSISTED");
            return false;
        }
        return true;
    }

    // Куда класть ноду плеера: в BoundGraph состояния (если задано state_name)
    // или в корневой AnimGraph. Ошибку формируем здесь же — вызывающему остаётся
    // только вернуть её.
    bool ResolveTargetGraph(UAnimBlueprint* AnimBP,
                            const FString& StateMachineName,
                            const FString& StateName,
                            UEdGraph*& OutGraph,
                            TSharedPtr<FJsonObject>& OutFailure)
    {
        OutGraph = nullptr;

        if (StateName.IsEmpty())
        {
            // Зеркало проверки ниже: state_machine_name БЕЗ state_name раньше
            // тихо игнорировался, и нода уезжала в корневой AnimGraph — вызывающий
            // явно указал состояние (пусть и не назвал конкретный state), а получал
            // успех с meta.target_graph:"AnimGraph" и wired_to_result:false,
            // ничего не объясняющий.
            if (!StateMachineName.IsEmpty())
            {
                OutFailure = FAssetCommonUtils::MakeFailureResponse(
                    TEXT("user"), TEXT("MISSING_STATE_NAME"),
                    TEXT("Param 'state_machine_name' was given without 'state_name' — "
                         "specify which state to target, or omit state_machine_name to use the root AnimGraph"));
                return false;
            }
            OutGraph = FindAnimGraph(AnimBP);
            if (!OutGraph)
            {
                OutFailure = FAssetCommonUtils::MakeFailureResponse(
                    TEXT("ue_internal"), TEXT("ANIMGRAPH_NOT_FOUND"),
                    TEXT("Could not locate the AnimGraph inside the AnimBP"));
                return false;
            }
            return true;
        }

        if (StateMachineName.IsEmpty())
        {
            OutFailure = FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("MISSING_STATE_MACHINE_NAME"),
                TEXT("Param 'state_name' requires 'state_machine_name' to locate the state"));
            return false;
        }

        UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName);
        if (!SMGraph)
        {
            OutFailure = FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("STATE_MACHINE_NOT_FOUND"),
                FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName));
            return false;
        }

        UAnimStateNode* StateNode = FindStateNode(SMGraph, StateName);
        if (!StateNode || !StateNode->BoundGraph)
        {
            OutFailure = FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("STATE_NOT_FOUND"),
                FString::Printf(TEXT("State '%s' not found in state machine '%s'"),
                    *StateName, *StateMachineName));
            return false;
        }

        OutGraph = StateNode->BoundGraph;
        return true;
    }

    // Parse node_position param: [x, y]. Returns FVector2D(0,0) when missing/invalid.
    FVector2D ParseNodePosition(const TSharedPtr<FJsonObject>& Params)
    {
        if (!Params.IsValid()) return FVector2D::ZeroVector;
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (Params->TryGetArrayField(TEXT("node_position"), Arr) && Arr && Arr->Num() >= 2)
        {
            return FVector2D((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber());
        }
        return FVector2D::ZeroVector;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FAnimationBPCommands
// ─────────────────────────────────────────────────────────────────────────────

FAnimationBPCommands::FAnimationBPCommands()
{
}

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Every command in this dispatcher mutates a UAnimBlueprint asset (compiles
    // + saves it) or imports a new UAnimSequence — none of them are read-only.
    // During an active PIE session these editor-only operations either silently
    // no-op or leave the asset half-mutated with a misleading error blaming the
    // source file (see root CLAUDE.md "Симптом, который уводит в сторону").
    if (FAssetCommonUtils::IsPieActive())
    {
        return FAssetCommonUtils::MakePieActiveFailure(
            FString::Printf(TEXT("AnimBP command '%s'"), *CommandType));
    }

    if (CommandType == TEXT("create_animation_blueprint"))   return HandleCreateAnimationBlueprint(Params);
    if (CommandType == TEXT("set_anim_skeleton"))            return HandleSetAnimSkeleton(Params);
    if (CommandType == TEXT("add_state_machine"))            return HandleAddStateMachine(Params);
    if (CommandType == TEXT("add_anim_state"))               return HandleAddAnimState(Params);
    if (CommandType == TEXT("add_anim_transition"))          return HandleAddAnimTransition(Params);
    if (CommandType == TEXT("add_play_anim_node"))           return HandleAddPlayAnimNode(Params);
    if (CommandType == TEXT("add_blend_space_player_node"))  return HandleAddBlendSpacePlayerNode(Params);
    // anim_sequence_import (v3.26.0) is the subject-first alias for the same
    // handler — see unreal-mcp/CLAUDE.md naming convention. import_anim_sequence
    // stays forever; both names route here.
    if (CommandType == TEXT("import_anim_sequence") ||
        CommandType == TEXT("anim_sequence_import"))         return HandleImportAnimSequence(Params);

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_ANIMBP_COMMAND"),
        FString::Printf(TEXT("AnimationBP category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// create_animation_blueprint
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleCreateAnimationBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString Name;
    if (!Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_NAME"), TEXT("Required param 'name' is missing or empty"));
    }

    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath) || SkeletonPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SKELETON_PATH"),
            TEXT("Required param 'skeleton_path' is missing or empty"));
    }

    FString ParentClassPath = TEXT("/Script/Engine.AnimInstance");
    Params->TryGetStringField(TEXT("parent_class_path"), ParentClassPath);

    FString PackagePath = TEXT("/Game/Animations");
    Params->TryGetStringField(TEXT("package_path"), PackagePath);

    // Resolve Skeleton.
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("skeleton_path"), SkeletonPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SKELETON_NOT_FOUND"),
            FString::Printf(TEXT("USkeleton not found at '%s'"), *SkeletonPath), D);
    }

    // Resolve parent class (must inherit UAnimInstance).
    UClass* ParentClass = LoadClass<UAnimInstance>(nullptr, *ParentClassPath);
    if (!ParentClass)
    {
        // LoadClass needs the "_C" suffix for BP-generated classes — accept that.
        ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
    }
    if (!ParentClass || !ParentClass->IsChildOf(UAnimInstance::StaticClass()))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("parent_class_path"), ParentClassPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_PARENT_CLASS"),
            FString::Printf(TEXT("Parent class '%s' not found or not a UAnimInstance subclass"), *ParentClassPath), D);
    }

    // Idempotency.
    const FString AssetPath = PackagePath + TEXT("/") + Name;
    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);
    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));
    if (bOverwrite && UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        UEditorAssetLibrary::DeleteAsset(AssetPath);
    }

    // Create the asset via AssetTools + UAnimBlueprintFactory.
    UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass = ParentClass;
    Factory->TargetSkeleton = Skeleton;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    IAssetTools& AssetTools = AssetToolsModule.Get();

    UObject* Created = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Created);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("ANIMBP_CREATE_FAILED"),
            FString::Printf(TEXT("AssetTools.CreateAsset returned null/non-UAnimBlueprint for '%s'"), *AssetPath));
    }

    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("skeleton_path"), SkeletonPath);
    Meta->SetStringField(TEXT("parent_class"), ParentClassPath);
    return FAssetCommonUtils::MakeSuccessResponse(
        bOverwrite ? TEXT("overwritten") : TEXT("created"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_anim_skeleton
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleSetAnimSkeleton(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_BLUEPRINT_NAME"),
            TEXT("Required param 'blueprint_name' is missing or empty"));
    }
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath) || SkeletonPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SKELETON_PATH"),
            TEXT("Required param 'skeleton_path' is missing or empty"));
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("blueprint_name"), BlueprintName);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName), D);
    }

    USkeleton* NewSkel = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!NewSkel)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("skeleton_path"), SkeletonPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SKELETON_NOT_FOUND"),
            FString::Printf(TEXT("USkeleton not found at '%s'"), *SkeletonPath), D);
    }

    AnimBP->TargetSkeleton = NewSkel;
    FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();

    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("skeleton_path"), SkeletonPath);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_state_machine
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleAddStateMachine(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_BLUEPRINT_NAME"),
            TEXT("Required param 'blueprint_name' is missing or empty"));
    }
    FString StateMachineName;
    if (!Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_STATE_MACHINE_NAME"),
            TEXT("Required param 'state_machine_name' is missing or empty"));
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName));
    }

    UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
    if (!AnimGraph)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("ANIMGRAPH_NOT_FOUND"),
            TEXT("Could not locate the AnimGraph (UAnimationGraph) inside the AnimBP"));
    }

    const FVector2D Pos = ParseNodePosition(Params);

    // Create the SM node + delegate sub-graph creation to PostPlacedNewNode.
    UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
    SMNode->CreateNewGuid();
    SMNode->NodePosX = static_cast<int32>(Pos.X);
    SMNode->NodePosY = static_cast<int32>(Pos.Y);
    AnimGraph->AddNode(SMNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    SMNode->PostPlacedNewNode();
    SMNode->AllocateDefaultPins();

    // Rename the auto-created EditorStateMachineGraph to the requested name.
    if (SMNode->EditorStateMachineGraph)
    {
        FBlueprintEditorUtils::RenameGraph(SMNode->EditorStateMachineGraph, StateMachineName);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    // FIX-ANIM-MCP-001: без компиляции здесь UAnimBlueprintGeneratedClass
    // остаётся старым — тот же класс бага, что задокументирован в корневом
    // CLAUDE.md про правку Defaults/графа Blueprint'а через MCP. Немедленный
    // pie_start спавнил юнита со стухшим классом (state machine пустая), а
    // команда честно отчиталась ok:true.
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("state_machine_name"), StateMachineName);
    Meta->SetStringField(TEXT("anim_graph"), AnimGraph->GetName());
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_anim_state
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleAddAnimState(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName, StateMachineName, StateName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("state_name"), StateName) || StateName.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_REQUIRED_PARAMS"),
            TEXT("Required params: blueprint_name, state_machine_name, state_name"));
    }

    FString AnimAssetPath;
    Params->TryGetStringField(TEXT("animation_asset_path"), AnimAssetPath);

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName));
    }

    UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName);
    if (!SMGraph)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("STATE_MACHINE_NOT_FOUND"),
            FString::Printf(TEXT("State machine '%s' not found in AnimBP '%s'"),
                *StateMachineName, *BlueprintName));
    }

    const FVector2D Pos = ParseNodePosition(Params);

    // Create the state node + let PostPlacedNewNode build its BoundGraph.
    UAnimStateNode* StateNode = NewObject<UAnimStateNode>(SMGraph);
    StateNode->CreateNewGuid();
    StateNode->NodePosX = static_cast<int32>(Pos.X);
    StateNode->NodePosY = static_cast<int32>(Pos.Y);
    SMGraph->AddNode(StateNode, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    StateNode->PostPlacedNewNode();
    StateNode->AllocateDefaultPins();

    // Rename BoundGraph to the requested state name.
    if (StateNode->BoundGraph)
    {
        FBlueprintEditorUtils::RenameGraph(StateNode->BoundGraph, StateName);
    }

    // Seed BoundGraph with a SequencePlayer AND wire it to "Output Animation Pose".
    //
    // MCP-ANIM-001: раньше плеер просто клался в граф без соединения, и состояние
    // отдавало пустую позу — при этом команда возвращала ok с seeded_player:true.
    // Теперь несоединённого плеера не бывает: либо связь есть, либо ok:false.
    //
    // FIX-ANIM-MCP-001: StateNode уже добавлен в SMGraph (выше) ДО этой проверки —
    // ранний return здесь раньше оставлял "призрачное" состояние в графе: не
    // сохранённое (SaveAsset ниже не вызван), но живущее в загруженном в память
    // UAnimBlueprint, пока он не перезагружен с диска. Повторный вызов с верным
    // animation_asset_path создавал ВТОРОЕ состояние ("Idle_1"), а FindStateNode
    // по имени продолжал находить первое, мёртвое. Откатываем StateNode на любом
    // раннем выходе, а не просто пропускаем сохранение.
    bool bSeededPlayer = false;
    bool bWiredToResult = false;
    bool bReplacedPreviousLink = false;
    if (!AnimAssetPath.IsEmpty() && StateNode->BoundGraph)
    {
        UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AnimAssetPath);
        if (!Seq)
        {
            SMGraph->RemoveNode(StateNode);
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("animation_asset_path"), AnimAssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"), TEXT("ANIM_SEQUENCE_NOT_FOUND"),
                FString::Printf(TEXT("UAnimSequence not found at '%s'"), *AnimAssetPath), D);
        }

        UAnimGraphNode_SequencePlayer* Player = NewObject<UAnimGraphNode_SequencePlayer>(StateNode->BoundGraph);
        Player->CreateNewGuid();
        Player->Node.SetSequence(Seq);
        StateNode->BoundGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
        Player->PostPlacedNewNode();
        Player->AllocateDefaultPins();
        bSeededPlayer = true;

        FString WireError;
        bWiredToResult = WirePoseToStateResult(StateNode->BoundGraph, Player, WireError, bReplacedPreviousLink);
        if (!bWiredToResult)
        {
            SMGraph->RemoveNode(StateNode);
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("state_name"), StateName);
            D->SetStringField(TEXT("animation_asset_path"), AnimAssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"), WireError,
                FString::Printf(
                    TEXT("State '%s' was created and seeded, but the player could not be connected ")
                    TEXT("to Output Animation Pose — the state would return an empty pose"),
                    *StateName),
                D);
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    // See HandleAddStateMachine above: without this the state machine looks
    // populated in-editor but the compiled class the game actually runs is stale.
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("state_machine_name"), StateMachineName);
    Meta->SetStringField(TEXT("state_name"), StateName);
    Meta->SetBoolField(TEXT("seeded_player"), bSeededPlayer);
    if (bSeededPlayer)
    {
        Meta->SetBoolField(TEXT("replaced_previous_link"), bReplacedPreviousLink);
    }
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_anim_transition
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleAddAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName, StateMachineName, FromState, ToState;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName) || StateMachineName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("from_state"), FromState) || FromState.IsEmpty() ||
        !Params->TryGetStringField(TEXT("to_state"), ToState) || ToState.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_REQUIRED_PARAMS"),
            TEXT("Required params: blueprint_name, state_machine_name, from_state, to_state"));
    }
    int32 PriorityOrder = 1;
    {
        double Tmp;
        if (Params->TryGetNumberField(TEXT("priority_order"), Tmp)) { PriorityOrder = (int32)Tmp; }
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName));
    }
    UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName);
    if (!SMGraph)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("STATE_MACHINE_NOT_FOUND"),
            FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName));
    }
    UAnimStateNode* From = FindStateNode(SMGraph, FromState);
    UAnimStateNode* To   = FindStateNode(SMGraph, ToState);
    if (!From || !To)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetBoolField(TEXT("from_found"), From != nullptr);
        D->SetBoolField(TEXT("to_found"), To != nullptr);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("STATE_NOT_FOUND"),
            FString::Printf(TEXT("State(s) not found: from='%s', to='%s'"), *FromState, *ToState), D);
    }

    UAnimStateTransitionNode* Trans = NewObject<UAnimStateTransitionNode>(SMGraph);
    Trans->CreateNewGuid();
    Trans->Bidirectional = false;
    Trans->PriorityOrder = PriorityOrder;
    SMGraph->AddNode(Trans, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Trans->PostPlacedNewNode();
    Trans->AllocateDefaultPins();
    // Wire the transition to the states (creates the visual arrow + pin links).
    Trans->CreateConnections(From, To);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    // See HandleAddStateMachine above: without this the compiled class the
    // game actually runs does not have the transition yet.
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("from_state"), FromState);
    Meta->SetStringField(TEXT("to_state"), ToState);
    Meta->SetNumberField(TEXT("priority_order"), PriorityOrder);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_play_anim_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleAddPlayAnimNode(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName, AnimAssetPath;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("animation_asset_path"), AnimAssetPath) || AnimAssetPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_REQUIRED_PARAMS"),
            TEXT("Required params: blueprint_name, animation_asset_path"));
    }
    bool bLoop = true;
    Params->TryGetBoolField(TEXT("loop"), bLoop);
    double PlayRateD = 1.0;
    Params->TryGetNumberField(TEXT("play_rate"), PlayRateD);
    const float PlayRate = static_cast<float>(PlayRateD);

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName));
    }
    // MCP-ANIM-001: опциональная запись ВНУТРЬ состояния. Раньше нода всегда
    // ложилась в КОРНЕВОЙ AnimGraph, параметра «в какое состояние» не было вовсе,
    // и подключить плеер внутрь состояния через MCP было невозможно в принципе.
    FString StateMachineName, StateName;
    Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName);
    Params->TryGetStringField(TEXT("state_name"), StateName);

    UEdGraph* TargetGraph = nullptr;
    TSharedPtr<FJsonObject> TargetFailure;
    if (!ResolveTargetGraph(AnimBP, StateMachineName, StateName, TargetGraph, TargetFailure))
    {
        return TargetFailure;
    }

    UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AnimAssetPath);
    if (!Seq)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("animation_asset_path"), AnimAssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIM_SEQUENCE_NOT_FOUND"),
            FString::Printf(TEXT("UAnimSequence not found at '%s'"), *AnimAssetPath), D);
    }

    const FVector2D Pos = ParseNodePosition(Params);

    UAnimGraphNode_SequencePlayer* Player = NewObject<UAnimGraphNode_SequencePlayer>(TargetGraph);
    Player->CreateNewGuid();
    Player->NodePosX = static_cast<int32>(Pos.X);
    Player->NodePosY = static_cast<int32>(Pos.Y);
    // UE 5.7: setters exist on FAnimNode_SequencePlayer (ENGINE_API).
    Player->Node.SetSequence(Seq);
    Player->Node.SetLoopAnimation(bLoop);
    Player->Node.SetPlayRate(PlayRate);
    TargetGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Player->PostPlacedNewNode();
    Player->AllocateDefaultPins();

    // В состоянии плеер обязан быть подключён к Output Animation Pose, иначе
    // состояние отдаёт пустую позу. В корневом AnimGraph подключать некуда —
    // там свой result-узел и своя композиция, это решает вызывающий.
    //
    // FIX-ANIM-MCP-001: Player уже добавлен в TargetGraph (AddNode выше) ДО
    // этой проверки — ранний return оставлял неподключённый узел висеть в
    // графе (не сохранённый, но живой в загруженном в память AnimBP). Откат
    // на отказе, а не просто пропуск сохранения.
    bool bWiredToResult = false;
    bool bReplacedPreviousLink = false;
    if (!StateName.IsEmpty())
    {
        FString WireError;
        bWiredToResult = WirePoseToStateResult(TargetGraph, Player, WireError, bReplacedPreviousLink);
        if (!bWiredToResult)
        {
            TargetGraph->RemoveNode(Player);
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("state_name"), StateName);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"), WireError,
                FString::Printf(TEXT("Player added to state '%s' but not connected to Output Animation Pose"),
                    *StateName), D);
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    // See HandleAddStateMachine above: without this the compiled class the
    // game actually runs does not have the new player node yet.
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("animation_asset_path"), AnimAssetPath);
    Meta->SetBoolField(TEXT("loop"), bLoop);
    Meta->SetNumberField(TEXT("play_rate"), PlayRate);
    Meta->SetStringField(TEXT("target_graph"), StateName.IsEmpty() ? TEXT("AnimGraph") : StateName);
    Meta->SetBoolField(TEXT("wired_to_result"), bWiredToResult);
    if (bWiredToResult)
    {
        Meta->SetBoolField(TEXT("replaced_previous_link"), bReplacedPreviousLink);
    }
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// add_blend_space_player_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleAddBlendSpacePlayerNode(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString BlueprintName, BlendSpacePath;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty() ||
        !Params->TryGetStringField(TEXT("blend_space_path"), BlendSpacePath) || BlendSpacePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_REQUIRED_PARAMS"),
            TEXT("Required params: blueprint_name, blend_space_path"));
    }

    UAnimBlueprint* AnimBP = LoadAnimBlueprintByName(BlueprintName);
    if (!AnimBP)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("ANIMBP_NOT_FOUND"),
            FString::Printf(TEXT("UAnimBlueprint not found for '%s'"), *BlueprintName));
    }
    // MCP-ANIM-001: как и у add_play_anim_node — опциональная запись в состояние.
    FString StateMachineName, StateName;
    Params->TryGetStringField(TEXT("state_machine_name"), StateMachineName);
    Params->TryGetStringField(TEXT("state_name"), StateName);

    UEdGraph* TargetGraph = nullptr;
    TSharedPtr<FJsonObject> TargetFailure;
    if (!ResolveTargetGraph(AnimBP, StateMachineName, StateName, TargetGraph, TargetFailure))
    {
        return TargetFailure;
    }

    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
    if (!BlendSpace)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("blend_space_path"), BlendSpacePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("BLEND_SPACE_NOT_FOUND"),
            FString::Printf(TEXT("UBlendSpace not found at '%s'"), *BlendSpacePath), D);
    }

    const FVector2D Pos = ParseNodePosition(Params);

    UAnimGraphNode_BlendSpacePlayer* Player = NewObject<UAnimGraphNode_BlendSpacePlayer>(TargetGraph);
    Player->CreateNewGuid();
    Player->NodePosX = static_cast<int32>(Pos.X);
    Player->NodePosY = static_cast<int32>(Pos.Y);
    // UE 5.7: setters exist on FAnimNode_BlendSpacePlayer (ANIMGRAPHRUNTIME_API).
    // Note: BlendSpacePlayer uses SetLoop, not SetLoopAnimation.
    Player->Node.SetBlendSpace(BlendSpace);
    TargetGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Player->PostPlacedNewNode();
    Player->AllocateDefaultPins();

    // FIX-ANIM-MCP-001: Player уже добавлен в TargetGraph — откат на отказе
    // вместо призрачного узла, тот же дефект, что у add_play_anim_node выше.
    bool bWiredToResult = false;
    bool bReplacedPreviousLink = false;
    if (!StateName.IsEmpty())
    {
        FString WireError;
        bWiredToResult = WirePoseToStateResult(TargetGraph, Player, WireError, bReplacedPreviousLink);
        if (!bWiredToResult)
        {
            TargetGraph->RemoveNode(Player);
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("state_name"), StateName);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"), WireError,
                FString::Printf(TEXT("Blend space player added to state '%s' but not connected to Output Animation Pose"),
                    *StateName), D);
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    // See HandleAddStateMachine above: without this the compiled class the
    // game actually runs does not have the new player node yet.
    FKismetEditorUtilities::CompileBlueprint(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("blend_space_path"), BlendSpacePath);
    Meta->SetStringField(TEXT("target_graph"), StateName.IsEmpty() ? TEXT("AnimGraph") : StateName);
    Meta->SetBoolField(TEXT("wired_to_result"), bWiredToResult);
    if (bWiredToResult)
    {
        Meta->SetBoolField(TEXT("replaced_previous_link"), bReplacedPreviousLink);
    }
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// -----------------------------------------------------------------------------
// import_anim_sequence
// -----------------------------------------------------------------------------

TSharedPtr<FJsonObject> FAnimationBPCommands::HandleImportAnimSequence(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString SourcePath;
    if (!Params->TryGetStringField(TEXT("sourcePath"), SourcePath) || SourcePath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SOURCE_PATH"),
            TEXT("Required param 'sourcePath' is missing or empty"));
    }

    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath) || SkeletonPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_SKELETON_PATH"),
            TEXT("Required param 'skeleton_path' is missing — an AnimSequence cannot exist without a skeleton"));
    }

    if (!FPaths::FileExists(SourcePath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("sourcePath"), SourcePath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("SOURCE_FILE_NOT_FOUND"),
            FString::Printf(TEXT("No file at source path '%s'"), *SourcePath), D);
    }

    const FString Ext = FPaths::GetExtension(SourcePath, /*bIncludeDot=*/false).ToLower();
    if (Ext != TEXT("fbx"))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("extension"), Ext);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("UNSUPPORTED_ANIM_FORMAT"),
            FString::Printf(TEXT("import_anim_sequence expects .fbx; got '.%s'"), *Ext), D);
    }

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("skeleton_path"), SkeletonPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("SKELETON_NOT_FOUND"),
            FString::Printf(TEXT("USkeleton not found at '%s'"), *SkeletonPath), D);
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);
    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }
    const bool bReplaceExisting = (Decision.Action == TEXT("overwrite") || Decision.Action == TEXT("update"));

    FString PackagePath, AssetName;
    if (!FAssetCommonUtils::SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split asset path '%s' into package + name"), *AssetPath), D);
    }

    // Interchange в UE 5.8 повторно входит в TaskGraph и роняет редактор на
    // RecursionGuard — тот же обход, что в import_static_mesh / import_skeletal_mesh.
    // RAII guard (FIX-ANIM-MCP-001): единственная строка восстановления после
    // вызова — старая форма этого кода — не срабатывает на раннем return/
    // исключении; деструктор срабатывает на любом пути.
    FScopedInterchangeFbxImportDisable InterchangeGuard;

    UFbxFactory* FbxFactory = NewObject<UFbxFactory>();
    // Скелет задаём фабрике ЗАРАНЕЕ: без него импорт анимации либо заводит новый
    // скелет, либо не даёт AnimSequence вовсе — то самое молчаливое «успешно».
    if (FbxFactory->ImportUI)
    {
        FbxFactory->ImportUI->Skeleton = Skeleton;
        FbxFactory->ImportUI->MeshTypeToImport = FBXIT_Animation;
        FbxFactory->ImportUI->bImportAsSkeletal = true;
        FbxFactory->ImportUI->bImportMesh = false;
        FbxFactory->ImportUI->bImportAnimations = true;
        FbxFactory->ImportUI->bImportMaterials = false;
        FbxFactory->ImportUI->bImportTextures = false;
        FbxFactory->ImportUI->bAutomatedImportShouldDetectType = false;
    }

    UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
    ImportData->DestinationPath  = PackagePath;
    ImportData->bReplaceExisting = bReplaceExisting;
    ImportData->bSkipReadOnly    = false;
    ImportData->Filenames.Add(SourcePath);
    ImportData->Factory = FbxFactory;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);

    // Перечитываем результат, а не верим вызову: ImportAssetsAutomated отдаёт
    // непустой массив и тогда, когда анимации в FBX не оказалось.
    UAnimSequence* AnimSeq = nullptr;
    for (UObject* Obj : Imported)
    {
        if (UAnimSequence* Candidate = Cast<UAnimSequence>(Obj))
        {
            AnimSeq = Candidate;
            break;
        }
    }
    if (!AnimSeq)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("sourcePath"), SourcePath);
        D->SetNumberField(TEXT("importedObjects"), Imported.Num());
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("ANIM_SEQUENCE_IMPORT_FAILED"),
            FString::Printf(TEXT("Import of '%s' produced no UAnimSequence — FBX may contain no animation stack"),
                *SourcePath), D);
    }

    // FIX-ANIM-MCP-001: rename-to-final-name, with a real rollback instead of
    // "delete then hope rename succeeds". Old shape: DeleteAsset(target) THEN
    // RenameAsset(imported -> target); if RenameAsset returned false, AnimSeq
    // was left pointing at the OLD (pre-rename) object — non-null — so the
    // `if (!AnimSeq)` guard right after never tripped, and a hard failure of
    // RenameAsset surfaced as {ok:true, status:"created"} with an asset that
    // still lived under its FBX-derived import name, not at AssetPath. On
    // overwrite that was worse: the pre-existing asset at AssetPath was
    // already deleted by the time RenameAsset failed, so retrying left
    // nothing at AssetPath at all.
    //
    // New shape: move the pre-existing target OUT OF THE WAY (rename to a
    // backup path) instead of deleting it outright. Only delete the backup
    // once the new asset is confirmed renamed into place; on any failure,
    // rename the backup back and clean up the orphaned import.
    FString BackupPath;
    bool bHasBackup = false;
    if (AnimSeq->GetName() != AssetName)
    {
        const FString NewObjPath = PackagePath / AssetName;

        if (bReplaceExisting && UEditorAssetLibrary::DoesAssetExist(NewObjPath))
        {
            BackupPath = PackagePath / (AssetName + TEXT("__mcp_import_backup"));
            if (UEditorAssetLibrary::DoesAssetExist(BackupPath))
            {
                // Leftover from a previous failed run — stale, safe to drop.
                UEditorAssetLibrary::DeleteAsset(BackupPath);
            }
            if (!UEditorAssetLibrary::RenameAsset(NewObjPath, BackupPath))
            {
                UEditorAssetLibrary::DeleteAsset(AnimSeq->GetPathName()); // best-effort cleanup of the fresh import
                TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
                D->SetStringField(TEXT("assetPath"), AssetPath);
                return FAssetCommonUtils::MakeFailureResponse(
                    TEXT("ue_internal"), TEXT("ANIM_SEQUENCE_OVERWRITE_PREP_FAILED"),
                    FString::Printf(TEXT("Could not move the existing asset at '%s' aside before overwrite — "
                                         "left untouched"), *AssetPath), D);
            }
            bHasBackup = true;
        }

        const FString ImportedPath = AnimSeq->GetPathName();
        if (!UEditorAssetLibrary::RenameAsset(ImportedPath, NewObjPath))
        {
            // RenameAsset failed: do NOT trust the old handle — it may or may
            // not still resolve, and either way it is not at AssetPath.
            AnimSeq = nullptr;
        }
        else
        {
            AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(NewObjPath));
        }

        if (!AnimSeq)
        {
            if (bHasBackup)
            {
                UEditorAssetLibrary::RenameAsset(BackupPath, NewObjPath); // restore, best-effort
            }
            TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("assetPath"), AssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"), TEXT("ANIM_SEQUENCE_RENAME_FAILED"),
                FString::Printf(TEXT("Imported animation could not be renamed to '%s'"), *AssetPath), D);
        }

        if (bHasBackup)
        {
            UEditorAssetLibrary::DeleteAsset(BackupPath);
        }
    }

    const int32 FrameCount = AnimSeq->GetNumberOfSampledKeys();
    const bool bSkeletonMatches = (AnimSeq->GetSkeleton() == Skeleton);

    // FIX-ANIM-MCP-001: раньше оба значения только клались в meta и никак не
    // влияли на вердикт — FBX с чужой иерархией или нулевой длиной давал
    // {ok:true, meta:{skeletonMatches:false, frameCount:0}}. Весь смысл
    // обязательного skeleton_path в том, чтобы этого не случилось.
    if (!bSkeletonMatches || FrameCount == 0)
    {
        // Ассет после неудачного вызова должен остаться в прежнем состоянии:
        // на overwrite — восстанавливаем backup; иначе — удаляем свежесозданный
        // брак, чтобы он не занимал AssetPath перед повторной попыткой.
        UEditorAssetLibrary::DeleteAsset(AssetPath);
        if (bHasBackup)
        {
            const FString NewObjPath = PackagePath / AssetName;
            UEditorAssetLibrary::RenameAsset(BackupPath, NewObjPath);
        }

        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        D->SetBoolField(TEXT("skeletonMatches"), bSkeletonMatches);
        D->SetNumberField(TEXT("frameCount"), FrameCount);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            bSkeletonMatches ? TEXT("ANIM_SEQUENCE_EMPTY") : TEXT("SKELETON_MISMATCH"),
            bSkeletonMatches
                ? FString::Printf(TEXT("Imported '%s' has zero sampled keys — FBX likely has no valid animation stack"),
                    *SourcePath)
                : FString::Printf(TEXT("Imported animation's skeleton does not match requested skeleton_path '%s'"),
                    *SkeletonPath),
            D);
    }

    AnimSeq->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("skeleton_path"), SkeletonPath);
    Meta->SetNumberField(TEXT("frameCount"), FrameCount);
    Meta->SetNumberField(TEXT("sequenceLength"), AnimSeq->GetPlayLength());
    Meta->SetBoolField(TEXT("skeletonMatches"), bSkeletonMatches);
    return FAssetCommonUtils::MakeSuccessResponse(
        bReplaceExisting ? TEXT("overwritten") : TEXT("created"), AssetPath, Meta);
}
