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
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
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
    if (CommandType == TEXT("material_get_graph"))
    {
        return HandleMaterialGetGraph(Params);
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

    // Без PostEditChange граф записан, но список параметров материала не
    // пересобирается — дочерние Material Instance не видят добавленные
    // TextureSampleParameter2D/ScalarParameter и отвечают not_in_parent.
    Mat->PostEditChange();
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

    // Resolve UClass* via StaticClass() — avoids slow LoadObject for known types.
    UClass* NodeClass = nullptr;
    if      (NodeType == TEXT("Constant"))                 NodeClass = UMaterialExpressionConstant::StaticClass();
    else if (NodeType == TEXT("Constant3Vector"))          NodeClass = UMaterialExpressionConstant3Vector::StaticClass();
    else if (NodeType == TEXT("ScalarParameter"))          NodeClass = UMaterialExpressionScalarParameter::StaticClass();
    else if (NodeType == TEXT("VectorParameter"))          NodeClass = UMaterialExpressionVectorParameter::StaticClass();
    else if (NodeType == TEXT("TextureSample"))            NodeClass = UMaterialExpressionTextureSample::StaticClass();
    else if (NodeType == TEXT("TextureSampleParameter2D")) NodeClass = UMaterialExpressionTextureSampleParameter2D::StaticClass();
    else if (NodeType == TEXT("Lerp"))                     NodeClass = UMaterialExpressionLinearInterpolate::StaticClass();
    else if (NodeType == TEXT("Multiply"))                 NodeClass = UMaterialExpressionMultiply::StaticClass();
    else if (NodeType == TEXT("Add"))                      NodeClass = UMaterialExpressionAdd::StaticClass();
    else if (NodeType == TEXT("Panner"))                   NodeClass = UMaterialExpressionPanner::StaticClass();
    // TextureCoordinate — без него нельзя тайлить текстуру: сэмплер без входа UV
    // берёт TexCoord0 как есть, и уникальная развёртка меша растягивает картинку.
    else if (NodeType == TEXT("TextureCoordinate"))         NodeClass = UMaterialExpressionTextureCoordinate::StaticClass();
    else if (NodeType == TEXT("Noise"))                    NodeClass = UMaterialExpressionNoise::StaticClass();

    if (!NodeClass)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("nodeType"), NodeType);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("UNKNOWN_NODE_TYPE"),
            FString::Printf(TEXT("Unsupported nodeType '%s'"), *NodeType),
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

    // Без PostEditChange граф записан, но список параметров материала не
    // пересобирается — дочерние Material Instance не видят добавленные
    // TextureSampleParameter2D/ScalarParameter и отвечают not_in_parent.
    Mat->PostEditChange();
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
    for (UMaterialExpression* E : Mat->GetExpressions())
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

    // Без PostEditChange граф записан, но список параметров материала не
    // пересобирается — дочерние Material Instance не видят добавленные
    // TextureSampleParameter2D/ScalarParameter и отвечают not_in_parent.
    Mat->PostEditChange();
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

        // UMaterialExpressionPanner: SpeedX (float), SpeedY (float)
        if (!bHandled)
        {
            if (UMaterialExpressionPanner* Panner = Cast<UMaterialExpressionPanner>(Expr))
            {
                if (Key == TEXT("SpeedX") && Val->Type == EJson::Number)
                {
                    Panner->SpeedX = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
                else if (Key == TEXT("SpeedY") && Val->Type == EJson::Number)
                {
                    Panner->SpeedY = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
            }
        }

        // UMaterialExpressionTextureCoordinate: UTiling / VTiling (float),
        // CoordinateIndex (int). Без этого блока нода из 3.20.0 создавалась, но
        // оставалась при дефолтном тайлинге 1×1 — то есть бесполезной.
        if (!bHandled)
        {
            if (UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(Expr))
            {
                if (Key == TEXT("UTiling") && Val->Type == EJson::Number)
                {
                    TexCoord->UTiling = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
                else if (Key == TEXT("VTiling") && Val->Type == EJson::Number)
                {
                    TexCoord->VTiling = static_cast<float>(Val->AsNumber());
                    bHandled = true;
                }
                else if (Key == TEXT("CoordinateIndex") && Val->Type == EJson::Number)
                {
                    TexCoord->CoordinateIndex = static_cast<int32>(Val->AsNumber());
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

    // Без PostEditChange граф записан, но список параметров материала не
    // пересобирается — дочерние Material Instance не видят добавленные
    // TextureSampleParameter2D/ScalarParameter и отвечают not_in_parent.
    Mat->PostEditChange();
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

// ─────────────────────────────────────────────────────────────────────────────
// material_get_graph  (FEAT-MAT-002 / v3.24.0, hang fix v3.24.1)
//
// Read-only dump of a master material graph. The UE 5.8 Python API cannot walk
// expressions (UMaterial::Expressions is protected, expression_collection is not
// exposed), so without this command there is no way to tell a *declared*
// parameter from a *wired* one.
//
// v3.24.1 — первая версия ВЕШАЛА РЕДАКТОР. Три источника блокировки, все убраны:
//
//   1. Разыменование TObjectPtr<UTexture> (`if (TexBase->Texture)` и
//      `Texture->GetPathName()`). TObjectPtr при обращении разрешает object
//      handle, а неразрешённый хендл тянет СИНХРОННУЮ загрузку пакета текстуры
//      прямо в game thread. Ни одна другая material-команда чужие Texture-
//      указатели не читает (material_set_node_param только ПИШЕТ уже
//      загруженный ассет), поэтому грабли всплыли именно здесь. Теперь путь
//      берётся из самого хендла: TObjectPtr::GetPathName() + IsResolved(),
//      без разрешения и без загрузки.
//
//   2. UMaterialExpression::CountInputs(). Реализация в движке —
//          int32 Index = 0; while (GetInput(Index)) { Index += 1; } return Index;
//      то есть завершение цикла целиком отдано на откуп virtual-override'у
//      конкретного узла. Мы зовём это на КАЖДОМ узле ЛЮБОГО материала: один
//      узел, чей GetInput не отдаёт nullptr на выходе за диапазон, вешает
//      game thread навсегда. Заменено на собственный обход с жёстким потолком.
//
//   3. Отсутствие бюджета времени: любая заминка = мёртвый мост навсегда
//      (server-поток стоит в Future.Get(), новые коннекты получают WinError
//      10061, потому что accept-очередь переполняется). Теперь обход
//      прерывается по таймауту и отдаёт частичный ответ с timedOut:true.
//
// Плюс пошаговое логирование. Оно обязано быть ЗДЕСЬ, а не в мосту: логи
// "MCPServerRunnable: Executing command" и "EpicUnrealMCPBridge: Executing
// command" пишутся из СОКЕТНОГО потока и попадают в threaded-очередь, которую
// разгребает game thread. Виснет game thread — очередь не разгребается, и в
// Client.log не появляется ни строчки о команде (ровно это и наблюдалось:
// в логе был только ping, а про material_get_graph — тишина). Логи ниже идут
// из ticker-лямбды, то есть из самого game thread, и каждый принудительно
// флашится, поэтому последняя строка в файле точно укажет место следующего
// висяка.
// ─────────────────────────────────────────────────────────────────────────────

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPMaterialGraph, Log, All);

#if WITH_EDITOR
namespace MaterialGraphRead
{
    /** Верхний потолок на число входов одного узла — страховка от зацикливания. */
    static constexpr int32 MaxInputsPerNode = 64;

    /** Шаг обхода в лог + немедленный флаш: иначе висяк не локализуется. */
    static void Step(const FString& Message)
    {
        UE_LOG(LogUnrealMCPMaterialGraph, Display, TEXT("material_get_graph: %s"), *Message);
        if (GLog)
        {
            GLog->Flush();
        }
    }

    // Serializes an FLinearColor as [r, g, b, a].
    static void SetColorField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FLinearColor& C)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        Arr.Add(MakeShared<FJsonValueNumber>(C.R));
        Arr.Add(MakeShared<FJsonValueNumber>(C.G));
        Arr.Add(MakeShared<FJsonValueNumber>(C.B));
        Arr.Add(MakeShared<FJsonValueNumber>(C.A));
        Obj->SetArrayField(Field, Arr);
    }

    /** "/Game/X/T_Foo.T_Foo" -> "T_Foo"; "None"/пусто -> "". */
    static FString ShortNameFromPath(const FString& Path)
    {
        if (Path.IsEmpty() || Path == TEXT("None"))
        {
            return FString();
        }
        FString Result = Path;
        int32 Index = INDEX_NONE;
        if (Result.FindLastChar(TCHAR('.'), Index))
        {
            Result = Result.Mid(Index + 1);
        }
        return Result;
    }

    // Describes the expression that a root pin (or an input pin) points at.
    static TSharedPtr<FJsonObject> DescribeSource(const UMaterialExpression* Expr, int32 OutputIndex)
    {
        TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("node"), Expr->GetName());
        J->SetStringField(TEXT("nodeClass"), Expr->GetClass()->GetName());
        if (!Expr->Desc.IsEmpty())
        {
            J->SetStringField(TEXT("nodeId"), Expr->Desc);
        }
        J->SetNumberField(TEXT("outputIndex"), OutputIndex);
        return J;
    }

    // Adds a root connection entry only when the pin is actually wired.
    static void AddRootConnection(const TSharedPtr<FJsonObject>& Out, const TCHAR* PinName, const FExpressionInput& In)
    {
        // Выражения — сабобъекты того же пакета, что и материал: разрешение
        // указателя не тянет загрузку с диска. Но на всякий случай IsValid.
        UMaterialExpression* Expr = In.Expression.Get();
        if (!Expr || !IsValid(Expr))
        {
            return;
        }
        Out->SetObjectField(PinName, DescribeSource(Expr, In.OutputIndex));
    }

    // Fills type-specific properties for a single expression node.
    static void FillNodeProps(UMaterialExpression* E, const TSharedPtr<FJsonObject>& P)
    {
        // Generic: every UMaterialExpressionParameter subclass exposes a name.
        if (E->HasAParameterName())
        {
            P->SetStringField(TEXT("ParameterName"), E->GetParameterName().ToString());
        }

        if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(E))
        {
            P->SetNumberField(TEXT("DefaultValue"), Scalar->DefaultValue);
        }
        else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(E))
        {
            SetColorField(P, TEXT("DefaultValue"), Vector->DefaultValue);
        }

        if (const UMaterialExpressionConstant* Const = Cast<UMaterialExpressionConstant>(E))
        {
            P->SetNumberField(TEXT("R"), Const->R);
        }
        else if (const UMaterialExpressionConstant3Vector* Const3 = Cast<UMaterialExpressionConstant3Vector>(E))
        {
            SetColorField(P, TEXT("Constant"), Const3->Constant);
        }

        if (const UMaterialExpressionPanner* Panner = Cast<UMaterialExpressionPanner>(E))
        {
            P->SetNumberField(TEXT("SpeedX"), Panner->SpeedX);
            P->SetNumberField(TEXT("SpeedY"), Panner->SpeedY);
            P->SetBoolField(TEXT("bFractionalPart"), Panner->bFractionalPart);
        }

        // Covers TextureSample, TextureSampleParameter2D, TextureObject, ... —
        // all of them inherit the Texture pointer from UMaterialExpressionTextureBase.
        //
        // КРИТИЧНО: НЕ разыменовываем TObjectPtr. `if (TexBase->Texture)` и
        // `Texture->GetPathName()` разрешают object handle и могут утащить
        // game thread в синхронную загрузку пакета текстуры — именно это
        // вешало редактор в 3.24.0. GetPathName()/IsResolved() читают хендл
        // как есть, без разрешения.
        if (const UMaterialExpressionTextureBase* TexBase = Cast<UMaterialExpressionTextureBase>(E))
        {
            const FString TexPath = TexBase->Texture.GetPathName();
            const bool bHasTexture = !TexPath.IsEmpty() && TexPath != TEXT("None");
            P->SetStringField(TEXT("Texture"), bHasTexture ? ShortNameFromPath(TexPath) : FString());
            P->SetStringField(TEXT("TexturePath"), bHasTexture ? TexPath : FString());
            P->SetBoolField(TEXT("TextureLoaded"), TexBase->Texture.IsResolved());
        }

        if (const UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(E))
        {
            P->SetNumberField(TEXT("CoordinateIndex"), TexCoord->CoordinateIndex);
            P->SetNumberField(TEXT("UTiling"), TexCoord->UTiling);
            P->SetNumberField(TEXT("VTiling"), TexCoord->VTiling);
        }

        if (const UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(E))
        {
            P->SetNumberField(TEXT("ConstA"), AddExpr->ConstA);
            P->SetNumberField(TEXT("ConstB"), AddExpr->ConstB);
        }
        else if (const UMaterialExpressionMultiply* MulExpr = Cast<UMaterialExpressionMultiply>(E))
        {
            P->SetNumberField(TEXT("ConstA"), MulExpr->ConstA);
            P->SetNumberField(TEXT("ConstB"), MulExpr->ConstB);
        }
        else if (const UMaterialExpressionLinearInterpolate* LerpExpr = Cast<UMaterialExpressionLinearInterpolate>(E))
        {
            P->SetNumberField(TEXT("ConstA"), LerpExpr->ConstA);
            P->SetNumberField(TEXT("ConstB"), LerpExpr->ConstB);
            P->SetNumberField(TEXT("ConstAlpha"), LerpExpr->ConstAlpha);
        }

        // Custom HLSL nodes: truncate, shader bodies can run to thousands of chars.
        if (const UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(E))
        {
            const int32 MaxCodeChars = 2000;
            const FString& Code = CustomExpr->Code;
            P->SetStringField(TEXT("Code"), Code.Len() > MaxCodeChars ? Code.Left(MaxCodeChars) : Code);
            P->SetBoolField(TEXT("CodeTruncated"), Code.Len() > MaxCodeChars);
        }
    }
}
#endif // WITH_EDITOR

