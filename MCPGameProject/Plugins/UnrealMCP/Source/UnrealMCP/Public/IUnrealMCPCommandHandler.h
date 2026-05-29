// Точка расширения UnrealMCP: внешние (project-specific) обработчики команд.
//
// Общий плагин UnrealMCP не знает про конкретные игры. Project-specific модули
// реализуют этот интерфейс и регистрируют себя через
// FEpicUnrealMCPModule::RegisterCommandHandler — тогда bridge маршрутизирует
// неизвестные ему команды в зарегистрированные обработчики. Это держит ядро
// плагина generic: в нём нет ни одной строки про конкретный проект.

#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Интерфейс внешнего обработчика MCP-команд.
 *
 * Реализуется в project-specific плагинах и регистрируется в модуле UnrealMCP.
 * Bridge опрашивает зарегистрированные обработчики для команд, которые не
 * совпали ни с одним встроенным handler'ом.
 */
class IUnrealMCPCommandHandler
{
public:
    virtual ~IUnrealMCPCommandHandler() = default;

    /**
     * Может ли этот обработчик выполнить команду с данным именем.
     * @param CommandType  Имя команды (поле "type" из JSON-запроса).
     * @return true если HandleCommand следует вызвать для этой команды.
     */
    virtual bool CanHandleCommand(const FString& CommandType) const = 0;

    /**
     * Выполнить команду.
     * @param CommandType  Имя команды.
     * @param Params       JSON-параметры запроса (поле "params").
     * @return JSON-результат ({ ok / success / error / ... }). Никогда nullptr.
     */
    virtual TSharedPtr<FJsonObject> HandleCommand(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params) = 0;
};
