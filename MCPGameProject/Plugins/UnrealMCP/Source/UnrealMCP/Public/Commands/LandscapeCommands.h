#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handlers for the landscape MCP commands.
 *
 * Primitives (2):
 *   - create_landscape        : creates a real UE5 Landscape Actor from a 16-bit
 *                               grayscale PNG heightmap file and places it in the
 *                               currently open editor world.
 *   - landscape_set_properties: edits an EXISTING landscape in place (material,
 *                               Nanite, location) so swapping the material does
 *                               not require re-importing the heightmap.
 *
 * Why this exists: Python UE 5.8 API (spawn_actor / generic Actor spawning)
 * produces only a placeholder Landscape without components.  The C++ import
 * path (ALandscape::Import) is the only way to create a fully-functional
 * Landscape from a heightmap inside the editor.
 *
 * Params (all keys are snake_case to match Python callers):
 *   - heightmap_path         (string, required)  : absolute OS path to a
 *                                                  16-bit grayscale PNG.
 *   - section_size           (int,    optional)  : quads per section (default 63).
 *                                                  UE accepts: 7,15,31,63,127,255.
 *   - sections_per_component (int,    optional)  : sections per component (default 1).
 *                                                  UE accepts: 1 or 2.
 *   - components_x           (int,    optional)  : component count X (default 32).
 *   - components_y           (int,    optional)  : component count Y (default 32).
 *   - scale_x                (float,  optional)  : X scale in cm (default 100.0).
 *   - scale_y                (float,  optional)  : Y scale in cm (default 100.0).
 *   - scale_z                (float,  optional)  : Z scale in cm (default 100.0).
 *                                                  Real Z range = scale_z * 512 cm.
 *   - material_path          (string, optional)  : /Game/... path of a
 *                                                  UMaterialInterface to assign.
 *   - actor_label            (string, optional)  : label for the actor in the
 *                                                  editor (default "Landscape").
 *
 * Width/Height are derived from the component grid:
 *   width  = components_x * sections_per_component * section_size + 1
 *   height = components_y * sections_per_component * section_size + 1
 *
 * The PNG must match exactly (width x height) in pixels.
 * Pixel values are big-endian 16-bit → UE height (0=bottom, 65535=top).
 *
 * Response (success meta fields):
 *   actor_label, width, height, components (NxM string), section_size,
 *   scale ([x,y,z]), location ([x,y,z]).
 */
class UNREALMCP_API FLandscapeCommands
{
public:
    FLandscapeCommands();

    /** Routes an incoming landscape command to the appropriate handler. */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * create_landscape — import a PNG heightmap and create an ALandscape actor
     * in the currently active editor world.
     */
    TSharedPtr<FJsonObject> HandleCreateLandscape(const TSharedPtr<FJsonObject>& Params);

    /**
     * landscape_set_properties — edit an existing ALandscape in place (v3.21.0).
     *
     * Params:
     *   - actor_label   (string, optional) : landscape to edit; defaults to the
     *                                        only landscape in the world when
     *                                        there is exactly one.
     *   - material_path (string, optional) : /Game/... UMaterialInterface.
     *   - enable_nanite (bool,   optional) : toggles ALandscapeProxy::bEnableNanite
     *                                        and rebuilds the Nanite representation.
     *   - nanite_lod    (int,    optional) : NaniteLODIndex (only with Nanite on).
     *   - location      (array3, optional) : new world location in cm.
     *   - save          (bool,   optional) : save the map afterwards (default true).
     *
     * Scale is deliberately NOT editable: it is tied to the imported heightmap
     * resolution, so changing it silently desyncs the terrain from the grid.
     * Re-run create_landscape instead.
     */
    TSharedPtr<FJsonObject> HandleLandscapeSetProperties(const TSharedPtr<FJsonObject>& Params);
};
