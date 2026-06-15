"""Быстрая проверка FIX-UI-008: pie_start(num_clients=2) → 2 РАЗНЫХ world."""
from __future__ import annotations
import json, logging, sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)
from unreal_mcp_server import UnrealConnection


def send(conn, cmd, params):
    try:
        return conn.send_command(cmd, params)
    except Exception as e:
        return {"_exc": str(e)}


def main():
    conn = UnrealConnection(); conn.connect()
    send(conn, "pie_stop", {}); time.sleep(3)
    print("pie_start(num_clients=2):", json.dumps(send(conn, "pie_start", {"num_clients": 2}), ensure_ascii=False)[:200])

    # Ждём оба клиента
    worlds = []
    for _ in range(40):
        st = send(conn, "pie_status", {})
        res = st.get("result", st) if isinstance(st, dict) else {}
        clients = res.get("clients") or []
        if len(clients) >= 2 and all(c.get("current_widget") for c in clients):
            worlds = [(c.get("index"), c.get("world_name"), c.get("controller_name"), c.get("current_widget")) for c in clients]
            print("plugin_version:", res.get("plugin_version"))
            break
        time.sleep(1.0)

    print("clients:")
    for w in worlds:
        print("  ", w)
    distinct_worlds = len({w[1] for w in worlds}) if worlds else 0
    print(f"distinct world_names = {distinct_worlds}  (ожидаем 2)")
    return 0 if distinct_worlds >= 2 else 1


if __name__ == "__main__":
    sys.exit(main())
