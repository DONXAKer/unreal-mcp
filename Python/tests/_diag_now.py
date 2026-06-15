import sys, os, json, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c=UnrealConnection(port=55557); assert c.connect(),"нет 55557"
def s(cmd,p=None):
    r=c.send_command(cmd,p or {}); return r.get("result",r) if isinstance(r,dict) else r
t=s("get_widget_tree",{"controller_index":0})
uws=[uw.get("class") for uw in (t.get("user_widgets") or [])]
print("p1 user_widgets:", uws)
dep=any("Deployment" in (u or "") for u in uws)
print("deployment present:", dep)
if dep:
    print("deploy state:", json.dumps(s("wc_get_deployment_state",{"controller_index":0}), ensure_ascii=False)[:100])
    # выбрать карточку
    for uw in (t.get("user_widgets") or []):
        if "Deployment" in (uw.get("class") or ""):
            def find(n):
                if not isinstance(n,dict): return None
                if "CatalogEntry" in (n.get("class") or "") or "Button" in (n.get("class") or ""):
                    return n
                for ch in (n.get("children") or []):
                    r=find(ch)
                    if r: return r
                return None
            card=find(uw.get("root"))
            if card:
                print("invoke card:", card.get("name"))
                s("invoke_button_click",{"widget_name":card.get("name"),"controller_index":0})
                time.sleep(0.8)
    before=s("wc_get_deployment_state",{"controller_index":0}).get("deployed_count",0)
    # zone 2 (blue) = правая сторона; zone 1 (red) = левая. Свип по всей ширине.
    for (nx,ny) in [(nx,ny) for ny in (0.42,0.52,0.62) for nx in (0.72,0.80,0.28,0.36,0.5)]:
        s("screen_click",{"x":nx,"y":ny,"normalized":True,"action":"down","controller_index":0})
        time.sleep(0.3)
        s("screen_click",{"x":nx,"y":ny,"normalized":True,"action":"up","controller_index":0})
        time.sleep(0.45)
        now=s("wc_get_deployment_state",{"controller_index":0}).get("deployed_count",0)
        print(f"click ({nx},{ny}) deployed={now}")
        if now>before: print(f"*** ПОСТАВЛЕН на ({nx},{ny}) ***"); break
