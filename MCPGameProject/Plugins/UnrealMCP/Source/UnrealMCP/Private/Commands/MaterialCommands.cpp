#include "Commands/MaterialCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/Texture.h"
#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    enum class EParamKind
    {
        Unknown,
        Scalar,
        Vector,
        Texture,
    };

    // Inspect the parent material's parameter list and decide the kind of the
    // given parameter name. This is more reliable than inferring from the JSON
    // value shape alone (e.g., [1,0,0,1] matches both vector and would be valid
    // for a scalar misencoded by caller).
    EParamKind ResolveParamKind(UMaterialInterface* Parent, const FString& ParamName)
    {
        if (!Parent) return EParamKind::Unknown;

        TArray<FMaterialParameterInfo> ScalarInfos;  TArray<FGuid> ScalarGuids;
        Parent->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
        for (const FMaterialParameterInfo& Info : ScalarInfos)
        {
            if (Info.Name.ToString() == ParamName) return EParamKind::Scalar;
        }

        TArray<FMaterialParameterInfo> VectorInfos;  TArray<FGuid> VectorGuids;
        Parent->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
        for (const FMaterialParameterInfo& Info : VectorInfos)
        {
            if (Info.Name.ToString() == ParamName) return EParamKind::Vector;
        }

        TArray<FMaterialParameterInfo> TextureInfos;  TArray<FGuid> TextureGuids;
        Parent->GetAllTextureParameterInfo(TextureInfos, TextureGuids);
        for (const FMaterialParameterInfo& Info : TextureInfos)
        {
            if (Info.Name.ToString() == ParamName) return EParamKind::Texture;
        }

        return EParamKind::Unknown;
    }

    // Applies a single JSON value to the MI, honoring kind resolved against
    // parent. Returns true if the parameter was applied.
    bool ApplyParamToMI(
        UMaterialInstanceConstant* MI,
        const FString& ParamName,
        const TSharedPtr<FJsonValue>& Value,
        EParamKind Kind,
        FString& OutErrorReason)
    {
        if (!MI || !Value.IsValid())
        {
            OutErrorReason = TEXT("mi_or_value_null");
            return false;
        }

        switch (Kind)
        {
        case EParamKind::Scalar:
        {
            if (Value->Type == EJson::Number)
            {
                UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
                    MI, FName(*ParamName), static_cast<float>(Value->AsNumber()));
                return true;
            }
            OutErrorReason = TEXT("expected_number_for_scalar");
            return false;
        }
        case EParamKind::Vector:
        {
            if (Value->Type == EJson::Array)
            {
                const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
                FLinearColor Color(0, 0, 0, 1);
                if (Arr.Num() >= 1) Color.R = static_cast<float>(Arr[0]->AsNumber());
                if (Arr.Num() >= 2) Color.G = static_cast<float>(Arr[1]->AsNumber());
                if (Arr.Num() >= 3) Color.B = static_cast<float>(Arr[2]->AsNumber());
                if (Arr.Num() >= 4) Color.A = static_cast<float>(Arr[3]->AsNumber());
                UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
                    MI, FName(*ParamName), Color);
                return true;
            }
            OutErrorReason = TEXT("expected_array_for_vector");
            return false;
        }
        case EParamKind::Texture:
        {
            if (Value->Type == EJson::String)
            {
                const FString TexturePath = Value->AsString();
                UObject* Obj = UEditorAssetLibrary::LoadAsset(TexturePath);
                UTexture* Tex = Cast<UTexture>(Obj);
                if (!Tex)
                {
                    OutErrorReason = FString::Printf(TEXT("texture_not_found:%s"), *TexturePath);
                    return false;
                }
                UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
                    MI, FName(*ParamName), Tex);
                return true;
            }
            OutErrorReason = TEXT("expected_string_path_for_texture");
            return false;
        }
        default:
            OutErrorReason = TEXT("unknown_param");
            return false;
        }
    }

    // Walk the params object and apply each entry. Populates OutAppliedNames and
    // OutSkipped (param name → reason) for inclusion in meta.
    void ApplyParamsToMI(
        UMaterialInstanceConstant* MI,
        UMaterialInterface* Parent,
        const TSharedPtr<FJsonObject>& ParamsObj,
        TArray<FString>& OutAppliedNames,
        TMap<FString, FString>& OutSkipped)
    {
        if (!MI || !ParamsObj.IsValid()) return;

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ParamsObj->Values)
        {
            const FString& ParamName = Pair.Key;
            const TSharedPtr<FJsonValue>& ParamValue = Pair.Value;

            EParamKind Kind = ResolveParamKind(Parent, ParamName);
            if (Kind == EParamKind::Unknown)
            {
                OutSkipped.Add(ParamName, TEXT("not_in_parent"));
                continue;
            }

            FString Reason;
            if (ApplyParamToMI(MI, ParamName, ParamValue, Kind, Reason))
            {
                OutAppliedNames.Add(ParamName);
            }
            else
            {
                OutSkipped.Add(ParamName, Reason);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FMaterialCommands
// ─────────────────────────────────────────────────────────────────────────────

FMaterialCommands::FMaterialCommands()
{
}

TSharedPtr<FJsonObject> FMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_material_instance"))
    {
        return HandleCreateMaterialInstance(Params);
    }
    if (CommandType == TEXT("set_material_instance_params"))
    {
        return HandleSetMaterialInstanceParams(Params);
    }

    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("ue_internal"),
        TEXT("UNKNOWN_MATERIAL_COMMAND"),
        FString::Printf(TEXT("Material category received unknown command '%s'"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// create_material_instance
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString ParentPath;
    if (!Params->TryGetStringField(TEXT("parentMaterial"), ParentPath) || ParentPath.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_PARENT_MATERIAL"),
            TEXT("Required param 'parentMaterial' is missing or empty"));
    }

    UObject* ParentObj = UEditorAssetLibrary::LoadAsset(ParentPath);
    UMaterialInterface* Parent = Cast<UMaterialInterface>(ParentObj);
    if (!Parent)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("parentMaterial"), ParentPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("PARENT_MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("Parent material not found or not a UMaterialInterface at '%s'"), *ParentPath),
            Details);
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    FAssetCommonUtils::FIdempotencyDecision Decision =
        FAssetCommonUtils::ResolveIdempotency(AssetPath, IfExists);
    if (Decision.Action == TEXT("skip") || Decision.Action == TEXT("fail"))
    {
        return Decision.SkipResponse;
    }

    const bool bUpdate = (Decision.Action == TEXT("update"));
    const bool bOverwrite = (Decision.Action == TEXT("overwrite"));

    // For "update" on an existing MI we skip recreation and just mutate params below.
    UMaterialInstanceConstant* MI = nullptr;
    if (bUpdate && Decision.ExistingAsset)
    {
        MI = Cast<UMaterialInstanceConstant>(Decision.ExistingAsset);
        if (!MI)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("EXISTING_ASSET_NOT_MI"),
                FString::Printf(TEXT("Asset at '%s' exists but is not a UMaterialInstanceConstant"), *AssetPath));
        }
    }
    else
    {
        if (bOverwrite && Decision.ExistingAsset)
        {
            UEditorAssetLibrary::DeleteAsset(AssetPath);
        }

        FString PackagePath, AssetName;
        if (!FAssetCommonUtils::SplitAssetPath(AssetPath, PackagePath, AssetName))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("assetPath"), AssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("INVALID_ASSET_PATH"),
                TEXT("Cannot split 'assetPath' into package + name"),
                Details);
        }

        UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
        Factory->InitialParent = Parent;

        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
        MI = Cast<UMaterialInstanceConstant>(NewAsset);
        if (!MI)
        {
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("MI_CREATE_FAILED"),
                FString::Printf(TEXT("AssetTools::CreateAsset returned null/non-MI for '%s'"), *AssetPath));
        }
    }

    // Apply params.
    TArray<FString> AppliedNames;
    TMap<FString, FString> Skipped;
    const TSharedPtr<FJsonObject>* InParams = nullptr;
    if (Params->TryGetObjectField(TEXT("params"), InParams) && InParams && (*InParams).IsValid())
    {
        ApplyParamsToMI(MI, Parent, *InParams, AppliedNames, Skipped);
    }

    // Persist changes.
    UMaterialEditingLibrary::UpdateMaterialInstance(MI);
    MI->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    // Build meta.
    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("parentMaterial"), ParentPath);

    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    for (const FString& N : AppliedNames) AppliedJson.Add(MakeShared<FJsonValueString>(N));
    Meta->SetArrayField(TEXT("appliedParams"), AppliedJson);

    if (Skipped.Num() > 0)
    {
        TSharedPtr<FJsonObject> SkippedObj = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& P : Skipped)
        {
            SkippedObj->SetStringField(P.Key, P.Value);
        }
        Meta->SetObjectField(TEXT("skippedParams"), SkippedObj);
    }

    const FString Status = bUpdate ? TEXT("updated") : (bOverwrite ? TEXT("overwritten") : TEXT("created"));
    return FAssetCommonUtils::MakeSuccessResponse(Status, AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// set_material_instance_params
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleSetMaterialInstanceParams(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString IfMissing = TEXT("fail");
    {
        FString Tmp;
        if (Params->TryGetStringField(TEXT("ifMissing"), Tmp) && !Tmp.IsEmpty())
        {
            IfMissing = Tmp.ToLower();
        }
    }

    UObject* Obj = UEditorAssetLibrary::LoadAsset(AssetPath);
    UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Obj);
    if (!MI)
    {
        if (IfMissing == TEXT("skip"))
        {
            TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
            Meta->SetStringField(TEXT("reason"), TEXT("not_found"));
            return FAssetCommonUtils::MakeSuccessResponse(TEXT("skipped"), AssetPath, Meta);
        }
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MI_NOT_FOUND"),
            FString::Printf(TEXT("No UMaterialInstanceConstant at '%s'"), *AssetPath),
            Details);
    }

    const TSharedPtr<FJsonObject>* InParams = nullptr;
    if (!Params->TryGetObjectField(TEXT("params"), InParams) || !InParams || !(*InParams).IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_PARAMS_FIELD"),
            TEXT("Required param 'params' (object) is missing"));
    }

    UMaterialInterface* Parent = MI->Parent;
    TArray<FString> AppliedNames;
    TMap<FString, FString> Skipped;
    ApplyParamsToMI(MI, Parent, *InParams, AppliedNames, Skipped);

    UMaterialEditingLibrary::UpdateMaterialInstance(MI);
    MI->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    for (const FString& N : AppliedNames) AppliedJson.Add(MakeShared<FJsonValueString>(N));
    Meta->SetArrayField(TEXT("appliedParams"), AppliedJson);
    if (Skipped.Num() > 0)
    {
        TSharedPtr<FJsonObject> SkippedObj = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& P : Skipped)
        {
            SkippedObj->SetStringField(P.Key, P.Value);
        }
        Meta->SetObjectField(TEXT("skippedParams"), SkippedObj);
    }

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}