TSharedPtr<FJsonObject> FMaterialCommands::HandleMaterialGetGraph(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
    return FAssetCommonUtils::MakeFailureResponse(
        TEXT("config"),
        TEXT("EDITOR_ONLY"),
        TEXT("material_get_graph requires an editor build (material expressions are editor-only data)"));
#else
    using MaterialGraphRead::Step;

    const double StartSeconds = FPlatformTime::Seconds();

    Step(TEXT("enter handler"));

    FString AssetPath;
    TSharedPtr<FJsonObject> ParamFailure;
    if (!FAssetCommonUtils::RequireAssetPath(Params, AssetPath, ParamFailure))
    {
        Step(TEXT("missing assetPath -> bail"));
        return ParamFailure;
    }

    bool bIncludeUnconnectedInputs = true;
    Params->TryGetBoolField(TEXT("includeUnconnectedInputs"), bIncludeUnconnectedInputs);

    int32 MaxNodes = 500;
    {
        double Tmp = 0.0;
        if (Params->TryGetNumberField(TEXT("maxNodes"), Tmp) && Tmp > 0.0)
        {
            MaxNodes = static_cast<int32>(Tmp);
        }
    }

    // Бюджет времени. Любая непредвиденная заминка теперь превращается в
    // ответ с timedOut:true, а не в мёртвый мост.
    // Дефолт 4с намеренно МЕНЬШЕ 5-секундного socket-таймаута Python-клиента
    // (unreal_mcp_server.py: settimeout(5)): иначе клиент отваливается раньше,
    // чем плагин успевает отдать частичный ответ, и наружу это выглядит как
    // тот же самый висяк.
    double TimeBudgetSeconds = 4.0;
    {
        double Tmp = 0.0;
        if (Params->TryGetNumberField(TEXT("timeBudgetSeconds"), Tmp) && Tmp > 0.0)
        {
            TimeBudgetSeconds = Tmp;
        }
    }

    Step(FString::Printf(TEXT("params: assetPath='%s' maxNodes=%d includeUnconnected=%d budget=%.1fs"),
        *AssetPath, MaxNodes, bIncludeUnconnectedInputs ? 1 : 0, TimeBudgetSeconds));

    Step(TEXT("resolving asset (StaticFindObject, then StaticLoadObject) ..."));
    UObject* Obj = FAssetCommonUtils::LoadAssetObject(AssetPath);
    Step(Obj ? TEXT("asset resolved") : TEXT("asset NOT resolved"));

    if (!Obj)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("ASSET_NOT_FOUND"),
            FString::Printf(TEXT("No asset found at '%s'"), *AssetPath),
            Details);
    }

    // Accept both UMaterial and UMaterialInstance — an instance has no graph of
    // its own, so fall through to the base material whose params it overrides.
    FString ResolvedFrom = TEXT("material");
    UMaterial* Mat = Cast<UMaterial>(Obj);
    if (!Mat)
    {
        if (UMaterialInterface* MatIface = Cast<UMaterialInterface>(Obj))
        {
            Step(TEXT("asset is a MaterialInstance -> GetBaseMaterial() ..."));
            Mat = MatIface->GetBaseMaterial();
            ResolvedFrom = TEXT("instance");
            Step(TEXT("base material resolved"));
        }
    }

    if (!Mat)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("assetPath"), AssetPath);
        Details->SetStringField(TEXT("actualClass"), Obj->GetClass()->GetName());
        return FAssetCommonUtils::MakeFailureResponse(
            TEXT("user"),
            TEXT("MATERIAL_NOT_FOUND"),
            FString::Printf(TEXT("Asset at '%s' is not a UMaterial or UMaterialInstance"), *AssetPath),
            Details);
    }

    // ── Nodes ────────────────────────────────────────────────────────────────
    Step(TEXT("fetching expression collection ..."));
    TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();
    Step(FString::Printf(TEXT("expression collection size = %d"), Expressions.Num()));

    TArray<TSharedPtr<FJsonValue>> NodesJson;
    int32 TotalExpressions = 0;
    bool bTimedOut = false;

    for (int32 NodeIndex = 0; NodeIndex < Expressions.Num(); ++NodeIndex)
    {
        if (FPlatformTime::Seconds() - StartSeconds > TimeBudgetSeconds)
        {
            bTimedOut = true;
            Step(FString::Printf(TEXT("TIME BUDGET EXCEEDED at node index %d — returning partial graph"), NodeIndex));
            break;
        }

        UMaterialExpression* E = Expressions[NodeIndex].Get();
        if (!E || !IsValid(E))
        {
            continue;
        }
        ++TotalExpressions;
        if (NodesJson.Num() >= MaxNodes)
        {
            continue;
        }

        const FString NodeName = E->GetName();
        const FString NodeClass = E->GetClass()->GetName();
        Step(FString::Printf(TEXT("node[%d] %s (%s) — props"), NodeIndex, *NodeName, *NodeClass));

        TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
        NodeJson->SetStringField(TEXT("name"), NodeName);
        NodeJson->SetStringField(TEXT("class"), NodeClass);
        NodeJson->SetStringField(TEXT("nodeId"), E->Desc);
        NodeJson->SetNumberField(TEXT("posX"), E->MaterialExpressionEditorX);
        NodeJson->SetNumberField(TEXT("posY"), E->MaterialExpressionEditorY);

        TSharedPtr<FJsonObject> PropsJson = MakeShared<FJsonObject>();
        MaterialGraphRead::FillNodeProps(E, PropsJson);
        NodeJson->SetObjectField(TEXT("props"), PropsJson);

        Step(FString::Printf(TEXT("node[%d] %s — inputs"), NodeIndex, *NodeName));

        // НЕ используем E->CountInputs(): его тело — while (GetInput(i)) ++i,
        // цикл завершает virtual-override узла. Идём сами с жёстким потолком.
        TArray<TSharedPtr<FJsonValue>> InputsJson;
        for (int32 i = 0; i < MaterialGraphRead::MaxInputsPerNode; ++i)
        {
            const FExpressionInput* In = E->GetInput(i);
            if (!In)
            {
                break;
            }

            UMaterialExpression* Src = In->Expression.Get();
            const bool bConnected = (Src != nullptr) && IsValid(Src);
            if (!bConnected && !bIncludeUnconnectedInputs)
            {
                continue;
            }

            TSharedPtr<FJsonObject> InputJson = MakeShared<FJsonObject>();
            InputJson->SetNumberField(TEXT("index"), i);
            InputJson->SetStringField(TEXT("name"), E->GetInputName(i).ToString());
            InputJson->SetBoolField(TEXT("connected"), bConnected);
            if (bConnected)
            {
                InputJson->SetStringField(TEXT("fromNode"), Src->GetName());
                InputJson->SetStringField(TEXT("fromClass"), Src->GetClass()->GetName());
                InputJson->SetNumberField(TEXT("fromOutputIndex"), In->OutputIndex);
                if (!Src->Desc.IsEmpty())
                {
                    InputJson->SetStringField(TEXT("fromNodeId"), Src->Desc);
                }
            }
            InputsJson.Add(MakeShared<FJsonValueObject>(InputJson));
        }
        NodeJson->SetArrayField(TEXT("inputs"), InputsJson);

        NodesJson.Add(MakeShared<FJsonValueObject>(NodeJson));
    }

    Step(FString::Printf(TEXT("nodes done (%d serialized) — root connections"), NodesJson.Num()));

    // ── Root (material attribute) connections ────────────────────────────────
    // In UE 5.8 these live on UMaterialEditorOnlyData, NOT on UMaterial itself.
    TSharedPtr<FJsonObject> RootJson = MakeShared<FJsonObject>();
