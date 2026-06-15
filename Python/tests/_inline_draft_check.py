"""Diag: login 2 клиента → проверить доходит ли flow до DRAFT (start-game→dice1→draft)."""
from __future__ import annotations
import logging, sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)
from unreal_mcp_server import UnrealConnection
from tests._fixtures import ensure_test_user


def _send(conn, cmd, params, retries=4):
    last=None
    for _ in range(retries):
        try:
            r = conn.send_command(cmd, params)
            if isinstance(r, dict): return r.get("result", r) if r.get("status")=="success" else {"error": r.get("error"), "_raw": r}
            return {}
        except Exception as e:
            last=e; time.sleep(0.5)
            try: conn.disconnect()
            except Exception: pass
            try: conn.connect()
            except Exception: pass
    return {"error": str(last)}


def _wait_any(conn, ctrl, names, timeout=20.0):
    dl=time.monotonic()+timeout
    while time.monotonic()<dl:
        for n in names:
            if _send(conn,"find_widget",{"widget_name":n,"controller_index":ctrl}).get("found"): return n
        time.sleep(0.4)
    return None


def main():
    ts=int(time.time())
    u1,u2=f"wc_e2e_a_{ts}",f"wc_e2e_b_{ts}"
    ensure_test_user(u1,"Test1234",f"{u1}@x.com")
    ensure_test_user(u2,"Test1234",f"{u2}@x.com")
    conn=UnrealConnection(); conn.connect()

    _send(conn,"pie_stop",{}); time.sleep(3)
    _send(conn,"pie_start",{"num_clients":2})
    # wait both Login
    dl=time.monotonic()+30
    while time.monotonic()<dl:
        st=_send(conn,"pie_status",{}); cs=st.get("clients") or []
        if len(cs)==2 and all(c.get("current_widget")=="WBP_Login_C" for c in cs): break
        time.sleep(0.5)
    print("PIE ready, both on Login")

    for ctrl,login in [(0,u1),(1,u2)]:
        _send(conn,"set_text_on_widget",{"widget_name":"LoginUsernameInput","text":login,"controller_index":ctrl})
        _send(conn,"set_text_on_widget",{"widget_name":"LoginPasswordInput","text":"Test1234","controller_index":ctrl})
        _send(conn,"invoke_button_click",{"widget_name":"LoginButton","controller_index":ctrl})
        w=_wait_any(conn,ctrl,["WBP_MainMenu_C_0","WBP_UnitSelection_C_0","WBP_Draft_C_0"],25)
        print(f"  client {ctrl} login -> {w}")
        if ctrl==0: time.sleep(2)

    # FindGame если на MainMenu
    for ctrl in (0,1):
        if _send(conn,"find_widget",{"widget_name":"WBP_MainMenu_C_0","controller_index":ctrl}).get("found"):
            _send(conn,"invoke_button_click",{"widget_name":"FindGameButton","controller_index":ctrl})
            print(f"  client {ctrl} FindGame clicked")

    # Poll до 60s: какие фазовые widgets появляются
    print("\n--- polling phase widgets (60s) ---")
    seen=set()
    dl=time.monotonic()+60
    while time.monotonic()<dl:
        for ctrl in (0,1):
            for n in ["WBP_UnitSelection_C_0","WBP_Draft_C_0","WBP_DeploymentScreen_C_0","WBP_BattleHUD_C_0","WBP_Mulligan_C_0"]:
                key=f"c{ctrl}:{n}"
                if key not in seen and _send(conn,"find_widget",{"widget_name":n,"controller_index":ctrl}).get("found"):
                    seen.add(key); print(f"  [{time.strftime('%H:%M:%S')}] c{ctrl}: {n} APPEARED")
        if any("Draft" in k for k in seen):
            # дошли до draft — успех первого этапа
            pass
        time.sleep(1.0)
    print(f"\nseen widgets: {sorted(seen)}")
    return 0


main()
