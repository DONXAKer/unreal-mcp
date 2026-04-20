#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Texture-pipeline MCP commands (MCP-CONTENT-002).
 *
 * Primitives:
 *   - import_texture              : import an on-disk image file as a UTexture2D asset.
 *   - generate_placeholder_texture: create a solid-color UTexture2D with an optional
 *                                   text label (for bootstrapping before real art exists).
 *
 * Both primitives honor the unified pipeline response contract from
 * FAssetCommonUtils and the `ifExists` idempotency policy (skip/overwrite/
 * update/fail).
 */
class UNREALMCP_API FTextureCommands
{
public:
    FTextureCommands();

    /**
     * Routes an incoming texture command to the appropriate handler.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * import_texture — imports an image file from disk via UTextureFactory.
     *
     * Params:
     *   - sourcePath   (string, required): absolute path on disk to the image file.
     *   - assetPath    (string, required): destination /Game/... package path (no extension).
     *   - sRGB         (bool,   optional): default true.
     *   - compression  (string, optional): "BC7" | "Default" | "NormalMap" | "Grayscale"; default "BC7".
     *   - mipGen       (string, optional): "FromTexture" | "NoMipmaps" | "Sharpen0"; default "FromTexture".
     *   - ifExists     (string, optional): "skip" | "overwrite" | "update" | "fail"; default "skip".
     */
    TSharedPtr<FJsonObject> HandleImportTexture(const TSharedPtr<FJsonObject>& Params);

    /**
     * generate_placeholder_texture — creates a UTexture2D filled with a solid color.
     * An optional text label is drawn into the top-left using a simple bitmap
     * glyph table (ASCII uppercase + digits + a few symbols). No engine font is
     * required — this is intentional for headless reliability.
     *
     * Params:
     *   - assetPath  (string, required): /Game/... path.
     *   - size       (int,    optional): square side length in pixels; default 512.
     *   - color      (array[4] of number, optional): RGBA 0..1; default [0.2,0.2,0.3,1].
     *   - label      (string, optional): text to draw; default "".
     *   - ifExists   (string, optional): "skip" | "overwrite" | "update" | "fail".
     */
    TSharedPtr<FJsonObject> HandleGeneratePlaceholderTexture(const TSharedPtr<FJsonObject>& Params);
};
