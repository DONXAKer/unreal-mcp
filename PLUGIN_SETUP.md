# Установка плагина UnrealMCP в существующий проект

Это руководство описывает как добавить плагин и Python-сервер в уже существующий Unreal Engine 5.5+ проект.

## Содержание

- [Требования](#требования)
- [Установка плагина (C++)](#установка-плагина-c)
- [Настройка Python-сервера](#настройка-python-сервера)
- [Настройка MCP-клиента](#настройка-mcp-клиента)
- [Конфигурация mcp.json](#конфигурация-mcpjson)
- [Проверка работы](#проверка-работы)

---

## Требования

- **Unreal Engine 5.5+**
- **Python 3.12+**
- **uv** — менеджер Python-окружений ([установка](https://docs.astral.sh/uv/getting-started/installation/))
- **MCP-клиент** — Claude Desktop, Cursor или Windsurf

---

## Установка плагина (C++)

### 1. Скопировать плагин в проект

```bash
cp -r MCPGameProject/Plugins/UnrealMCP /path/to/your/project/Plugins/
```

Структура после копирования:
```
YourProject/
└── Plugins/
    └── UnrealMCP/
        ├── UnrealMCP.uplugin
        └── Source/UnrealMCP/
            ├── UnrealMCP.Build.cs
            ├── Public/
            └── Private/
```

### 2. Зависимости в Build.cs вашего модуля

Если ваш игровой модуль использует типы из плагина, добавьте в `YourGame.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    // ... ваши зависимости
    "UnrealMCP"   // только если напрямую используете типы плагина
});
```

> **Примечание:** Обычно зависимость от `UnrealMCP` не нужна — плагин запускается автоматически как Editor-плагин и не требует явного подключения из игрового кода.

### 3. Включить плагин в Editor

1. Открыть **Edit → Plugins** в Unreal Editor
2. Найти **"UnrealMCP"** в категории **Editor**
3. Поставить галочку **Enabled**
4. Перезапустить Editor

### 4. Собрать проект

```bash
# Сгенерировать файлы проекта
# ПКМ на .uproject → Generate Visual Studio project files

# Собрать в режиме Development Editor
# Build → Build Solution (Visual Studio)
# или через Unreal Build Tool:
UnrealBuildTool -Target="YourProjectEditor Win64 Development" -Project="YourProject.uproject"
```

После успешной сборки в Output Log должно появиться:
```
LogUnrealMCP: MCP Server started on port 55557
```

---

## Настройка Python-сервера

### 1. Перейти в директорию Python

```bash
cd Python/
```

### 2. Установить зависимости через uv

```bash
uv sync
```

Это создаст `.venv` и установит все зависимости из `pyproject.toml` (включая `fastmcp`).

### 3. Проверить запуск сервера

```bash
# Стандартный сервер
uv run unreal_mcp_server.py

# Расширенный сервер (50+ инструментов: Blueprint Graph, UMG, ComponentBoundEvent и др.)
uv run unreal_mcp_server_advanced.py
```

> **Важно:** Unreal Editor должен быть открыт и запущен до старта сервера.

### Два варианта сервера

| Сервер | Файл | Инструменты |
|--------|------|-------------|
| Стандартный | `unreal_mcp_server.py` | Actor, Blueprint, Editor базовые операции |
| Расширенный | `unreal_mcp_server_advanced.py` | Всё из стандартного + Blueprint Graph (30+ типов узлов), UMG, функции, ComponentBoundEvent |

---

## Настройка MCP-клиента

### Claude Desktop

Файл конфигурации: `~/.config/claude-desktop/mcp.json`
Windows: `%USERPROFILE%\.config\claude-desktop\mcp.json`

### Cursor

Файл: `.cursor/mcp.json` в корне проекта

### Windsurf

Файл: `~/.config/windsurf/mcp.json`

---

## Конфигурация mcp.json

### Стандартный сервер

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory",
        "/absolute/path/to/repository/Python",
        "run",
        "unreal_mcp_server.py"
      ]
    }
  }
}
```

### Расширенный сервер (рекомендуется)

```json
{
  "mcpServers": {
    "unrealMCP-advanced": {
      "command": "uv",
      "args": [
        "--directory",
        "/absolute/path/to/repository/Python",
        "run",
        "unreal_mcp_server_advanced.py"
      ]
    }
  }
}
```

### Оба сервера одновременно

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": ["--directory", "/absolute/path/to/repository/Python", "run", "unreal_mcp_server.py"]
    },
    "unrealMCP-advanced": {
      "command": "uv",
      "args": ["--directory", "/absolute/path/to/repository/Python", "run", "unreal_mcp_server_advanced.py"]
    }
  }
}
```

> Замените `/absolute/path/to/repository/Python` на реальный путь к папке `Python/` этого репозитория.

---

## Проверка работы

1. Открыть Unreal Editor — в Output Log должно быть: `LogUnrealMCP: MCP Server started on port 55557`
2. Запустить Python-сервер в отдельном терминале: `uv run unreal_mcp_server_advanced.py`
3. В MCP-клиенте (Claude Desktop и др.) попросить:
   > *"Получи список акторов на уровне"* — `get_actors_in_level`

---

## Решение проблем

**Плагин не компилируется**
- Убедитесь что включены зависимости `BlueprintGraph` и `KismetCompiler` в `UnrealMCP.Build.cs` (уже включены по умолчанию)
- Проверьте версию UE — требуется 5.5+

**Сервер не подключается к Unreal**
- Порт 55557 должен быть свободен
- Editor должен быть открыт до запуска сервера
- Проверьте `unreal_mcp.log` в папке `Python/`

**Ошибка FastMCP при передаче boolean значений**
- Расширенный сервер содержит встроенный патч, исправляющий эту ошибку
- Если используете стандартный сервер — обновите FastMCP: `uv add fastmcp --upgrade`
