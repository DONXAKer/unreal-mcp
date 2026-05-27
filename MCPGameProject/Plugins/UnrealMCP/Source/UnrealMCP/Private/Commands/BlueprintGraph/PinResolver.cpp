// Реализация FUnrealMCPPinResolver (MCP-PLUGIN-001).

#include "Commands/BlueprintGraph/PinResolver.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node.h"

// ─────────────────────────────────────────────────────────────────────────────
// FPinResolutionError::ToJson
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FPinResolutionError::ToJson() const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("query"), Query);
    Obj->SetStringField(TEXT("reason"), Reason);

    TArray<TSharedPtr<FJsonValue>> PinsArr;
    for (const FPinDescriptor& Desc : AvailablePins)
    {
        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("name"), Desc.Name);
        PinObj->SetStringField(TEXT("friendlyName"), Desc.FriendlyName);
        PinObj->SetStringField(TEXT("direction"), Desc.Direction);
        PinObj->SetStringField(TEXT("pinCategory"), Desc.PinCategory);
        PinObj->SetBoolField(TEXT("isSplit"), Desc.bIsSplit);
        PinObj->SetBoolField(TEXT("isSubPin"), Desc.bIsSubPin);
        PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
    }
    Obj->SetArrayField(TEXT("availablePins"), PinsArr);
    return Obj;
}

