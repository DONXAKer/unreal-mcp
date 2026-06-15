import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c = UnrealConnection(port=55557)
assert c.connect(), "нет 55557"
r = c.send_command("get_widget_tree", {"controller_index":0})
res = r.get("result", r) if isinstance(r, dict) else r
def walk(n, d=0):
    if not isinstance(n, dict): return
    vis = n.get("visibility", n.get("Visibility", "?"))
    print("  "*d + f"{n.get('name')} [{n.get('class')}] vis={vis}")
    for ch in (n.get("children") or []):
        walk(ch, d+1)
for uw in (res.get("user_widgets") or []):
    if "Deployment" in uw.get("class",""):
        print(f"=== {uw.get('class')} in_viewport={uw.get('is_in_viewport')} ===")
        walk(uw.get("root"))
