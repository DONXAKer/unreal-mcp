// WarCard-specific game commands (MCP-PLUGIN-006).
//
// Эти команды project-specific — они зовут BlueprintCallable методы конкретных
// UWorldSubsystem'ов клиента WarCard (например UDeploymentSubsystem::DeployUnit)
// через UE reflection. Плагин **не** include'ит клиентские заголовки —
// resolution идёт через class path ("/Script/Client.<ClassName>") и FindFunction
// по имени. Это держит плагин компилируемым без зависимости от Client.Build.cs,
// но всё равно даёт типобезопасный вызов через ProcessEvent + FProperty
// iteration (с проверкой signature на каждом вызове).
//
// Если функция переименована или удалена в клиенте — команда вернёт error,
// а не упадёт; плагин продолжит работать.

#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWorld;
class UObject;

/**
 * Handler класс для WarCard-specific runtime команд (deployment / battle).
 *
 * Поддерживаемые команды:
 *   wc_select_unit           — UUnitSelectionSubsystem::AddUnitToComposition(UnitId) → bool
 *   wc_deselect_unit         — UUnitSelectionSubsystem::RemoveUnitFromComposition(UnitId) → bool
 *   wc_confirm_selection     — UUnitSelectionSubsystem::SendCompositionToServer()
 *   wc_get_selection_state   — { count, ready }
 *   wc_deploy_unit           — UDeploymentSubsystem::DeployUnit(UnitId, GridX, GridY) → bool
 *   wc_confirm_deployment    — UDeploymentSubsystem::ConfirmDeployment()
 *   wc_get_deployment_state  — { deployed_count, ready }
 *   wc_surrender             — UActionCardSubsystem::Surrender()
 *   wc_end_turn              — UActionCardSubsystem::EndTurn() → bool
 *   wc_get_battle_state      — { my_turn, ap, max_ap }
 */
class FWarCardGameCommands
{
public:
    FWarCardGameCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleSelectUnit(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeselectUnit(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConfirmSelection(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSelectionState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeployUnit(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConfirmDeployment(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetDeploymentState(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSurrender(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleEndTurn(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBattleState(const TSharedPtr<FJsonObject>& Params);

    /**
     * Найти UWorldSubsystem по class path в PIE world выбранного клиента.
     *
     * @param ControllerIndex  Индекс PIE-клиента (multi-client setup).
     * @param SubsystemClassPath  Путь вида "/Script/Client.DeploymentSubsystem".
     * @param OutError  [out] Сообщение об ошибке для error response.
     * @return Subsystem instance или nullptr (тогда OutError содержит причину).
     */
    static UObject* ResolveSubsystem(
        int32 ControllerIndex,
        const FString& SubsystemClassPath,
        FString& OutError);
};
