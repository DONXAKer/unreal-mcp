"""One-shot: pie_start n=2 через прямой UnrealConnection."""
import sys, time
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
from unreal_mcp_server import UnrealConnection
c = UnrealConnection(); c.connect()
print(c.send_command("pie_start", {"num_clients": 2}))
time.sleep(8)
print(c.send_command("pie_status", {}))
