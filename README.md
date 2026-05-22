# UnrealMCP — Model Context Protocol for Unreal Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5%2B-orange)](https://www.unrealengine.com)
[![Python](https://img.shields.io/badge/Python-3.12%2B-yellow)](https://www.python.org)
[![Status](https://img.shields.io/badge/Status-Experimental-red)](https://github.com/DONXAKer/unreal-mcp)

Lets AI clients (Claude, Cursor, Windsurf) create and modify Unreal Engine assets through natural language using the Model Context Protocol.

## Overview

UnrealMCP bridges an MCP client to the Unreal Editor via a thin Python server and a C++ plugin. It provides two layers:

- **Primitives** — low-level C++ commands (create texture, import mesh, spawn actor…)
- **Recipes** — project-specific workflows composed from primitives (`wc.create_card`, `wc.create_match_arena`)

Recipes live in your project's `Content/Python/recipes/*.py` and are discovered automatically at server startup. Adding a recipe never requires touching the plugin or the Python server.

## Tier Coverage

| Tier | Asset types | Status |
|------|-------------|--------|
| 1 | Texture (import/placeholder), Material Instance, Blueprint (create/template), Static Mesh import, DataTable (import/set/get), SoundWave import | ✅ Done |
| 2 | Level authoring (create, load, save, spawn actors), Blueprint folder-path save | ✅ Done |
| 3 | Niagara copy + parameterize | ✅ Done (copy/params only) |
| 4 | Niagara full authoring (emitters, modules from scratch), World Partition, GameMode setup | Out of scope v1 |
| 5 | Automated CI pipeline, packaged-game asset updates | Out of scope v1 |

## Architecture

```
MCP Client (Claude / Cursor / Windsurf)
    │  MCP Protocol (stdio or HTTP)
    ▼
Python MCP Server  (stdio: unreal_mcp_server.py · HTTP/SSE: unreal_mcp_server_http.py)
    │  tools/*_tools.py     →  117 @mcp.tool() commands in 13 domain modules
    │  @recipe framework    →  discovers Content/Python/recipes/*.py
    │  tools/primitives.py  →  thin wrappers used by recipes
    │  TCP Socket + JSON  (port 55557)
    ▼
C++ Plugin  (EpicUnrealMCPBridge.cpp)
    │  per-category dispatch
    ├── TextureCommands      import_texture, generate_placeholder_texture
    ├── MaterialCommands     create_material_instance, set_material_instance_params
    ├── EpicUnrealMCPBlueprintCommands  create_blueprint, create_blueprint_from_template, …
    ├── LevelCommands        create_level, load_level, save_level, spawn_actor_in_level, …
    ├── MeshCommands         import_static_mesh
    ├── DataAssetCommands    import_datatable_from_csv, set_datatable_row, import_sound_wave
    ├── NiagaraCommands      copy_niagara_system, set_niagara_parameters
    └── AssetCommands        asset_exists, delete_asset
    ▼
UE5 Editor Subsystems
```

## Quick Start

**Requirements:** Unreal Engine 5.5+. For the local stdio server — Python 3.12+ and [uv](https://docs.astral.sh/uv/). For the containerised HTTP server — only Docker.

### 1. Plugin

Use the bundled example project (plugin already connected):

```bash
# Right-click MCPGameProject.uproject → Generate Visual Studio project files
# Build in Development Editor
```

Or add the plugin to an existing project — see [PLUGIN_SETUP.md](PLUGIN_SETUP.md).

### 2. Project config

Create `mcp-project.json` in your UE project root (next to the `.uproject` file). See **Config schema** below.

### 3. MCP server

Two ways to run it.

**A. Local — stdio, for desktop MCP clients:**

```bash
cd Python/
uv sync
uv run unreal_mcp_server.py        # registers all 13 tool modules (117 tools)
```

**B. Docker — HTTP/SSE, for networked / pipeline clients:**

```bash
docker build -t unreal-mcp .
docker run --rm -p 3001:3001 \
  -e UNREAL_HOST=host.docker.internal \
  -e UNREAL_PORT=55557 \
  -e MCP_HTTP_PORT=3001 \
  unreal-mcp
```

The container runs `unreal_mcp_server_http.py` and serves the MCP Streamable-HTTP
endpoint (legacy SSE kept as a fallback) on `http://localhost:3001`. It reaches the
Unreal Editor's in-process C++ bridge over TCP at `UNREAL_HOST:UNREAL_PORT`.

| Env var | Default | Purpose |
|---|---|---|
| `UNREAL_HOST` | `host.docker.internal` | Host running the Unreal Editor |
| `UNREAL_PORT` | `55557` | TCP port of the in-Editor C++ bridge |
| `MCP_HTTP_PORT` | `3001` | Port the MCP server listens on |

### 4. MCP client

For a **stdio** client, point it at the server script:

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": ["--directory", "/absolute/path/to/Python", "run", "unreal_mcp_server.py"]
    }
  }
}
```

For an **HTTP** client (e.g. an AI-Workflow pipeline agent), point it at the
running container's URL — `http://localhost:3001`.

Config file locations:

| Client | Path |
|--------|------|
| Claude Desktop | `~/.config/claude-desktop/mcp.json` |
| Cursor | `.cursor/mcp.json` (project root) |
| Windsurf | `~/.config/windsurf/mcp.json` |

## Config Schema (`mcp-project.json`)

The file lives in your UE project root. All fields are optional except `projectName`.

```json
{
  "projectName": "WarCard",
  "assetRoot": "/Game",
  "naming": {
    "blueprint":       "BP_",
    "material":        "M_",
    "materialInstance": "MI_",
    "texture":         "T_",
    "staticMesh":      "SM_",
    "dataTable":       "DT_",
    "niagara":         "NS_",
    "soundWave":       "SW_"
  },
  "paths": {
    "cards":       "/Game/Card",
    "levels":      "/Game/Maps",
    "materials":   "/Game/Art/Materials",
    "textures":    "/Game/Art/Textures",
    "meshes":      "/Game/Art/Meshes",
    "dataTables":  "/Game/Data",
    "niagara":     "/Game/VFX",
    "sounds":      "/Game/Audio"
  },
  "defaults": {
    "texture": {
      "sRGB":        true,
      "compression": "BC7",
      "mipGen":      "FromTexture"
    },
    "material": {
      "masterMaterial": "/Game/Art/Materials/M_CardBase"
    }
  },
  "recipesDir": "Content/Python/recipes"
}
```

`namespace` is derived from `projectName` automatically:
- `"WarCard"` → `"wc"` (uppercase initials)
- `"Project Name"` → `"pn"` (space-split initials)
- `"warcard"` → `"warcard"` (no uppercase signal, use full)

## Recipes Cookbook

Recipes live in `<projectRoot>/<recipesDir>/*.py`. They are discovered by the framework on startup and on every `reload_recipes()` call.

### Minimal recipe

```python
from tools.recipe_framework import recipe, arg
from tools.project_config import get_config
from tools.result_format import ok, fail
from tools.primitives import import_texture

@recipe(name="import_card_art", desc="Import card art texture")
@arg("card_id", int, required=True)
@arg("art_path", str, required=True)
def import_card_art(card_id, art_path):
    cfg = get_config()
    dest = f"{cfg.paths.textures}/T_CardArt_{card_id}"
    return import_texture(sourcePath=art_path, destAssetPath=dest, ifExists="skip")
```

### Decorator order

Stack `@arg` above `@recipe`. Decorators closest to the function run first; the framework reverses the list so declared order is preserved.

```python
@recipe(name="my_recipe", desc="...", produces=["..."])
@arg("first_arg", str, required=True)
@arg("second_arg", int, required=False, default=0)
def my_recipe(first_arg, second_arg):
    ...
```

### `produces` parameter

Optional list of asset path templates that this recipe creates. Used by the contract-validator to detect missing assets.

```python
@recipe(
    name="create_card",
    desc="...",
    produces=[
        "{paths.textures}/T_CardArt_{card_id}",
        "{paths.cards}/MI_Card_{card_id}",
        "{paths.cards}/BP_Card_{card_id}",
    ]
)
```

Template variables `{paths.*}`, `{naming.*}`, `{assetRoot}` are resolved from `mcp-project.json` at validation time. Argument names (e.g. `{card_id}`) are matched to `@arg` declarations — the validator treats them as wildcards for expected-asset checks.

### Namespace rules

- Tool name registered = `<namespace>.<name>` (e.g. `wc.create_card`)
- Use only alphanumeric + underscore in `name` — no dots or dashes
- One recipe file = one logical domain (all card recipes, all level recipes, etc.)

### Reload cycle

After editing a recipe file:
```
reload_recipes()   # hot reload — no server restart needed
```

After editing `mcp-project.json`:
```
reload_config()    # then reload_recipes() to pick up new paths
```

After editing `primitives.py` or other Python tools — restart the server.

After editing C++ commands — rebuild the UE plugin, restart the Editor.

## Available Commands (v1.18.1)

The Python MCP server exposes **117 `@mcp.tool()` commands** across 13 domain
modules in `Python/tools/` (each registered by a `register_*_tools(mcp)` call),
plus the `reload_config` / `reload_recipes` server tools. Recipes add
higher-level project workflows on top — see the Recipes Cookbook.

> **Server coverage differs.** The stdio server (`unreal_mcp_server.py`)
> registers all 13 modules. The Docker / HTTP server (`unreal_mcp_server_http.py`)
> currently registers the 5 core modules — `editor`, `blueprint`, `node`,
> `project`, `umg` (84 tools) — plus the server tools.

### Naming convention (adopted v1.17.0)

**Going forward, new commands MUST use `subject_first` style** to keep
related operations grouped alphabetically and discoverable by tab-completion:

| Domain | Preferred style | Examples |
|---|---|---|
| Blueprint class lifecycle | `blueprint_<verb>` | `blueprint_create`, `blueprint_compile`, `blueprint_list` |
| Variables | `variable_<verb>` | `variable_create`, `variable_rename`, `variable_list` |
| Functions | `function_<verb>` | `function_create`, `function_set_flags` |
| Components | `component_<verb>` | `component_add`, `component_delete`, `component_list` |
| Nodes | `node_<verb>` | `node_add`, `node_connect`, `node_delete` |
| Pins | `pin_<verb>` | `pin_split`, `pin_set_default`, `pin_disconnect` |
| Animation BP | `anim_<verb>` | `anim_create`, `anim_set_skeleton` |

**Legacy names are retained** for backward compatibility. Existing pipelines
and tasks (`tasks/active/*.md`) continue to work with `create_blueprint`,
`compile_blueprint`, `rename_component`, etc. There is no plan to remove
legacy names — both styles will be supported indefinitely.

When implementing a new command, the `unreal-mcp-plugin-dev` agent enforces
`subject_first` for the new identifier and adds the canonical name to the
outer bridge allow-list. Old verb_first names are added ONLY when the agent
explicitly notes they are needed for an existing caller in `tasks/active/`.

### Commands by module

Counts are `@mcp.tool()` entries per file in `Python/tools/`.

#### Editor — `editor_tools.py` (10)
`get_actors_in_level`, `find_actors_by_name`, `spawn_actor`, `spawn_blueprint_actor`, `delete_actor`, `set_actor_transform`, `get_actor_properties`, `set_actor_property`, `focus_viewport`, `ping`

#### Level — `level_tools.py` (7)
`create_level`, `load_level`, `save_level`, `spawn_actor_in_level`, `remove_actor_from_level`, `set_actor_transform_in_level`, `list_actors_in_level`

#### Blueprint class & components — `blueprint_tools.py` (26)
- Class: `create_blueprint`, `create_blueprint_from_template`, `reparent_blueprint`, `compile_blueprint`, `compile_blueprint_verbose`, `validate_blueprint`, `set_blueprint_property`, `set_pawn_properties`
- Components: `add_component_to_blueprint`, `delete_component_from_blueprint`, `rename_component`, `list_components`, `set_component_transform`, `set_component_property`, `set_static_mesh_properties`, `set_physics_properties`
- Introspection: `list_blueprints`, `get_blueprint_class_info`, `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_variable_details`, `get_blueprint_function_details`
- Interfaces: `create_blueprint_interface`, `implement_blueprint_interface`, `remove_blueprint_interface`, `add_interface_function`

#### Blueprint graph — `node_tools.py` (35)
- Nodes: `add_blueprint_event_node`, `add_blueprint_input_action_node`, `add_blueprint_function_node`, `add_blueprint_branch_node`, `add_blueprint_variable_get_node`, `add_blueprint_variable_set_node`, `connect_blueprint_nodes`, `delete_node`, `set_node_property`, `find_blueprint_nodes`
- Events: `create_custom_event`, `add_custom_event_input`, `add_component_bound_event`
- Self / component refs: `add_blueprint_self_reference`, `add_blueprint_get_self_component_reference`
- Pins: `split_struct_pin`, `recombine_struct_pin`, `set_pin_default_value`, `get_pin_info`, `disconnect_pin`
- Variables: `add_blueprint_variable`, `rename_blueprint_variable`, `delete_blueprint_variable`, `list_blueprint_variables`, `set_variable_default_value`, `set_blueprint_variable_properties`, `set_blueprint_variable_flags`
- Functions: `create_function`, `delete_function`, `rename_function`, `add_function_input`, `add_function_output`, `add_function_local_variable`, `set_function_flags`, `list_blueprint_functions`

#### UMG — `umg_tools.py` (12)
`create_umg_widget_blueprint`, `build_umg_widget`, `add_widget_to_umg`, `delete_widget_from_umg`, `add_text_block_to_widget`, `add_button_to_widget`, `add_panel_widget_to_widget`, `set_widget_property`, `set_text_block_binding`, `bind_widget_event`, `add_widget_to_viewport`, `get_umg_hierarchy`

#### Animation Blueprint — `animation_tools.py` (7)
`create_animation_blueprint`, `set_anim_skeleton`, `add_state_machine`, `add_anim_state`, `add_anim_transition`, `add_play_anim_node`, `add_blend_space_player_node`

#### Input — `project_tools.py` (1)
`create_input_mapping`

#### Materials — `material_tools.py` (8)
`create_material_instance`, `set_material_instance_params`, `set_mesh_material_color`, `get_available_materials`, `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `get_blueprint_material_info`

#### Textures — `texture_tools.py` (2)
`import_texture`, `generate_placeholder_texture`

#### Static Mesh — `mesh_tools.py` (1)
`import_static_mesh`

#### Niagara — `niagara_tools.py` (2)
`copy_niagara_system`, `set_niagara_parameters`

#### DataTable & Sound — `data_asset_tools.py` (4)
`import_datatable_from_csv`, `set_datatable_row`, `get_datatable_rows`, `import_sound_wave`

#### Asset utilities — `asset_tools.py` (2)
`asset_exists`, `delete_asset`

#### Server tools (always registered)
`reload_config` — reload `mcp-project.json` from disk · `reload_recipes` — hot-reload recipes without restarting the server

## Primitives Reference

All primitives are in `Python/tools/primitives.py`. Each wraps one C++ command.

| Primitive | Command | Key params | Returns |
|-----------|---------|-----------|---------|
| `import_texture` | `import_texture` | `sourcePath`, `destAssetPath`, `sRGB`, `compression`, `mipGen`, `ifExists` | `{ok, status, assetPath}` |
| `generate_placeholder_texture` | `generate_placeholder_texture` | `destAssetPath`, `size`, `color`, `label`, `ifExists` | `{ok, status, assetPath}` |
| `create_material_instance` | `create_material_instance` | `parentMaterial`, `destAssetPath`, `params`, `ifExists` | `{ok, status, assetPath}` |
| `set_material_instance_params` | `set_material_instance_params` | `assetPath`, `params` | `{ok, status, assetPath}` |
| `create_blueprint_from_template` | `create_blueprint_from_template` | `templatePath`, `destAssetPath`, `defaultsOverride`, `ifExists` | `{ok, status, assetPath}` |
| `create_level` | `create_level` | `destMapPath`, `template`, `ifExists` | `{ok, status, assetPath}` |
| `load_level` | `load_level` | `mapPath` | `{ok, status}` |
| `save_level` | `save_level` | `mapPath?` | `{ok, status}` |
| `spawn_actor_in_level` | `spawn_actor_in_level` | `actorClass`, `mapPath?`, `name?`, `transform?`, `properties?`, `ifExists` | `{ok, status, meta.actorName}` |
| `list_actors_in_level` | `list_actors_in_level` | `mapPath?`, `classFilter?` | `{ok, meta.actors[]}` |
| `asset_exists` | `asset_exists` | `assetPath` | `{ok, meta.exists, meta.class?}` |
| `delete_asset` | `delete_asset` | `assetPath`, `ifMissing` | `{ok, status}` |
| `import_datatable_from_csv` | `import_datatable_from_csv` | `csvPath`, `destAssetPath`, `rowStruct?`, `ifExists` | `{ok, status, meta.rowCount}` |
| `set_datatable_row` | `set_datatable_row` | `assetPath`, `rowName`, `rowJson` | `{ok, status, meta.rowName}` |
| `get_datatable_rows` | `get_datatable_rows` | `assetPath` | `{ok, meta.rowCount, meta.rows[]}` |
| `import_sound_wave` | `import_sound_wave` | `wavPath`, `destAssetPath`, `ifExists` | `{ok, status, assetPath}` |
| `copy_niagara_system` | `copy_niagara_system` | `sourcePath`, `destPath`, `ifExists` | `{ok, status, assetPath}` |
| `set_niagara_parameters` | `set_niagara_parameters` | `assetPath`, `params` | `{ok, meta.paramsSet, meta.skippedParams[]}` |

**`ifExists` policy (all creation commands):**
- `"skip"` — return `ok("skipped", ...)` if asset already exists, no-op
- `"overwrite"` — delete existing asset, then create fresh

## Error Format

All responses follow this shape:

**Success:**
```json
{
  "ok": true,
  "status": "created",
  "assetPath": "/Game/Cards/T_CardArt_1",
  "meta": { "...": "..." }
}
```

**Error:**
```json
{
  "ok": false,
  "error": {
    "category": "config",
    "code": "MASTER_MATERIAL_MISSING",
    "message": "No master material configured",
    "details": { "card_id": 1 }
  }
}
```

Error categories:

| Category | Meaning |
|----------|---------|
| `config` | Bad or missing `mcp-project.json` field |
| `user` | Missing required arg, invalid value |
| `ue_internal` | UE Editor-side failure (factory error, save failed) |
| `not_found` | Referenced asset does not exist |
| `test` | Test recipe assertion failure |

## Dev Workflow

### Adding a primitive (new C++ command)

1. Add handler method in the appropriate `Commands/*.cpp` (or create a new file)
2. Register command name in `EpicUnrealMCPBridge.cpp` dispatch chain
3. Add Python wrapper in `tools/primitives.py`
4. Bump `VersionName` in `UnrealMCP.uplugin` (patch for fixes, minor for new commands)
5. Rebuild the plugin (close Editor → Build → reopen Editor)

### Adding a recipe

1. Edit `Content/Python/recipes/warcard_recipes.py` (or create a new file in `recipesDir`)
2. Call `reload_recipes()` in the MCP client — no restart needed

### Version bump rules

| Change type | Bump |
|-------------|------|
| Bug fix in C++ | patch (1.6.2 → 1.6.3) |
| New C++ command | minor (1.6.x → 1.7.0) |
| Breaking change in command signature | major (1.x → 2.0.0) |

### Live Coding caveat

UE Live Coding does NOT reload the MCP plugin dispatch table. After any C++ change:
1. Close Unreal Editor
2. Rebuild in Development Editor
3. Reopen Editor

## Testing

### Run all tests

```
wc.run_tests()
```

Returns `{total, passed, failed, results[{ok, name, duration_ms, error?}]}`.

### Add a test recipe

1. Create `Python/tests/test_<feature>.py`
2. Implement `def run() -> dict` — return `ok(...)` on pass, `fail(...)` on fail
3. `wc.run_tests()` discovers it automatically on next call

### Test structure

```python
from tools.result_format import ok, fail

def run() -> dict:
    # 1. Setup
    # 2. Call primitives or recipes
    # 3. Assert
    # 4. Cleanup
    if errors:
        return fail("test", "ASSERTIONS_FAILED", "; ".join(errors))
    return ok("created", "test-report", name="test_my_feature", duration_ms=0)
```

## Troubleshooting

**Asset doesn't appear on disk after creation**
- Ensure you are on plugin version ≥ 1.6.3 (fix: `create_blueprint` now calls `SaveAsset`)
- Check that the plugin is enabled in Edit → Plugins → UnrealMCP

**`reload_recipes()` returns 0 recipes**
- Verify `mcp-project.json` is in the UE project root (next to `.uproject`)
- Verify `recipesDir` path exists and contains `*.py` files (not starting with `_`)
- Check Python server log for import errors

**`version mismatch` in logs**
- Rebuild the C++ plugin. The Python server always reads `VersionName` from the connected plugin at handshake.

**Recipe not registered after file save**
- Call `reload_recipes()` explicitly. The framework does not watch for file changes.

**`NIAGARA_UNAVAILABLE` error**
- The `Niagara` plugin must be enabled in your UE project. Enable via Edit → Plugins → Niagara, then rebuild.

**Plugin fails to compile on UE 5.5 vs 5.7**
- Check `NiagaraEditor` dependency in `Build.cs`. If not installed, remove it — runtime `Niagara` module is sufficient for copy/parameterize operations.

## Repository Structure

```
unreal-mcp/
├── MCPGameProject/              # Example UE project (plugin bundled)
│   └── Plugins/UnrealMCP/       # C++ plugin
│       └── Source/UnrealMCP/
│           ├── Public/Commands/
│           └── Private/Commands/
├── Dockerfile                          # builds the HTTP/SSE server image
├── Python/
│   ├── unreal_mcp_server.py            # stdio server — registers all 13 tool modules
│   ├── unreal_mcp_server_http.py       # HTTP/SSE server (Docker, port 3001)
│   ├── unreal_mcp_server_advanced.py   # monolithic variant — 47 inline tools + helpers/
│   ├── tools/
│   │   ├── editor_tools.py  blueprint_tools.py  node_tools.py  umg_tools.py
│   │   ├── animation_tools.py  level_tools.py  material_tools.py  project_tools.py
│   │   ├── texture_tools.py  mesh_tools.py  niagara_tools.py  data_asset_tools.py  asset_tools.py
│   │   ├── primitives.py          # Python wrappers used by recipes
│   │   ├── recipe_framework.py    # @recipe / @arg decorators + registry
│   │   ├── project_config.py      # mcp-project.json loader
│   │   └── result_format.py       # ok() / fail() helpers
│   └── tests/
│       ├── __runner.py            # Test runner helper for wc.run_tests
│       ├── test_foundation.py
│       ├── test_texture.py
│       ├── test_material.py
│       ├── test_create_card_primitives.py
│       ├── test_create_level.py
│       ├── test_import_static_mesh.py
│       ├── test_create_match_arena.py
│       ├── test_datatable.py
│       ├── test_soundwave.py
│       └── test_niagara.py
└── PLUGIN_SETUP.md              # Setup guide for existing projects
```

## License

MIT
