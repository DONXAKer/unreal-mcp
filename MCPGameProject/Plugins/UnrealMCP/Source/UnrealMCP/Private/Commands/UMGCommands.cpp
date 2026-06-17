#include "Commands/UMGCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"

// Widget Blueprint / Widget Tree
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

// UMG panel and leaf widgets
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/SpinBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"

// Blueprint editor utils
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/Layout/Anchors.h"

// For create_umg_widget_blueprint / bind_widget_event / add_widget_to_viewport / set_text_block_binding
#include "WidgetBlueprintFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace UMGCommandsUtils
{
    /** Parse "R,G,B" or "R,G,B,A" string into FLinearColor. Returns false on failure. */
    static bool ParseLinearColor(const FString& Str, FLinearColor& OutColor)
    {
        TArray<FString> Parts;
        Str.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() < 3) return false;
        OutColor.R = FCString::Atof(*Parts[0]);
        OutColor.G = FCString::Atof(*Parts[1]);
        OutColor.B = FCString::Atof(*Parts[2]);
        OutColor.A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.0f;
        return true;
    }

    /** Parse "X,Y" string into FVector2D. Returns false on failure. */
    static bool ParseVector2D(const FString& Str, FVector2D& OutVec)
    {
        TArray<FString> Parts;
        Str.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() < 2) return false;
        OutVec.X = FCString::Atof(*Parts[0]);
        OutVec.Y = FCString::Atof(*Parts[1]);
        return true;
    }

    /** Parse "T,L,B,R", "H,V", or single float into FMargin. Returns false on failure. */
    static bool ParseMargin(const FString& Str, FMargin& OutMargin)
    {
        TArray<FString> Parts;
        Str.ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() == 1)
        {
            float Val = FCString::Atof(*Parts[0]);
            OutMargin = FMargin(Val);
            return true;
        }
        if (Parts.Num() == 2)
        {
            OutMargin = FMargin(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
            return true;
        }
        if (Parts.Num() == 4)
        {
            OutMargin = FMargin(
                FCString::Atof(*Parts[0]),
                FCString::Atof(*Parts[1]),
                FCString::Atof(*Parts[2]),
                FCString::Atof(*Parts[3])
            );
            return true;
        }
        return false;
    }

    static EHorizontalAlignment ParseHAlign(const FString& Str)
    {
        if (Str == TEXT("Center"))  return HAlign_Center;
        if (Str == TEXT("Right"))   return HAlign_Right;
        if (Str == TEXT("Fill"))    return HAlign_Fill;
        return HAlign_Left;
    }

    static EVerticalAlignment ParseVAlign(const FString& Str)
    {
        if (Str == TEXT("Center"))  return VAlign_Center;
        if (Str == TEXT("Bottom"))  return VAlign_Bottom;
        if (Str == TEXT("Fill"))    return VAlign_Fill;
        return VAlign_Top;
    }

    static ESlateVisibility ParseVisibility(const FString& Str)
    {
        if (Str == TEXT("Collapsed"))           return ESlateVisibility::Collapsed;
        if (Str == TEXT("Hidden"))              return ESlateVisibility::Hidden;
        if (Str == TEXT("HitTestInvisible"))    return ESlateVisibility::HitTestInvisible;
        if (Str == TEXT("SelfHitTestInvisible")) return ESlateVisibility::SelfHitTestInvisible;
        return ESlateVisibility::Visible;
    }

    /** Read a JSON array of numbers into a FVector2D. Returns false if missing/too short. */
    static bool GetVector2DFromArray(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FVector2D& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (!Params->TryGetArrayField(FieldName, Arr) || !Arr || Arr->Num() < 2) return false;
        Out.X = (float)(*Arr)[0]->AsNumber();
        Out.Y = (float)(*Arr)[1]->AsNumber();
        return true;
    }

    /** Read a JSON array of numbers into a FLinearColor. Returns false if missing/too short. */
    static bool GetLinearColorFromArray(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, FLinearColor& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (!Params->TryGetArrayField(FieldName, Arr) || !Arr || Arr->Num() < 3) return false;
        Out.R = (float)(*Arr)[0]->AsNumber();
        Out.G = (float)(*Arr)[1]->AsNumber();
        Out.B = (float)(*Arr)[2]->AsNumber();
        Out.A = Arr->Num() >= 4 ? (float)(*Arr)[3]->AsNumber() : 1.0f;
        return true;
    }

    /**
     * Load a Widget Blueprint by short name or full content path.
     * Tries several common UI asset locations ( /Game/UI, /Game/Blueprints, /Game/Widgets ).
     */
    static UWidgetBlueprint* LoadWidgetBlueprintByName(const FString& WidgetName)
    {
        // If caller passed a full path ( /Game/... ), try that first.
        if (WidgetName.StartsWith(TEXT("/")))
        {
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(WidgetName))
            {
                if (UWidgetBlueprint* WB = Cast<UWidgetBlueprint>(Asset)) return WB;
            }
        }

        // Try common UI paths by short name.
        const TArray<FString> SearchPaths = {
            TEXT("/Game/UI/"),
            TEXT("/Game/Blueprints/"),
            TEXT("/Game/Widgets/"),
            TEXT("/Game/UI/Widgets/"),
        };
        for (const FString& Dir : SearchPaths)
        {
            const FString ObjectPath = Dir + WidgetName + TEXT(".") + WidgetName;
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(ObjectPath))
            {
                if (UWidgetBlueprint* WB = Cast<UWidgetBlueprint>(Asset)) return WB;
            }
            const FString PackagePath = Dir + WidgetName;
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(PackagePath))
            {
                if (UWidgetBlueprint* WB = Cast<UWidgetBlueprint>(Asset)) return WB;
            }
        }

        // Last resort: shared Blueprint finder (handles /Game/Blueprints/ too).
        if (UBlueprint* BP = FEpicUnrealMCPCommonUtils::FindBlueprint(WidgetName))
        {
            return Cast<UWidgetBlueprint>(BP);
        }
        return nullptr;
    }

    /** Ensure WidgetTree has a UCanvasPanel as root; return it (or nullptr if root is wrong type). */
    static UCanvasPanel* EnsureRootCanvasPanel(UWidgetBlueprint* WB)
    {
        if (!WB || !WB->WidgetTree) return nullptr;
        if (!WB->WidgetTree->RootWidget)
        {
            UCanvasPanel* NewRoot = WB->WidgetTree->ConstructWidget<UCanvasPanel>(
                UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
            WB->WidgetTree->RootWidget = NewRoot;
            return NewRoot;
        }
        return Cast<UCanvasPanel>(WB->WidgetTree->RootWidget);
    }


    /**
     * Find a UPanelWidget inside the WidgetTree by name.
     * Supports optional parent_name param in add_*_to_widget commands.
     * Returns nullptr if name is empty or widget is not a panel.
     */
    static UPanelWidget* FindParentPanelWidget(UWidgetBlueprint* WB, const FString& ParentName)
    {
        if (!WB || !WB->WidgetTree || ParentName.IsEmpty()) return nullptr;
        UWidget* Found = WB->WidgetTree->FindWidget(FName(*ParentName));
        return Found ? Cast<UPanelWidget>(Found) : nullptr;
    }

    /** Persist Widget Blueprint: mark dirty + save package to disk. */
    static void MarkAndSaveWidgetBlueprint(UWidgetBlueprint* WB)
    {
        if (!WB) return;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WB);
        if (UPackage* Pkg = WB->GetPackage())
        {
            Pkg->MarkPackageDirty();
        }
        // UEditorAssetLibrary::SaveAsset expects the object path (e.g. /Game/UI/WBP_Foo).
        const FString ObjectPath = WB->GetPathName();
        // Strip the trailing ".WBP_Foo" class/object suffix if present.
        FString PackageName = ObjectPath;
        int32 DotIdx;
        if (PackageName.FindChar('.', DotIdx))
        {
            PackageName = PackageName.Left(DotIdx);
        }
        UEditorAssetLibrary::SaveAsset(PackageName, /*bOnlyIfIsDirty=*/ false);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Dispatch
// ─────────────────────────────────────────────────────────────────────────────

FUnrealMCPUMGCommands::FUnrealMCPUMGCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_widget_to_umg"))            return HandleAddWidgetToUMG(Params);
    if (CommandType == TEXT("add_text_block_to_widget"))     return HandleAddTextBlockToWidget(Params);
    if (CommandType == TEXT("add_button_to_widget"))         return HandleAddButtonToWidget(Params);
    if (CommandType == TEXT("add_panel_widget_to_widget"))   return HandleAddPanelWidgetToWidget(Params);
    if (CommandType == TEXT("delete_widget_from_umg"))       return HandleDeleteWidgetFromUMG(Params);
    if (CommandType == TEXT("set_widget_property"))          return HandleSetWidgetProperty(Params);
    if (CommandType == TEXT("get_umg_hierarchy"))            return HandleGetUMGHierarchy(Params);
    if (CommandType == TEXT("create_umg_widget_blueprint"))  return HandleCreateUMGWidgetBlueprint(Params);
    if (CommandType == TEXT("bind_widget_event"))            return HandleBindWidgetEvent(Params);
    if (CommandType == TEXT("add_widget_to_viewport"))       return HandleAddWidgetToViewport(Params);
    if (CommandType == TEXT("set_text_block_binding"))       return HandleSetTextBlockBinding(Params);

    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Unknown UMG command: %s"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget type map
// ─────────────────────────────────────────────────────────────────────────────

UClass* FUnrealMCPUMGCommands::GetWidgetClassByType(const FString& WidgetType)
{
    // Supported widget types and their UClasses.
    // Not static: UClass* in static storage without UPROPERTY is GC-unsafe by convention,
    // even though engine built-in classes carry RF_Native and are never collected in practice.
    const TMap<FString, UClass*> TypeMap = {
        { TEXT("CanvasPanel"),    UCanvasPanel::StaticClass()    },
        { TEXT("TextBlock"),      UTextBlock::StaticClass()      },
        { TEXT("Button"),         UButton::StaticClass()         },
        { TEXT("VerticalBox"),    UVerticalBox::StaticClass()    },
        { TEXT("HorizontalBox"),  UHorizontalBox::StaticClass()  },
        { TEXT("ScrollBox"),      UScrollBox::StaticClass()      },
        { TEXT("SizeBox"),        USizeBox::StaticClass()        },
        { TEXT("Spacer"),         USpacer::StaticClass()         },
        { TEXT("SpinBox"),        USpinBox::StaticClass()        },
        { TEXT("Image"),          UImage::StaticClass()          },
        { TEXT("Overlay"),        UOverlay::StaticClass()        },
        { TEXT("Border"),         UBorder::StaticClass()         },
        { TEXT("ProgressBar"),    UProgressBar::StaticClass()   },
        { TEXT("WidgetSwitcher"), UWidgetSwitcher::StaticClass() },
    };

    UClass* const* Found = TypeMap.Find(WidgetType);
    return Found ? *Found : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_widget_to_umg
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddWidgetToUMG(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ──────────────────────────────────────────────────────
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));

    FString WidgetType;
    if (!Params->TryGetStringField(TEXT("widget_type"), WidgetType))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_type' parameter"));

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    // ── Optional params ───────────────────────────────────────────────────────
    FString ParentName;
    Params->TryGetStringField(TEXT("parent_name"), ParentName);

    bool bIsVariable = true;
    Params->TryGetBoolField(TEXT("is_variable"), bIsVariable);

    // ── Load Widget Blueprint ─────────────────────────────────────────────────
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(
        UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!WidgetBlueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *BlueprintPath));

    if (!WidgetBlueprint->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));

    // ── Resolve widget class ──────────────────────────────────────────────────
    UClass* WidgetClass = GetWidgetClassByType(WidgetType);
    if (!WidgetClass)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown widget type: '%s'. Supported: CanvasPanel, TextBlock, Button, "
                "VerticalBox, HorizontalBox, ScrollBox, SizeBox, Spacer, SpinBox, Image, Overlay, Border, ProgressBar, WidgetSwitcher"),
                *WidgetType));

    // ── Check name uniqueness ─────────────────────────────────────────────────
    if (WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName)))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' already exists in this Widget Blueprint"), *WidgetName));

    // ── Create the widget ─────────────────────────────────────────────────────
    UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(
        WidgetClass, FName(*WidgetName));
    if (!NewWidget)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ConstructWidget returned null"));

    NewWidget->bIsVariable = bIsVariable;

    // ── Attach to hierarchy ───────────────────────────────────────────────────
    if (ParentName.IsEmpty())
    {
        // No parent specified: set as root or add to root panel
        if (!WidgetBlueprint->WidgetTree->RootWidget)
        {
            WidgetBlueprint->WidgetTree->RootWidget = NewWidget;
        }
        else
        {
            UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetBlueprint->WidgetTree->RootWidget);
            if (!RootPanel)
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    TEXT("Root widget is not a panel. Specify 'parent_name' explicitly."));
            RootPanel->AddChild(NewWidget);
        }
    }
    else
    {
        UWidget* ParentWidget = WidgetBlueprint->WidgetTree->FindWidget(FName(*ParentName));
        if (!ParentWidget)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parent widget '%s' not found"), *ParentName));

        UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget);
        if (!ParentPanel)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Parent widget '%s' is not a panel widget"), *ParentName));

        ParentPanel->AddChild(NewWidget);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
    WidgetBlueprint->MarkPackageDirty();
    // Persist to disk synchronously — the Editor autosave is async and misses
    // the pipeline's commit window, so build_umg_widget output never lands in
    // the commit. Same SaveAsset pattern as HandleCreateUMGWidgetBlueprint.
    UEditorAssetLibrary::SaveAsset(BlueprintPath, /*bOnlyIfIsDirty=*/ false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_type"), WidgetType);
    Result->SetStringField(TEXT("parent_name"), ParentName.IsEmpty() ? TEXT("(root)") : ParentName);
    Result->SetBoolField(TEXT("is_variable"), bIsVariable);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_text_block_to_widget
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddTextBlockToWidget(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    // ── Required params ──────────────────────────────────────────────────────
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString TextBlockName;
    if (!Params->TryGetStringField(TEXT("text_block_name"), TextBlockName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'text_block_name' parameter"));

    // ── Optional params ──────────────────────────────────────────────────────
    FString Text;
    Params->TryGetStringField(TEXT("text"), Text);

    FVector2D Position(0.0f, 0.0f);
    GetVector2DFromArray(Params, TEXT("position"), Position);

    FVector2D Size(200.0f, 50.0f);
    GetVector2DFromArray(Params, TEXT("size"), Size);

    int32 FontSize = 12;
    Params->TryGetNumberField(TEXT("font_size"), FontSize);

    FLinearColor Color(1.0f, 1.0f, 1.0f, 1.0f);
    GetLinearColorFromArray(Params, TEXT("color"), Color);

    // ── Load Widget Blueprint ────────────────────────────────────────────────
    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Blueprints, or /Game/Widgets"), *WidgetName));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));
    // -- Optional parent_name: add to named panel instead of root canvas
    FString TBParentName;
    Params->TryGetStringField(TEXT("parent_name"), TBParentName);
    UPanelWidget* ParentPanel = FindParentPanelWidget(WB, TBParentName);
    if (!ParentPanel)
    {
        UCanvasPanel* CanvasPanel = EnsureRootCanvasPanel(WB);
        if (!CanvasPanel)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                TEXT("Widget Blueprint's root is not a CanvasPanel; cannot place TextBlock"));
        ParentPanel = CanvasPanel;
    }

    // ── Name uniqueness ──────────────────────────────────────────────────────
    if (WB->WidgetTree->FindWidget(FName(*TextBlockName)))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' already exists in '%s'"), *TextBlockName, *WidgetName));

    // ── Construct TextBlock ──────────────────────────────────────────────────
    UTextBlock* NewText = WB->WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(*TextBlockName));
    if (!NewText)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ConstructWidget<UTextBlock> returned null"));

    NewText->bIsVariable = true;  // required for BindWidget auto-binding
    NewText->SetText(FText::FromString(Text));

    // Font — note UTextBlock stores its font in a Slate FSlateFontInfo struct;
    // we mutate the existing copy to keep the default font family/typeface
    // and only override the Size.
    FSlateFontInfo FontInfo = NewText->GetFont();
    FontInfo.Size = FontSize;
    NewText->SetFont(FontInfo);

    NewText->SetColorAndOpacity(FSlateColor(Color));

    // ── Add to canvas and configure slot ─────────────────────────────────────
    UPanelSlot* RawSlot = ParentPanel->AddChild(NewText);
    UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RawSlot);
    if (CanvasSlot)
    {
        CanvasSlot->SetPosition(Position);
        CanvasSlot->SetSize(Size);
    }

    // ── Persist ──────────────────────────────────────────────────────────────
    MarkAndSaveWidgetBlueprint(WB);

    // ── Build response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), WidgetName);
    Result->SetStringField(TEXT("child_name"), TextBlockName);
    Result->SetStringField(TEXT("type"), TEXT("TextBlock"));
    {
        TArray<TSharedPtr<FJsonValue>> PosArr;
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.X));
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.Y));
        Result->SetArrayField(TEXT("position"), PosArr);

        TArray<TSharedPtr<FJsonValue>> SizeArr;
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
        Result->SetArrayField(TEXT("size"), SizeArr);
    }
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_button_to_widget
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddButtonToWidget(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    // ── Required params ──────────────────────────────────────────────────────
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString ButtonName;
    if (!Params->TryGetStringField(TEXT("button_name"), ButtonName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'button_name' parameter"));

    // ── Optional params ──────────────────────────────────────────────────────
    FString Text;
    Params->TryGetStringField(TEXT("text"), Text);

    FVector2D Position(0.0f, 0.0f);
    GetVector2DFromArray(Params, TEXT("position"), Position);

    FVector2D Size(200.0f, 50.0f);
    GetVector2DFromArray(Params, TEXT("size"), Size);

    int32 FontSize = 12;
    Params->TryGetNumberField(TEXT("font_size"), FontSize);

    FLinearColor TextColor(1.0f, 1.0f, 1.0f, 1.0f);
    GetLinearColorFromArray(Params, TEXT("color"), TextColor);

    FLinearColor BgColor(0.1f, 0.1f, 0.1f, 1.0f);
    GetLinearColorFromArray(Params, TEXT("background_color"), BgColor);

    // ── Load Widget Blueprint ────────────────────────────────────────────────
    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Blueprints, or /Game/Widgets"), *WidgetName));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));
    // -- Optional parent_name: add to named panel instead of root canvas
    FString BtnParentName;
    Params->TryGetStringField(TEXT("parent_name"), BtnParentName);
    UPanelWidget* ParentPanel = FindParentPanelWidget(WB, BtnParentName);
    if (!ParentPanel)
    {
        UCanvasPanel* CanvasPanel = EnsureRootCanvasPanel(WB);
        if (!CanvasPanel)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                TEXT("Widget Blueprint's root is not a CanvasPanel; cannot place Button"));
        ParentPanel = CanvasPanel;
    }

    // ── Name uniqueness ──────────────────────────────────────────────────────
    if (WB->WidgetTree->FindWidget(FName(*ButtonName)))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' already exists in '%s'"), *ButtonName, *WidgetName));

    // ── Construct Button ─────────────────────────────────────────────────────
    UButton* NewButton = WB->WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), FName(*ButtonName));
    if (!NewButton)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ConstructWidget<UButton> returned null"));

    NewButton->bIsVariable = true;
    NewButton->SetBackgroundColor(BgColor);

    // ── Construct child TextBlock label (not BindWidget-named — internal) ────
    // Give it a unique auto-name; BindWidget only matters for the Button itself.
    const FName LabelName(*FString::Printf(TEXT("%s_Label"), *ButtonName));
    UTextBlock* Label = WB->WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), LabelName);
    if (Label)
    {
        Label->bIsVariable = false;
        Label->SetText(FText::FromString(Text));

        FSlateFontInfo FontInfo = Label->GetFont();
        FontInfo.Size = FontSize;
        Label->SetFont(FontInfo);

        Label->SetColorAndOpacity(FSlateColor(TextColor));

        NewButton->AddChild(Label);
    }

    // ── Add button to canvas and configure slot ──────────────────────────────
    UPanelSlot* RawSlot = ParentPanel->AddChild(NewButton);
    UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RawSlot);
    if (CanvasSlot)
    {
        CanvasSlot->SetPosition(Position);
        CanvasSlot->SetSize(Size);
    }

    // ── Persist ──────────────────────────────────────────────────────────────
    MarkAndSaveWidgetBlueprint(WB);

    // ── Build response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), WidgetName);
    Result->SetStringField(TEXT("child_name"), ButtonName);
    Result->SetStringField(TEXT("type"), TEXT("Button"));
    {
        TArray<TSharedPtr<FJsonValue>> PosArr;
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.X));
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.Y));
        Result->SetArrayField(TEXT("position"), PosArr);

        TArray<TSharedPtr<FJsonValue>> SizeArr;
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
        Result->SetArrayField(TEXT("size"), SizeArr);
    }
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_panel_widget_to_widget
// ─────────────────────────────────────────────────────────────────────────────

