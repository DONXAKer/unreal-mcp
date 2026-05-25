# Changelog

All notable changes to UnrealMCP (C++ plugin + Python MCP server) are documented here. History maintained from **v2.0.0** onward.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); plugin follows [SemVer](https://semver.org/) (`VersionName` in `UnrealMCP.uplugin`).

Bump rules:
- **patch** — C++ bug fixes that don't change command signatures or add commands
- **minor** — new commands, new Python tools, new spec fields (backward-compatible)
- **major** — removed commands, changed signatures, removed Python primitives

---

## [Unreleased]

Pending work; will be cut into the next minor or patch release.

---

## [2.1.0] — 2026-05-24

Adds declarative graph builder; Python-only (no plugin rebuild needed for this release).

### Added

- **`build_blueprint_graph` MCP tool** (`Python/tools/graph_builder.py`) — atomically build an EventGraph or function graph from a declarative JSON spec instead of orchestrating 15-30 separate `add_blueprint_*_node` + `connect_blueprint_nodes` calls. Supports 9 node types covering ~80% of UI-task patterns:
  `Event`, `FunctionCall`, `VariableGet`, `VariableSet`, `Branch`, `ForEachLoop`, `DynamicCast`, `BindEvent`, `CustomEvent`.
  Includes pre-validation (no UE5 state-change until spec is structurally valid), atomic rollback on failure (`rollback_on_failure: true` by default), and a structured result envelope (`{success, nodes_created, connections_made, compile_result, node_id_map}` or `{success: false, phase, fail_at, cause, rolled_back, rollback_result}`).
  See `Python/tools/graph_builder.py` docstring for the full spec schema.

### Why

Sonnet-driven `impl_bp` regularly produced UMG widgets with empty `K2Node_ComponentBoundEvent` connections (clicks → nothing). Each loopback added 25 min and another LLM-call-orchestration risk surface. A high-level builder removes the per-node orchestration step, lets `plan_bp` emit the wiring as data instead of prose, and lets `review_bp` compare actual graph against the same spec deterministically. Source: FEAT-MULLIGAN-001 + 002 retrospective.

---

## [2.0.0] — 2026-05-23

Initial tracked release. Brings tooling parity, an error-envelope contract, and recipe-level lifecycle hooks.

### Added

- **`ping` response now carries `plugin_version`** (`EpicUnrealMCPBridge.cpp`) — pipeline guards (`check_unreal`) use presence of this field to confirm the editor loaded the right plugin binary. Earlier plugins answered `pong` without it; pipeline now hard-fails when the field is missing instead of false-greening on stale binary. Source: FEAT-MULLIGAN-001 post-mortem (false-green widget creation without `WidgetSwitcher`).
- **`list_recipes` MCP server tool** — returns full metadata (qualified name, description, args[], produces[]) for every registered recipe. Lets clients introspect without re-running discovery.
- **HTTP server registers all 13 tool modules** (`unreal_mcp_server_http.py`). Pre-2.0 only registered 5; in-container pipelines now have the same tool surface as the stdio server.
- **Recipe-level `rollback_on_failure` parameter** (`recipe_framework.py`). When set, the framework journals every primitive call that creates/overwrites an asset and reverses the journal on recipe failure (`delete_asset` for `created`, surfaced-as-skipped for `overwritten` since no backup is taken). Result lands under `error.details.rollback`.
- **`meta.produces_check`** in recipe responses. After a successful recipe call, each `produces[]` template is expanded with the recipe's actual arguments and verified via `asset_exists`. Missing assets appear in `meta.produces_check.missing[]` — the recipe response stays successful, only signaling.

### Changed

- **Dual-key result envelope** — every response now carries both the unified `ok`/`status`/`assetPath`/`meta` shape AND the legacy `success`/`message` shape (`ok == success` invariant). Old and new consumers coexist; nothing breaks for clients that read either side.
- **Naming convention guidance** — `subject_first` style (`blueprint_*`, `variable_*`, `node_*`, `component_*`, `pin_*`, `anim_*`) is enforced for NEW commands. Legacy `verb_first` names (`create_blueprint`, `compile_blueprint`, etc.) kept indefinitely for backward compat. New `subject_first` is added to bridge allow-list only.

### Fixed

- **`BPConnector.cpp:147`** — `Schema->CanLinkedTo(...)` was a typo (method doesn't exist on `UEdGraphSchema_K2`); changed to `Schema->CanCreateConnection(...)`. Plugin failed to compile against UE 5.7 until this fix.

---

## Migration notes

### 1.x → 2.0.0

- No breaking changes for client code that reads `success`/`message`. New code SHOULD prefer the unified `ok`/`status`/`assetPath`/`meta`/`error` envelope.
- Existing recipes work unchanged. Add `rollback_on_failure=True` to the `@recipe` decorator if you want automatic cleanup on failure.
- HTTP-server clients gain access to tool modules that were previously stdio-only (Niagara, Animation BP, etc.). No client-side change needed; new tools just appear in the registered list after the server restart.

### 2.0.0 → 2.1.0

- No breaking changes. `build_blueprint_graph` is additive — existing `add_blueprint_*_node` + `connect_blueprint_nodes` primitives keep working.
- To benefit from the builder, update `plan_bp` prompts (in the calling pipeline, not this plugin) to emit graph specs in the planner output. Operators using ad-hoc MCP calls can keep doing them; the builder is opt-in per call.