// ─────────────────────────────────────────────────────────────────────────────
// CollectPins — сбор диагностики для error response.
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPinDescriptor> FUnrealMCPPinResolver::CollectPins(UEdGraphNode* Node)
{
    TArray<FPinDescriptor> Result;
    if (!Node)
    {
        return Result;
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin)
        {
            continue;
        }
        FPinDescriptor Desc;
        Desc.Name = Pin->PinName.ToString();
        Desc.FriendlyName = Pin->PinFriendlyName.IsEmpty() ? TEXT("") : Pin->PinFriendlyName.ToString();
        Desc.Direction = (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output");
        Desc.PinCategory = Pin->PinType.PinCategory.ToString();
        Desc.bIsSplit = (Pin->SubPins.Num() > 0);
        Desc.bIsSubPin = (Pin->ParentPin != nullptr);
        Result.Add(MoveTemp(Desc));
    }
    return Result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveNoReconstruct — основная логика без re-allocate pins.
// ─────────────────────────────────────────────────────────────────────────────

UEdGraphPin* FUnrealMCPPinResolver::ResolveNoReconstruct(
    UEdGraphNode* Node,
    const FString& Query,
    EEdGraphPinDirection Direction)
{
    if (!Node || Query.IsEmpty())
    {
        return nullptr;
    }

    // 1) Exact PinName match.
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        if (Direction != EGPD_MAX && Pin->Direction != Direction) continue;
        if (Pin->PinName.ToString().Equals(Query, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }

    // 2) Fallback на PinFriendlyName.
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        if (Direction != EGPD_MAX && Pin->Direction != Direction) continue;
        if (!Pin->PinFriendlyName.IsEmpty()
            && Pin->PinFriendlyName.ToString().Equals(Query, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }

    // 3) Sub-pin lookup. UE сабпины именуются "Parent_SubField" (через "_"),
    //    но удобный API запроса — через "." или прямо составное имя.
    if (Query.Contains(TEXT(".")) || Query.Contains(TEXT("_")))
    {
        // Сначала пробуем "Parent.Sub.Field" → массив сегментов.
        TArray<FString> Segments;
        FString Working = Query;
        Working.ReplaceInline(TEXT("_"), TEXT("."));
        Working.ParseIntoArray(Segments, TEXT("."), /*InCullEmpty=*/true);

        if (Segments.Num() >= 2)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin) continue;
                if (Direction != EGPD_MAX && Pin->Direction != Direction) continue;

                // Проверяем что первый сегмент совпадает с PinName или FriendlyName parent'а.
                const bool bMatchesName = Pin->PinName.ToString().Equals(Segments[0], ESearchCase::IgnoreCase);
                const bool bMatchesFriendly = !Pin->PinFriendlyName.IsEmpty()
                    && Pin->PinFriendlyName.ToString().Equals(Segments[0], ESearchCase::IgnoreCase);
                if (!bMatchesName && !bMatchesFriendly)
                {
                    continue;
                }

                // Идём по дереву SubPins.
                if (UEdGraphPin* SubResolved = ResolveSubPin(Pin, Segments, 1, Direction))
                {
                    return SubResolved;
                }
            }
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveSubPin — рекурсия по дереву split-pin'ов.
// ─────────────────────────────────────────────────────────────────────────────

UEdGraphPin* FUnrealMCPPinResolver::ResolveSubPin(
    UEdGraphPin* Root,
    const TArray<FString>& Segments,
    int32 StartIdx,
    EEdGraphPinDirection Direction)
{
    if (!Root || StartIdx >= Segments.Num())
    {
        // Дошли до конца сегментов — возвращаем текущий пин.
        if (Root && (Direction == EGPD_MAX || Root->Direction == Direction))
        {
            return Root;
        }
        return nullptr;
    }

    const FString& WantedSegment = Segments[StartIdx];

    for (UEdGraphPin* Sub : Root->SubPins)
    {
        if (!Sub) continue;
        const FString SubName = Sub->PinName.ToString();
        const FString SubFriendly = Sub->PinFriendlyName.IsEmpty() ? TEXT("") : Sub->PinFriendlyName.ToString();

        // Сабпины именуются "ParentName_SubField" — нам интересно совпадение
        // именно сегмента (после последнего "_"), но также допускаем точное
        // совпадение SubName, FriendlyName или endswith.
        const bool bDirectMatch = SubName.Equals(WantedSegment, ESearchCase::IgnoreCase);
        const bool bFriendlyMatch = !SubFriendly.IsEmpty() && SubFriendly.Equals(WantedSegment, ESearchCase::IgnoreCase);
        const bool bSuffixMatch = SubName.EndsWith(TEXT("_") + WantedSegment, ESearchCase::IgnoreCase);

        if (bDirectMatch || bFriendlyMatch || bSuffixMatch)
        {
            if (StartIdx + 1 >= Segments.Num())
            {
                if (Direction == EGPD_MAX || Sub->Direction == Direction)
                {
                    return Sub;
                }
                return nullptr;
            }
            // Иначе — идём глубже.
            if (UEdGraphPin* Deeper = ResolveSubPin(Sub, Segments, StartIdx + 1, Direction))
            {
                return Deeper;
            }
        }
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolvePin — публичная точка входа.
// ─────────────────────────────────────────────────────────────────────────────

UEdGraphPin* FUnrealMCPPinResolver::ResolvePin(
    UEdGraphNode* Node,
    const FString& Query,
    EEdGraphPinDirection Direction,
    FPinResolutionError& OutError)
{
    OutError.Query = Query;
    OutError.Reason.Empty();
    OutError.AvailablePins.Empty();

    if (!Node)
    {
        OutError.Reason = TEXT("Node is null");
        return nullptr;
    }
    if (Query.IsEmpty())
    {
        OutError.Reason = TEXT("Empty pin query");
        OutError.AvailablePins = CollectPins(Node);
        return nullptr;
    }

    // Попытка 1: без реконструкции.
    if (UEdGraphPin* Found = ResolveNoReconstruct(Node, Query, Direction))
    {
        return Found;
    }

    // Попытка 2: для K2Node — ReconstructNode (материализует optional/dynamic пины
    // у CallFunction / CreateWidget / CastTo и т.п.) и повтор.
    if (UK2Node* K2 = Cast<UK2Node>(Node))
    {
        K2->ReconstructNode();
        if (UEdGraphPin* Found = ResolveNoReconstruct(Node, Query, Direction))
        {
            return Found;
        }
    }

    OutError.Reason = FString::Printf(
        TEXT("Pin '%s' not found on node '%s' (tried exact, friendly, sub-pin lookup, ReconstructNode)"),
        *Query, *Node->GetName());
    OutError.AvailablePins = CollectPins(Node);
    return nullptr;
}

UEdGraphPin* FUnrealMCPPinResolver::ResolvePinAny(
    UEdGraphNode* Node,
    const FString& Query,
    FPinResolutionError& OutError)
{
    return ResolvePin(Node, Query, EGPD_MAX, OutError);
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeErrorResponse — общий error-объект для команд PinResolver.
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPPinResolver::MakeErrorResponse(
    const FString& ErrorMessage,
    const FPinResolutionError& Error)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetBoolField(TEXT("success"), false);
    Response->SetStringField(TEXT("error"), ErrorMessage);
    Response->SetObjectField(TEXT("details"), Error.ToJson());
    return Response;
}