namespace UMGCommandsUtils
{
    /** Map requested panel type name → UClass. Returns nullptr for unsupported types. */
    static UClass* ResolvePanelClass(const FString& TypeName)
    {
        const TMap<FString, UClass*> Map = {
            { TEXT("HorizontalBox"),     UHorizontalBox::StaticClass()    },
            { TEXT("VerticalBox"),       UVerticalBox::StaticClass()      },
            { TEXT("UniformGridPanel"),  UUniformGridPanel::StaticClass() },
            { TEXT("CanvasPanel"),       UCanvasPanel::StaticClass()      },
            { TEXT("ScrollBox"),         UScrollBox::StaticClass()        },
            { TEXT("WrapBox"),           UWrapBox::StaticClass()          },
            { TEXT("Overlay"),           UOverlay::StaticClass()          },
        };
        UClass* const* Found = Map.Find(TypeName);
        return Found ? *Found : nullptr;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddPanelWidgetToWidget(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    // ── Required params ──────────────────────────────────────────────────────
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString PanelName;
    if (!Params->TryGetStringField(TEXT("panel_name"), PanelName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'panel_name' parameter"));

    FString PanelType;
    if (!Params->TryGetStringField(TEXT("panel_type"), PanelType))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'panel_type' parameter"));

    // ── Optional params ──────────────────────────────────────────────────────
    FVector2D Position(0.0f, 0.0f);
    GetVector2DFromArray(Params, TEXT("position"), Position);

    FVector2D Size(400.0f, 100.0f);
    GetVector2DFromArray(Params, TEXT("size"), Size);

    // ── Resolve panel class ──────────────────────────────────────────────────
    UClass* PanelClass = ResolvePanelClass(PanelType);
    if (!PanelClass)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unsupported panel_type '%s'. Supported: HorizontalBox, VerticalBox, "
                "UniformGridPanel, CanvasPanel, ScrollBox, WrapBox, Overlay"), *PanelType));

    // ── Load Widget Blueprint ────────────────────────────────────────────────
    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Blueprints, or /Game/Widgets"), *WidgetName));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));
    // -- Optional parent_name: add to named panel instead of root canvas
    FString PanelParentName;
    Params->TryGetStringField(TEXT("parent_name"), PanelParentName);
    UPanelWidget* ParentPanel = FindParentPanelWidget(WB, PanelParentName);
    if (!ParentPanel)
    {
        UCanvasPanel* CanvasPanel = EnsureRootCanvasPanel(WB);
        if (!CanvasPanel)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                TEXT("Widget Blueprint's root is not a CanvasPanel; cannot place panel"));
        ParentPanel = CanvasPanel;
    }

