// Утилиты multiplayer PIE (MCP-PLUGIN-003).
//
// MCP сначала работал с одним PlayerController. Для tests matchmaking/draft/battle
// нужны 2 PIE-клиента — этот helper резолвит PlayerController по индексу
// через GEngine->GetWorldContexts() и используется во всех PIE-aware командах
// (simulate_key, click_widget_by_name, set_text_on_widget, pie_screenshot, ...).

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class APlayerController;
class UWorld;

/**
 * Резолв PIE-клиентов по индексу + общие хелперы для multi-client PIE.
 *
 * Идея:
 *   - PIE с N клиентами создаёт N WorldContext'ов типа PIE (плюс editor world).
 *     Каждый со своим UWorld и своим списком PlayerController'ов.
 *   - GetPlayerControllerByIndex(N) пробегает PIE WorldContexts (game world)
 *     по PIEInstance, возвращает GetFirstPlayerController() из N-го.
 *   - Дополнительно поддерживается случай "один world, N PC" (split-screen):
 *     если PIE-инстансов меньше чем num_clients, используем Iterator по PC
 *     в первом PIE world.
 */
class UNREALMCP_API FUnrealMCPPIEUtils
{
public:
    /**
     * Получить PlayerController по индексу клиента.
     *
     * @param Index 0-based; 0 — первый клиент (server в listen-server mode).
     * @return nullptr если PIE не запущен или индекс за границами.
     */
    static APlayerController* GetPlayerControllerByIndex(int32 Index);

    /**
     * Количество текущих PIE-клиентов.
     * Считаются WorldContext'ы типа PIE + случай "один world с несколькими PC".
     */
    static int32 GetNumPIEClients();

    /**
     * Количество ОТДЕЛЬНЫХ клиентских PIE-WorldContext'ов (без dedicated-server).
     * >1 → true multi-world (каждый клиент в своём UWorld) → разделение клиентов
     * делает фильтр по World, и фильтр по OwningPlayer не нужен (и вреден в
     * listen-server world, где несколько PC). ==1 → single-world split-screen,
     * где для разделения клиентов нужен фильтр по OwningPlayer.
     */
    static int32 GetNumPIEWorldContexts();

    /**
     * Получить UWorld N-го PIE-клиента. Для multi-PIE — отдельный world per client.
     * Для single-world multi-PC — возвращает общий PIE world.
     */
    static UWorld* GetPIEWorldForClient(int32 Index);

    /**
     * Собрать JSON-описание клиента для pie_status:
     *   { index, controller_class, current_widget, current_level, world_name, ... }.
     */
    static TSharedPtr<class FJsonObject> DescribeClient(int32 Index);
};
