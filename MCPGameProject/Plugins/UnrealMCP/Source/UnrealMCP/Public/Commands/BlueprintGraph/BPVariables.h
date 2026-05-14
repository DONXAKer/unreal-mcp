#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Forward declarations
struct FEdGraphPinType;
struct FBPVariableDescription;

/**
 * Utility class for creating and managing Blueprint variables
 */
class UNREALMCP_API FBPVariables
{
public:
    /**
     * Creates a new variable in a Blueprint
     * @param Params JSON containing blueprint_name, variable_name, variable_type, default_value, is_public, tooltip, category
     * @return JSON with success and variable details
     */
    static TSharedPtr<FJsonObject> CreateVariable(const TSharedPtr<FJsonObject>& Params);

    /**
     * Modifies properties of an existing variable without deleting it
     * @param Params JSON containing blueprint_name, variable_name, and optional properties:
     *        is_blueprint_writable, is_public, is_editable_in_instance,
     *        tooltip, category, default_value, is_config, expose_on_spawn,
     *        var_name, var_type, friendly_name, replication
     * @return JSON with success and modified properties
     */
    static TSharedPtr<FJsonObject> SetVariableProperties(const TSharedPtr<FJsonObject>& Params);

    // ── Phase 1B (v1.11.0) — Variable lifecycle commands ─────────────────────

    /**
     * Renames a variable in the Blueprint using FBlueprintEditorUtils::RenameMemberVariable
     * Params: blueprint_name, old_name, new_name
     */
    static TSharedPtr<FJsonObject> RenameVariable(const TSharedPtr<FJsonObject>& Params);

    /**
     * Deletes a variable from the Blueprint using FBlueprintEditorUtils::RemoveMemberVariable
     * Params: blueprint_name, variable_name
     */
    static TSharedPtr<FJsonObject> DeleteVariable(const TSharedPtr<FJsonObject>& Params);

    /**
     * Sets the default value of a variable (string-form, parsed via ImportText on the CDO).
     * Params: blueprint_name, variable_name, default_value
     */
    static TSharedPtr<FJsonObject> SetVariableDefaultValue(const TSharedPtr<FJsonObject>& Params);

    /**
     * Read-only: returns the list of all NewVariables of the Blueprint.
     * Params: blueprint_name
     */
    static TSharedPtr<FJsonObject> ListVariables(const TSharedPtr<FJsonObject>& Params);

    /**
     * Flag-oriented update (instance_editable, expose_on_spawn, blueprint_read_only, category,
     * replication=None|Replicated|RepNotify). Subset of SetVariableProperties with
     * stricter, simpler semantics; does NOT replace the legacy command.
     * Params: blueprint_name, variable_name, + any of the optional flag fields above.
     */
    static TSharedPtr<FJsonObject> SetVariableFlags(const TSharedPtr<FJsonObject>& Params);

    /**
     * Sets the default value of a variable (JSON-value form, used internally by CreateVariable
     * and SetVariableProperties). Public so the dispatcher-level handler can reuse it.
     */
    static void SetDefaultValue(FBPVariableDescription& Variable, const TSharedPtr<FJsonValue>& Value);

private:
    /**
     * Converts a type string to FEdGraphPinType
     * Supported types: bool, int, float, string, vector, rotator
     */
    static FEdGraphPinType GetPinTypeFromString(const FString& TypeString);
};