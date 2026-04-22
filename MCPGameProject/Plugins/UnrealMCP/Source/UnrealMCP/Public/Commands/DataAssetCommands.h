#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Data Asset and Sound import MCP commands.
 *
 * Commands (4):
 *   - import_datatable_from_csv : import a CSV file as a UDataTable asset.
 *   - set_datatable_row         : add/update a single row in a UDataTable by name.
 *   - get_datatable_rows        : read all rows from a UDataTable as JSON.
 *   - import_sound_wave         : import a WAV file as a USoundWave asset.
 *
 * All responses follow the unified MCP Content Pipeline contract defined in
 * FAssetCommonUtils (ok, status, assetPath, meta / error).
 */
class UNREALMCP_API FDataAssetCommands
{
public:
    FDataAssetCommands();

    /** Routes an incoming data-asset command to the appropriate handler. */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * import_datatable_from_csv — import a CSV on disk into a UDataTable asset.
     * Params:
     *   - csvPath   (string, required): absolute path to .csv file on disk.
     *   - assetPath (string, required): destination /Game/... path (no ext).
     *   - rowStruct (string, optional): full UScriptStruct path e.g.
     *               "/Script/MyGame.FMyRow". If omitted, the factory uses its default.
     *   - ifExists  (string, optional): "skip" (default) | "overwrite".
     */
    TSharedPtr<FJsonObject> HandleImportDataTableFromCsv(const TSharedPtr<FJsonObject>& Params);

    /**
     * set_datatable_row — find or add a row then apply JSON field values.
     * Params:
     *   - assetPath (string, required): /Game/... path to an existing UDataTable.
     *   - rowName   (string, required): row key (FName).
     *   - rowJson   (object, required): map of FieldName → value.
     *               v1 supports: string, bool, integer, float, double.
     */
    TSharedPtr<FJsonObject> HandleSetDataTableRow(const TSharedPtr<FJsonObject>& Params);

    /**
     * get_datatable_rows — read all rows of a UDataTable as a JSON array.
     * Params:
     *   - assetPath (string, required)
     * Returns meta: { rowCount:N, rows:[{ rowName, fields:{...} }] }
     */
    TSharedPtr<FJsonObject> HandleGetDataTableRows(const TSharedPtr<FJsonObject>& Params);

    /**
     * import_sound_wave — import a WAV file into a USoundWave asset.
     * Params:
     *   - wavPath   (string, required): absolute path to .wav file on disk.
     *   - assetPath (string, required): destination /Game/... path (no ext).
     *   - ifExists  (string, optional): "skip" (default) | "overwrite".
     */
    TSharedPtr<FJsonObject> HandleImportSoundWave(const TSharedPtr<FJsonObject>& Params);
};
