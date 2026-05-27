"""Smoke for MCP-PLUGIN-003: multi-client PIE shape verification.

Минимальный сценарий:
  1. pie_stop (на случай если PIE уже запущен с прошлого прогона)
  2. pie_status — должен показать is_running=false, num_clients=0
  3. pie_start(num_clients=2)
  4. pie_status — должен показать is_running=true, num_clients=2, clients[]
  5. pie_stop

Запуск:
  uv run python tests/_smoke_pie_multiclient.py

Требует UE Editor с UnrealMCP v2.9.0+. PIE будет запущен — STOMP сервер
необязателен (мы просто смотрим shape ответа, не геймплей).
"""

from __future__ import annotations

import json
import sys
import time
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

    _send(conn, "pie_stop", {})
    time.sleep(0.5)

    pre = _result(_send(conn, "pie_status", {}))
    print(f"pre-start status: is_running={pre.get('is_running')} num_clients={pre.get('num_clients')}")

    start = _result(_send(conn, "pie_start", {"num_clients": 2}))
    print(f"pie_start: started={start.get('started')} clients_in_response={len(start.get('clients', []))}")

    time.sleep(3.0)

    status = _result(_send(conn, "pie_status", {}))
    is_running = status.get("is_running")
    num_clients = status.get("num_clients")
    clients = status.get("clients", [])
    print(f"\nRESULT: is_running={is_running} num_clients={num_clients} clients_array={len(clients)}")
    for c in clients:
        print(f"  - client[{c.get('index')}]: controller={c.get('controller_class')} world={c.get('world_name')}")

    # Скриншоты по каждому клиенту: должны лечь в разные файлы с суффиксом _client<N>.
    shot0 = _result(_send(conn, "pie_screenshot", {
        "filename": "smoke_mc.png",
        "show_ui": True,
        "controller_index": 0,
    }))
    shot1 = _result(_send(conn, "pie_screenshot", {
        "filename": "smoke_mc.png",
        "show_ui": True,
        "controller_index": 1,
    }))
    file0 = shot0.get("filename") or shot0.get("assetPath")
    file1 = shot1.get("filename") or shot1.get("assetPath")
    print(f"\nscreenshots: client0={file0!r}  client1={file1!r}")
    distinct_files = bool(file0) and bool(file1) and file0 != file1

    _send(conn, "pie_stop", {})

    if is_running and num_clients == 2 and len(clients) == 2 and distinct_files:
        print("\nSMOKE: multi-client PIE shape + per-client screenshots OK.")
        return 0
    else:
        print(f"\nSMOKE: failure — shape_ok={is_running and num_clients == 2 and len(clients) == 2} distinct_files={distinct_files}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
