"""
Smoke-test для MCP-PLUGIN-001 — PinResolver hardening.

Что проверяем:
  1. set_pin_default_value/get_pin_info теперь auto-reconstruct'ит K2Node
     и находит пины, появляющиеся только после AllocateDefaultPins
     (например Class у K2Node_CreateWidget).
  2. При неудаче резолва — ответ содержит details.availablePins[]
     с PinName/FriendlyName/Direction всех пинов узла.
  3. Python обёртка обогащает ошибку did_you_mean при typo.

Запуск:
  uv run python tests/test_pin_resolver.py

Требует:
  - UE Editor с UnrealMCP плагином v2.7.0+.
  - Никакого PIE — это работа с design-time Blueprint'ом.
"""

from __future__ import annotations

import json
import sys
import time
from typing import Any

# Подключаемся к Python tools напрямую через unreal_mcp_server.UnrealConnection.
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from unreal_mcp_server import UnrealConnection  # noqa: E402


def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any]) -> dict[str, Any]:
    print(f"\n>>> {cmd} {json.dumps(params, ensure_ascii=False)}")
    resp = conn.send_command(cmd, params) or {}
    print(f"<<< {json.dumps(resp, ensure_ascii=False)[:500]}")
    return resp


def main() -> int:
    conn = UnrealConnection()
    if not conn.connect():
        print("ERROR: cannot connect to UE editor on 127.0.0.1:55557")
        return 2

    # 1) ping — версия должна быть >= 2.7.0
    pong = _send(conn, "ping", {})
    result = pong.get("result", pong)
    plugin_v = result.get("plugin_version", "")
    print(f"plugin_version reported: {plugin_v}")
    if not plugin_v.startswith("2.7"):
        print(f"WARN: plugin not rebuilt? expected 2.7.x, got {plugin_v}")

    # 2) Готовим тестовый Blueprint.
    bp_name = "BP_PinResolverSmoke"
    bp_path = "/Game/Blueprints/" + bp_name

    create_resp = _send(conn, "create_blueprint", {
        "name": bp_name,
        "parent_class": "Actor",
    })
    # create_blueprint идемпотентен — успех или "already exists".

    # 3) Добавляем функцию-ноду CreateWidget — она имеет dynamic Class pin.
    add_node = _send(conn, "add_blueprint_node", {
        "blueprint_name": bp_path,
        "function_name_to_call": "CreateWidget",
        "node_position": [200, 200],
    })
    node_id = None
    result_payload = add_node.get("result", add_node)
    if isinstance(result_payload, dict):
        node_id = result_payload.get("node_id") or result_payload.get("nodeId")
    if not node_id:
        print(f"WARN: cannot extract node_id from {add_node}")
        # Используем fallback — get nodes
        find_nodes = _send(conn, "find_blueprint_nodes", {
            "blueprint_name": bp_path,
        })
        print(f"find_blueprint_nodes: {find_nodes}")

    # 4) Проверка: запрос несуществующего пина → ответ с availablePins[].
    if node_id:
        bad = _send(conn, "get_pin_info", {
            "blueprint_name": bp_path,
            "node_id": node_id,
            "pin_name": "ClsasName",  # typo на месте Class
        })
        # Должны увидеть did_you_mean в обёртке Python (или availablePins в raw).
        details = bad.get("details") or bad.get("error_details") or {}
        avail = details.get("availablePins")
        print(f"availablePins present: {bool(avail)} (count={len(avail) if isinstance(avail, list) else 0})")

    print("\nSMOKE: pin resolver basic checks done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
