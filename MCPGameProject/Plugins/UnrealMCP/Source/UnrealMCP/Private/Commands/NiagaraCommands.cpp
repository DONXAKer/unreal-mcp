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
#include "NiagaraParameterCollection.h"
// NiagaraEditorModule not required for parameter writes; NiagaraSystem is sufficient.
#endif // WITH_EDITOR

// ─────────────────────────────────────────────────────────────────────────────
// FNiagaraCommands
// ─────────────────────────────────────────────────────────────────────────────

FNiagaraCommands::FNiagaraCommands()
{
}

TSharedPtr<FJsonObject> FNiagaraCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("copy_niagara_system"))    return HandleCopyNiagaraSystem(Params);
    if (CommandType == TEXT("set_niagara_parameters")) return HandleSetNiagaraParameters(Params);

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
        const FString& RawParamName = Pair.Key;
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
            if (ExposedParams.FindParameterOffset(BoolVar) != INDEX_NONE)
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
                    if (ExposedParams.FindParameterOffset(Vec4Var) != INDEX_NONE)
                    {
                        ExposedParams.SetParameterValue(FVector4f(X, Y, Z, W), Vec4Var);
                        bApplied = true;
                    }
                }
                if (!bApplied)
                {
                    FNiagaraVariable Vec3Var(FNiagaraTypeDefinition::GetVec3Def(), FName(*QualifiedName));
                    if (ExposedParams.FindParameterOffset(Vec3Var) != INDEX_NONE)
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
            if (ExposedParams.FindParameterOffset(FloatVar) != INDEX_NONE)
            {
                ExposedParams.SetParameterValue((float)NumVal, FloatVar);
                bApplied = true;
            }
            else
            {
                FNiagaraVariable IntVar(FNiagaraTypeDefinition::GetIntDef(), FName(*QualifiedName));
                if (ExposedParams.FindParameterOffset(IntVar) != INDEX_NONE)
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
