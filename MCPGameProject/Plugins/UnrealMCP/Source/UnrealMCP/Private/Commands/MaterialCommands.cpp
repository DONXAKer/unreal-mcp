#include "Commands/MaterialCommands.h"
#include "Commands/AssetCommonUtils.h"

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/MaterialFactoryNew.h"
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
                UObject* Obj = FAssetCommonUtils::LoadAssetObject(TexturePath);
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
    if (CommandType == TEXT("material_create"))
    {
        return HandleMaterialCreate(Params);
    }
    if (CommandType == TEXT("material_add_node"))
    {
        return HandleMaterialAddNode(Params);
    }
    if (CommandType == TEXT("material_connect"))
    {
        return HandleMaterialConnect(Params);
    }
    if (CommandType == TEXT("material_set_node_param"))
    {
        return HandleMaterialSetNodeParam(Params);
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

    UObject* ParentObj = FAssetCommonUtils::LoadAssetObject(ParentPath);
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

    UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
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

// ─────────────────────────────────────────────────────────────────────────────
// material_create
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleMaterialCreate(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString Domain = TEXT("Surface");
    {
        FString Tmp;
        if (Params->TryGetStringField(TEXT("domain"), Tmp) && !Tmp.IsEmpty())
        {
            Domain = Tmp;
        }
    }

    FString IfExists;
    Params->TryGetStringField(TEXT("ifExists"), IfExists);

    // Idempotency check
    bool bExists = UEditorAssetLibrary::DoesAssetExist(AssetPath);
    if (bExists)
    {
        if (IfExists == TEXT("skip"))
        {
            TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
            Meta->SetStringField(TEXT("reason"), TEXT("already_exists"));
            return FAssetCommonUtils::MakeSuccessResponse(TEXT("skipped"), AssetPath, Meta);
        }
        else if (IfExists == TEXT("overwrite"))
        {
            UEditorAssetLibrary::DeleteAsset(AssetPath);
        }
        else
        {
            // "fail" or default
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("assetPath"), AssetPath);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("ASSET_ALREADY_EXISTS"),
                FString::Printf(TEXT("Asset already exists at '%s'. Use ifExists=overwrite or skip."), *AssetPath),
                Details);
        }
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

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
    UMaterial* Mat = Cast<UMaterial>(NewAsset);
    if (!Mat)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("MATERIAL_CREATE_FAILED"),
            FString::Printf(TEXT("AssetTools::CreateAsset returned null for '%s'"), *AssetPath));
    }

    // Set domain
    if (Domain == TEXT("UserInterface"))
    {
        Mat->MaterialDomain = EMaterialDomain::MD_UI;
    }
    else
    {
        Mat->MaterialDomain = EMaterialDomain::MD_Surface;
    }

    Mat->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("domain"), Domain);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("created"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// material_add_node
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleMaterialAddNode(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString NodeType;
    if (!Params->TryGetStringField(TEXT("nodeType"), NodeType) || NodeType.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_NODE_TYPE"),
            TEXT("Required param 'nodeType' is missing or empty"));
    }

    FString NodeId;
    if (!Params->TryGetStringField(TEXT("nodeId"), NodeId) || NodeId.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_NODE_ID"),
            TEXT("Required param 'nodeId' is missing or empty"));
    }

    int32 PosX = 0;
    int32 PosY = 0;
    {
        double Tmp = 0.0;
        if (Params->TryGetNumberField(TEXT("posX"), Tmp)) PosX = static_cast<int32>(Tmp);
        if (Params->TryGetNumberField(TEXT("posY"), Tmp)) PosY = static_cast<int32>(Tmp);
    }

    UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
    UMaterial* Mat = Cast<UMaterial>(Obj);
    if (!Mat)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("No UMaterial found at '%s'"), *AssetPath),
            Details);
    }

    // Map nodeType string → /Script/Engine class path (without 'U' prefix)
    static const TMap<FString, FString> NodeTypeToClassPath = {
        { TEXT("Constant"),                 TEXT("/Script/Engine.MaterialExpressionConstant") },
        { TEXT("Constant3Vector"),          TEXT("/Script/Engine.MaterialExpressionConstant3Vector") },
        { TEXT("ScalarParameter"),          TEXT("/Script/Engine.MaterialExpressionScalarParameter") },
        { TEXT("VectorParameter"),          TEXT("/Script/Engine.MaterialExpressionVectorParameter") },
        { TEXT("TextureSample"),            TEXT("/Script/Engine.MaterialExpressionTextureSample") },
        { TEXT("TextureSampleParameter2D"), TEXT("/Script/Engine.MaterialExpressionTextureSampleParameter2D") },
        { TEXT("Lerp"),                     TEXT("/Script/Engine.MaterialExpressionLinearInterpolate") },
        { TEXT("Multiply"),                 TEXT("/Script/Engine.MaterialExpressionMultiply") },
        { TEXT("Add"),                      TEXT("/Script/Engine.MaterialExpressionAdd") },
        { TEXT("Panner"),                   TEXT("/Script/Engine.MaterialExpressionPanner") },
        { TEXT("Noise"),                    TEXT("/Script/Engine.MaterialExpressionNoise") },
    };

    const FString* ClassPathPtr = NodeTypeToClassPath.Find(NodeType);
    if (!ClassPathPtr)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("nodeType"), NodeType);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("UNKNOWN_NODE_TYPE"),
            FString::Printf(TEXT("Unsupported nodeType '%s'"), *NodeType),
            Details);
    }

    UClass* NodeClass = LoadObject<UClass>(nullptr, **ClassPathPtr);
    if (!NodeClass)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("classPath"), *ClassPathPtr);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("NODE_CLASS_NOT_FOUND"),
            FString::Printf(TEXT("Could not load UClass for '%s'"), **ClassPathPtr),
            Details);
    }

    UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, NodeClass, PosX, PosY);
    if (!Expr)
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("ue_internal"),
            TEXT("NODE_CREATE_FAILED"),
            FString::Printf(TEXT("UMaterialEditingLibrary::CreateMaterialExpression returned null for nodeType '%s'"), *NodeType));
    }

    // Store the caller-supplied ID in the Desc field so we can find this node later
    Expr->Desc = NodeId;

    Mat->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("nodeId"), NodeId);
    Meta->SetStringField(TEXT("nodeType"), NodeType);
    Meta->SetNumberField(TEXT("posX"), PosX);
    Meta->SetNumberField(TEXT("posY"), PosY);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("created"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers for material_connect and material_set_node_param
// ─────────────────────────────────────────────────────────────────────────────

UMaterialExpression* FMaterialCommands::FindExprByDesc(UMaterial* Mat, const FString& Desc)
{
    if (!Mat) return nullptr;
    for (UMaterialExpression* E : Mat->Expressions)
    {
        if (E && E->Desc == Desc) return E;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// material_connect
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleMaterialConnect(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString FromNodeId;
    if (!Params->TryGetStringField(TEXT("fromNodeId"), FromNodeId) || FromNodeId.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_FROM_NODE_ID"),
            TEXT("Required param 'fromNodeId' is missing or empty"));
    }

    FString FromOutput;
    Params->TryGetStringField(TEXT("fromOutput"), FromOutput);

    FString ToNodeId;
    Params->TryGetStringField(TEXT("toNodeId"), ToNodeId);

    FString ToInput = TEXT("BaseColor");
    {
        FString Tmp;
        if (Params->TryGetStringField(TEXT("toInput"), Tmp) && !Tmp.IsEmpty())
        {
            ToInput = Tmp;
        }
    }

    UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
    UMaterial* Mat = Cast<UMaterial>(Obj);
    if (!Mat)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("No UMaterial found at '%s'"), *AssetPath),
            Details);
    }

    UMaterialExpression* SrcExpr = FindExprByDesc(Mat, FromNodeId);
    if (!SrcExpr)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("fromNodeId"), FromNodeId);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("SOURCE_NODE_NOT_FOUND"),
            FString::Printf(TEXT("Expression with Desc='%s' not found in material"), *FromNodeId),
            Details);
    }

    if (ToNodeId.IsEmpty())
    {
        // Connect to root material property
        static const TMap<FString, EMaterialProperty> PropMap = {
            { TEXT("BaseColor"),     EMaterialProperty::MP_BaseColor },
            { TEXT("EmissiveColor"), EMaterialProperty::MP_EmissiveColor },
            { TEXT("Opacity"),       EMaterialProperty::MP_Opacity },
            { TEXT("Roughness"),     EMaterialProperty::MP_Roughness },
            { TEXT("Metallic"),      EMaterialProperty::MP_Metallic },
            { TEXT("Normal"),        EMaterialProperty::MP_Normal },
        };

        const EMaterialProperty* PropPtr = PropMap.Find(ToInput);
        if (!PropPtr)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("toInput"), ToInput);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("UNKNOWN_ROOT_PROPERTY"),
                FString::Printf(TEXT("Unknown root material property '%s'. Use: BaseColor, EmissiveColor, Opacity, Roughness, Metallic, Normal"), *ToInput),
                Details);
        }

        bool bOk = UMaterialEditingLibrary::ConnectMaterialProperty(SrcExpr, FromOutput, *PropPtr);
        if (!bOk)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("fromNodeId"), FromNodeId);
            Details->SetStringField(TEXT("fromOutput"), FromOutput);
            Details->SetStringField(TEXT("toInput"), ToInput);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("PIN_NOT_FOUND"),
                FString::Printf(TEXT("ConnectMaterialProperty failed: output pin '%s' not found on node '%s'"), *FromOutput, *FromNodeId),
                Details);
        }
    }
    else
    {
        // Connect expression → expression
        UMaterialExpression* DstExpr = FindExprByDesc(Mat, ToNodeId);
        if (!DstExpr)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("toNodeId"), ToNodeId);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("user"),
                TEXT("TARGET_NODE_NOT_FOUND"),
                FString::Printf(TEXT("Expression with Desc='%s' not found in material"), *ToNodeId),
                Details);
        }

        bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(SrcExpr, FromOutput, DstExpr, ToInput);
        if (!bOk)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("fromNodeId"), FromNodeId);
            Details->SetStringField(TEXT("fromOutput"), FromOutput);
            Details->SetStringField(TEXT("toNodeId"), ToNodeId);
            Details->SetStringField(TEXT("toInput"), ToInput);
            return FAssetCommonUtils::MakeFailureResponse(
                TEXT("ue_internal"),
                TEXT("PIN_NOT_FOUND"),
                FString::Printf(TEXT("ConnectMaterialExpressions failed: pin '%s'->'%s' not found"), *FromOutput, *ToInput),
                Details);
        }
    }

    Mat->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("fromNodeId"), FromNodeId);
    Meta->SetStringField(TEXT("fromOutput"), FromOutput);
    Meta->SetStringField(TEXT("toNodeId"), ToNodeId.IsEmpty() ? TEXT("(root)") : ToNodeId);
    Meta->SetStringField(TEXT("toInput"), ToInput);
    return FAssetCommonUtils::MakeSuccessResponse(TEXT("connected"), AssetPath, Meta);
}

