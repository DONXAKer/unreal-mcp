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
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"

// Factories / asset tools
#include "Factories/AnimBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

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
    bool SplitPackagePath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
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
    if (CommandType == TEXT("create_animation_blueprint"))   return HandleCreateAnimationBlueprint(Params);
    if (CommandType == TEXT("set_anim_skeleton"))            return HandleSetAnimSkeleton(Params);
    if (CommandType == TEXT("add_state_machine"))            return HandleAddStateMachine(Params);
    if (CommandType == TEXT("add_anim_state"))               return HandleAddAnimState(Params);
    if (CommandType == TEXT("add_anim_transition"))          return HandleAddAnimTransition(Params);
    if (CommandType == TEXT("add_play_anim_node"))           return HandleAddPlayAnimNode(Params);
    if (CommandType == TEXT("add_blend_space_player_node"))  return HandleAddBlendSpacePlayerNode(Params);

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

    // Optionally seed BoundGraph with a SequencePlayer driving a result pin.
    // NOTE: We do NOT wire up the result pin here — that's a deeper graph-edit
    // operation; users can call add_play_anim_node later if they need explicit
    // wiring. Here we just drop the player into the state graph for visibility.
    bool bSeededPlayer = false;
    if (!AnimAssetPath.IsEmpty() && StateNode->BoundGraph)
    {
        if (UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AnimAssetPath))
        {
            UAnimGraphNode_SequencePlayer* Player = NewObject<UAnimGraphNode_SequencePlayer>(StateNode->BoundGraph);
            Player->CreateNewGuid();
            Player->Node.SetSequence(Seq);
            StateNode->BoundGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
            Player->PostPlacedNewNode();
            Player->AllocateDefaultPins();
            bSeededPlayer = true;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("state_machine_name"), StateMachineName);
    Meta->SetStringField(TEXT("state_name"), StateName);
    Meta->SetBoolField(TEXT("seeded_player"), bSeededPlayer);
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
    UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
    if (!AnimGraph)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("ANIMGRAPH_NOT_FOUND"),
            TEXT("Could not locate the AnimGraph inside the AnimBP"));
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

    UAnimGraphNode_SequencePlayer* Player = NewObject<UAnimGraphNode_SequencePlayer>(AnimGraph);
    Player->CreateNewGuid();
    Player->NodePosX = static_cast<int32>(Pos.X);
    Player->NodePosY = static_cast<int32>(Pos.Y);
    // UE 5.7: setters exist on FAnimNode_SequencePlayer (ENGINE_API).
    Player->Node.SetSequence(Seq);
    Player->Node.SetLoopAnimation(bLoop);
    Player->Node.SetPlayRate(PlayRate);
    AnimGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Player->PostPlacedNewNode();
    Player->AllocateDefaultPins();

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("animation_asset_path"), AnimAssetPath);
    Meta->SetBoolField(TEXT("loop"), bLoop);
    Meta->SetNumberField(TEXT("play_rate"), PlayRate);
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
    UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
    if (!AnimGraph)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("ANIMGRAPH_NOT_FOUND"),
            TEXT("Could not locate the AnimGraph inside the AnimBP"));
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

    UAnimGraphNode_BlendSpacePlayer* Player = NewObject<UAnimGraphNode_BlendSpacePlayer>(AnimGraph);
    Player->CreateNewGuid();
    Player->NodePosX = static_cast<int32>(Pos.X);
    Player->NodePosY = static_cast<int32>(Pos.Y);
    // UE 5.7: setters exist on FAnimNode_BlendSpacePlayer (ANIMGRAPHRUNTIME_API).
    // Note: BlendSpacePlayer uses SetLoop, not SetLoopAnimation.
    Player->Node.SetBlendSpace(BlendSpace);
    AnimGraph->AddNode(Player, /*bUserAction=*/true, /*bSelectNewNode=*/false);
    Player->PostPlacedNewNode();
    Player->AllocateDefaultPins();

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
    AnimBP->GetPackage()->MarkPackageDirty();
    const FString AssetPath = AnimBP->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("blend_space_path"), BlendSpacePath);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}
