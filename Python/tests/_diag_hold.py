import sys, os, time, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c=UnrealConnection(port=55557); assert c.connect(),"нет 55557"
def s(cmd,p=None):
    try:
        r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
    except Exception as e: return {"exc":str(e)}
print("start:", json.dumps(s("pie_start",{"mode":"new_window"}), ensure_ascii=False)[:110])
time.sleep(10)
# LMB удержание через реальные тики
print("down:", json.dumps(s("simulate_key",{"key":"LeftMouseButton","action":"down","controller_index":0}), ensure_ascii=False)[:110])
time.sleep(0.6)
print("up:", json.dumps(s("simulate_key",{"key":"LeftMouseButton","action":"up","controller_index":0}), ensure_ascii=False)[:110])
time.sleep(0.4)
# W удержание (камера)
print("W down:", json.dumps(s("simulate_key",{"key":"W","action":"down","controller_index":0}), ensure_ascii=False)[:90])
time.sleep(0.8)
print("W up:", json.dumps(s("simulate_key",{"key":"W","action":"up","controller_index":0}), ensure_ascii=False)[:90])
print("done")
