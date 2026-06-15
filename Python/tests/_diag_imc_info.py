import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection
c=UnrealConnection(port=55557); assert c.connect(),"нет 55557"
for p in ("/Game/Game/Core/Input/IMC_FreeCamera","/Game/Core/Input/IMC_FreeCamera"):
    r=c.send_command("input_mapping_context_get_info",{"context_path":p})
    res=r.get("result",r) if isinstance(r,dict) else r
    if isinstance(res,dict) and res.get("mappings") is not None:
        print("PATH OK:", p)
        for m in res["mappings"]:
            print(f"  key={m.get('key'):<20} action={(m.get('action') or '').split('.')[-1]:<22} triggers={m.get('triggers')}")
        break
    else:
        print("FAIL", p, "->", json.dumps(res, ensure_ascii=False)[:80])
