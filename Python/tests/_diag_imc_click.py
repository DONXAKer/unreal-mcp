"""Дизамбигуатор: дошёл ли LeftMouseButton до IA_LeftMouseClick контроллера.
simulate_key инъектит на уровне APlayerController::InputKey — минует Slate/HUD.
Если [DIAG-CLK] OnLeftMouseClick появится → путь IA→контроллер цел (виноват HUD).
PIE уже должен быть в Deployment (оставлен предыдущим self-test)."""
from __future__ import annotations
import sys, time, json
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_server import UnrealConnection

c = UnrealConnection(port=55557)
assert c.connect(), "нет соединения 55557"
def s(cmd, p=None):
    r = c.send_command(cmd, p or {}); return r.get("result", r) if isinstance(r, dict) else r

print("status:", json.dumps(s("pie_status", {}), ensure_ascii=False)[:160])
for i in range(3):
    r = s("simulate_key", {"key": "LeftMouseButton", "controller_index": 0})
    print(f"simulate_key LeftMouseButton #{i}:", json.dumps(r, ensure_ascii=False)[:160])
    time.sleep(0.6)
print("done")
