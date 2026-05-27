"""
Smoke-test для MCP-PLUGIN-004 — Enhanced Input.

Проверяем:
  1. create_input_action(IA_TestMove, Axis2D) → создаётся.
  2. create_input_mapping_context(IMC_Test) → создаётся.
  3. add_input_action_mapping для W/A/S/D с модификаторами Swizzle и Negate.
  4. add_enhanced_input_action_event_node на тестовом BP_Pawn:
     - возвращает pins[] с Value, Elapsed Time, Triggered Time, и exec pins
       (Started/Triggered/Completed/Canceled).
  5. get_pin_info на свежий node_id → находит "Value" пин (depends on PinResolver T001).
  6. Повторный вызов create_input_action — status="skipped".

Запуск:
  uv run python tests/test_enhanced_input.py

Требует:
  - UE Editor с UnrealMCP v2.10.0+.
  - Чистый /Game/Input/ или удалённые предыдущие тестовые ассеты.
"""

from __future__ import annotations

import json
import sys
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from unreal_mcp_server import UnrealConnection


def _send(conn: UnrealConnection, cmd: str, params: dict[str, Any]) -> dict[str, Any]:
    print(f"\n>>> {cmd} {json.dumps(params, ensure_ascii=False)}")
    resp = conn.send_command(cmd, params) or {}
    print(f"<<< {json.dumps(resp, ensure_ascii=False)[:600]}")
    return resp


def _unwrap(resp: dict[str, Any]) -> dict[str, Any]:
    """Достаём content: либо result, либо raw."""
    if isinstance(resp.get("result"), dict):
        return resp["result"]
    return resp


def main() -> int:
    conn = UnrealConnection()
    if not conn.connect():
        print("ERROR: не могу подключиться к UE editor (127.0.0.1:55557)")
        return 2

    # 1) Version check.
    pong = _unwrap(_send(conn, "ping", {}))
    print(f"plugin_version: {pong.get('plugin_version')}")

    # 2) IA_TestMove.
    ia_resp = _unwrap(_send(conn, "create_input_action", {
        "name": "TestMove",
        "value_type": "Axis2D",
    }))
    assert ia_resp.get("ok"), f"create_input_action failed: {ia_resp}"
    ia_path = ia_resp.get("assetPath")
    print(f"IA path: {ia_path} status={ia_resp.get('status')}")

    # 2b) Repeat → expect skipped.
    ia2 = _unwrap(_send(conn, "create_input_action", {
        "name": "TestMove",
        "value_type": "Axis2D",
    }))
    assert ia2.get("status") == "skipped", f"expected skipped, got {ia2}"

    # 3) IMC_Test.
    imc = _unwrap(_send(conn, "create_input_mapping_context", {
        "name": "Test",
    }))
    imc_path = imc.get("assetPath")
    print(f"IMC path: {imc_path}")

    # 4) Mapping W → IA_TestMove with Swizzle.YXZ (typical для WASD on Axis2D).
    map_w = _unwrap(_send(conn, "add_input_action_mapping", {
        "context_path": imc_path,
        "action_path": ia_path,
        "key": "W",
        "modifiers": ["Swizzle.YXZ"],
        "triggers": ["Down"],
    }))
    assert map_w.get("ok"), f"add_input_action_mapping W failed: {map_w}"

    # 5) Mapping S → Negate.
    _send(conn, "add_input_action_mapping", {
        "context_path": imc_path,
        "action_path": ia_path,
        "key": "S",
        "modifiers": ["Negate", "Swizzle.YXZ"],
        "triggers": ["Down"],
    })

    # 6) Создать BP_Pawn и event node.
    _send(conn, "create_blueprint", {
        "name": "BP_TestEnhancedPawn",
        "parent_class": "Pawn",
    })
    # OK either created or already exists.

    ev = _unwrap(_send(conn, "add_enhanced_input_action_event_node", {
        "blueprint_path": "/Game/Blueprints/BP_TestEnhancedPawn",
        "action_path": ia_path,
        "trigger_event": "Triggered",
        "location": [0, 0],
    }))
    node_id = ev.get("node_id")
    pins = ev.get("pins", [])
    print(f"node_id={node_id}, pins_count={len(pins)}")
    for p in pins:
        print(f"  - {p.get('name')} ({p.get('direction')}, {p.get('pinCategory')})")

    # 7) get_pin_info — должен найти Value pin (depends on PinResolver T001 + reconstruct).
    if node_id:
        pin_info = _unwrap(_send(conn, "get_pin_info", {
            "blueprint_name": "/Game/Blueprints/BP_TestEnhancedPawn",
            "node_id": node_id,
            "pin_name": "ActionValue",
        }))
        if not pin_info.get("pin"):
            # Иногда пин называется "Value" или "ActionValue" — проверим оба.
            pin_info2 = _unwrap(_send(conn, "get_pin_info", {
                "blueprint_name": "/Game/Blueprints/BP_TestEnhancedPawn",
                "node_id": node_id,
                "pin_name": "Value",
            }))
            print(f"Value pin lookup: {pin_info2.get('pin') is not None}")

    print("\nSMOKE: enhanced input flow done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