// ─────────────────────────────────────────────────────────────────────────────
// material_set_node_param
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMaterialCommands::HandleMaterialSetNodeParam(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        return ParamFailure;
    }

    FString NodeId;
    if (!Params->TryGetStringField(TEXT("nodeId"), NodeId) || NodeId.IsEmpty())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_NODE_ID"),
            TEXT("Required param 'nodeId' is missing or empty"));
    }

    const TSharedPtr<FJsonObject>* InParams = nullptr;
    if (!Params->TryGetObjectField(TEXT("params"), InParams) || !InParams || !(*InParams).IsValid())
    {
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MISSING_PARAMS_FIELD"),
            TEXT("Required param 'params' (object) is missing"));
    }

    UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
    UMaterial* Mat = Cast<UMaterial>(Obj);
    if (!Mat)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("No UMaterial found at '%s'"), *AssetPath),
            Details);
    }

    UMaterialExpression* Expr = FindExprByDesc(Mat, NodeId);
    if (!Expr)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("nodeId"), NodeId);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("NODE_NOT_FOUND"),
            FString::Printf(TEXT("Expression with Desc='%s' not found in material"), *NodeId),
            Details);
    }

    TArray<FString> Applied;
    TArray<FString> Skipped;

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*InParams)->Values)
    {
        const FString& Key = Pair.Key;
        const TSharedPtr<FJsonValue>& Val = Pair.Value;
        bool bHandled = false;

        // UMaterialExpressionConstant: R (float)
        if (UMaterialExpressionConstant* Const = Cast<UMaterialExpressionConstant>(Expr))
        {
            if (Key == TEXT("R") && Val->Type == EJson::Number)
            {
                Const->R = static_cast<float>(Val->AsNumber());
                bHandled = true;
            }
        }

        // UMaterialExpressionConstant3Vector: Constant (FLinearColor from [r,g,b,a])
        if (!bHandled)
        {
            if (UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
            {
                if (Key == TEXT("Constant") && Val->Type == EJson::Array)
                {
                    const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
                    FLinearColor Color(0, 0, 0, 1);
                    if (Arr.Num() >= 1) Color.R = static_cast<float>(Arr[0]->AsNumber());
                    if (Arr.Num() >= 2) Color.G = static_cast<float>(Arr[1]->AsNumber());
                    if (Arr.Num() >= 3) Color.B = static_cast<float>(Arr[2]->AsNumber());
                    if (Arr.Num() >= 4) Color.A = static_cast<float>(Arr[3]->AsNumber());
                    Const3->Constant = Color;
                    bHandled = true;
                }
            }
        }

        // UMaterialExpressionScalarParameter: ParameterName (string), DefaultValue (float)
        if (!bHandled)
        {
            if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
            {
                if (Key == TEXT("ParameterName") && Val->Type == EJson::String)
                {
                    ScalarParam->ParameterName = FName(*Val->AsString());
                    bHandled = true;
                }
                else if (Key == TEXT("DefaultValue") && Val->Type == EJson::Number)
                {
                    ScalarParam->DefaultValue = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
            }
        }

        // UMaterialExpressionVectorParameter: ParameterName (string), DefaultValue ([r,g,b,a])
        if (!bHandled)
        {
            if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr))
            {
                if (Key == TEXT("ParameterName") && Val->Type == EJson::String)
                {
                    VecParam->ParameterName = FName(*Val->AsString());
                    bHandled = true;
                }
                else if (Key == TEXT("DefaultValue") && Val->Type == EJson::Array)
                {
                    const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
                    FLinearColor Color(0, 0, 0, 1);
                    if (Arr.Num() >= 1) Color.R = static_cast<float>(Arr[0]->AsNumber());
                    if (Arr.Num() >= 2) Color.G = static_cast<float>(Arr[1]->AsNumber());
                    if (Arr.Num() >= 3) Color.B = static_cast<float>(Arr[2]->AsNumber());
                    if (Arr.Num() >= 4) Color.A = static_cast<float>(Arr[3]->AsNumber());
                    VecParam->DefaultValue = Color;
                    bHandled = true;
                }
            }
        }

        // UMaterialExpressionTextureSample / TextureSampleParameter2D: Texture (string path)
        if (!bHandled)
        {
            if ((Cast<UMaterialExpressionTextureSample>(Expr) || Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                && Key == TEXT("Texture") && Val->Type == EJson::String)
            {
                UObject* TexObj = FAssetCommonUtils::LoadAssetObject(Val->AsString());
                UTexture* Tex = Cast<UTexture>(TexObj);
                if (Tex)
                {
                    if (UMaterialExpressionTextureSampleParameter2D* TSP2D = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                    {
                        TSP2D->Texture = Tex;
                    }
                    else if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
                    {
                        TS->Texture = Tex;
                    }
                    bHandled = true;
                }
                else
                {
                    Skipped.Add(FString::Printf(TEXT("%s:texture_not_found:%s"), *Key, *Val->AsString()));
                    continue;
                }
            }
        }

        // UMaterialExpressionTextureSampleParameter2D: ParameterName (string)
        if (!bHandled)
        {
            if (UMaterialExpressionTextureSampleParameter2D* TSP2D = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
            {
                if (Key == TEXT("ParameterName") && Val->Type == EJson::String)
                {
                    TSP2D->ParameterName = FName(*Val->AsString());
                    bHandled = true;
                }
            }
        }

        // UMaterialExpressionNoise: Scale (float), Levels (int)
        if (!bHandled)
        {
            if (UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Expr))
            {
                if (Key == TEXT("Scale") && Val->Type == EJson::Number)
                {
                    Noise->Scale = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
                else if (Key == TEXT("Levels") && Val->Type == EJson::Number)
                {
                    Noise->Levels = static_cast<int32>(Val->AsNumber());
                    bHandled = true;
                }
            }
        }

        if (bHandled)
        {
            Applied.Add(Key);
        }
        else
        {
            Skipped.Add(Key);
        }
    }

    Mat->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("nodeId"), NodeId);

    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    for (const FString& N : Applied) AppliedJson.Add(MakeShared<FJsonValueString>(N));
    Meta->SetArrayField(TEXT("applied"), AppliedJson);

    if (Skipped.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> SkippedJson;
        for (const FString& N : Skipped) SkippedJson.Add(MakeShared<FJsonValueString>(N));
        Meta->SetArrayField(TEXT("skipped"), SkippedJson);
    }

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("updated"), AssetPath, Meta);
}
