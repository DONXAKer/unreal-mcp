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
Python MCP Server  (Python/unreal_mcp_server_advanced.py, port auto)
    │  @recipe framework  →  discovers Content/Python/recipes/*.py
    │  tools/primitives.py  →  thin wrappers over send_command()
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

**Requirements:** Unreal Engine 5.5+, Python 3.12+, [uv](https://docs.astral.sh/uv/)

### 1. Plugin

Use the bundled example project (plugin already connected):

```bash
# Right-click MCPGameProject.uproject → Generate Visual Studio project files
# Build in Development Editor
```

Or add the plugin to an existing project — see [PLUGIN_SETUP.md](PLUGIN_SETUP.md).

### 2. Project config

Create `mcp-project.json` in your UE project root (next to the `.uproject` file). See **Config schema** below.

### 3. Python server

```bash
cd Python/
uv sync
uv run unreal_mcp_server_advanced.py
```

### 4. MCP client

```json
{
  "mcpServers": {
    "unrealMCP": {
      "command": "uv",
      "args": ["--directory", "/absolute/path/to/Python", "run", "unreal_mcp_server_advanced.py"]
    }
  }
}
```

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
├── Python/
│   ├── unreal_mcp_server.py           # Basic server
│   ├── unreal_mcp_server_advanced.py  # Full server (recommended)
│   ├── tools/
│   │   ├── primitives.py          # Python wrappers for C++ commands
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
