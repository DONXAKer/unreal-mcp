#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

TSharedPtr<FJsonObject> FBPVariables::CreateVariable(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString VariableName = Params->GetStringField(TEXT("variable_name"));
    FString VariableType = Params->GetStringField(TEXT("variable_type"));

    bool IsPublic = Params->HasField(TEXT("is_public")) ? Params->GetBoolField(TEXT("is_public")) : false;
    FString Tooltip = Params->HasField(TEXT("tooltip")) ? Params->GetStringField(TEXT("tooltip")) : TEXT("");
    FString Category = Params->HasField(TEXT("category")) ? Params->GetStringField(TEXT("category")) : TEXT("Default");

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);

    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Blueprint not found");
        return Result;
    }

    FEdGraphPinType VarType = GetPinTypeFromString(VariableType);
    FName VarName = FName(*VariableName);

    if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, VarType))
    {
        FBPVariableDescription& Variable = Blueprint->NewVariables.Last();
        Variable.FriendlyName = VariableName;
        Variable.Category = FText::FromString(Category);
        Variable.PropertyFlags = CPF_BlueprintVisible | CPF_BlueprintReadOnly;
        if (IsPublic)
        {
            Variable.PropertyFlags |= CPF_Edit;
        }

        if (!Tooltip.IsEmpty())
        {
            Variable.SetMetaData(FBlueprintMetadata::MD_Tooltip, Tooltip);
        }

        if (Params->HasField(TEXT("default_value")))
        {
            SetDefaultValue(Variable, Params->Values.FindRef("default_value"));
        }

        Blueprint->MarkPackageDirty();

        // Force immediate refresh of the Blueprint editor
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

        // Force asset registry update
        if (GEditor)
        {
            // Note: Asset registry notifications removed for UE5.5 compatibility
            // FAssetRegistryModule::AssetRegistryHelpers::GetAssetRegistry().AssetCreated(Blueprint);

            // Broadcast compilation event to refresh all editors
            // GEditor->BroadcastBlueprintCompiled(Blueprint); // Removed for UE5.5 compatibility

            // Additional refresh for property windows
            FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
            PropertyModule.NotifyCustomizationModuleChanged();
        }

        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        Result->SetBoolField("success", true);

        TSharedPtr<FJsonObject> VarInfo = MakeShared<FJsonObject>();
        VarInfo->SetStringField("name", VariableName);
        VarInfo->SetStringField("type", VariableType);
        VarInfo->SetBoolField("is_public", IsPublic);
        VarInfo->SetStringField("category", Category);

        Result->SetObjectField("variable", VarInfo);
    }
    else
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Failed to create variable");
    }

    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::SetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString VariableName = Params->GetStringField(TEXT("variable_name"));

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);

    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
        return Result;
    }

    // Find the variable in the Blueprint
    FBPVariableDescription* VarDesc = nullptr;
    for (FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        if (Var.VarName == FName(*VariableName))
        {
            VarDesc = &Var;
            break;
        }
    }

    if (!VarDesc)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Variable not found: %s"), *VariableName));
        return Result;
    }

    // Track which properties were updated
    TSharedPtr<FJsonObject> UpdatedProperties = MakeShared<FJsonObject>();

    // Update var_name (rename variable)
    if (Params->HasField(TEXT("var_name")))
    {
        FString NewVarName = Params->GetStringField(TEXT("var_name"));
        VarDesc->VarName = FName(*NewVarName);
        UpdatedProperties->SetStringField("var_name", NewVarName);
    }

    // Update var_type (change variable type)
    if (Params->HasField(TEXT("var_type")))
    {
        FString TypeString = Params->GetStringField(TEXT("var_type"));
        FEdGraphPinType NewType = GetPinTypeFromString(TypeString);
        VarDesc->VarType = NewType;
        UpdatedProperties->SetStringField("var_type", TypeString);
    }

    // Update is_blueprint_writable (Set node)
    if (Params->HasField(TEXT("is_blueprint_writable")))
    {
        bool bIsWritable = Params->GetBoolField(TEXT("is_blueprint_writable"));
        if (bIsWritable)
        {
            VarDesc->PropertyFlags &= ~CPF_BlueprintReadOnly;
        }
        else
        {
            VarDesc->PropertyFlags |= CPF_BlueprintReadOnly;
        }
        UpdatedProperties->SetBoolField("is_blueprint_writable", bIsWritable);
    }

    // Update is_public
    if (Params->HasField(TEXT("is_public")))
    {
        bool bIsPublic = Params->GetBoolField(TEXT("is_public"));
        if (bIsPublic)
        {
            VarDesc->PropertyFlags |= CPF_Edit;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Edit;
        }
        UpdatedProperties->SetBoolField("is_public", bIsPublic);
    }

    // Update is_editable_in_instance (opposite of CPF_DisableEditOnInstance)
    if (Params->HasField(TEXT("is_editable_in_instance")))
    {
        bool bIsEditable = Params->GetBoolField(TEXT("is_editable_in_instance"));
        if (bIsEditable)
        {
            VarDesc->PropertyFlags &= ~CPF_DisableEditOnInstance;
        }
        else
        {
            VarDesc->PropertyFlags |= CPF_DisableEditOnInstance;
        }
        UpdatedProperties->SetBoolField("is_editable_in_instance", bIsEditable);
    }

    // Update is_config
    if (Params->HasField(TEXT("is_config")))
    {
        bool bIsConfig = Params->GetBoolField(TEXT("is_config"));
        if (bIsConfig)
        {
            VarDesc->PropertyFlags |= CPF_Config;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Config;
        }
        UpdatedProperties->SetBoolField("is_config", bIsConfig);
    }

    // Update friendly_name
    if (Params->HasField(TEXT("friendly_name")))
    {
        FString FriendlyName = Params->GetStringField(TEXT("friendly_name"));
        VarDesc->FriendlyName = FriendlyName;
        UpdatedProperties->SetStringField("friendly_name", FriendlyName);
    }

    // Update tooltip
    if (Params->HasField(TEXT("tooltip")))
    {
        FString Tooltip = Params->GetStringField(TEXT("tooltip"));
        VarDesc->SetMetaData(FBlueprintMetadata::MD_Tooltip, *Tooltip);
        UpdatedProperties->SetStringField("tooltip", Tooltip);
    }

    // Update category
    if (Params->HasField(TEXT("category")))
    {
        FString Category = Params->GetStringField(TEXT("category"));
        VarDesc->Category = FText::FromString(Category);
        UpdatedProperties->SetStringField("category", Category);
    }

    // Update replication_enabled (Row 15 - CPF_Net flag)
    if (Params->HasField(TEXT("replication_enabled")))
    {
        bool bReplicationEnabled = Params->GetBoolField(TEXT("replication_enabled"));
        if (bReplicationEnabled)
        {
            VarDesc->PropertyFlags |= CPF_Net;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Net;
        }
        UpdatedProperties->SetBoolField("replication_enabled", bReplicationEnabled);
    }

    // Update replication_condition (Row 16 - ELifetimeCondition)
    if (Params->HasField(TEXT("replication_condition")))
    {
        int32 ReplicationConditionValue = (int32)Params->GetNumberField(TEXT("replication_condition"));
        VarDesc->ReplicationCondition = (ELifetimeCondition)ReplicationConditionValue;
        UpdatedProperties->SetNumberField("replication_condition", ReplicationConditionValue);
    }

    // Update is_private (Row 7 - MD_AllowPrivateAccess metadata)
    if (Params->HasField(TEXT("is_private")))
    {
        bool bIsPrivate = Params->GetBoolField(TEXT("is_private"));
        if (bIsPrivate)
        {
            VarDesc->SetMetaData(TEXT("AllowPrivateAccess"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("AllowPrivateAccess"));
        }
        UpdatedProperties->SetBoolField("is_private", bIsPrivate);
    }

    // Update expose_on_spawn (metadata)
    if (Params->HasField(TEXT("expose_on_spawn")))
    {
        bool bExposeOnSpawn = Params->GetBoolField(TEXT("expose_on_spawn"));
        if (bExposeOnSpawn)
        {
            VarDesc->SetMetaData(TEXT("ExposeOnSpawn"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("ExposeOnSpawn"));
        }
        UpdatedProperties->SetBoolField("expose_on_spawn", bExposeOnSpawn);
    }

    // Update default_value
    if (Params->HasField(TEXT("default_value")))
    {
        SetDefaultValue(*VarDesc, Params->Values.FindRef("default_value"));
        UpdatedProperties->SetStringField("default_value", "updated");
    }

    // Update expose_to_cinematics (CPF_Interp)
    if (Params->HasField(TEXT("expose_to_cinematics")))
    {
        bool bExposeToCinematics = Params->GetBoolField(TEXT("expose_to_cinematics"));
        if (bExposeToCinematics)
        {
            VarDesc->PropertyFlags |= CPF_Interp;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Interp;
        }
        UpdatedProperties->SetBoolField("expose_to_cinematics", bExposeToCinematics);
    }

    // Update slider_range_min (MD_UIMin)
    if (Params->HasField(TEXT("slider_range_min")))
    {
        FString SliderMin = Params->GetStringField(TEXT("slider_range_min"));
        VarDesc->SetMetaData(TEXT("UIMin"), *SliderMin);
        UpdatedProperties->SetStringField("slider_range_min", SliderMin);
    }

    // Update slider_range_max (MD_UIMax)
    if (Params->HasField(TEXT("slider_range_max")))
    {
        FString SliderMax = Params->GetStringField(TEXT("slider_range_max"));
        VarDesc->SetMetaData(TEXT("UIMax"), *SliderMax);
        UpdatedProperties->SetStringField("slider_range_max", SliderMax);
    }

    // Update value_range_min (MD_ClampMin)
    if (Params->HasField(TEXT("value_range_min")))
    {
        FString ClampMin = Params->GetStringField(TEXT("value_range_min"));
        VarDesc->SetMetaData(TEXT("ClampMin"), *ClampMin);
        UpdatedProperties->SetStringField("value_range_min", ClampMin);
    }

    // Update value_range_max (MD_ClampMax)
    if (Params->HasField(TEXT("value_range_max")))
    {
        FString ClampMax = Params->GetStringField(TEXT("value_range_max"));
        VarDesc->SetMetaData(TEXT("ClampMax"), *ClampMax);
        UpdatedProperties->SetStringField("value_range_max", ClampMax);
    }

    // Update units (MD_Units)
    if (Params->HasField(TEXT("units")))
    {
        FString Units = Params->GetStringField(TEXT("units"));
        VarDesc->SetMetaData(TEXT("Units"), *Units);
        UpdatedProperties->SetStringField("units", Units);
    }

    // Update bitmask (MD_Bitmask)
    if (Params->HasField(TEXT("bitmask")))
    {
        bool bIsBitmask = Params->GetBoolField(TEXT("bitmask"));
        if (bIsBitmask)
        {
            VarDesc->SetMetaData(TEXT("Bitmask"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("Bitmask"));
        }
        UpdatedProperties->SetBoolField("bitmask", bIsBitmask);
    }

    // Update bitmask_enum (MD_BitmaskEnum)
    if (Params->HasField(TEXT("bitmask_enum")))
    {
        FString BitmaskEnum = Params->GetStringField(TEXT("bitmask_enum"));
        VarDesc->SetMetaData(TEXT("BitmaskEnum"), *BitmaskEnum);
        UpdatedProperties->SetStringField("bitmask_enum", BitmaskEnum);
    }

    // Mark Blueprint as modified and compile
    Blueprint->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Force property editor refresh for metadata changes
    // This ensures Details Panel dropdowns (Units, etc.) synchronize with metadata
    if (GEditor)
    {
        FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.NotifyCustomizationModuleChanged();
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    Result->SetBoolField("success", true);
    Result->SetStringField("variable_name", VariableName);
    Result->SetObjectField("properties_updated", UpdatedProperties);
    Result->SetStringField("message", "Variable properties updated successfully");

    return Result;
}

FEdGraphPinType FBPVariables::GetPinTypeFromString(const FString& TypeString)
{
    FEdGraphPinType PinType;

    // Поддержка контейнера массива: "array:inner_type"
    bool bIsArray = TypeString.StartsWith(TEXT("array:"));
    const FString InnerType = bIsArray ? TypeString.Mid(6) : TypeString;

    if (bIsArray)
    {
        PinType.ContainerType = EPinContainerType::Array;
    }

    // Accept both lowercase (internal) and title-case (Python tool) names.
    // Python sends "Integer", "Float", "Boolean", "String", "Name", "Text",
    // "Vector", "Rotator".  The old lowercase-only check caused "Integer" to
    // fall into the default branch and produce a real/float pin instead.
    const FString InnerTypeLower = InnerType.ToLower();

    if (InnerTypeLower == TEXT("bool") || InnerTypeLower == TEXT("boolean"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (InnerTypeLower == TEXT("int") || InnerTypeLower == TEXT("integer"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (InnerTypeLower == TEXT("int64"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
    }
    else if (InnerTypeLower == TEXT("float"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (InnerTypeLower == TEXT("double"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (InnerTypeLower == TEXT("string"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (InnerTypeLower == TEXT("text"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
    }
    else if (InnerTypeLower == TEXT("name"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
    }
    else if (InnerTypeLower == TEXT("vector"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (InnerTypeLower == TEXT("rotator"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (InnerType.StartsWith(TEXT("struct:")))
    {
        // Формат: "struct:/Script/PackageName.StructName" или "struct:ShortName"
        const FString StructPath = InnerType.Mid(7);
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;

        UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
        if (!Struct)
        {
            Struct = LoadObject<UScriptStruct>(nullptr, *StructPath);
        }
        if (!Struct)
        {
            Struct = FindFirstObject<UScriptStruct>(*StructPath, EFindFirstObjectOptions::None,
                ELogVerbosity::Warning, TEXT("MCP struct variable type lookup"));
        }
        if (Struct)
        {
            PinType.PinSubCategoryObject = Struct;
        }
    }
    else if (InnerType.StartsWith(TEXT("object:")))
    {
        // Формат: "object:/Script/PackageName.ClassName" или "object:ShortName"
        const FString ClassPath = InnerType.Mid(7);
        PinType.PinCategory = UEdGraphSchema_K2::PC_Object;

        UClass* TargetClass = nullptr;
        if (ClassPath.StartsWith(TEXT("/")))
        {
            TargetClass = Cast<UClass>(StaticFindObject(UClass::StaticClass(), nullptr, *ClassPath));
            if (!TargetClass)
            {
                TargetClass = LoadObject<UClass>(nullptr, *ClassPath);
            }
        }
        if (!TargetClass)
        {
            TargetClass = FindFirstObject<UClass>(*ClassPath, EFindFirstObjectOptions::None,
                ELogVerbosity::Warning, TEXT("MCP object variable type lookup"));
        }
        if (TargetClass)
        {
            PinType.PinSubCategoryObject = TargetClass;
        }
    }
    else
    {
        // По умолчанию: float
        PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }

    return PinType;
}

void FBPVariables::SetDefaultValue(FBPVariableDescription& Variable, const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
    {
        return;
    }

    FString StringValue;

    // Convert JSON value to string representation for default value
    if (Value->Type == EJson::String)
    {
        StringValue = Value->AsString();
    }
    else if (Value->Type == EJson::Number)
    {
        StringValue = FString::Printf(TEXT("%g"), Value->AsNumber());
    }
    else if (Value->Type == EJson::Boolean)
    {
        StringValue = Value->AsBool() ? TEXT("true") : TEXT("false");
    }
    else if (Value->Type == EJson::Null)
    {
        StringValue = TEXT("");
    }
    else
    {
        // For complex types, convert to empty string
        StringValue = TEXT("");
    }

    // Update Variable.DefaultValue for Blueprint display
    Variable.DefaultValue = StringValue;
}

// ---------------------------------------------------------------------------
// Phase 1B (v1.11.0) — Variable lifecycle
// ---------------------------------------------------------------------------

namespace
{
    static FBPVariableDescription* FindVarDesc(UBlueprint* Blueprint, const FString& VariableName)
    {
        if (!Blueprint) return nullptr;
        for (FBPVariableDescription& Var : Blueprint->NewVariables)
        {
            if (Var.VarName == FName(*VariableName))
            {
                return &Var;
            }
        }
        return nullptr;
    }

    static FString PinTypeToDisplayString(const FEdGraphPinType& PinType)
    {
        FString Base;
        const FName Cat = PinType.PinCategory;
        if (Cat == UEdGraphSchema_K2::PC_Boolean) Base = TEXT("bool");
        else if (Cat == UEdGraphSchema_K2::PC_Int) Base = TEXT("int");
        else if (Cat == UEdGraphSchema_K2::PC_Real)
        {
            Base = (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float) ? TEXT("float") : TEXT("double");
        }
        else if (Cat == UEdGraphSchema_K2::PC_String) Base = TEXT("string");
        else if (Cat == UEdGraphSchema_K2::PC_Text) Base = TEXT("text");
        else if (Cat == UEdGraphSchema_K2::PC_Name) Base = TEXT("name");
        else if (Cat == UEdGraphSchema_K2::PC_Struct)
        {
            const UObject* Obj = PinType.PinSubCategoryObject.Get();
            Base = FString::Printf(TEXT("struct:%s"), Obj ? *Obj->GetName() : TEXT("?"));
        }
        else if (Cat == UEdGraphSchema_K2::PC_Object)
        {
            const UObject* Obj = PinType.PinSubCategoryObject.Get();
            Base = FString::Printf(TEXT("object:%s"), Obj ? *Obj->GetName() : TEXT("?"));
        }
        else
        {
            Base = Cat.ToString();
        }
        if (PinType.ContainerType == EPinContainerType::Array)
        {
            return FString::Printf(TEXT("array:%s"), *Base);
        }
        return Base;
    }
}

TSharedPtr<FJsonObject> FBPVariables::RenameVariable(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString OldName;
    if (!Params->TryGetStringField(TEXT("old_name"), OldName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'old_name' parameter"));
    }
    FString NewName;
    if (!Params->TryGetStringField(TEXT("new_name"), NewName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    if (!FindVarDesc(Blueprint, OldName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *OldName));
    }
    if (FindVarDesc(Blueprint, NewName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable name already exists: %s"), *NewName));
    }

    // RenameMemberVariable updates all Get/Set node references and metadata.
    FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*OldName), FName(*NewName));

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("old_name"), OldName);
    Result->SetStringField(TEXT("new_name"), NewName);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::DeleteVariable(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    if (!FindVarDesc(Blueprint, VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
    }

    // RemoveMemberVariable will also strip Get/Set references throughout graphs.
    FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VariableName));

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("variable_name"), VariableName);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::SetVariableDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }
    if (!Params->HasField(TEXT("default_value")))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'default_value' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    FBPVariableDescription* VarDesc = FindVarDesc(Blueprint, VariableName);
    if (!VarDesc)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
    }

    // 1) Update the BP's stored DefaultValue (used for new instances and editor display).
    SetDefaultValue(*VarDesc, Params->Values.FindRef(TEXT("default_value")));

    // 2) Push the value into the generated class' CDO so existing references see it.
    if (Blueprint->GeneratedClass)
    {
        UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
        FProperty* Prop = Blueprint->GeneratedClass->FindPropertyByName(FName(*VariableName));
        if (CDO && Prop && !VarDesc->DefaultValue.IsEmpty())
        {
            // PropertyValueFromString parses the textual form into the CDO instance.
            FBlueprintEditorUtils::PropertyValueFromString(Prop, VarDesc->DefaultValue, reinterpret_cast<uint8*>(CDO));
            CDO->Modify();
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("variable_name"), VariableName);
    Result->SetStringField(TEXT("default_value"), VarDesc->DefaultValue);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::ListVariables(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    TArray<TSharedPtr<FJsonValue>> Arr;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Var.VarName.ToString());
        Obj->SetStringField(TEXT("type"), PinTypeToDisplayString(Var.VarType));
        // CPF_Edit ⇒ Instance Editable
        Obj->SetBoolField(TEXT("is_instance_editable"), (Var.PropertyFlags & CPF_Edit) != 0);
        // ExposeOnSpawn is metadata, not a flag in modern UE
        Obj->SetBoolField(TEXT("expose_on_spawn"), Var.HasMetaData(TEXT("ExposeOnSpawn")));
        // BlueprintReadOnly flag
        Obj->SetBoolField(TEXT("blueprint_read_only"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
        Obj->SetStringField(TEXT("category"), Var.Category.ToString());
        Obj->SetStringField(TEXT("default_value"), Var.DefaultValue);
        // Replication info
        const bool bReplicated = (Var.PropertyFlags & CPF_Net) != 0;
        const bool bRepNotify  = (Var.PropertyFlags & CPF_RepNotify) != 0;
        FString Replication = TEXT("None");
        if (bRepNotify) Replication = TEXT("RepNotify");
        else if (bReplicated) Replication = TEXT("Replicated");
        Obj->SetStringField(TEXT("replication"), Replication);

        Arr.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetArrayField(TEXT("variables"), Arr);
    Result->SetNumberField(TEXT("count"), Arr.Num());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::SetVariableFlags(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }
    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    FBPVariableDescription* VarDesc = FindVarDesc(Blueprint, VariableName);
    if (!VarDesc)
    {
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Variable not found: %s"), *VariableName));
    }

    TSharedPtr<FJsonObject> Updated = MakeShared<FJsonObject>();

    // instance_editable -> CPF_Edit
    if (Params->HasField(TEXT("instance_editable")))
    {
        const bool b = Params->GetBoolField(TEXT("instance_editable"));
        if (b) VarDesc->PropertyFlags |= CPF_Edit;
        else   VarDesc->PropertyFlags &= ~CPF_Edit;
        Updated->SetBoolField(TEXT("instance_editable"), b);
    }

    // blueprint_read_only -> CPF_BlueprintReadOnly
    if (Params->HasField(TEXT("blueprint_read_only")))
    {
        const bool b = Params->GetBoolField(TEXT("blueprint_read_only"));
        if (b) VarDesc->PropertyFlags |= CPF_BlueprintReadOnly;
        else   VarDesc->PropertyFlags &= ~CPF_BlueprintReadOnly;
        Updated->SetBoolField(TEXT("blueprint_read_only"), b);
    }

    // expose_on_spawn -> CPF_ExposeOnSpawn + metadata "ExposeOnSpawn" + CPF_Edit (must also be editable)
    if (Params->HasField(TEXT("expose_on_spawn")))
    {
        const bool b = Params->GetBoolField(TEXT("expose_on_spawn"));
        if (b)
        {
            VarDesc->PropertyFlags |= CPF_ExposeOnSpawn;
            VarDesc->PropertyFlags |= CPF_Edit; // ExposeOnSpawn requires Edit
            VarDesc->SetMetaData(TEXT("ExposeOnSpawn"), TEXT("true"));
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_ExposeOnSpawn;
            VarDesc->RemoveMetaData(TEXT("ExposeOnSpawn"));
        }
        Updated->SetBoolField(TEXT("expose_on_spawn"), b);
    }

    // category (FText)
    if (Params->HasField(TEXT("category")))
    {
        const FString Cat = Params->GetStringField(TEXT("category"));
        VarDesc->Category = FText::FromString(Cat);
        Updated->SetStringField(TEXT("category"), Cat);
    }

    // replication: "None" | "Replicated" | "RepNotify"
    if (Params->HasField(TEXT("replication")))
    {
        const FString Mode = Params->GetStringField(TEXT("replication"));
        // Reset both first.
        VarDesc->PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
        if (Mode.Equals(TEXT("Replicated"), ESearchCase::IgnoreCase))
        {
            VarDesc->PropertyFlags |= CPF_Net;
        }
        else if (Mode.Equals(TEXT("RepNotify"), ESearchCase::IgnoreCase))
        {
            VarDesc->PropertyFlags |= (CPF_Net | CPF_RepNotify);
        }
        Updated->SetStringField(TEXT("replication"), Mode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_name"), BlueprintName);
    Result->SetStringField(TEXT("variable_name"), VariableName);
    Result->SetObjectField(TEXT("flags_updated"), Updated);
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}