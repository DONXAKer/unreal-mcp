# Установка плагина UnrealMCP в существующий проект

## Требования

- Unreal Engine 5.5+
- Python 3.12+
- [uv](https://docs.astral.sh/uv/getting-started/installation/)
- MCP-клиент: Claude Desktop, Cursor или Windsurf

## 1. Плагин (C++)

### Вариант A — Junction (рекомендуется для monorepo)

Если плагин и UE-проект живут рядом (e.g. `D:\WarCard\unreal-mcp\` и `D:\WarCard\client\`):

```cmd
mklink /J "D:\WarCard\client\Plugins\UnrealMCP" "D:\WarCard\unreal-mcp\MCPGameProject\Plugins\UnrealMCP"
```

Junction автоматически отражает любые изменения плагина в проекте без копирования.

> После создания junction добавьте `client/Plugins/UnrealMCP/` в `.gitignore` проекта (junction-путь — не отдельная копия, отслеживать его не нужно).

### Вариант B — Копия

```bash
cp -r MCPGameProject/Plugins/UnrealMCP /path/to/your/project/Plugins/
```

## 2. Включить в Editor

1. Edit → Plugins → поиск "UnrealMCP" → Enable
2. Перезапустить Editor

## 3. Собрать проект

```bash
# ПКМ на .uproject → Generate Visual Studio project files
# Build → Build Solution (VS) или через UBT:
UnrealBuildTool -Target="YourProjectEditor Win64 Development" -Project="YourProject.uproject"
```

После успешной сборки в Output Log появится:
```
LogUnrealMCP: MCP Server started on port 55557
```

## 4. Создать mcp-project.json

Файл кладётся в корень UE-проекта (рядом с `.uproject`).

```json
{
  "projectName": "YourProject",
  "assetRoot": "/Game",
  "naming": {
    "blueprint":        "BP_",
    "material":         "M_",
    "materialInstance": "MI_",
    "texture":          "T_",
    "staticMesh":       "SM_",
    "dataTable":        "DT_",
    "niagara":          "NS_",
    "soundWave":        "SW_"
  },
  "paths": {
    "levels":      "/Game/Maps",
    "materials":   "/Game/Art/Materials",
    "textures":    "/Game/Art/Textures",
    "meshes":      "/Game/Art/Meshes",
    "dataTables":  "/Game/Data",
    "niagara":     "/Game/VFX",
    "sounds":      "/Game/Audio"
  },
  "defaults": {
    "texture": { "sRGB": true, "compression": "BC7", "mipGen": "FromTexture" },
    "material": { "masterMaterial": null }
  },
  "recipesDir": "Content/Python/recipes"
}
```

## 5. Создать папку рецептов

```
YourProject/Content/Python/recipes/
```

Положите туда свои `*.py` файлы с `@recipe`-функциями. Пример — `Content/Python/recipes/warcard_recipes.py` в проекте WarCard.

## 6. Python-сервер

```bash
cd unreal-mcp/Python/
uv sync
uv run unreal_mcp_server_advanced.py
```

## 7. Настройка MCP-клиента

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": [
        "--directory", "/absolute/path/to/unreal-mcp/Python",
        "run", "unreal_mcp_server_advanced.py"
      ]
    }
  }
}
```

## Решение проблем

| Симптом | Причина / решение |
|---------|------------------|
| Плагин не компилируется | Проверьте UE 5.5+. Если `NiagaraEditor` вызывает ошибку — удалите из `Build.cs` PrivateDependencyModuleNames |
| `reload_recipes()` → 0 recipes | Убедитесь что `mcp-project.json` в корне проекта и `recipesDir` существует |
| Blueprint создаётся, но не видно в Content Browser | Обновитесь до ≥ 1.6.3 (fix: `create_blueprint` теперь вызывает `SaveAsset`) |
| `NIAGARA_UNAVAILABLE` | Включите плагин Niagara в Edit → Plugins, пересоберите |
| Порт 55557 занят | Измените порт в `unreal_mcp_server_advanced.py` (переменная `UE_TCP_PORT`) и перезапустите Editor |
