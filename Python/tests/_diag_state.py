import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
for port in (55557, 55558):
    c = UnrealConnection(port=port)
    if not c.connect():
        print(f"[{port}] НЕТ СОЕДИНЕНИЯ"); continue
    def s(cmd,p=None):
        r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
    st = s("pie_status",{})
    print(f"[{port}] running={st.get('is_running')} widget={st.get('current_widget')}")
    t = s("get_widget_tree", {"controller_index":0})
    uws = [uw.get("class","?") for uw in (t.get("user_widgets") or [])]
    print(f"[{port}] user_widgets: {uws}")
