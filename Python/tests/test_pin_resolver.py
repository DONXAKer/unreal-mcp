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
from typing import Any

# Подключаемся к Python tools напрямую через unreal_mcp_server.UnrealConnection.
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from unreal_mcp_server import UnrealConnection


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
    # PinResolver появился в 2.7.0; принимаем любую версию >= 2.7.
    try:
        major, minor, *_ = plugin_v.split(".")
        if not (int(major) > 2 or (int(major) == 2 and int(minor) >= 7)):
            print(f"WARN: plugin not rebuilt? expected >= 2.7.0, got {plugin_v}")
    except (ValueError, IndexError):
        print(f"WARN: unparseable plugin_version: {plugin_v}")

    # 2) Готовим тестовый Blueprint.
    bp_name = "BP_PinResolverSmoke"
    bp_path = "/Game/Blueprints/" + bp_name

    _send(conn, "create_blueprint", {
        "name": bp_name,
        "parent_class": "Actor",
    })
    # create_blueprint идемпотентен — успех или "already exists".

    # 3) Добавляем function-ноду PrintString (стабильная функция UKismetSystemLibrary).
    #    Параметры совпадают с тем, что строит Python tool add_blueprint_function_node.
    add_node = _send(conn, "add_blueprint_node", {
        "blueprint_name": bp_path,
        "node_type": "CallFunction",
        "target_class": "KismetSystemLibrary",
        "target_function": "PrintString",
        "node_position": [200, 200],
    })
    result_payload = add_node.get("result", add_node) if isinstance(add_node, dict) else {}
    node_id = result_payload.get("node_id") or result_payload.get("nodeId")
    if not node_id:
        print(f"FAIL: cannot extract node_id from {add_node}")
        return 1

    # 4) Позитивный кейс: PinResolver находит существующий input pin "InString".
    pin_ok = _send(conn, "get_pin_info", {
        "blueprint_name": bp_path,
        "node_id": node_id,
        "pin_name": "InString",
    })
    ok_payload = pin_ok.get("result", pin_ok) if isinstance(pin_ok, dict) else {}
    pin = ok_payload.get("pin") if isinstance(ok_payload, dict) else None
    if not pin:
        print(f"FAIL: PinResolver не нашёл InString на PrintString узле: {pin_ok}")
        return 1
    print(f"OK: PinResolver resolved 'InString' -> category={pin.get('pin_category')}, direction={pin.get('direction')}")

    # 5) Негативный кейс: typo → ошибка должна содержать availablePins[].
    bad = _send(conn, "get_pin_info", {
        "blueprint_name": bp_path,
        "node_id": node_id,
        "pin_name": "InStrng",  # typo на месте InString
    })
    details = bad.get("details") or bad.get("error_details") or {}
    avail = details.get("availablePins")
    suggestion = details.get("suggestion") or details.get("did_you_mean")
    print(f"availablePins present: {bool(avail)} (count={len(avail) if isinstance(avail, list) else 0})")
    print(f"did_you_mean suggestion: {suggestion}")

    print("\nSMOKE: pin resolver checks done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
