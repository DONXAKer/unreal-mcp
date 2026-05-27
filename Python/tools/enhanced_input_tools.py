"""
Enhanced Input tools for Unreal MCP (MCP-PLUGIN-004).

UE5.7 native Enhanced Input System — UInputAction + UInputMappingContext +
K2Node_EnhancedInputAction. Параллельно с legacy `create_input_mapping`
(UInputSettings) для backward compat.

Команды:
  - create_input_action               — UInputAction DataAsset.
  - create_input_mapping_context      — UInputMappingContext DataAsset.
  - add_input_action_mapping          — key + modifiers + triggers → IMC.
  - add_enhanced_input_action_event_node — K2Node в Blueprint event graph
                                          (Value/Elapsed Time pins materialise
                                          сразу через PinResolver T001).
"""

from __future__ import annotations

import logging
from typing import Any

from mcp.server.fastmcp import Context, FastMCP

from tools._envelope import wrap_with_envelope

logger = logging.getLogger("UnrealMCP")


def register_enhanced_input_tools(mcp: FastMCP) -> None:
    """Регистрация Enhanced Input MCP tools."""
    mcp = wrap_with_envelope(mcp)

    @mcp.tool()
    def create_input_action(
        ctx: Context[Any, Any, Any],
        name: str,
        value_type: str,
        path: str = "/Game/Input/Actions",
    ) -> dict[str, Any]:
        """Создать UInputAction DataAsset (UE5.7 Enhanced Input).

        Идемпотентно: если ассет уже есть — возвращает status="skipped".

        Args:
            name: Имя ассета. Авто-префикс "IA_" если отсутствует.
            value_type: "Bool" | "Axis1D" | "Axis2D" | "Axis3D".
            path: Папка в Content Browser. По умолчанию /Game/Input/Actions.

        Returns:
            { ok, status, assetPath, value_type }.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("create_input_action", {
            "name": name,
            "value_type": value_type,
            "path": path,
        })
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def create_input_mapping_context(
        ctx: Context[Any, Any, Any],
        name: str,
        path: str = "/Game/Input/Contexts",
    ) -> dict[str, Any]:
        """Создать UInputMappingContext DataAsset.

        Идемпотентно: status="skipped" если уже есть.

        Args:
            name: Имя ассета. Авто-префикс "IMC_" если отсутствует.
            path: Папка. По умолчанию /Game/Input/Contexts.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        response = unreal.send_command("create_input_mapping_context", {
            "name": name,
            "path": path,
        })
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def add_input_action_mapping(
        ctx: Context[Any, Any, Any],
        context_path: str,
        action_path: str,
        key: str,
        modifiers: list[str] | None = None,
        triggers: list[str] | None = None,
    ) -> dict[str, Any]:
        """Привязать key→action в UInputMappingContext.

        Args:
            context_path: Полный путь к IMC (e.g. "/Game/Input/Contexts/IMC_Default").
            action_path: Полный путь к IA.
            key: Engine key string ("W", "LeftMouseButton", "Gamepad_FaceButton_Bottom").
            modifiers: Список встроенных UInputModifier по имени:
                       "Negate", "DeadZone", "Scalar", "Smooth", "ScaleByDeltaTime",
                       "FOVScaling", "ToWorldSpace", "Swizzle" (default YXZ),
                       "Swizzle.YXZ", "Swizzle.XZY", "Swizzle.ZYX", "Swizzle.ZXY", "Swizzle.YZX".
            triggers: Список встроенных UInputTrigger:
                      "Down", "Pressed", "Released", "Hold", "HoldAndRelease",
                      "Tap", "Pulse", "RepeatedTap", "ChordAction".

        Returns:
            { ok, key, modifiers_applied: int, triggers_applied: int }.
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {
            "context_path": context_path,
            "action_path": action_path,
            "key": key,
        }
        if modifiers:
            params["modifiers"] = modifiers
        if triggers:
            params["triggers"] = triggers

        response = unreal.send_command("add_input_action_mapping", params)
        return response or {"status": "error", "error": "No response"}

    @mcp.tool()
    def add_enhanced_input_action_event_node(
        ctx: Context[Any, Any, Any],
        blueprint_path: str,
        action_path: str,
        trigger_event: str = "Triggered",
        location: list[float] | None = None,
    ) -> dict[str, Any]:
        """Создать K2Node_EnhancedInputAction в event graph Blueprint.

        Узел сразу содержит output-пины (Value, Elapsed Time, Triggered Time)
        благодаря AllocateDefaultPins + ReconstructNode — это use-case
        для PinResolver hardening из MCP-PLUGIN-001.

        Args:
            blueprint_path: Путь к BP (Pawn/PlayerController/Actor).
            action_path: Путь к IA-ассету.
            trigger_event: Какое событие — "Started" | "Triggered" |
                           "Completed" | "Canceled" | "Ongoing".
                           Default "Triggered" (наиболее частое).
            location: Позиция узла [X, Y] в graph. Default [0, 0].

        Returns:
            {
              ok, node_id (NodeGuid string),
              node_title, trigger_event,
              pins: [{name, friendlyName, direction, pinCategory}, ...]
            }
        """
        from unreal_mcp_server import get_unreal_connection

        unreal = get_unreal_connection()
        if not unreal:
            return {"status": "error", "error": "No Unreal connection"}

        params: dict[str, Any] = {
            "blueprint_path": blueprint_path,
            "action_path": action_path,
            "trigger_event": trigger_event,
            "location": location or [0, 0],
        }
        response = unreal.send_command("add_enhanced_input_action_event_node", params)
        return response or {"status": "error", "error": "No response"}

    logger.info("Enhanced Input tools registered (create_input_action, create_input_mapping_context, add_input_action_mapping, add_enhanced_input_action_event_node)")
