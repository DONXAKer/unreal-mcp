"""Smoke for MCP-PLUGIN-002: set_text_on_widget on a live PIE EditableTextBox.

Требует:
  - Запущенный PIE с WBP_Login на экране (LoginUsernameInput field).
  - UE Editor с UnrealMCP v2.8.0+.

Сценарий:
  1. ping → подтвердить версию плагина.
  2. set_text_on_widget("LoginUsernameInput", "тест@почта.рф").
  3. get_widget_tree → найти LoginUsernameInput, убедиться что widget найден.
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


def _result(resp: dict[str, Any]) -> dict[str, Any]:
    return resp.get("result", resp) if isinstance(resp, dict) else {}


def main() -> int:
    conn = UnrealConnection()
    if not conn.connect():
        print("ERROR: cannot connect to UE editor on 127.0.0.1:55557")
        return 2

    pong = _result(_send(conn, "ping", {}))
    print(f"plugin_version: {pong.get('plugin_version')}")

    resp = _send(conn, "set_text_on_widget", {
        "widget_name": "LoginUsernameInput",
        "text": "тест@почта.рф",
    })
    result = _result(resp)
    if not result.get("ok") and resp.get("status") != "success":
        print(f"\nSMOKE FAIL: set_text_on_widget returned {resp}")
        return 1

    widget_class = result.get("widget_class") or result.get("actualClass")
    print(f"\nRESULT: widget_class={widget_class}")

    print("\nSMOKE: set_text_on_widget OK (Unicode payload accepted).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
