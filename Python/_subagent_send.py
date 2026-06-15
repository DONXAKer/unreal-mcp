"""Direct TCP sender to UnrealMCP plugin (port 55557) for subagent usage."""
import json
import socket
import sys


def send(cmd_type: str, params: dict) -> dict:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(15)
    s.connect(("127.0.0.1", 55557))
    payload = {"type": cmd_type, "params": params}
    s.sendall(json.dumps(payload).encode("utf-8"))
    chunks = []
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        chunks.append(chunk)
        data = b"".join(chunks).decode("utf-8")
        try:
            obj = json.loads(data)
            s.close()
            return obj
        except json.JSONDecodeError:
            continue
    s.close()
    raise RuntimeError("no response")


if __name__ == "__main__":
    cmd = sys.argv[1]
    params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    resp = send(cmd, params)
    print(json.dumps(resp, indent=2, ensure_ascii=False))
