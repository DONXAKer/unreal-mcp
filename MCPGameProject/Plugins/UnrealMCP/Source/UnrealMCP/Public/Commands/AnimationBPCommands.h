#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Animation Blueprint MCP commands (Phase 3B — v1.16.0).
 *
 * Commands (7):
 *   - create_animation_blueprint      : create a new UAnimBlueprint asset bound to a USkeleton.
 *   - set_anim_skeleton               : retarget an existing UAnimBlueprint to a different USkeleton.
 *   - add_state_machine               : add a UAnimGraphNode_StateMachine to the AnimGraph.
 *   - add_anim_state                  : add a UAnimStateNode to an existing state machine,
 *                                       optionally seeded with a UAnimSequence player.
 *   - add_anim_transition             : add a UAnimStateTransitionNode between two states.
 *   - add_play_anim_node              : add a free-standing UAnimGraphNode_SequencePlayer to the AnimGraph.
 *   - add_blend_space_player_node     : add a UAnimGraphNode_BlendSpacePlayer to the AnimGraph.
 *
 * All responses follow the unified MCP Content Pipeline contract defined in
 * FAssetCommonUtils (ok, status, assetPath, meta / error).
 *
 * API choices (UE 5.7):
 *   - Use UAnimBlueprintFactory + IAssetTools::CreateAsset for asset creation.
 *   - AnimGraph discovery: search AnimBP->FunctionGraphs for the UEdGraph whose
 *     class is UAnimationGraph (the AnimGraph). Most AnimBPs have exactly one.
 *   - Node placement: AddNode(Node, true, false) + PostPlacedNewNode() so the
 *     engine's PostPlace logic creates BoundGraph / EditorStateMachineGraph etc.
 *   - Transitions: UAnimStateTransitionNode::CreateConnections(prev, next).
 *   - Sequence/BlendSpace players use FAnimNode_*Player::SetSequence/SetBlendSpace/
 *     SetLoopAnimation/SetLoop/SetPlayRate (UE 5.7 public ENGINE_API/
 *     ANIMGRAPHRUNTIME_API setters).
 */
class UNREALMCP_API FAnimationBPCommands
{
public:
    FAnimationBPCommands();

    /** Routes an incoming Animation BP command to the appropriate handler. */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * create_animation_blueprint
     * Params:
     *   - name                (string, required): asset name (without extension).
     *   - skeleton_path       (string, required): /Game/... path to a USkeleton.
     *   - parent_class_path   (string, optional): default "/Script/Engine.AnimInstance".
     *   - package_path        (string, optional): default "/Game/Animations".
     *   - ifExists            (string, optional): "skip" (default) | "overwrite".
     */
    TSharedPtr<FJsonObject> HandleCreateAnimationBlueprint(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_anim_skeleton
     * Params:
     *   - blueprint_name (string, required): short name or /Game/... path.
     *   - skeleton_path  (string, required): /Game/... path to a USkeleton.
     */
    TSharedPtr<FJsonObject> HandleSetAnimSkeleton(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_state_machine
     * Params:
     *   - blueprint_name      (string, required)
     *   - state_machine_name  (string, required): rename target for the SM sub-graph.
     *   - node_position       (array, optional): [x, y] graph coordinates.
     */
    TSharedPtr<FJsonObject> HandleAddStateMachine(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_anim_state
     * Params:
     *   - blueprint_name        (string, required)
     *   - state_machine_name    (string, required)
     *   - state_name            (string, required)
     *   - animation_asset_path  (string, optional): /Game/... path to a UAnimSequence;
     *                                               if set, seed BoundGraph with a player.
     *   - node_position         (array, optional)
     */
    TSharedPtr<FJsonObject> HandleAddAnimState(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_anim_transition
     * Params:
     *   - blueprint_name      (string, required)
     *   - state_machine_name  (string, required)
     *   - from_state          (string, required)
     *   - to_state            (string, required)
     *   - priority_order      (int, optional): default 1.
     */
    TSharedPtr<FJsonObject> HandleAddAnimTransition(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_play_anim_node
     * Params:
     *   - blueprint_name        (string, required)
     *   - animation_asset_path  (string, required): /Game/... path to a UAnimSequence.
     *   - loop                  (bool, optional): default true.
     *   - play_rate             (float, optional): default 1.0.
     *   - node_position         (array, optional)
     */
    TSharedPtr<FJsonObject> HandleAddPlayAnimNode(const TSharedPtr<FJsonObject>& Params);

    /**
     * add_blend_space_player_node
     * Params:
     *   - blueprint_name    (string, required)
     *   - blend_space_path  (string, required): /Game/... path to a UBlendSpace.
     *   - node_position     (array, optional)
     */
    TSharedPtr<FJsonObject> HandleAddBlendSpacePlayerNode(const TSharedPtr<FJsonObject>& Params);
};
