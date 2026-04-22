#include "Commands/DataAssetCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/CSVImportFactory.h"
#include "Factories/SoundFactory.h"
#include "Engine/DataTable.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Field.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Split "/Game/Data/DT_Foo" into ("/Game/Data", "DT_Foo").
    // Prefixed DA_ to avoid anonymous-namespace ODR clash in unity builds.
    bool DA_SplitAssetPath(const FString& InAssetPath, FString& OutPackagePath, FString& OutAssetName)
    {
        int32 LastSlash = INDEX_NONE;
        if (!InAssetPath.FindLastChar(TCHAR('/'), LastSlash) || LastSlash <= 0)
        {
            return false;
        }
        OutPackagePath = InAssetPath.Left(LastSlash);
        OutAssetName = InAssetPath.Mid(LastSlash + 1);
        return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FDataAssetCommands
// ─────────────────────────────────────────────────────────────────────────────

FDataAssetCommands::FDataAssetCommands()
{
}

TSharedPtr<FJsonObject> FDataAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("import_datatable_from_csv")) return HandleImportDataTableFromCsv(Params);
    if (CommandType == TEXT("set_datatable_row"))         return HandleSetDataTableRow(Params);
    if (CommandType == TEXT("get_datatable_rows"))        return HandleGetDataTableRows(Params);
    if (CommandType == TEXT("import_sound_wave"))         return HandleImportSoundWave(Params);

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_DATAASSET_COMMAND"),
        FString::Printf(TEXT("DataAsset category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// import_datatable_from_csv
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FDataAssetCommands::HandleImportDataTableFromCsv(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString CsvPath;
    if (!Params->TryGetStringField(TEXT("csvPath"), CsvPath) || CsvPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_CSV_PATH"),
            TEXT("Required param 'csvPath' is missing or empty"));
    }

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    if (!IFileManager::Get().FileExists(*CsvPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("csvPath"), CsvPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("CSV_FILE_NOT_FOUND"),
            FString::Printf(TEXT("CSV file not found on disk: '%s'"), *CsvPath), D);
    }

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

    FString PackagePath, AssetName;
    if (!DA_SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split '%s' into package+name"), *AssetPath), D);
    }

    // Build factory.
    UCSVImportFactory* Factory = NewObject<UCSVImportFactory>();
    Factory->AddToRoot(); // prevent GC during import

    // Optional rowStruct override — set on AutomatedImportSettings, not on the factory directly.
    FString RowStructPath;
    if (Params->TryGetStringField(TEXT("rowStruct"), RowStructPath) && !RowStructPath.IsEmpty())
    {
        UScriptStruct* RowStruct = FindObject<UScriptStruct>(nullptr, *RowStructPath, false);
        if (!RowStruct)
        {
            RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
        }
        if (RowStruct)
        {
            Factory->AutomatedImportSettings.ImportRowStruct = RowStruct;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("DataAssetCommands: rowStruct '%s' not found; using factory default."), *RowStructPath);
        }
    }

    // Run import via AssetTools.ImportAssets (not ImportAssetsWithDialog which opens a file dialog).
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    TArray<UObject*> Imported = AssetTools.ImportAssets(
        TArray<FString>{ CsvPath },
        PackagePath,
        Factory,
        /*bSyncToBrowser=*/false
    );

    Factory->RemoveFromRoot();

    if (Imported.Num() == 0 || !Imported[0])
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DATATABLE_IMPORT_FAILED"),
            FString::Printf(TEXT("CSVImportFactory returned no asset for '%s'"), *CsvPath));
    }

    UDataTable* DataTable = Cast<UDataTable>(Imported[0]);
    if (!DataTable)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DATATABLE_CAST_FAILED"),
            TEXT("Imported object is not a UDataTable"));
    }

    const int32 RowCount = DataTable->GetRowMap().Num();

    // Ensure the asset is saved.
    const FString PackageName = DataTable->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("rowCount"), RowCount);

    const FString Status = bOverwrite ? TEXT("overwritten") : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_datatable_row
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FDataAssetCommands::HandleSetDataTableRow(const TSharedPtr<FJsonObject>& Params)
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

    FString RowNameStr;
    if (!Params->TryGetStringField(TEXT("rowName"), RowNameStr) || RowNameStr.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_ROW_NAME"),
            TEXT("Required param 'rowName' is missing or empty"));
    }

    const TSharedPtr<FJsonObject>* RowJsonPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("rowJson"), RowJsonPtr) || !RowJsonPtr || !RowJsonPtr->IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_ROW_JSON"),
            TEXT("Required param 'rowJson' (object) is missing"));
    }
    const TSharedPtr<FJsonObject>& RowJsonObj = *RowJsonPtr;

    // Load the DataTable.
    UObject* AssetObj = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(AssetObj);
    if (!DataTable)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("DATATABLE_NOT_FOUND"),
            FString::Printf(TEXT("No UDataTable at '%s'"), *AssetPath), D);
    }

    UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DATATABLE_NO_STRUCT"),
            TEXT("DataTable has no associated row struct"));
    }

    const FName RowName(*RowNameStr);

    // Find existing row or add a new one.
    uint8* RowData = DataTable->FindRowUnchecked(RowName);
    if (!RowData)
    {
        // Add a zeroed new row.
        DataTable->AddRow(RowName, *static_cast<FTableRowBase*>(FMemory::Malloc(RowStruct->GetStructureSize())));
        RowStruct->InitializeStruct(DataTable->FindRowUnchecked(RowName));
        RowData = DataTable->FindRowUnchecked(RowName);
    }

    if (!RowData)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("DATATABLE_ROW_ADD_FAILED"),
            FString::Printf(TEXT("Failed to add/find row '%s'"), *RowNameStr));
    }

    // Apply JSON fields via FProperty reflection (v1: string/bool/int/float/double).
    TArray<FString> Applied;
    TArray<FString> Skipped;
    for (const auto& Pair : RowJsonObj->Values)
    {
        const FString& FieldName = Pair.Key;
        const TSharedPtr<FJsonValue>& Val = Pair.Value;

        FProperty* Prop = FindFProperty<FProperty>(RowStruct, *FieldName);
        if (!Prop)
        {
            Skipped.Add(FieldName);
            continue;
        }
        void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);

        if (FStrProperty* SP = CastField<FStrProperty>(Prop))
        {
            FString S;
            if (Val.IsValid() && Val->TryGetString(S)) { SP->SetPropertyValue(ValuePtr, S); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
        {
            FString S;
            if (Val.IsValid() && Val->TryGetString(S)) { NP->SetPropertyValue(ValuePtr, FName(*S)); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FTextProperty* TP = CastField<FTextProperty>(Prop))
        {
            FString S;
            if (Val.IsValid() && Val->TryGetString(S)) { TP->SetPropertyValue(ValuePtr, FText::FromString(S)); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
        {
            double N = 0;
            if (Val.IsValid() && Val->TryGetNumber(N)) { IP->SetPropertyValue(ValuePtr, (int32)N); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
        {
            double N = 0;
            if (Val.IsValid() && Val->TryGetNumber(N)) { FP->SetPropertyValue(ValuePtr, (float)N); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
        {
            double N = 0;
            if (Val.IsValid() && Val->TryGetNumber(N)) { DP->SetPropertyValue(ValuePtr, N); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
        {
            bool B = false;
            if (Val.IsValid() && Val->TryGetBool(B)) { BP->SetPropertyValue(ValuePtr, B); Applied.Add(FieldName); }
            else { Skipped.Add(FieldName); }
        }
        else
        {
            Skipped.Add(FieldName);
        }
    }

    // Mark dirty and save.
    DataTable->MarkPackageDirty();
    const FString PackageName = DataTable->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("rowName"), RowNameStr);
    Meta->SetNumberField(TEXT("fieldsApplied"), Applied.Num());
    TArray<TSharedPtr<FJsonValue>> SkippedArr;
    for (const FString& S : Skipped) SkippedArr.Add(MakeShared<FJsonValueString>(S));
    Meta->SetArrayField(TEXT("skippedFields"), SkippedArr);

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_datatable_rows
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FDataAssetCommands::HandleGetDataTableRows(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    UObject* AssetObj = UEditorAssetLibrary::LoadAsset(AssetPath);
    UDataTable* DataTable = Cast<UDataTable>(AssetObj);
    if (!DataTable)
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("DATATABLE_NOT_FOUND"),
            FString::Printf(TEXT("No UDataTable at '%s'"), *AssetPath), D);
    }

    UScriptStruct* RowStruct = DataTable->GetRowStruct();

    TArray<TSharedPtr<FJsonValue>> RowsJson;
    for (const TPair<FName, uint8*>& RowPair : DataTable->GetRowMap())
    {
        TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
        RowObj->SetStringField(TEXT("rowName"), RowPair.Key.ToString());

        TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
        if (RowStruct && RowPair.Value)
        {
            for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                const FString PropName = Prop->GetName();
                void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowPair.Value);

                if (FStrProperty* SP = CastField<FStrProperty>(Prop))
                {
                    Fields->SetStringField(PropName, SP->GetPropertyValue(ValuePtr));
                }
                else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
                {
                    Fields->SetStringField(PropName, NP->GetPropertyValue(ValuePtr).ToString());
                }
                else if (FTextProperty* TP = CastField<FTextProperty>(Prop))
                {
                    Fields->SetStringField(PropName, TP->GetPropertyValue(ValuePtr).ToString());
                }
                else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
                {
                    Fields->SetNumberField(PropName, (double)IP->GetPropertyValue(ValuePtr));
                }
                else if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
                {
                    Fields->SetNumberField(PropName, (double)FP->GetPropertyValue(ValuePtr));
                }
                else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
                {
                    Fields->SetNumberField(PropName, DP->GetPropertyValue(ValuePtr));
                }
                else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
                {
                    Fields->SetBoolField(PropName, BP->GetPropertyValue(ValuePtr));
                }
                // Other types: skip silently for v1.
            }
        }
        RowObj->SetObjectField(TEXT("fields"), Fields);
        RowsJson.Add(MakeShared<FJsonValueObject>(RowObj));
    }

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("rowCount"), RowsJson.Num());
    Meta->SetArrayField(TEXT("rows"), RowsJson);

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// import_sound_wave
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FDataAssetCommands::HandleImportSoundWave(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_PARAMS"), TEXT("Request params object is missing"));
    }

    FString WavPath;
    if (!Params->TryGetStringField(TEXT("wavPath"), WavPath) || WavPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("MISSING_WAV_PATH"),
            TEXT("Required param 'wavPath' is missing or empty"));
    }

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    if (!IFileManager::Get().FileExists(*WavPath))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("wavPath"), WavPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("io"), TEXT("WAV_FILE_NOT_FOUND"),
            FString::Printf(TEXT("WAV file not found on disk: '%s'"), *WavPath), D);
    }

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

    FString PackagePath, AssetName;
    if (!DA_SplitAssetPath(AssetPath, PackagePath, AssetName))
    {
        TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
        D->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"), TEXT("INVALID_ASSET_PATH"),
            FString::Printf(TEXT("Cannot split '%s' into package+name"), *AssetPath), D);
    }

    USoundFactory* Factory = NewObject<USoundFactory>();
    Factory->AddToRoot();

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    TArray<UObject*> Imported = AssetTools.ImportAssets(
        TArray<FString>{ WavPath },
        PackagePath,
        Factory,
        /*bSyncToBrowser=*/false
    );

    Factory->RemoveFromRoot();

    if (Imported.Num() == 0 || !Imported[0])
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("SOUND_IMPORT_FAILED"),
            FString::Printf(TEXT("SoundFactory returned no asset for '%s'"), *WavPath));
    }

    USoundWave* SoundWave = Cast<USoundWave>(Imported[0]);
    if (!SoundWave)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"), TEXT("SOUND_CAST_FAILED"),
            TEXT("Imported object is not a USoundWave"));
    }

    const FString PackageName = SoundWave->GetPackage()->GetName();
    UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("durationSeconds"), (double)SoundWave->Duration);

    const FString Status = bOverwrite ? TEXT("overwritten") : TEXT("created");
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}
