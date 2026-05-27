// Единый helper для резолва пинов на узлах Blueprint-графа (MCP-PLUGIN-001).
//
// Зачем нужен:
//   Команды set_node_property, get_pin_info, connect_nodes, set_pin_default_value
//   независимо реализовывали поиск пина (FindPin / FindPinByName / FindPinChecked).
//   Все три варианта спотыкались на trivial-ситуациях:
//     - K2Node_CallFunction с динамическими пинами (Class у CreateWidget — нет
//       до ReconstructNode/AllocateDefaultPins).
//     - Sub-pins после split_struct_pin (PinName вида "Transform_Location_X",
//       а вызов приходит как "Transform.Location.X").
//     - PinFriendlyName != PinName (например "Exec Then" vs "then").
//
// Этот helper нормализует поведение и при неудаче возвращает структурированный
// FPinResolutionError со списком всех реальных пинов узла — Python-обёртка
// использует его для эвристики did_you_mean.
//
// Live-режим: никакого кэширования (требование таски). Каждый вызов — fresh.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"

class UEdGraphNode;
class UEdGraphPin;

/** Описание одного пина для диагностики (used в FPinResolutionError). */
struct UNREALMCP_API FPinDescriptor
{
    /** Внутреннее имя пина (PinName.ToString()). */
    FString Name;

    /** Дружественное имя (PinFriendlyName.ToString(), может быть пустым). */
    FString FriendlyName;

    /** "input" | "output". */
    FString Direction;

    /** Категория пина (exec/bool/int/struct/...) — помогает в подсказках. */
    FString PinCategory;

    /** Есть ли у пина sub-pins (split struct). */
    bool bIsSplit = false;

    /** Является ли пин sub-pin'ом другого. */
    bool bIsSubPin = false;
};

/** Структурированная ошибка резолва — основа для error.details.availablePins[]. */
struct UNREALMCP_API FPinResolutionError
{
    /** Запрос, который не удалось зарезолвить. */
    FString Query;

    /** Краткая причина (для error message). */
    FString Reason;

    /** Полный список пинов узла на момент попытки (для подсказок). */
    TArray<FPinDescriptor> AvailablePins;

    /** Сериализация в JSON-объект (вкладывается в error.details). */
    TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Единая точка резолва пина на UEdGraphNode.
 *
 * Алгоритм (по порядку):
 *   1. Exact match по PinName (FName, case-insensitive).
 *   2. Fallback на PinFriendlyName (FText, case-insensitive).
 *   3. Sub-pin lookup: если Query содержит ".", рекурсивно идём по SubPins.
 *      Также поддерживается формат "Parent_SubField" (как UE реально называет
 *      сабпины), и автоматическое преобразование "Parent.SubField" → "Parent_SubField".
 *   4. Если узел — K2Node, а пин не найден — вызываем ReconstructNode() и
 *      повторяем попытку (CallFunction-нодам это нужно для materialise dynamic pins).
 *
 * При неудаче кладёт диагностику в OutError и возвращает nullptr.
 */
class UNREALMCP_API FUnrealMCPPinResolver
{
public:
    /**
     * Найти пин на узле по запросу.
     * @param Node       Узел-владелец. Если nullptr — fail.
     * @param Query      Имя или friendly-имя, возможно с "." для sub-pins.
     * @param Direction  Фильтр по направлению (EGPD_MAX = любой).
     * @param OutError   При неудаче заполняется диагностикой (AvailablePins).
     * @return           Найденный pin или nullptr.
     */
    static UEdGraphPin* ResolvePin(
        UEdGraphNode* Node,
        const FString& Query,
        EEdGraphPinDirection Direction,
        FPinResolutionError& OutError);

    /**
     * Тонкая обёртка: без направления (для read-only ops типа get_pin_info).
     */
    static UEdGraphPin* ResolvePinAny(
        UEdGraphNode* Node,
        const FString& Query,
        FPinResolutionError& OutError);

    /**
     * Собрать список всех пинов узла (для error reporting и debug).
     */
    static TArray<FPinDescriptor> CollectPins(UEdGraphNode* Node);

    /**
     * Конструктор error-response для MCP: возвращает JSON вида
     *   { success: false, error: "<msg>", details: { query, reason, availablePins[] } }.
     * Используется во всех 4 командах, переключённых на PinResolver.
     */
    static TSharedPtr<FJsonObject> MakeErrorResponse(const FString& ErrorMessage, const FPinResolutionError& Error);

private:
    /** Внутренняя реализация без reconstruct — чтобы избежать бесконечной рекурсии. */
    static UEdGraphPin* ResolveNoReconstruct(
        UEdGraphNode* Node,
        const FString& Query,
        EEdGraphPinDirection Direction);

    /** Рекурсивный поиск по дереву sub-pins. */
    static UEdGraphPin* ResolveSubPin(
        UEdGraphPin* Root,
        const TArray<FString>& Segments,
        int32 StartIdx,
        EEdGraphPinDirection Direction);
};
