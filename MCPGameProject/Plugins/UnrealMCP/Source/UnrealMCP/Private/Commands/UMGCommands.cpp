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

// Blueprint editor utils
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Fonts/SlateFontInfo.h"

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
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Dispatch
// ─────────────────────────────────────────────────────────────────────────────

FUnrealMCPUMGCommands::FUnrealMCPUMGCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_widget_to_umg"))       return HandleAddWidgetToUMG(Params);
    if (CommandType == TEXT("set_widget_property"))     return HandleSetWidgetProperty(Params);
    if (CommandType == TEXT("get_umg_hierarchy"))       return HandleGetUMGHierarchy(Params);

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
                "VerticalBox, HorizontalBox, ScrollBox, SizeBox, Spacer, SpinBox, Image, Overlay, Border, ProgressBar"),
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

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget_name"), WidgetName);
    Result->SetStringField(TEXT("widget_type"), WidgetType);
    Result->SetStringField(TEXT("parent_name"), ParentName.IsEmpty() ? TEXT("(root)") : ParentName);
    Result->SetBoolField(TEXT("is_variable"), bIsVariable);
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
    // Reflection-based fallback: float, bool, int32 UPROPERTY fields
    // ─────────────────────────────────────────────────────────────────────────
    else
    {
        FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
        if (Prop)
        {
            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
            const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, ValuePtr, Widget, PPF_None);
            if (ImportResult != nullptr)
            {
                bModified = true;
            }
            else
            {
                return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to import value '%s' for property '%s'"),
                        *PropertyValue, *PropertyName));
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
        TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
        SizeObj->SetNumberField(TEXT("x"), Sp->Size.X);
        SizeObj->SetNumberField(TEXT("y"), Sp->Size.Y);
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
