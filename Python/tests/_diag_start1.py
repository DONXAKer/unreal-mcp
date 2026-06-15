import sys, os, json, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c=UnrealConnection(port=55557)
assert c.connect(),"нет 55557"
def s(cmd,p=None):
    try:
        r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
    except Exception as e:
        return {"exc":str(e)}
print("start:", json.dumps(s("pie_start",{"mode":"new_window","num_clients":1}), ensure_ascii=False)[:160])
time.sleep(10)
print("status:", json.dumps(s("pie_status",{}), ensure_ascii=False)[:160])
