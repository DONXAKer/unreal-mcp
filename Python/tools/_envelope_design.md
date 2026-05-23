# Envelope Decorator — Design Document

Дизайн `wrap_with_envelope(mcp)` — обёртки вокруг `FastMCP.tool`, которая нормализует выход 117 `@mcp.tool()` функций в единый dual-key shape (unified `ok/status/assetPath/meta` + legacy `success/message` для backward compat). Жил в `Python/tools/_envelope.py` (создаётся в PR1).

Контекст: см. [`result_format.py`](result_format.py), `tools/primitives.py:_call` (уже разворачивает bridge wrap для рецептов), `tools/recipe_framework.py` (рецепты уже возвращают unified). Этот декоратор закрывает дыру: прямые tools-вызовы из MCP-клиента сейчас отдают legacy либо bridge-wrap наружу.

## Цель

После применения `mcp = wrap_with_envelope(mcp)` в `register_X_tools(mcp)`:
- **Любой** возврат из любого `@mcp.tool()` функции проходит через `_normalize_outbound(...)`.
- На выходе клиент видит **обогащённый dict** с обоими наборами ключей.
- Внутренности tool-функций **не нужно** менять — миграция идёт без churn'а.

## Целевой выходной shape

### Success
```json
{
  "ok": true,
  "status": "created",
  "assetPath": "/Game/Cards/T_CardArt_1",
  "meta": { "...whatever payload was..." },

  "success": true,
  "message": "created"
}
```

### Failure
```json
{
  "ok": false,
  "error": {
    "category": "ue_internal",
    "code": "BRIDGE_ERROR",
    "message": "Connection refused",
    "details": { "raw": "..." }
  },

  "success": false,
  "message": "Connection refused"
}
```

**Инвариант:** в любом ответе одновременно есть и `ok`, и `success`. Они всегда равны (`ok == success`). Если `ok=true`, то `error` отсутствует. Если `ok=false`, то `status`/`assetPath` отсутствуют, `meta` опционален.

## 7 типов входа

Decorator получает то, что вернула tool-функция. Семь возможных shape'ов:

### Тип 1 — Bridge wrap success
```python
{"status": "success", "result": {"assetPath": "/Game/X", "size": 256}}
```
Это сырой ответ от C++ bridge'а через `conn.send_command(...)`. Большинство tools отдают **именно его** без распаковки.

**Mapping:**
- `ok = true`
- `status = result.get("status") or "created"` (если C++ положил статус внутрь result — используем; иначе дефолт)
- `assetPath = result.get("assetPath") or result.get("path") or ""`
- `meta = {k: v for k, v in result.items() if k not in ("status", "assetPath", "path")}`
- `success = true`
- `message = status` (короткое string-описание)

### Тип 2 — Bridge wrap error
```python
{"status": "error", "error": "Component class not found: Foo"}
# или
{"status": "error", "message": "..."}
```

**Mapping:**
- `ok = false`
- `error.category = "ue_internal"`
- `error.code = "BRIDGE_ERROR"`
- `error.message = raw["error"] or raw["message"] or "Unknown UE error"`
- `error.details = {"raw": <original dict>}`
- `success = false`
- `message = error.message`

### Тип 3 — Legacy success=True с payload
```python
{"success": True, "name": "BP_Card_1", "assetPath": "/Game/X"}
# или просто
{"success": True, "message": "Created"}
```

**Mapping:**
- `ok = true`
- `status = raw.get("status") or "created"`
- `assetPath = raw.get("assetPath") or raw.get("path") or ""`
- `meta = {k: v for k, v in raw.items() if k not in ("success", "message", "status", "assetPath", "path")}`
- `success = true`
- `message = raw.get("message") or status`

### Тип 4 — Legacy success=False
```python
{"success": False, "message": "Failed to connect to Unreal Engine"}
```
Это типичный return из tool после `unreal = get_unreal_connection(); if not unreal: return {success: False, message: ...}`.

**Mapping:**
- `ok = false`
- `error.category = "ue_internal"` (default; см. heuristic ниже)
- `error.code = "TOOL_FAILURE"`
- `error.message = raw["message"] or "Unknown failure"`
- `error.details = {}`
- `success = false`
- `message = error.message`

**Heuristic для category:** если `message` содержит `"connect"|"bridge"|"Unreal"` → `ue_internal`. Если `"missing required"|"invalid"|"must"` → `user`. Иначе → `ue_internal`. (Грубо, но лучше чем всё `ue_internal`.)

### Тип 5 — Уже-unified ok
```python
{"ok": True, "status": "created", "assetPath": "/Game/X", "meta": {...}}
```
Это то что возвращают рецепты и `result_format.ok(...)`.

**Mapping:** добавляем только legacy-зеркала, нативную часть не трогаем:
- `success = true`
- `message = raw.get("status") or "ok"`
- Остальные ключи (`ok`, `status`, `assetPath`, `meta`) — as-is.

**НЕ переписывать** `meta`/`assetPath`/`status` — они уже правильные.

### Тип 6 — Уже-unified fail
```python
{"ok": False, "error": {"category": "user", "code": "MISSING_ARG", "message": "...", "details": {...}}}
```
Из `result_format.fail(...)`.

**Mapping:**
- `success = false`
- `message = raw["error"]["message"]`
- Остальное as-is.

### Тип 7 — None / exception / странное
```python
None
# или
{}
# или tool бросил исключение и FastMCP не поймал
```

**Mapping (для None и пустого dict):**
- `ok = false`
- `error.category = "ue_internal"`
- `error.code = "NO_RESPONSE"`
- `error.message = "Tool returned None or empty"`
- `success = false`
- `message = "Tool returned None or empty"`

