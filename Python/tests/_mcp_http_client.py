"""HTTP-MCP клиент-шим для тестов (Фаза 0 миграции на офиц. сервер UE 5.8).

Drop-in замена `unreal_mcp_server.UnrealConnection`: тот же интерфейс
`UnrealConnection(host, port).send_command(name, params) -> dict`, но вместо raw-TCP
:55557 ходит в официальный Epic MCP-сервер (streamable HTTP) через MCP Python SDK.

Почему SDK, а не голый urllib: офиц. сервер отдаёт результаты tools/call по
streamable-HTTP SSE-каналу (POST → 200 text/event-stream, тело приходит событием),
плюс session-handshake. SDK (`mcp.client.streamable_http`) это корректно разруливает.

Особенности официального сервера (подтверждены в Фазе 0):
  - tool-search режим: native-тулы только list_toolsets / describe_toolset / call_tool;
    реальные тулы вызываются через call_tool({toolset_name, tool_name, arguments}).
  - результат тула завёрнут как {"returnValue": <value>}; разворачиваем.
  - имена полей UPROPERTY камелкейсятся в JSON (bMyTurn / aP / maxAP). Маппинг
    под старый контракт (my_turn / ap / max_ap) — задача Фазы 1 (имена полей структур),
    шим лишь возвращает то, что отдал сервер.

Порт по умолчанию: MCP_HTTP_PORT (8137 на этой машине — 8000 занят Docker-прокси).
Старые тестовые порты 55557/55558 маппятся на 8137/8138.
"""

from __future__ import annotations

import asyncio
import json
import os
import re
import threading
from typing import Any

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

DEFAULT_HOST = os.environ.get("UNREAL_HOST", "127.0.0.1")
DEFAULT_PORT = int(os.environ.get("MCP_HTTP_PORT", "8137"))
DEFAULT_PATH = os.environ.get("MCP_HTTP_PATH", "/mcp")
_NATIVE_META = {"list_toolsets", "describe_toolset", "call_tool"}
# Старые raw-TCP порты двух редакторов -> офиц. HTTP-порты.
_PORT_MAP = {55557: 8137, 55558: 8138}


def _result_to_dict(result: Any) -> dict[str, Any]:
    """CallToolResult -> dict, как ждут тесты. Разворачивает {"returnValue": ...}."""
    if getattr(result, "isError", False):
        text = "".join(getattr(b, "text", "") for b in (result.content or []))
        return {"status": "error", "error": text or "tool error"}

    payload: Any = None
    sc = getattr(result, "structuredContent", None)
    if isinstance(sc, dict):
        payload = sc
    else:
        for block in result.content or []:
            text = getattr(block, "text", None)
            if text:
                try:
                    payload = json.loads(text)
                except json.JSONDecodeError:
                    payload = {"result": text}
                break

    if payload is None:
        return {"status": "success"}
    # Сервер заворачивает возврат UFunction в {"returnValue": <value>}.
    if isinstance(payload, dict) and set(payload.keys()) == {"returnValue"}:
        inner = payload["returnValue"]
        return inner if isinstance(inner, dict) else {"result": inner}
    return payload if isinstance(payload, dict) else {"result": payload}


class _AsyncMCP:
    """Постоянная MCP-сессия в выделенном asyncio-loop (фоновый поток)."""

    def __init__(self, url: str) -> None:
        self.url = url
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.thread.start()
        self.session: ClientSession | None = None
        self._ctx: Any = None
        self._sess_ctx: Any = None
        self.tool_to_toolset: dict[str, str] = {}

    def _run(self, coro: Any, timeout: float = 60.0) -> Any:
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout)

    async def _aopen(self) -> None:
        self._ctx = streamablehttp_client(self.url)
        read, write, _ = await self._ctx.__aenter__()
        self._sess_ctx = ClientSession(read, write)
        self.session = await self._sess_ctx.__aenter__()
        await self.session.initialize()
        await self._build_index()

    async def _build_index(self) -> None:
        assert self.session is not None
        res = await self.session.call_tool("list_toolsets", {})
        catalog = "".join(getattr(b, "text", "") for b in (res.content or []))
        # строки вида "- ToolsetName: описание"
        toolsets = re.findall(r"^-\s*([^\s:]+):", catalog, re.MULTILINE)
        for ts in toolsets:
            desc = await self.session.call_tool("describe_toolset", {"toolset_name": ts})
            txt = "".join(getattr(b, "text", "") for b in (desc.content or []))
            try:
                schema = json.loads(txt)
            except json.JSONDecodeError:
                continue
            for tool in schema.get("tools", []):
                full = tool.get("name", "")
                func = full.rsplit(".", 1)[-1] if "." in full else full
                self.tool_to_toolset[func] = ts

    async def _acall(self, name: str, params: dict[str, Any]) -> dict[str, Any]:
        assert self.session is not None
        if name in _NATIVE_META:
            res = await self.session.call_tool(name, params)
        else:
            toolset = self.tool_to_toolset.get(name)
            args = {"tool_name": name, "arguments": params}
            if toolset:
                args["toolset_name"] = toolset
            res = await self.session.call_tool("call_tool", args)
        return _result_to_dict(res)

    async def _aclose(self) -> None:
        try:
            if self._sess_ctx is not None:
                await self._sess_ctx.__aexit__(None, None, None)
            if self._ctx is not None:
                await self._ctx.__aexit__(None, None, None)
        except Exception:
            pass

    def open(self) -> None:
        self._run(self._aopen())

    def call(self, name: str, params: dict[str, Any]) -> dict[str, Any]:
        return self._run(self._acall(name, params))

    def close(self) -> None:
        try:
            self._run(self._aclose(), timeout=10)
        finally:
            self.loop.call_soon_threadsafe(self.loop.stop)


class UnrealConnection:
    """Drop-in замена raw-TCP коннекта: тот же интерфейс, транспорт — HTTP MCP."""

    def __init__(self, host: str | None = None, port: int | None = None) -> None:
        self.host = host if host is not None else DEFAULT_HOST
        raw_port = port if port is not None else DEFAULT_PORT
        self.port = _PORT_MAP.get(raw_port, raw_port)
        self.url = f"http://{self.host}:{self.port}{DEFAULT_PATH}"
        self._mcp: _AsyncMCP | None = None
        self.connected = False

    def connect(self) -> bool:
        try:
            self._mcp = _AsyncMCP(self.url)
            self._mcp.open()
            self.connected = True
            return True
        except Exception as e:  # noqa: BLE001 — диагностика для тестов
            print(f"[mcp-http] connect failed: {e}")
            self.connected = False
            return False

    def disconnect(self) -> None:
        if self._mcp is not None:
            self._mcp.close()
            self._mcp = None
        self.connected = False

    def send_command(self, command: str, params: dict[str, Any] | None = None) -> dict[str, Any] | None:
        if not self.connected and not self.connect():
            return None
        assert self._mcp is not None
        try:
            return self._mcp.call(command, params or {})
        except Exception as e:  # noqa: BLE001
            print(f"[mcp-http] {command} failed: {e}")
            return {"status": "error", "error": str(e)}


if __name__ == "__main__":
    conn = UnrealConnection()
    if not conn.connect():
        raise SystemExit("connect failed")
    print("tool->toolset:", conn._mcp.tool_to_toolset)
    print("get_actors_in_level ->", str(conn.send_command("get_actors_in_level", {}))[:200])
    print("wc_get_battle_state ->", conn.send_command("wc_get_battle_state", {"controller_index": 0}))
    conn.disconnect()