#if WITH_EDITORONLY_DATA
    if (const UMaterialEditorOnlyData* EditorOnly = Mat->GetEditorOnlyData())
    {
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("BaseColor"),           EditorOnly->BaseColor);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Metallic"),            EditorOnly->Metallic);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Specular"),            EditorOnly->Specular);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Roughness"),           EditorOnly->Roughness);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Anisotropy"),          EditorOnly->Anisotropy);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Normal"),              EditorOnly->Normal);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Tangent"),             EditorOnly->Tangent);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("EmissiveColor"),       EditorOnly->EmissiveColor);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Opacity"),             EditorOnly->Opacity);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("OpacityMask"),         EditorOnly->OpacityMask);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("WorldPositionOffset"), EditorOnly->WorldPositionOffset);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Displacement"),        EditorOnly->Displacement);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("SubsurfaceColor"),     EditorOnly->SubsurfaceColor);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("ClearCoat"),           EditorOnly->ClearCoat);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("ClearCoatRoughness"),  EditorOnly->ClearCoatRoughness);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("AmbientOcclusion"),    EditorOnly->AmbientOcclusion);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("Refraction"),          EditorOnly->Refraction);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("PixelDepthOffset"),    EditorOnly->PixelDepthOffset);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("SurfaceThickness"),    EditorOnly->SurfaceThickness);
        MaterialGraphRead::AddRootConnection(RootJson, TEXT("MaterialAttributes"),  EditorOnly->MaterialAttributes);

        // CustomizedUVs[0..7] — a Panner wired here pans *every* sampler that
        // reads TexCoord[N]; that is the usual way scrolling tank tracks work.
        for (int32 UVIndex = 0; UVIndex < 8; ++UVIndex)
        {
            MaterialGraphRead::AddRootConnection(
                RootJson,
                *FString::Printf(TEXT("CustomizedUV%d"), UVIndex),
                EditorOnly->CustomizedUVs[UVIndex]);
        }
    }
#endif // WITH_EDITORONLY_DATA

    Step(TEXT("root connections done — building response"));

    TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("materialPath"), Mat->GetPathName());
    Meta->SetStringField(TEXT("resolvedFrom"), ResolvedFrom);
    Meta->SetNumberField(TEXT("nodeCount"), TotalExpressions);
    Meta->SetNumberField(TEXT("returnedNodeCount"), NodesJson.Num());
    Meta->SetBoolField(TEXT("truncated"), TotalExpressions > NodesJson.Num());
    Meta->SetBoolField(TEXT("timedOut"), bTimedOut);
    Meta->SetNumberField(TEXT("elapsedSeconds"), FPlatformTime::Seconds() - StartSeconds);
    Meta->SetArrayField(TEXT("nodes"), NodesJson);
    Meta->SetObjectField(TEXT("rootConnections"), RootJson);

    Step(TEXT("done"));

    return FAssetCommonUtils::MakeSuccessResponse(TEXT("read"), AssetPath, Meta);
#endif // WITH_EDITOR
}
