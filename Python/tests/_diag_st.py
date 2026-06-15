import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
for port in (55557,55558):
    c=UnrealConnection(port=port)
    if not c.connect(): print(f"[{port}] нет соединения"); continue
    r=c.send_command("pie_status",{}); res=r.get("result",r) if isinstance(r,dict) else r
    print(f"[{port}] running={res.get('is_running')} widget={res.get('current_widget')} world={res.get('world_name')}")