**Для exception:** decorator оборачивает tool-вызов в `try/except` и формирует:
- `ok = false`
- `error.category = "ue_internal"`
- `error.code = "TOOL_EXCEPTION"`
- `error.message = f"{type(e).__name__}: {e}"`
- `error.details = {"traceback": traceback.format_exc()}` (только если log-level=DEBUG; иначе скрываем — может содержать пути)
- `success = false`

## Алгоритм детекции

Порядок проверок в `_normalize_outbound(raw)`:

```
1. if raw is None or raw == {}:               → Тип 7 (NO_RESPONSE)
2. if "ok" in raw and raw["ok"] is True:      → Тип 5 (unified ok)
3. if "ok" in raw and raw["ok"] is False:     → Тип 6 (unified fail)
4. if raw.get("status") == "success":         → Тип 1 (bridge wrap success)
5. if raw.get("status") == "error":           → Тип 2 (bridge wrap error)
6. if raw.get("success") is True:             → Тип 3 (legacy success)
7. if raw.get("success") is False:            → Тип 4 (legacy fail)
8. fallback:                                  → Тип 7 (UNKNOWN_SHAPE)
```

Порядок важен:
- Unified проверяется **до** legacy/bridge, чтобы рецепт-ответ не получил двойной wrapping.
- Bridge `{status: success, result: {...}}` проверяется **до** legacy, потому что bridge никогда не возвращает ключ `success`.
- Fallback в конце — на случай странного dict (тоже как ошибка, потому что dict без распознаваемых ключей бесполезен для клиента).

## Edge cases

| Сценарий | Поведение |
|---|---|
| `raw` — не dict (например, list или str) | Тип 7, `error.code = "INVALID_RESPONSE_TYPE"` |
| `raw["result"]` — не dict (внутри bridge wrap) | Тип 1, `meta = {"result": raw["result"]}` (положить как-есть) |
| `raw` содержит и `ok` и `success` (рецепт уже прошедший normalize) | Тип 5/6, как unified |
| `raw["message"]` — None или пустой | `message = status` (Тип 1/3) или `"Unknown error"` (Тип 2/4) |
| `assetPath = ""` (нет ключа) | оставляем `""`, **не** None |
| Tool вернул `{"ok": True}` без остальных полей | Тип 5, `status="ok"`, `assetPath=""`, `meta={}` |

## Тесты (Python/tests/unit/test_envelope.py)

Минимальный набор для PR1:

```python
@pytest.mark.parametrize("raw, expected_ok, expected_status, expected_asset", [
    # Тип 1: bridge wrap success
    ({"status": "success", "result": {"assetPath": "/Game/X", "size": 256}},
     True, "created", "/Game/X"),

    # Тип 2: bridge wrap error
    ({"status": "error", "error": "boom"},
     False, None, None),

    # Тип 3: legacy success
    ({"success": True, "name": "BP_X", "assetPath": "/Game/X"},
     True, "created", "/Game/X"),

    # Тип 4: legacy fail
    ({"success": False, "message": "no connect"},
     False, None, None),

    # Тип 5: unified ok (рецепт)
    ({"ok": True, "status": "skipped", "assetPath": "/Game/Y", "meta": {"reason": "exists"}},
     True, "skipped", "/Game/Y"),

    # Тип 6: unified fail (рецепт)
    ({"ok": False, "error": {"category": "user", "code": "X", "message": "bad", "details": {}}},
     False, None, None),

    # Тип 7: None
    (None, False, None, None),

    # Тип 7: empty
    ({}, False, None, None),

    # Edge: invalid type
    ("just a string", False, None, None),
])
def test_normalize_outbound(raw, expected_ok, expected_status, expected_asset):
    result = _normalize_outbound(raw)
    assert result["ok"] is expected_ok
    assert result["success"] == result["ok"]  # инвариант dual-key
    if expected_ok:
        assert result["status"] == expected_status
        assert result["assetPath"] == expected_asset
    else:
        assert "error" in result
        assert result["message"] == result["error"]["message"]
```

Плюс отдельные тесты:
- `test_unified_ok_preserved_meta` — `meta` рецепта не перезаписывается
- `test_bridge_wrap_inner_status_wins` — если в `result` есть `status: "skipped"`, он перебивает дефолт `"created"`
- `test_exception_caught_by_decorator` — функция бросает RuntimeError, наружу выходит Тип 7

## Где применяется

Точка вставки — каждый `register_X_tools(mcp)` в `tools/*_tools.py`:

```python
from tools._envelope import wrap_with_envelope

def register_blueprint_tools(mcp: FastMCP):
    mcp = wrap_with_envelope(mcp)   # ← новая строка

    @mcp.tool()
    def create_blueprint(...):
        ...
```

`wrap_with_envelope` возвращает обёртку, где `mcp.tool()` — это `mcp._original_tool()` с пост-обработкой возврата через `_normalize_outbound`. Все остальные методы FastMCP проксируются как есть.

Не используем monkey-patch `FastMCP.tool` напрямую — хрупко при апдейтах библиотеки.

## Что НЕ покрывает этот декоратор

- **Логирование ошибок** — это делает сама tool-функция или FastMCP middleware. Декоратор только формирует shape.
- **Retry / circuit breaker** — отдельная логика, не envelope.
- **Валидация входных параметров** — это делает pydantic в FastMCP до вызова tool.
- **Локализация error messages** — категория и code машинно-читаемы; message — английский (как было).
