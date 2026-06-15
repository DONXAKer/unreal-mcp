import sys, os, json, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
for port in (55557, 55558):
    c = UnrealConnection(port=port)
    if not c.connect():
        print(f"[{port}] НЕТ СОЕДИНЕНИЯ"); continue
    def s(cmd,p=None):
        r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
    print(f"[{port}] status:", json.dumps(s("pie_status",{}), ensure_ascii=False)[:160])
    print(f"[{port}] pie_stop:", json.dumps(s("pie_stop",{}), ensure_ascii=False)[:100])
    time.sleep(2)
    print(f"[{port}] pie_start:", json.dumps(s("pie_start",{"mode":"new_window","num_clients":1}), ensure_ascii=False)[:160])
