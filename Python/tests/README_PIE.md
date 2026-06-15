# PIE Smoke Tests — Playwright-like e2e для WarCard

Эта папка содержит Python-тесты, которые гоняют **игру в Unreal Editor** через PIE (Play-In-Editor) — стартуют сессию, ищут UMG-виджеты, кликают по ним, делают скриншоты. Используют MCP-команды, добавленные в UnrealMCP плагин v2.4.0+.

## Файлы PIE-инфраструктуры

| Файл | Назначение |
|------|-----------|
| `_pie_common.py` | Хелперы: `pie_send`, `wait_for_pie_ready`, `wait_for_widget_or_fail`, `stop_pie_safe`. Используют существующий `_smoke_common.send_command` (TCP к плагину на 127.0.0.1:55557). |
| `smoke_pie.py` | Standalone smoke на инфраструктуру: `pie_start` → wait ready → `get_widget_tree` → `pie_screenshot` → `pie_stop`. Не привязан к конкретным виджетам проекта. |
| `test_pie_smoke.py` | Runner-wrapper над smoke_pie для дискавери `__runner.py` (имя `test_*.py`). |

## Запуск

**Прямо из Python (standalone, без MCP-клиента):**
```bash
cd D:\WarCard\unreal-mcp\Python
python -m tests.smoke_pie
```
Требования:
1. Unreal Editor открыт с проектом `client/Client.uproject`.
2. UnrealMCP плагин активен (порт 55557).

**Через MCP-runner (когда Claude вызывает `wc_run_tests`):**
Тесты с префиксом `test_` обнаруживаются автоматически через `__runner.discover_test_modules()`.

## Где должны лежать тесты — здесь или в `client/Content/Python/tests/`?

Изначальный план предлагал `client/Content/Python/tests/`. На практике решили использовать **эту папку** (`unreal-mcp/Python/tests/`), потому что:
- Здесь уже есть готовый runner (`__runner.py`) и shared helpers (`_smoke_common.py`).
- Тесты общаются с плагином через TCP — им всё равно где физически лежать.
- В `client/Content/Python/` живёт MCP recipe framework для генерации контента — смешивать с тестами игры было бы путаницей.

Если в будущем потребуется запускать тесты из самого UE Python (через `unreal.send_command`), то имеет смысл завести отдельную папку в `client/Content/Python/tests/` — но это другая задача.

## Как добавить новый сценарный тест

Пример: проверить что после клика по `LoginButton` появляется главное меню.

```python
# tests/smoke_pie_login.py
from __future__ import annotations
import sys, time
from tests._pie_common import (
    pie_send, stop_pie_safe, unwrap_result,
    wait_for_pie_ready, wait_for_widget_or_fail,
)
from tests._smoke_common import run_steps, parse_no_cleanup


def main(argv):
    state = {}

    def s_start():
        unwrap_result(pie_send("pie_start", {}), 1, "pie_start")

    def s_ready():
        wait_for_pie_ready(timeout_s=15.0)

    def s_wait_login():
        # WBP_Login — корневой widget; в runtime инстанс называется WBP_Login_C_0.
        wait_for_widget_or_fail("WBP_Login_C_0", timeout_s=5.0)

    def s_click_login():
        # Cached geometry обновляется через 1-2 тика после появления — даём паузу.
        time.sleep(0.15)
        unwrap_result(
            pie_send("click_widget_by_name", {"widget_name": "LoginButton"}),
            4, "click_widget_by_name(LoginButton)",
        )

    def s_wait_menu():
        wait_for_widget_or_fail("WBP_MainMenu_C_0", timeout_s=10.0)

    def s_screenshot():
        pie_send("pie_screenshot", {"filename": "login_to_menu.png"})

    try:
        return run_steps("smoke_pie_login", [
            ("pie_start",         s_start),
            ("wait_pie_ready",    s_ready),
            ("wait WBP_Login",    s_wait_login),
            ("click LoginButton", s_click_login),
            ("wait WBP_MainMenu", s_wait_menu),
            ("screenshot",        s_screenshot),
        ])
    finally:
        stop_pie_safe()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

Чтобы он обнаруживался runner'ом — добавь `test_smoke_pie_login.py` рядом, как `test_pie_smoke.py`.

## Известные нюансы

1. **Имена виджетов в runtime** — UE добавляет суффикс `_C_<N>` к Blueprint-инстансам. Сначала прогони `get_widget_tree` чтобы узнать точные имена в твоей сцене:
   ```bash
   python -c "from tests._pie_common import pie_send; import json; print(json.dumps(pie_send('get_widget_tree', {}), indent=2)[:2000])"
   ```
2. **Cached geometry** — после `wait_for_widget_or_fail` дай 100-200ms (`time.sleep(0.15)`) перед `click_widget_by_name`, иначе геометрия может быть пустой и клик пройдёт мимо.
3. **`pie_screenshot` source** — если в ответе `source=fallback_editor_viewport`, значит PIE GameViewport не успел инициализироваться. Поставь больший `wait_for_pie_ready(timeout_s=...)` или явный `time.sleep` перед скриншотом.
4. **PIE не закрывается между прогонами** — `smoke_pie.py` использует `try/finally → stop_pie_safe()`. Если тест свалился по Ctrl+C, прогони `python -c "from tests._pie_common import stop_pie_safe; stop_pie_safe()"` или просто `pie_stop` в Editor.
5. **Сервер для e2e-сценариев** — Login → MainMenu → Matchmaking требует запущенный WarCard server (`server/`). Smoke на инфраструктуру (этот `smoke_pie.py`) сервер не требует.
