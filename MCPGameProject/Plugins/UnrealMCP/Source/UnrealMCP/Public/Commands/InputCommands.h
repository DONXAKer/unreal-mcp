#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for project-wide input mapping commands.
 *
 * Wraps UInputSettings to add / overwrite ActionMappings and AxisMappings,
 * then persists them to DefaultInput.ini via SaveKeyMappings().
 *
 * Note: this manipulates legacy "Input Settings" (UE 4-era), not the
 * EnhancedInput system (which uses UInputAction / UInputMappingContext assets).
 */
class FInputCommands
{
public:
    FInputCommands();

    /**
     * Routes incoming input commands.
     * @param CommandType  Command name (currently only "create_input_mapping").
     * @param Params       JSON parameters.
     * @return JSON response.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Register a legacy ActionMapping or AxisMapping on UInputSettings.
     *
     * Params:
     *   action_name (required)  — name of the mapping group.
     *   key (required)          — engine key name (e.g. "SpaceBar", "LeftMouseButton",
     *                              "Gamepad_FaceButton_Bottom"). See EKeys::* identifiers.
     *   input_type / mapping_type (opt, default "Action")
     *                            — "Action" or "Axis".
     *   scale (opt, default 1.0) — only used for "Axis" mappings.
     *   shift / ctrl / alt / cmd (opt, default false)
     *                            — modifier keys for ActionMapping.
     */
    TSharedPtr<FJsonObject> HandleCreateInputMapping(const TSharedPtr<FJsonObject>& Params);
};
