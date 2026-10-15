"""Геймплей/PIE через официальный MCP; UMG и контент через UnrealConnection."""
from __future__ import annotations

import json
import shutil
import subprocess
import threading
import urllib.request
from typing import Any

from unreal_mcp_server import UnrealConnection


class OfficialBridge:
    """Совместимый транспорт существующего smoke без изменения его сценария."""

    def __init__(self, url: str = "http://127.0.0.1:8137/mcp"):
        self.content = UnrealConnection()
        self.url = url
        self.session: str | None = None
        self._request_id = 0
        self._lock = threading.Lock()

    def connect(self) -> bool:
        if not self.content.connect():
            return False
        initialize = {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "warcard-solo-smoke", "version": "1"}}}
        request = urllib.request.Request(self.url, json.dumps(initialize).encode(),
                                         {"Content-Type": "application/json",
                                          "Accept": "application/json, text/event-stream"})
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                self.session = response.headers.get("Mcp-Session-Id")
            if not self.session:
                raise RuntimeError("Официальный MCP не вернул идентификатор сессии")
        except Exception:
            self.content.disconnect()
            raise
        return True

    def disconnect(self) -> None:
        self.session = None
        self.content.disconnect()

    @staticmethod
    def _envelope(raw: str, request_id: int) -> dict[str, Any]:
        raw = raw.strip()
        if raw.startswith("{"):
            return json.loads(raw)
        # SSE может содержать служебные события перед ответом JSON-RPC.
        for event in raw.replace("\r\n", "\n").split("\n\n"):
            data = "\n".join(line[5:].lstrip() for line in event.splitlines() if line.startswith("data:"))
            if not data:
                continue
            value = json.loads(data)
            if value.get("id") == request_id:
                return value
        raise RuntimeError("Официальный MCP не вернул ответ JSON-RPC")

    def send_command(self, command: str, params: dict[str, Any]) -> dict[str, Any]:
        if not command.startswith(("wc_", "pie_")):
            return self.content.send_command(command, params)
        if not self.session:
            raise RuntimeError("Сначала вызовите OfficialBridge.connect()")
        arguments = dict(params)
        if command == "pie_start":
            # Один клиент в отдельном окне — исходное назначение solo smoke.
            arguments = {"level_name": arguments.get("level_name", ""),
                         "mode": arguments.get("mode", "new_window"),
                         "window_width": arguments.get("window_width", 1280),
                         "window_height": arguments.get("window_height", 720),
                         "num_clients": arguments.get("num_clients", 1),
                         "dedicated_server": arguments.get("dedicated_server", False)}
        with self._lock:
            self._request_id += 1
            request_id = self._request_id
            toolset = "WarCardMCP.WarCardToolset" if command.startswith("wc_") else "WarCardMCP.WarCardPIEToolset"
            body = {"jsonrpc": "2.0", "id": request_id, "method": "tools/call", "params": {
                "name": "call_tool", "arguments": {"toolset_name": toolset,
                "tool_name": command, "arguments": arguments}}}
            curl = shutil.which("curl.exe") or shutil.which("curl")
            if not curl:
                raise RuntimeError("Для официального MCP нужен curl")
            response = subprocess.run([curl, "--silent", "--show-error", "--max-time", "25",
                "-X", "POST", self.url, "-H", "Content-Type: application/json",
                "-H", "Accept: application/json, text/event-stream",
                "-H", "Mcp-Session-Id: " + self.session, "-d", json.dumps(body)],
                capture_output=True, text=True, encoding="utf-8", timeout=28)
            if response.returncode:
                raise RuntimeError(f"Ошибка транспорта официального MCP: {command}: {response.stderr.strip()}")
            envelope = self._envelope(response.stdout, request_id)
        result = envelope.get("result", {})
        if envelope.get("error") or result.get("isError"):
            raise RuntimeError(f"Официальный MCP отклонил команду: {command}: {envelope}")
        value = result.get("structuredContent")
        if value is None:
            blocks = [b["text"] for b in result.get("content", []) if b.get("type") == "text"]
            if not blocks:
                raise RuntimeError(f"Пустой ответ официального MCP: {command}")
            value = json.loads(blocks[0])
        if isinstance(value, dict):
            value = value.get("returnValue", value)
        if isinstance(value, str):
            value = json.loads(value)
        if not isinstance(value, dict):
            raise RuntimeError(f"Неожиданный тип ответа официального MCP: {command}")
        for modern, legacy in (("bOk", "ok"), ("value", "return"), ("isRunning", "is_running"),
                               ("unitsJson", "units_json"), ("bMyTurn", "my_turn")):
            if modern in value:
                value[legacy] = value[modern]
        if command == "wc_deploy_unit" and "deployed" not in value:
            value["deployed"] = bool(value.get("ok")) and bool(value.get("return")) and not value.get("error")
        return {"status": "success", "result": value}
