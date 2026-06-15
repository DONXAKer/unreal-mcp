import sys, os, time, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c=UnrealConnection(port=55557); assert c.connect(),"нет 55557"
def s(cmd,p=None):
    try:
        r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
    except Exception as e: return {"exc":str(e)}
print("start:", json.dumps(s("pie_start",{"mode":"new_window"}), ensure_ascii=False)[:120])
time.sleep(10)
for k in ("LeftMouseButton","MiddleMouseButton","W"):
    print(f"simulate {k}:", json.dumps(s("simulate_key",{"key":k,"controller_index":0}), ensure_ascii=False)[:120])
    time.sleep(0.8)
print("done")
