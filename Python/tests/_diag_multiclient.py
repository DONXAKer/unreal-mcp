"""Диагностика multi-client input: что резолвит controller_index 0 и 1."""
from __future__ import annotations
import json, logging, sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)
from unreal_mcp_server import UnrealConnection


def send(conn, cmd, params):
    try:
        r = conn.send_command(cmd, params)
        return r
    except Exception as e:
        return {"_exc": str(e)}


def main():
    conn = UnrealConnection(); conn.connect()
    print("=== pie_status ===")
    print(json.dumps(send(conn, "pie_status", {}), ensure_ascii=False, indent=1))

    for ctrl in (0, 1):
        print(f"\n=== set_text LoginUsernameInput ctrl={ctrl} ===")
        r = send(conn, "set_text_on_widget",
                 {"widget_name": "LoginUsernameInput", "text": f"wd_iso", "controller_index": ctrl})
        print(json.dumps(r, ensure_ascii=False))
        r2 = send(conn, "set_text_on_widget",
                  {"widget_name": "LoginPasswordInput", "text": "Test1234", "controller_index": ctrl})
        print(json.dumps(r2, ensure_ascii=False))

    for ctrl in (0, 1):
        print(f"\n=== invoke_button_click LoginButton ctrl={ctrl} ===")
        r = send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": ctrl})
        print(json.dumps(r, ensure_ascii=False))
        time.sleep(2.5)

    time.sleep(3)
    print("\n=== pie_status after login attempts ===")
    print(json.dumps(send(conn, "pie_status", {}), ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
