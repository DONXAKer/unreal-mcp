// UMG runtime commands — операции над живыми UUserWidget в PIE-мире (MCP-PLUGIN-002).
//
// Категория отделена от UMGCommands.cpp (design-time WidgetBlueprint ops) и от
// UMGTestCommands.cpp (find / click / get_widget_tree). Здесь команды, которые
// мутируют runtime-state виджета — изначально `set_text_on_widget` для UMG
// input полей (UEditableTextBox / UEditableText / Multi-Line варианты).

#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWidget;
class UUserWidget;
class UWorld;
class APlayerController;

/**
 * Handler класс для UMG runtime команд, требующих активного PIE.
 *
 * Поддерживаемые команды:
 *   set_text_on_widget — записать Unicode-текст в EditableText/EditableTextBox/MultiLine варианты.
 *
 * Поиск виджета: через все живые UUserWidget активного PIE-мира,
 * фильтр по controller_index (на T2 — заглушка, T003 добавляет реальный лукап).
 */
class FUMGRuntimeCommands
{
public:
    FUMGRuntimeCommands();

    /**
     * Маршрутизатор incoming runtime команд.
     * @param CommandType  set_text_on_widget
     * @param Params       JSON параметры (см. per-handler доки).
     * @return JSON ответ.
     */
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * set_text_on_widget — записать любой Unicode-текст в edit-поле.
     *
     * Params:
     *   widget_name      (required) — UWidget::GetName() (case-insensitive).
     *   text             (required) — Unicode-строка.
     *   controller_index (opt, default 0) — на какого PlayerController смотреть
     *                                       (заглушка в T002; реализуется в T003).
     *
     * Поддерживаемые UClass'ы:
     *   UEditableTextBox, UEditableText,
     *   UMultiLineEditableTextBox, UMultiLineEditableText.
     *
     * Returns: { ok: bool, widget_name, widget_class, owner_user_widget }.
     * При неподдерживаемом классе ошибка содержит details.actualClass.
     */
    TSharedPtr<FJsonObject> HandleSetTextOnWidget(const TSharedPtr<FJsonObject>& Params);

    /**
     * Резолв PlayerController по индексу. На T002 — всегда GetFirstPlayerController.
     * T003 добавит реальный multi-client lookup через FUnrealMCPPIEUtils.
     */
    static APlayerController* ResolvePlayerController(int32 ControllerIndex);

    /** Поиск виджета по имени в PIE мире (логика из UMGTestCommands::FindWidgetByName). */
    static UWidget* FindWidgetByName(UWorld* PlayWorld, const FString& Target, UUserWidget*& OutOwner);
};
