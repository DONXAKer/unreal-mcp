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

## Naming convention (adopted v1.17.0)

**New commands MUST use `subject_first` style**: `<domain>_<verb>` where
domain is the noun the operation acts on (`blueprint`, `variable`, `function`,
`component`, `node`, `pin`, `anim`, `widget`, etc.) and verb is the action
(`create`, `add`, `delete`, `rename`, `list`, `compile`, `set_<x>`, `get_<x>`).

Rationale: grouping by subject keeps related operations alphabetically
adjacent (`variable_create` / `variable_delete` / `variable_list` cluster
together rather than scattering across `c-`, `d-`, `l-` prefixes), which
makes the surface easier to scan in `EpicUnrealMCPBridge.cpp` allow-list and
in the Python `@mcp.tool()` autocomplete.

**Legacy verb_first names remain supported** — existing callers
(`tasks/active/*.md`, recipes, smoke tests, downstream agent prompts) keep
working unchanged. There is no deprecation timeline; both styles coexist.

The `unreal-mcp-plugin-dev` agent enforces this rule when implementing new
commands (see `.claude/agents/unreal-mcp-plugin-dev.md`).

## Plugin structure (v1.17.0+)

### Command dispatchers (`Private/Commands/`)

- `EpicUnrealMCPEditorCommands` — Actor / Level operations (spawn, find, transform, properties, viewport focus)
- `EpicUnrealMCPBlueprintCommands` — Blueprint class, components, interfaces, materials, compile diagnostics
- `EpicUnrealMCPBlueprintGraphCommands` — Variables, functions, nodes, events, pins (delegates to managers below)
- `UMGCommands` — UMG widget Blueprints (panel/text/button, hierarchy, viewport)
- `AnimationBPCommands` (1.16.0+) — Animation Blueprints (skeleton, state machine, transitions, play anim, blend space)
- `InputCommands` (1.10.0+) — Input action / axis mappings
- `AssetCommands`, `TextureCommands`, `MaterialCommands`, `MeshCommands`, `LevelCommands`, `DataAssetCommands`, `NiagaraCommands` — content primitives

### Python `@mcp.tool()` modules (`Python/tools/`)

Full surface as of v1.17.0 — every outer-bridge command has a typed FastMCP wrapper:

- `blueprint_tools.py`, `node_tools.py`, `editor_tools.py`, `umg_tools.py`, `project_tools.py` — core (pre-1.16.0)
- `animation_tools.py` (1.16.0+) — Animation Blueprint operations
- `level_tools.py`, `material_tools.py`, `asset_tools.py`, `texture_tools.py`, `mesh_tools.py`, `data_asset_tools.py`, `niagara_tools.py` (1.17.0+) — content primitives

### Blueprint Graph managers (`Private/Commands/BlueprintGraph/`)

- `NodeManager` — central `add_blueprint_node` dispatcher (NodeType → creator)
- `BPConnector` — `connect_nodes` / `disconnect_pin`
- `NodeDeleter`, `NodePropertyManager` — `delete_node`, `set_node_property`, `find_blueprint_nodes`
- `BPVariables` — variable CRUD (1.11.0+)
- `EventManager` — `add_event_node`, `add_component_bound_event`, `create_custom_event`, `add_custom_event_input`
- `PinManager` (1.15.0+) — pin operations (`split_struct_pin`, `recombine_struct_pin`, `get_pin_info`, `set_pin_default_value`)
- `Function/FunctionManager`, `Function/FunctionIO` (1.12.0+) — function CRUD + input/output pins + local variables

### Node creators (`Private/Commands/BlueprintGraph/Nodes/`)

- `FControlFlowNodeCreator` — Branch, Switch*, Sequence, ForEachLoop, Comparison
- `FDataNodeCreator` — VariableGet/Set, MakeArray, MakeMap, MakeSet
- `FUtilityNodeCreator` — Print, CallFunction, Select, SpawnActor
- `FSpecializedNodeCreator` — GetDataTableRow, AddComponentByClass, Self, ConstructObject, Knot, BreakStruct, MakeStruct, CreateWidget, GetWorldSubsystem
- `FCastingNodeCreator` — DynamicCast, ClassDynamicCast, CastByteToEnum
- `FAnimationNodeCreator` — Timeline
- `FDelegateNodeCreator` (1.13.0+) — Add/Remove/Call/Clear Delegate
- `FFlowControlExtraNodeCreator` (1.14.0+) — Delay, MultiGate, Gate (macro), DoOnce (macro), FlipFlop (macro)

### Wire protocol

`EpicUnrealMCPBridge.cpp` listens on TCP `127.0.0.1:55557`. Each request is a
single JSON object:

```json
{ "type": "<command_name>", "params": { ... } }
```

Sent without trailing newline; the bridge replies with one JSON object and
closes the socket. The Python client (`Python/unreal_mcp_server.py::UnrealConnection`)
always reconnects per command.

## Решение проблем

| Симптом | Причина / решение |
|---------|------------------|
| Плагин не компилируется | Проверьте UE 5.5+. Если `NiagaraEditor` вызывает ошибку — удалите из `Build.cs` PrivateDependencyModuleNames |
| `reload_recipes()` → 0 recipes | Убедитесь что `mcp-project.json` в корне проекта и `recipesDir` существует |
| Blueprint создаётся, но не видно в Content Browser | Обновитесь до ≥ 1.6.3 (fix: `create_blueprint` теперь вызывает `SaveAsset`) |
| `NIAGARA_UNAVAILABLE` | Включите плагин Niagara в Edit → Plugins, пересоберите |
| Порт 55557 занят | Измените порт в `unreal_mcp_server_advanced.py` (переменная `UE_TCP_PORT`) и перезапустите Editor |
