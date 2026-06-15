import sys, os, time, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c = UnrealConnection(port=55557)
assert c.connect(), "нет 55557"
def s(cmd,p=None):
    r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
st = s("pie_status",{})
print("widget:", st.get("current_widget"), "running:", st.get("is_running"))
# зона красного = левая сторона (x0-2) → normalized x≈0.3, центр по вертикали
for (nx,ny) in [(0.30,0.50),(0.30,0.40),(0.35,0.55)]:
    s("screen_click", {"x":nx,"y":ny,"normalized":True,"action":"down","controller_index":0})
    time.sleep(0.35)
    s("screen_click", {"x":nx,"y":ny,"normalized":True,"action":"up","controller_index":0})
    time.sleep(0.5)
    d = s("wc_get_deployment_state", {"controller_index":0})
    print(f"click ({nx},{ny}) -> {json.dumps(d, ensure_ascii=False)[:90]}")
print("done")