    // ── Name uniqueness ──────────────────────────────────────────────────────
    if (WB->WidgetTree->FindWidget(FName(*PanelName)))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' already exists in '%s'"), *PanelName, *WidgetName));

    // ── Construct the panel widget ───────────────────────────────────────────
    UPanelWidget* NewPanel = WB->WidgetTree->ConstructWidget<UPanelWidget>(PanelClass, FName(*PanelName));
    if (!NewPanel)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ConstructWidget<UPanelWidget> returned null"));

    NewPanel->bIsVariable = true;  // required for BindWidget auto-binding

    // ── Add to root canvas and configure slot ────────────────────────────────
    UPanelSlot* RawSlot = ParentPanel->AddChild(NewPanel);
    UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RawSlot);
    if (CanvasSlot)
    {
        CanvasSlot->SetPosition(Position);
        CanvasSlot->SetSize(Size);
    }

    // ── Persist ──────────────────────────────────────────────────────────────
    MarkAndSaveWidgetBlueprint(WB);

    // ── Build response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), WidgetName);
    Result->SetStringField(TEXT("child_name"), PanelName);
    Result->SetStringField(TEXT("type"), PanelType);
    {
        TArray<TSharedPtr<FJsonValue>> PosArr;
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.X));
        PosArr.Add(MakeShared<FJsonValueNumber>(Position.Y));
        Result->SetArrayField(TEXT("position"), PosArr);

        TArray<TSharedPtr<FJsonValue>> SizeArr;
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
        SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
        Result->SetArrayField(TEXT("size"), SizeArr);
    }
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_widget_property
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
    // ── Required params ───────────────────────────────────────────────────────
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));

    FString PropertyValue;
    if (!Params->TryGetStringField(TEXT("property_value"), PropertyValue))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));

    // ── Load Widget Blueprint ─────────────────────────────────────────────────
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(
        UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!WidgetBlueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *BlueprintPath));

    if (!WidgetBlueprint->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));

    // ── Find the widget ───────────────────────────────────────────────────────
    UWidget* Widget = nullptr;
    if (WidgetName == TEXT("Root") || WidgetName.IsEmpty())
    {
        Widget = WidgetBlueprint->WidgetTree->RootWidget;
    }
    else
    {
        Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
    }

    if (!Widget)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' not found"), *WidgetName));

    bool bModified = false;

    // ─────────────────────────────────────────────────────────────────────────
    // Slot properties: "Slot.Padding", "Slot.HorizontalAlignment", etc.
    // ─────────────────────────────────────────────────────────────────────────
    if (PropertyName.StartsWith(TEXT("Slot.")))
    {
        FString SlotPropName = PropertyName.Mid(5);

        if (!Widget->Slot)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Widget '%s' has no slot (not attached to a parent panel)"), *WidgetName));

        UPanelSlot* Slot = Widget->Slot;

        // CanvasPanelSlot
        if (UCanvasPanelSlot* CPSlot = Cast<UCanvasPanelSlot>(Slot))
        {
            if (SlotPropName == TEXT("Position"))
            {
                FVector2D Vec;
                if (!UMGCommandsUtils::ParseVector2D(PropertyValue, Vec))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Position expects 'X,Y'"));
                CPSlot->SetPosition(Vec);
                bModified = true;
            }
            else if (SlotPropName == TEXT("Size"))
            {
                FVector2D Vec;
                if (!UMGCommandsUtils::ParseVector2D(PropertyValue, Vec))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Size expects 'X,Y'"));
                CPSlot->SetSize(Vec);
                bModified = true;
            }
            else if (SlotPropName == TEXT("Alignment"))
            {
                FVector2D Vec;
                if (!UMGCommandsUtils::ParseVector2D(PropertyValue, Vec))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Alignment expects 'X,Y'"));
                CPSlot->SetAlignment(Vec);
                bModified = true;
            }
            else if (SlotPropName == TEXT("AutoSize"))
            {
                CPSlot->SetAutoSize(PropertyValue.ToBool());
                bModified = true;
            }
            else if (SlotPropName == TEXT("Anchors"))
            {
                // format: "MinX,MinY,MaxX,MaxY"
                TArray<FString> Parts;
                PropertyValue.ParseIntoArray(Parts, TEXT(","));
                if (Parts.Num() == 4)
                {
                    FAnchors Anchors;
                    Anchors.Minimum = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
                    Anchors.Maximum = FVector2D(FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
                    CPSlot->SetAnchors(Anchors);
                    bModified = true;
                }
                else
                {
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Anchors expects 'MinX,MinY,MaxX,MaxY'"));
                }
            }
            else if (SlotPropName == TEXT("Offsets"))
            {
                // format: "Left,Top,Right,Bottom"
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Offsets expects float or 'L,T,R,B'"));
                CPSlot->SetOffsets(Margin);
                bModified = true;
            }
        }
        // VerticalBoxSlot
        else if (UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(Slot))
        {
            if (SlotPropName == TEXT("Padding"))
            {
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Padding expects float or 'T,L,B,R'"));
                VBSlot->SetPadding(Margin);
                bModified = true;
            }
            else if (SlotPropName == TEXT("HorizontalAlignment"))
            {
                VBSlot->SetHorizontalAlignment(UMGCommandsUtils::ParseHAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("VerticalAlignment"))
            {
                VBSlot->SetVerticalAlignment(UMGCommandsUtils::ParseVAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("FillHeight"))
            {
                FSlateChildSize NewSize;
                NewSize.SizeRule = ESlateSizeRule::Fill;
                NewSize.Value    = FCString::Atof(*PropertyValue);
                VBSlot->SetSize(NewSize);
                bModified = true;
            }
            else if (SlotPropName == TEXT("AutoHeight"))
            {
                FSlateChildSize NewSize;
                NewSize.SizeRule = ESlateSizeRule::Automatic;
                VBSlot->SetSize(NewSize);
                bModified = true;
            }
        }
        // HorizontalBoxSlot
        else if (UHorizontalBoxSlot* HBSlot = Cast<UHorizontalBoxSlot>(Slot))
        {
            if (SlotPropName == TEXT("Padding"))
            {
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Padding expects float or 'T,L,B,R'"));
                HBSlot->SetPadding(Margin);
                bModified = true;
            }
            else if (SlotPropName == TEXT("HorizontalAlignment"))
            {
                HBSlot->SetHorizontalAlignment(UMGCommandsUtils::ParseHAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("VerticalAlignment"))
            {
                HBSlot->SetVerticalAlignment(UMGCommandsUtils::ParseVAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("FillWidth"))
            {
                FSlateChildSize NewSize;
                NewSize.SizeRule = ESlateSizeRule::Fill;
                NewSize.Value    = FCString::Atof(*PropertyValue);
                HBSlot->SetSize(NewSize);
                bModified = true;
            }
            else if (SlotPropName == TEXT("AutoWidth"))
            {
                FSlateChildSize NewSize;
                NewSize.SizeRule = ESlateSizeRule::Automatic;
                HBSlot->SetSize(NewSize);
                bModified = true;
            }
        }
        // OverlaySlot
        else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(Slot))
        {
            if (SlotPropName == TEXT("Padding"))
            {
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Padding expects float or 'T,L,B,R'"));
                OSlot->SetPadding(Margin);
                bModified = true;
            }
            else if (SlotPropName == TEXT("HorizontalAlignment"))
            {
                OSlot->SetHorizontalAlignment(UMGCommandsUtils::ParseHAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("VerticalAlignment"))
            {
                OSlot->SetVerticalAlignment(UMGCommandsUtils::ParseVAlign(PropertyValue));
                bModified = true;
            }
        }
        // SizeBoxSlot
        else if (USizeBoxSlot* SBSlot = Cast<USizeBoxSlot>(Slot))
        {
            if (SlotPropName == TEXT("Padding"))
            {
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Padding expects float or 'T,L,B,R'"));
                SBSlot->SetPadding(Margin);
                bModified = true;
            }
            else if (SlotPropName == TEXT("HorizontalAlignment"))
            {
                SBSlot->SetHorizontalAlignment(UMGCommandsUtils::ParseHAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("VerticalAlignment"))
            {
                SBSlot->SetVerticalAlignment(UMGCommandsUtils::ParseVAlign(PropertyValue));
                bModified = true;
            }
        }
        // BorderSlot
        else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot))
        {
            if (SlotPropName == TEXT("Padding"))
            {
                FMargin Margin;
                if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot.Padding expects float or 'T,L,B,R'"));
                BorderSlot->SetPadding(Margin);
                bModified = true;
            }
            else if (SlotPropName == TEXT("HorizontalAlignment"))
            {
                BorderSlot->SetHorizontalAlignment(UMGCommandsUtils::ParseHAlign(PropertyValue));
                bModified = true;
            }
            else if (SlotPropName == TEXT("VerticalAlignment"))
            {
                BorderSlot->SetVerticalAlignment(UMGCommandsUtils::ParseVAlign(PropertyValue));
                bModified = true;
            }
        }

        if (!bModified)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unsupported slot property '%s' for this slot type"), *SlotPropName));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Common widget properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (PropertyName == TEXT("Visibility"))
    {
        Widget->SetVisibility(UMGCommandsUtils::ParseVisibility(PropertyValue));
        bModified = true;
    }
    else if (PropertyName == TEXT("IsEnabled"))
    {
        Widget->SetIsEnabled(PropertyValue.ToBool());
        bModified = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TextBlock properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
    {
        if (PropertyName == TEXT("Text"))
        {
            TextBlock->SetText(FText::FromString(PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("FontSize"))
        {
            FSlateFontInfo FontInfo = TextBlock->GetFont();
            FontInfo.Size = FCString::Atoi(*PropertyValue);
            TextBlock->SetFont(FontInfo);
            bModified = true;
        }
        else if (PropertyName == TEXT("ColorAndOpacity"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ColorAndOpacity expects 'R,G,B' or 'R,G,B,A'"));
            TextBlock->SetColorAndOpacity(FSlateColor(Color));
            bModified = true;
        }
        else if (PropertyName == TEXT("Justification"))
        {
            ETextJustify::Type Justify = ETextJustify::Left;
            if (PropertyValue == TEXT("Center")) Justify = ETextJustify::Center;
            else if (PropertyValue == TEXT("Right"))  Justify = ETextJustify::Right;
            TextBlock->SetJustification(Justify);
            bModified = true;
        }
        else if (PropertyName == TEXT("AutoWrapText"))
        {
            TextBlock->SetAutoWrapText(PropertyValue.ToBool());
            bModified = true;
        }
        else if (PropertyName == TEXT("WrapTextAt"))
        {
            TextBlock->SetWrapTextAt(FCString::Atof(*PropertyValue));
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Button properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (UButton* Button = Cast<UButton>(Widget))
    {
        if (PropertyName == TEXT("ColorAndOpacity"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ColorAndOpacity expects 'R,G,B' or 'R,G,B,A'"));
            Button->SetColorAndOpacity(Color);
            bModified = true;
        }
        else if (PropertyName == TEXT("BackgroundColor"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("BackgroundColor expects 'R,G,B' or 'R,G,B,A'"));
            Button->SetBackgroundColor(Color);
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Spacer properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (USpacer* Spacer = Cast<USpacer>(Widget))
    {
        if (PropertyName == TEXT("Size"))
        {
            FVector2D Vec;
            if (!UMGCommandsUtils::ParseVector2D(PropertyValue, Vec))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Size expects 'X,Y'"));
            Spacer->SetSize(Vec);
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SizeBox properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (USizeBox* SizeBox = Cast<USizeBox>(Widget))
    {
        if (PropertyName == TEXT("WidthOverride"))
        {
            SizeBox->SetWidthOverride(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("HeightOverride"))
        {
            SizeBox->SetHeightOverride(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("MinDesiredWidth"))
        {
            SizeBox->SetMinDesiredWidth(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("MinDesiredHeight"))
        {
            SizeBox->SetMinDesiredHeight(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("MaxDesiredWidth"))
        {
            SizeBox->SetMaxDesiredWidth(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("MaxDesiredHeight"))
        {
            SizeBox->SetMaxDesiredHeight(FCString::Atof(*PropertyValue));
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Image properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (UImage* Image = Cast<UImage>(Widget))
    {
        if (PropertyName == TEXT("ColorAndOpacity"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ColorAndOpacity expects 'R,G,B' or 'R,G,B,A'"));
            Image->SetColorAndOpacity(Color);
            bModified = true;
        }
        else if (PropertyName == TEXT("Opacity"))
        {
            Image->SetOpacity(FCString::Atof(*PropertyValue));
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ProgressBar properties
    // ─────────────────────────────────────────────────────────────────────────
    else if (UProgressBar* ProgressBar = Cast<UProgressBar>(Widget))
    {
        if (PropertyName == TEXT("Percent"))
        {
            ProgressBar->SetPercent(FCString::Atof(*PropertyValue));
            bModified = true;
        }
        else if (PropertyName == TEXT("FillColorAndOpacity"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FillColorAndOpacity expects 'R,G,B' or 'R,G,B,A'"));
            ProgressBar->SetFillColorAndOpacity(Color);
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Border properties
    //   BrushColor : FLinearColor as "R,G,B" / "R,G,B,A"
    //   Padding    : FMargin as float / "H,V" / "L,T,R,B"
    //   ContentColorAndOpacity / BrushColor handled explicitly because the
    //   reflection fallback's ImportText cannot parse bare-CSV struct literals.
    // ─────────────────────────────────────────────────────────────────────────
    else if (UBorder* Border = Cast<UBorder>(Widget))
    {
        if (PropertyName == TEXT("BrushColor"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("BrushColor expects 'R,G,B' or 'R,G,B,A'"));
            Border->SetBrushColor(Color);
            bModified = true;
        }
        else if (PropertyName == TEXT("ContentColorAndOpacity"))
        {
            FLinearColor Color;
            if (!UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ContentColorAndOpacity expects 'R,G,B' or 'R,G,B,A'"));
            Border->SetContentColorAndOpacity(Color);
            bModified = true;
        }
        else if (PropertyName == TEXT("Padding"))
        {
            FMargin Margin;
            if (!UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Padding expects float, 'H,V' or 'L,T,R,B'"));
            Border->SetPadding(Margin);
            bModified = true;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Reflection-based fallback: float, bool, int32, and common struct UPROPERTY
    // fields. UE's ImportText for FLinearColor / FMargin / FVector2D / FSlateColor
    // expects parenthesised literals like "(R=0.5,G=0.5,...)"; MCP callers pass
    // bare CSV ("0.5,0.5,0.5,1.0"). Pre-parse those struct types from CSV so the
    // typical color/margin/vector cases succeed instead of failing ImportText.
    // ─────────────────────────────────────────────────────────────────────────
    else
    {
        FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
        if (Prop)
        {
            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
            bool bHandled = false;

            if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
            {
                const FName StructName = StructProp->Struct ? StructProp->Struct->GetFName() : NAME_None;

                if (StructName == NAME_LinearColor)
                {
                    FLinearColor Color;
                    if (UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                    {
                        *static_cast<FLinearColor*>(ValuePtr) = Color;
                        bHandled = true;
                    }
                }
                else if (StructName == TEXT("SlateColor"))
                {
                    FLinearColor Color;
                    if (UMGCommandsUtils::ParseLinearColor(PropertyValue, Color))
                    {
                        *static_cast<FSlateColor*>(ValuePtr) = FSlateColor(Color);
                        bHandled = true;
                    }
                }
                else if (StructName == TEXT("Margin"))
                {
                    FMargin Margin;
                    if (UMGCommandsUtils::ParseMargin(PropertyValue, Margin))
                    {
                        *static_cast<FMargin*>(ValuePtr) = Margin;
                        bHandled = true;
                    }
                }
                else if (StructName == NAME_Vector2D)
                {
                    FVector2D Vec;
                    if (UMGCommandsUtils::ParseVector2D(PropertyValue, Vec))
                    {
                        *static_cast<FVector2D*>(ValuePtr) = Vec;
                        bHandled = true;
                    }
                }
            }

            if (!bHandled)
            {
                const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, ValuePtr, Widget, PPF_None);
                if (ImportResult != nullptr)
                {
                    bHandled = true;
                }
            }

            if (bHandled)
            {
                bModified = true;
            }
            else
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to import value '%s' for property '%s' (type '%s')"),
                        *PropertyValue, *PropertyName,
                        *Prop->GetCPPType()));
            }
        }
        else
        {
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Property '%s' not supported for widget type '%s'"),
                    *PropertyName, *Widget->GetClass()->GetName()));
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
    WidgetBlueprint->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(BlueprintPath, /*bOnlyIfIsDirty=*/false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("property_name"), PropertyName);
    Result->SetStringField(TEXT("status"), TEXT("success"));
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// get_umg_hierarchy
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleGetUMGHierarchy(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));

    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(
        UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!WidgetBlueprint)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *BlueprintPath));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);

    if (WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
    {
        Result->SetObjectField(TEXT("root"), BuildWidgetJson(WidgetBlueprint->WidgetTree->RootWidget));
    }
    else
    {
        Result->SetBoolField(TEXT("empty"), true);
    }

    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildWidgetJson — recursive hierarchy serializer
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::BuildWidgetJson(UWidget* Widget)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    if (!Widget) return Obj;

    Obj->SetStringField(TEXT("name"), Widget->GetName());
    Obj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
    Obj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

    // Include type-specific display data for quick inspection
    if (UTextBlock* TB = Cast<UTextBlock>(Widget))
    {
        Obj->SetStringField(TEXT("text"), TB->GetText().ToString());
    }
    else if (USpacer* Sp = Cast<USpacer>(Widget))
    {
        // UE5.7+: USpacer::Size прямой доступ помечен deprecated — используем getter.
        TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
        const FVector2D SpSize = Sp->GetSize();
        SizeObj->SetNumberField(TEXT("x"), SpSize.X);
        SizeObj->SetNumberField(TEXT("y"), SpSize.Y);
        Obj->SetObjectField(TEXT("size"), SizeObj);
    }

    // Slot info
    if (Widget->Slot)
    {
        Obj->SetStringField(TEXT("slot_type"), Widget->Slot->GetClass()->GetName());
    }

    // Children
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        TArray<TSharedPtr<FJsonValue>> Children;
        for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
        {
            UWidget* Child = Panel->GetChildAt(i);
            if (Child)
            {
                Children.Add(MakeShared<FJsonValueObject>(BuildWidgetJson(Child)));
            }
        }
        Obj->SetArrayField(TEXT("children"), Children);
    }

    return Obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// create_umg_widget_blueprint
// Создаёт новый Widget Blueprint в указанной папке content browser'а.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCreateUMGWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    // Optional path — default "/Game/UI". Normalize: must start with "/Game/" and end with "/".
    FString PackagePath = TEXT("/Game/UI");
    Params->TryGetStringField(TEXT("path"), PackagePath);
    PackagePath.TrimStartAndEndInline();
    if (PackagePath.StartsWith(TEXT("Game/")))
        PackagePath = TEXT("/") + PackagePath;
    if (!PackagePath.StartsWith(TEXT("/")))
        PackagePath = TEXT("/") + PackagePath;
    if (!PackagePath.StartsWith(TEXT("/Game/")) && PackagePath != TEXT("/Game"))
        PackagePath = TEXT("/Game/") + PackagePath.RightChop(1);
    if (PackagePath.EndsWith(TEXT("/")))
        PackagePath.RemoveAt(PackagePath.Len() - 1);

    // Optional parent class — default UUserWidget.
    FString ParentClassName = TEXT("UserWidget");
    Params->TryGetStringField(TEXT("parent_class"), ParentClassName);

    UClass* ParentClass = UUserWidget::StaticClass();
    if (!ParentClassName.IsEmpty() && ParentClassName != TEXT("UserWidget") && ParentClassName != TEXT("UUserWidget"))
    {
        UClass* FoundClass = LoadClass<UObject>(nullptr, *ParentClassName);
        if (!FoundClass)
        {
            // Try common script-paths.
            const TArray<FString> SearchPaths = {
                FString::Printf(TEXT("/Script/UMG.%s"), *ParentClassName),
                FString::Printf(TEXT("/Script/Client.%s"), *ParentClassName),
                FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassName),
            };
            for (const FString& P : SearchPaths)
            {
                FoundClass = LoadClass<UObject>(nullptr, *P);
                if (FoundClass) break;
            }
        }
        if (FoundClass && FoundClass->IsChildOf(UUserWidget::StaticClass()))
        {
            ParentClass = FoundClass;
        }
        // If parent class lookup failed or class is not UserWidget-compatible, fall back silently.
    }

    // Reject duplicate.
    const FString FullAssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *WidgetName);
    if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint already exists: %s"), *FullAssetPath));

    // Create the WBP via the factory.
    UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
    WidgetFactory->ParentClass = ParentClass;

    const FString PackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *WidgetName);
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create package: %s"), *PackageName));

    UObject* NewAsset = WidgetFactory->FactoryCreateNew(
        UWidgetBlueprint::StaticClass(),
        Package,
        *WidgetName,
        RF_Standalone | RF_Public,
        nullptr,
        GWarn);

    UWidgetBlueprint* NewWBP = Cast<UWidgetBlueprint>(NewAsset);
    if (!NewWBP)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("WidgetBlueprintFactory failed to create asset"));

    FAssetRegistryModule::AssetCreated(NewWBP);
    Package->MarkPackageDirty();
    UEditorAssetLibrary::SaveAsset(FullAssetPath, /*bOnlyIfIsDirty=*/ false);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("path"), FullAssetPath);
    Result->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
    Result->SetBoolField(TEXT("success"), true);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// bind_widget_event
// Создаёт K2Node_ComponentBoundEvent в EventGraph WidgetBlueprint'а,
// привязывая делегат (например, OnClicked у Button) к обработчику.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleBindWidgetEvent(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("widget_component_name"), ComponentName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_component_name' parameter"));

    FString EventName;
    if (!Params->TryGetStringField(TEXT("event_name"), EventName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

    FString FunctionName;
    Params->TryGetStringField(TEXT("function_name"), FunctionName);
    if (FunctionName.IsEmpty())
        FunctionName = FString::Printf(TEXT("BndEvt__%s_%s"), *ComponentName, *EventName);

    // ── Load Widget Blueprint ────────────────────────────────────────────────
    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found in /Game/UI, /Game/Blueprints, or /Game/Widgets"), *WidgetName));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));

    // ── Find child widget in WidgetTree ──────────────────────────────────────
    UWidget* Child = WB->WidgetTree->FindWidget(FName(*ComponentName));
    if (!Child)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget component '%s' not found in WBP '%s'"), *ComponentName, *WidgetName));

    // Ensure it is exposed as a variable (BindWidget-style) so the generated class has a property for it.
    if (!Child->bIsVariable)
    {
        Child->bIsVariable = true;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WB);
    }

    // ── Find FMulticastDelegateProperty on the child widget's class ──────────
    UClass* ChildClass = Child->GetClass();
    FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(ChildClass, FName(*EventName));
    if (!DelegateProp)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Delegate '%s' not found on widget class '%s'"), *EventName, *ChildClass->GetName()));

    // ── Find the component FObjectProperty on the generated WBP class ───────
    // Must compile the blueprint at least once so the generated class has the BindWidget property.
    UClass* GeneratedClass = WB->GeneratedClass;
    if (!GeneratedClass)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WB);
        GeneratedClass = WB->GeneratedClass;
    }
    FObjectProperty* ComponentProp = nullptr;
    if (GeneratedClass)
    {
        for (TFieldIterator<FObjectProperty> PropIt(GeneratedClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
        {
            if (PropIt->GetName() == ComponentName)
            {
                ComponentProp = *PropIt;
                break;
            }
        }
    }
    if (!ComponentProp)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Component property '%s' not found on generated WBP class — compile the blueprint first"), *ComponentName));

    // ── Get / create the event graph ─────────────────────────────────────────
    if (WB->UbergraphPages.Num() == 0)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no event graph"));

    UEdGraph* Graph = WB->UbergraphPages[0];
    if (!Graph)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to retrieve event graph"));

    // Skip if a matching ComponentBoundEvent already exists.
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_ComponentBoundEvent* Existing = Cast<UK2Node_ComponentBoundEvent>(Node))
        {
            if (Existing->ComponentPropertyName == ComponentProp->GetFName() &&
                Existing->DelegatePropertyName == DelegateProp->GetFName())
            {
                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                Result->SetBoolField(TEXT("success"), true);
                Result->SetStringField(TEXT("node_id"), Existing->NodeGuid.ToString());
                Result->SetStringField(TEXT("status"), TEXT("already_bound"));
                Result->SetStringField(TEXT("function_name"), FunctionName);
                return Result;
            }
        }
    }

    // ── Create the K2Node_ComponentBoundEvent ────────────────────────────────
    UK2Node_ComponentBoundEvent* BoundEventNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
    if (!BoundEventNode)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to allocate K2Node_ComponentBoundEvent"));

    BoundEventNode->NodePosX = 0;
    BoundEventNode->NodePosY = 0;
    BoundEventNode->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);

    Graph->AddNode(BoundEventNode, true, false);
    BoundEventNode->PostPlacedNewNode();
    BoundEventNode->AllocateDefaultPins();

    Graph->NotifyGraphChanged();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WB);
    MarkAndSaveWidgetBlueprint(WB);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("node_id"), BoundEventNode->NodeGuid.ToString());
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_component_name"), ComponentName);
    Result->SetStringField(TEXT("event_name"), EventName);
    Result->SetStringField(TEXT("function_name"), FunctionName);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_widget_to_viewport
// Editor-side helper: создаёт инстанс WidgetBlueprint'а в активном мире
// (PIE если запущен, иначе editor world) и добавляет в viewport.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddWidgetToViewport(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    int32 ZOrder = 0;
    Params->TryGetNumberField(TEXT("z_order"), ZOrder);

    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName));

    UClass* WidgetClass = WB->GeneratedClass;
    if (!WidgetClass)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("WidgetBlueprint '%s' has no generated class — compile it first"), *WidgetName));

    if (!WidgetClass->IsChildOf(UUserWidget::StaticClass()))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Generated class for '%s' is not a UserWidget subclass"), *WidgetName));

    // Pick the most useful world: PIE if running, otherwise the editor world.
    UWorld* World = nullptr;
    if (GEditor)
    {
        if (GEditor->PlayWorld)
        {
            World = GEditor->PlayWorld;
        }
        else
        {
            FWorldContext& EditorContext = GEditor->GetEditorWorldContext();
            World = EditorContext.World();
        }
    }
    if (!World)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active world found — start PIE or open a level first"));

    UUserWidget* WidgetInstance = UWidgetBlueprintLibrary::Create(World, WidgetClass, /*OwningPlayer=*/ nullptr);
    if (!WidgetInstance)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Failed to create widget instance from '%s'"), *WidgetName));

    WidgetInstance->AddToViewport(ZOrder);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_class"), WidgetClass->GetPathName());
    Result->SetNumberField(TEXT("z_order"), ZOrder);
    Result->SetStringField(TEXT("world"), World->GetName());
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// set_text_block_binding
// Регистрирует FDelegateEditorBinding на UWidgetBlueprint::Bindings,
// привязывая UPROPERTY (по умолчанию "Text") к функции/свойству UserWidget'а.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetTextBlockBinding(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    FString TextBlockName;
    if (!Params->TryGetStringField(TEXT("text_block_name"), TextBlockName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'text_block_name' parameter"));

    // Python sends `binding_property` (function/property on UserWidget).
    // Older callers may send `binding_property_name` — accept both.
    FString BindingProperty;
    if (!Params->TryGetStringField(TEXT("binding_property"), BindingProperty))
    {
        Params->TryGetStringField(TEXT("binding_property_name"), BindingProperty);
    }
    if (BindingProperty.IsEmpty())
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'binding_property' parameter"));

    // Which UPROPERTY on the TextBlock are we binding? Default = Text.
    FString BindingType = TEXT("Text");
    Params->TryGetStringField(TEXT("binding_type"), BindingType);

    UWidgetBlueprint* WB = LoadWidgetBlueprintByName(WidgetName);
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget Blueprint '%s' not found"), *WidgetName));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));

    UWidget* Found = WB->WidgetTree->FindWidget(FName(*TextBlockName));
    if (!Found)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' not found in '%s'"), *TextBlockName, *WidgetName));

    if (!Cast<UTextBlock>(Found))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget '%s' is not a TextBlock (got '%s')"), *TextBlockName, *Found->GetClass()->GetName()));

    Found->bIsVariable = true;

    // Build the FDelegateEditorBinding entry.
    FDelegateEditorBinding NewBinding;
    NewBinding.ObjectName    = TextBlockName;
    NewBinding.PropertyName  = FName(*BindingType);          // e.g. "Text"
    NewBinding.FunctionName  = FName(*BindingProperty);      // function on the WBP that returns the value
    NewBinding.Kind          = EBindingKind::Function;

    // Replace any existing binding with the same (Object, Property) pair (operator== ignores Function).
    WB->Bindings.Remove(NewBinding);
    WB->Bindings.Add(NewBinding);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WB);
    MarkAndSaveWidgetBlueprint(WB);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("text_block_name"), TextBlockName);
    Result->SetStringField(TEXT("binding_property"), BindingProperty);
    Result->SetStringField(TEXT("binding_type"), BindingType);
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// delete_widget_from_umg
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleDeleteWidgetFromUMG(const TSharedPtr<FJsonObject>& Params)
{
    using namespace UMGCommandsUtils;

    // ── Required params ──────────────────────────────────────────────────────
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));

    FString WidgetName;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));

    // ── Load Widget Blueprint ────────────────────────────────────────────────
    UWidgetBlueprint* WB = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
    if (!WB)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *BlueprintPath));

    if (!WB->WidgetTree)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Widget Blueprint has no WidgetTree"));

    // ── Locate target widget ─────────────────────────────────────────────────
    UWidget* Target = WB->WidgetTree->FindWidget(FName(*WidgetName));
    if (!Target)
        return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

    // ── Detach from parent or clear root ─────────────────────────────────────
    // If the widget is a panel, all descendants stay parented to it; once we drop
    // the last reference (RemoveChild / nulling RootWidget) the whole subtree becomes
    // unreachable and is collected by the next GC pass — UE does the cleanup for us.
    bool bWasRoot = (WB->WidgetTree->RootWidget == Target);
    if (bWasRoot)
    {
        WB->WidgetTree->RootWidget = nullptr;
    }
    else
    {
        UPanelWidget* ParentPanel = Target->GetParent();
        if (!ParentPanel)
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Widget '%s' has no parent panel and is not the root — cannot detach"), *WidgetName));

        if (!ParentPanel->RemoveChild(Target))
            return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("RemoveChild failed for widget '%s'"), *WidgetName));
    }

    // ── Persist ──────────────────────────────────────────────────────────────
    // MarkAndSaveWidgetBlueprint already runs MarkBlueprintAsStructurallyModified — that's
    // the correct strength here because we changed the widget hierarchy, not just a property.
    MarkAndSaveWidgetBlueprint(WB);

    // ── Build response ───────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("removed"), true);
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("blueprint_path"), BlueprintPath);
    Result->SetBoolField(TEXT("was_root"), bWasRoot);
    return Result;
}
