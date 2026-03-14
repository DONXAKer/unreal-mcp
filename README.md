<div align="center">

# Model Context Protocol for Unreal Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5%2B-orange)](https://www.unrealengine.com)
[![Python](https://img.shields.io/badge/Python-3.12%2B-yellow)](https://www.python.org)
[![Status](https://img.shields.io/badge/Status-Experimental-red)](https://github.com/DONXAKer/unreal-mcp)

</div>

Позволяет AI-клиентам (Claude Desktop, Cursor, Windsurf) управлять Unreal Engine через естественный язык с использованием Model Context Protocol (MCP).

## ⚠️ Экспериментальный статус

Проект находится в **экспериментальном** состоянии. API и реализация могут изменяться без уведомления.

---

## Архитектура

```
AI-клиент (Claude / Cursor / Windsurf)
    ↓  MCP Protocol
Python MCP Server  (порт 55557)
    ↓  TCP Socket + JSON
C++ Plugin (Unreal Engine Editor)
    ↓
UE5 Editor Subsystems
```

---

## Возможности

### Стандартный сервер (`unreal_mcp_server.py`)

| Категория | Что умеет |
|-----------|-----------|
| **Акторы** | Создание, удаление, перемещение, поиск по имени, список акторов на уровне |
| **Blueprint** | Создание классов, добавление компонентов, настройка физики, компиляция |
| **Editor** | Фокус viewport, управление камерой |

### Расширенный сервер (`unreal_mcp_server_advanced.py`) — рекомендуется

Всё из стандартного, плюс:

| Категория | Что умеет |
|-----------|-----------|
| **Blueprint Graph** | 30+ типов узлов: Branch, Cast, CallFunction, VariableGet/Set, Switch, Timeline и др. |
| **VariableGet (внешний класс)** | Доступ к `UPROPERTY` внешнего C++ класса через `target_class` |
| **ComponentBoundEvent** | Привязка делегатов компонентов (Button.OnClicked и др.) |
| **UMG Widgets** | Создание виджетов, настройка свойств, управление иерархией |
| **Функции Blueprint** | Создание функций, добавление input/output параметров |
| **Редактирование узлов** | Изменение типов пинов, семантическое редактирование |

---

## Быстрый старт

### Требования

- Unreal Engine 5.5+
- Python 3.12+
- [uv](https://docs.astral.sh/uv/) — менеджер Python-окружений
- MCP-клиент: Claude Desktop, Cursor или Windsurf

### 1. Плагин (C++)

Используйте готовый пример-проект `MCPGameProject` (плагин уже подключён):

```bash
# ПКМ на MCPGameProject.uproject → Generate Visual Studio project files
# Собрать в Development Editor
```

Или добавьте плагин в существующий проект — см. [PLUGIN_SETUP.md](PLUGIN_SETUP.md).

### 2. Python-сервер

```bash
cd Python/
uv sync
uv run unreal_mcp_server_advanced.py
```

### 3. Настройка MCP-клиента

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory", "/absolute/path/to/Python",
        "run", "unreal_mcp_server_advanced.py"
      ]
    }
  }
}
```

Расположение файла конфигурации:

| Клиент | Путь |
|--------|------|
| Claude Desktop | `~/.config/claude-desktop/mcp.json` |
| Cursor | `.cursor/mcp.json` (корень проекта) |
| Windsurf | `~/.config/windsurf/mcp.json` |

---

## Структура репозитория

```
unreal-mcp/
├── MCPGameProject/              # Пример UE-проекта с плагином
│   └── Plugins/UnrealMCP/       # C++ плагин
│       └── Source/UnrealMCP/
│           ├── Public/Commands/BlueprintGraph/
│           └── Private/Commands/BlueprintGraph/
│               └── Nodes/
├── Python/
│   ├── unreal_mcp_server.py           # Стандартный сервер
│   ├── unreal_mcp_server_advanced.py  # Расширенный сервер
│   └── tools/
├── mcp.json                     # Пример конфигурации
└── PLUGIN_SETUP.md              # Установка в существующий проект
```

---

## Добавление в существующий проект

Подробное руководство: **[PLUGIN_SETUP.md](PLUGIN_SETUP.md)**

1. Скопировать `MCPGameProject/Plugins/UnrealMCP` → `YourProject/Plugins/`
2. Edit → Plugins → UnrealMCP → Enable
3. Пересобрать проект
4. Настроить `mcp.json`

---

## Лицензия

MIT
