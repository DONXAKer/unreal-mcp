"""WarCard project-specific MCP tools (MCP-PLUGIN-006).

Эти tools зовут BlueprintCallable методы UDeploymentSubsystem через UE
reflection (плагин не include'ит клиентские .h). Используются в e2e-тестах
после MATCH_FOUND для прохождения deployment phase без UI-кликов.
"""

from __future__ import annotations

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

logger = logging.getLogger("UnrealMCP")


def register_warcard_tools(mcp: FastMCP) -> None:
    """Регистрирует WarCard MCP tools для selection + deployment."""

    @mcp.tool()
    def wc_select_unit(
        ctx: Context[Any, Any, Any],
        unit_id: str,
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Выбрать юнит в композицию (UnitSelection phase, до Deployment).

        Под капотом — UUnitSelectionSubsystem::AddUnitToComposition.

        Returns: { ok, selected: bool, unit_id, controller_index, return }
        """
        from unreal_mcp_server import get_unreal_connection
        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}
        logger.info(f"wc_select_unit: unit='{unit_id}' ctrl={controller_index}")
        return unreal.send_command("wc_select_unit", {"unit_id": unit_id, "controller_index": controller_index}) or {}

    @mcp.tool()
    def wc_deselect_unit(
        ctx: Context[Any, Any, Any],
        unit_id: str,
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Убрать юнит из композиции. UUnitSelectionSubsystem::RemoveUnitFromComposition."""
        from unreal_mcp_server import get_unreal_connection
        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}
        return unreal.send_command("wc_deselect_unit", {"unit_id": unit_id, "controller_index": controller_index}) or {}

    @mcp.tool()
    def wc_confirm_selection(
        ctx: Context[Any, Any, Any],
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Отправить композицию на сервер — переход в Deployment.
        UUnitSelectionSubsystem::SendCompositionToServer."""
        from unreal_mcp_server import get_unreal_connection
        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}
        return unreal.send_command("wc_confirm_selection", {"controller_index": controller_index}) or {}

    @mcp.tool()
    def wc_get_selection_state(
        ctx: Context[Any, Any, Any],
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Снимок состояния UnitSelection phase.

        Returns: { ok, selected_count: int, ready: bool, controller_index }
        """
        from unreal_mcp_server import get_unreal_connection
        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}
        return unreal.send_command("wc_get_selection_state", {"controller_index": controller_index}) or {}

    @mcp.tool()
    def wc_deploy_unit(
        ctx: Context[Any, Any, Any],
        unit_id: str,
        grid_x: int,
        grid_y: int,
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Деплоит юнит в позицию (grid_x, grid_y) на DeploymentGrid.

        Под капотом — UDeploymentSubsystem::DeployUnit через UFunction reflection.
        Не требует UI-клика по GridCell — это direct subsystem call.

        Args:
            unit_id: ID юнита из ранее выбранного roster (см. UnitSelection phase).
            grid_x, grid_y: координаты ячейки 0-based.
            controller_index: PIE-клиент (для multi-client сценариев).

        Returns:
            { ok, deployed: bool, unit_id, grid_x, grid_y, controller_index,
              return: <bool> }
            Если deployed=false — UnitId не в roster, координата вне зоны
            игрока, либо ячейка занята.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {
            "unit_id": unit_id,
            "grid_x": grid_x,
            "grid_y": grid_y,
            "controller_index": controller_index,
        }
        logger.info(f"wc_deploy_unit: unit='{unit_id}' at ({grid_x},{grid_y}) ctrl={controller_index}")
        response = unreal.send_command("wc_deploy_unit", params)
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def wc_confirm_deployment(
        ctx: Context[Any, Any, Any],
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Подтвердить deployment — переход в фазу Battle.

        Под капотом — UDeploymentSubsystem::ConfirmDeployment(). Обычно вешается
        на ConfirmButton в WBP_DeploymentScreen.

        Args:
            controller_index: PIE-клиент.

        Returns:
            { ok, function, target_class, controller_index }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {"controller_index": controller_index}
        logger.info(f"wc_confirm_deployment: ctrl={controller_index}")
        response = unreal.send_command("wc_confirm_deployment", params)
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def wc_get_deployment_state(
        ctx: Context[Any, Any, Any],
        controller_index: int = 0,
    ) -> dict[str, Any]:
        """Снимок состояния deployment phase.

        Зовёт UDeploymentSubsystem::GetDeployedUnitCount + IsDeploymentReady.

        Args:
            controller_index: PIE-клиент.

        Returns:
            { ok, deployed_count: int, ready: bool, controller_index }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {"controller_index": controller_index}
        response = unreal.send_command("wc_get_deployment_state", params)
        return response or {"status": "error", "error": "No response"}

    logger.info("WarCard tools registered successfully")
