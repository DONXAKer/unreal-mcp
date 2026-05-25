# UnrealMCP

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.5%2B-orange)](https://www.unrealengine.com)
[![Python](https://img.shields.io/badge/Python-3.12%2B-yellow)](https://www.python.org)

Bridges an MCP client (Claude Desktop, Cursor, Windsurf, AI-Workflow pipelines) to a running Unreal Editor over TCP. Lets AI agents create and modify assets — Blueprints, materials, UMG widgets, levels, Niagara systems, Blueprint graphs — instead of clicking through the Editor manually.

→ Current version: **v2.1.0** (see [CHANGELOG.md](CHANGELOG.md))
→ Setup for existing projects: [PLUGIN_SETUP.md](PLUGIN_SETUP.md)

## How it fits together

```
MCP client (Claude / Cursor / pipeline)
       │ MCP protocol (stdio or HTTP)
       ▼
Python MCP server (Python/unreal_mcp_server*.py)
       │ TCP :55557, JSON commands
       ▼
UE5 Editor + UnrealMCP C++ plugin
```

Two layers of capability:

- **Primitives** — ~118 low-level `@mcp.tool()` commands wrapping individual UE Editor operations (`create_blueprint`, `import_texture`, `add_blueprint_function_node`, `set_pin_default_value`, …). Defined in `Python/tools/*.py`, dispatched to C++ over TCP.
- **Recipes** — project-specific workflows composed from primitives, registered automatically from `<UE-project>/Content/Python/recipes/*.py`. Adding a recipe never requires plugin or server changes — `reload_recipes()` picks it up.
- **Graph Builder** (since v2.1) — `build_blueprint_graph` MCP tool builds a whole Blueprint EventGraph from one declarative JSON spec. Replaces 15-30 sequential `add_blueprint_*_node` + `connect_blueprint_nodes` calls with one atomic operation (with rollback). See [`Python/tools/graph_builder.py`](Python/tools/graph_builder.py) docstring for the spec schema.

## Quick start

**Need:** UE 5.5+. For local stdio server — Python 3.12+ and [uv](https://docs.astral.sh/uv/). For containerised HTTP server — only Docker.

1. **Open the plugin in your UE project.** Either use the bundled `MCPGameProject/` (plugin already wired) or follow [PLUGIN_SETUP.md](PLUGIN_SETUP.md). Build in Development Editor, launch the Editor — the plugin's C++ bridge starts listening on TCP `:55557`.
2. **Drop `mcp-project.json` into your UE project root** (next to `.uproject`). Minimum content: `{"projectName": "MyGame"}`. See [Config schema](#config-schema) below.
3. **Run the Python server.**
   ```
   cd Python/                   # stdio (for desktop MCP clients)
   uv sync
   uv run unreal_mcp_server.py
   ```
   Or via Docker (HTTP/SSE on `:3001` for pipeline-style clients):
   ```
   docker build -t unreal-mcp .
   docker run --rm -p 3001:3001 \
     -e UNREAL_HOST=host.docker.internal -e UNREAL_PORT=55557 \
     unreal-mcp
   ```
4. **Point your MCP client at the server.** Stdio example for a `mcp.json`:
   ```json
   {"mcpServers": {"unrealMCP": {"command": "uv",
     "args": ["--directory", "/abs/path/to/Python", "run", "unreal_mcp_server.py"]}}}
   ```
   HTTP clients: connect to `http://localhost:3001` (Streamable-HTTP endpoint, SSE fallback).
5. **Sanity-check:** call the `ping` tool from your client. Response includes `plugin_version` — confirms the right plugin binary is loaded.

Client config locations: Claude Desktop `~/.config/claude-desktop/mcp.json` · Cursor `.cursor/mcp.json` · Windsurf `~/.config/windsurf/mcp.json`.

## Config schema

`mcp-project.json` in your UE project root. All fields optional except `projectName`.

```json
{
  "projectName": "WarCard",
  "assetRoot": "/Game",
  "naming":   { "blueprint": "BP_", "material": "M_", "materialInstance": "MI_",
                "texture": "T_", "staticMesh": "SM_", "dataTable": "DT_",
                "niagara": "NS_", "soundWave": "SW_" },
  "paths":    { "cards": "/Game/Card", "levels": "/Game/Maps",
                "materials": "/Game/Art/Materials", "textures": "/Game/Art/Textures",
                "meshes": "/Game/Art/Meshes", "dataTables": "/Game/Data",
                "niagara": "/Game/VFX", "sounds": "/Game/Audio" },
  "defaults": { "texture": {"sRGB": true, "compression": "BC7", "mipGen": "FromTexture"},
                "material": {"masterMaterial": "/Game/Art/Materials/M_CardBase"} },
  "recipesDir": "Content/Python/recipes"
}
```

`namespace` is derived from `projectName`: uppercase initials (`WarCard` → `wc`), space-split for multi-word (`"My Game"` → `mg`), or the full name lowercased if no uppercase signal.

## Where to look for what

| Need | Look at |
|---|---|
| List of all MCP tools | `Python/tools/<module>.py` — each `@mcp.tool()` is a callable command |
| Tool count + module breakdown | `register_*_tools(mcp)` calls in `unreal_mcp_server.py` |
| Recipe authoring | `_envelope_design.md`, `recipe_framework.py` docstring; `<UE-project>/Content/Python/recipes/*.py` for examples |
| Graph spec format | `Python/tools/graph_builder.py` module docstring |
| Result envelope (success/error shape) | "Error format" in [`Python/tools/result_format.py`](Python/tools/result_format.py) — `ok()` and `fail()` helpers; dual-key envelope rules |
| C++ command dispatch | `EpicUnrealMCPBridge.cpp` `HandleCommand()` |
| What's new in each release | [CHANGELOG.md](CHANGELOG.md) |
| Plugin install for existing project | [PLUGIN_SETUP.md](PLUGIN_SETUP.md) |

## Writing a recipe (1-minute version)

`<UE-project>/Content/Python/recipes/cards.py`:

```python
from tools.recipe_framework import recipe, arg
from tools.project_config import get_config
from tools.primitives import import_texture

@recipe(name="import_card_art", desc="Import card art texture",
        produces=["{paths.textures}/T_CardArt_{card_id}"])
@arg("card_id", int, required=True)
@arg("art_path", str, required=True)
def import_card_art(card_id, art_path):
    cfg = get_config()
    return import_texture(sourcePath=art_path,
                          destAssetPath=f"{cfg.paths.textures}/T_CardArt_{card_id}",
                          ifExists="skip")
```

Then in the MCP client:

```
reload_recipes()                # picks up new files; no server restart
wc.import_card_art(card_id=42, art_path="/abs/path/card-42.png")
```

Recipe gets registered as `<namespace>.<name>` (e.g. `wc.import_card_art`). `produces[]` templates are auto-verified after each successful call — missing assets land in `meta.produces_check.missing[]`. Set `rollback_on_failure=True` on the `@recipe` decorator to auto-`delete_asset` everything the recipe created if it ends in failure.

Decorator order: `@recipe` outermost, then `@arg`s (declaration-order preserved).

## Building a Blueprint graph (since v2.1)

For wiring tasks where `impl_bp` agents previously made dozens of `add_*_node` + `connect_*` calls in sequence, use `build_blueprint_graph` once with a declarative spec:

```python
spec = {
    "blueprint_path": "/Game/UI/MyWidget",
    "graph": "EventGraph",
    "nodes": [
        {"id": "evt", "type": "Event", "event_name": "ReceiveBeginPlay"},
        {"id": "log", "type": "FunctionCall",
                       "target": "KismetSystemLibrary", "function_name": "PrintString"}
    ],
    "connections": [
        {"from": "evt.then", "to": "log.exec"}
    ],
    "defaults": [
        {"pin": "log.InString", "value": "Hello"}
    ],
    "compile": True,
    "rollback_on_failure": True
}
result = build_blueprint_graph(blueprint_path="/Game/UI/MyWidget", spec=spec)
```

Atomicity: any failure (validation, node creation, pin connect, default-value set) triggers full reverse-walk delete of every node the call created. UE5 ends in a clean state ready for retry. Compile failures don't trigger rollback (graph is built, BP just doesn't compile — operator fixes via Editor).

Supported node types: `Event`, `FunctionCall`, `VariableGet`, `VariableSet`, `Branch`, `ForEachLoop`, `DynamicCast`, `BindEvent`, `CustomEvent`. Anything outside this set: return validation error with `unsupported_type` — caller falls back to manual primitive flow. To add a new type, extend `_create_node` in `graph_builder.py` (and `SUPPORTED_NODE_TYPES`).

## Dev cycle

| Edit happens in | To pick it up |
|---|---|
| A recipe `.py` file | `reload_recipes()` in MCP client (no restart) |
| `mcp-project.json` | `reload_config()` then `reload_recipes()` |
| `Python/tools/*.py` (a primitive, the framework, the graph builder) | Restart the Python server (`uv run`) or `docker restart` the HTTP server container |
| C++ command (`Commands/*.cpp`, `EpicUnrealMCPBridge.cpp`) | Close Editor → rebuild plugin in Development Editor → reopen Editor. Live Coding does NOT reload the dispatch table. |

Version bumps (`UnrealMCP.uplugin` `VersionName`):
- C++ bug fix without signature change → **patch**
- New command or new Python tool → **minor**
- Removed command or changed signature → **major**

Then record the change in [CHANGELOG.md](CHANGELOG.md) under the matching version.

## Testing

`wc.run_tests()` (or your project's `<ns>.run_tests()`) discovers and runs every `Python/tests/test_*.py`. Each test file must expose `def run() -> dict` returning `ok(...)` on pass or `fail(...)` on failure. Returns `{total, passed, failed, results[]}`.

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `version mismatch` in logs | Rebuild the C++ plugin. Python reads `VersionName` from the connected plugin at handshake. |
| `ping` works but `plugin_version` is missing in response | Plugin is pre-2.0 (or hasn't been rebuilt since). Rebuild + restart Editor. |
| Created asset doesn't appear on disk | Plugin < 1.6.3 didn't call `SaveAsset`. Rebuild to current. |
| `reload_recipes()` returns 0 | `mcp-project.json` missing, in the wrong place, or `recipesDir` doesn't exist / contains no `*.py` files. |
| `build_blueprint_graph` returns `phase: validation` | Inspect `errors[]`. Pre-validation runs before any UE5 call — fix the spec (unknown node type, duplicate id, unresolved pin reference, missing required field). |
| `build_blueprint_graph` returns `phase: create/connect` with `rolled_back: true` | Atomic rollback worked — UE5 is back to pre-call state. Inspect `cause` for the underlying UE error; fix the spec and retry. |
| `NIAGARA_UNAVAILABLE` | Enable Niagara plugin in your UE project (Edit → Plugins → Niagara), then rebuild. |
| C++ plugin fails to compile after UE engine upgrade | Likely a UE API rename (we hit `UEdGraphSchema_K2::CanLinkedTo` → `CanCreateConnection` between UE versions — see CHANGELOG v2.0.0). Read the build error, find the renamed API, edit the call site, rebuild. |

## License

MIT.
