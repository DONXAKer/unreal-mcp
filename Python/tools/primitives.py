"""
Python-side wrappers for MCP-CONTENT content primitives.

Recipes call these instead of hand-rolling `conn.send_command(...)` calls.
Each wrapper forwards to the C++ command of the same name (registered by
TextureCommands / MaterialCommands / BlueprintCommands in the plugin) and
returns the unified `{ok, status, assetPath, meta}` / `{ok, error{...}}`
response shape unchanged.

The C++ commands accept `assetPath` as the destination key; the recipe
layer uses `destAssetPath` (more explicit) so we normalize here.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from tools.result_format import fail


def _call(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    # Imported lazily to avoid a top-level cycle with the server module,
    # which imports this file indirectly via recipe discovery.
    from unreal_mcp_server import get_unreal_connection

    conn = get_unreal_connection()
    if conn is None:
        return fail(
            "ue_internal",
            "BRIDGE_UNREACHABLE",
            "Could not connect to the UnrealMCP plugin bridge",
            command=command,
        )
    raw = conn.send_command(command, params) or {}
    # Bridge wraps results in {"status":"success","result":{...}}; primitives
    # live in result. Everything else (error envelopes) is returned as-is.
    if isinstance(raw, dict) and "result" in raw and isinstance(raw["result"], dict):
        return raw["result"]
    return raw


def import_texture(
    sourcePath: str,
    destAssetPath: str,
    sRGB: Optional[bool] = None,
    compression: Optional[str] = None,
    mipGen: Optional[str] = None,
    ifExists: str = "skip",
) -> Dict[str, Any]:
    params: Dict[str, Any] = {
        "sourcePath": sourcePath,
        "assetPath": destAssetPath,
        "ifExists": ifExists,
    }
    if sRGB is not None:
        params["sRGB"] = sRGB
    if compression is not None:
        params["compression"] = compression
    if mipGen is not None:
        params["mipGen"] = mipGen
    return _call("import_texture", params)


def generate_placeholder_texture(
    destAssetPath: str,
    size: int = 512,
    color: Optional[List[float]] = None,
    label: str = "",
    ifExists: str = "skip",
) -> Dict[str, Any]:
    return _call("generate_placeholder_texture", {
        "assetPath": destAssetPath,
        "size": size,
        "color": color or [0.2, 0.2, 0.3, 1.0],
        "label": label,
        "ifExists": ifExists,
    })


def create_material_instance(
    parentMaterial: str,
    destAssetPath: str,
    params: Optional[Dict[str, Any]] = None,
    ifExists: str = "skip",
) -> Dict[str, Any]:
    return _call("create_material_instance", {
        "parentMaterial": parentMaterial,
        "assetPath": destAssetPath,
        "params": params or {},
        "ifExists": ifExists,
    })


def set_material_instance_params(
    assetPath: str,
    params: Dict[str, Any],
) -> Dict[str, Any]:
    return _call("set_material_instance_params", {
        "assetPath": assetPath,
        "params": params,
    })


def create_blueprint_from_template(
    templatePath: str,
    destAssetPath: str,
    defaultsOverride: Optional[Dict[str, Any]] = None,
    ifExists: str = "skip",
) -> Dict[str, Any]:
    return _call("create_blueprint_from_template", {
        "templatePath": templatePath,
        "assetPath": destAssetPath,
        "defaultsOverride": defaultsOverride or {},
        "ifExists": ifExists,
    })


# ── Level primitives (MCP-CONTENT-003b) ──────────────────────────────────────

def create_level(
    destMapPath: str,
    template: str = "Empty",
    ifExists: str = "skip",
) -> Dict[str, Any]:
    return _call("create_level", {
        "destMapPath": destMapPath,
        "template": template,
        "ifExists": ifExists,
    })


def load_level(mapPath: str) -> Dict[str, Any]:
    return _call("load_level", {"mapPath": mapPath})


def save_level(mapPath: Optional[str] = None) -> Dict[str, Any]:
    params: Dict[str, Any] = {}
    if mapPath:
        params["mapPath"] = mapPath
    return _call("save_level", params)


def spawn_actor_in_level(
    actorClass: str,
    mapPath: Optional[str] = None,
    name: Optional[str] = None,
    transform: Optional[Dict[str, Any]] = None,
    properties: Optional[Dict[str, Any]] = None,
    ifExists: str = "skip",
) -> Dict[str, Any]:
    params: Dict[str, Any] = {"actorClass": actorClass, "ifExists": ifExists}
    if mapPath:
        params["mapPath"] = mapPath
    if name:
        params["name"] = name
    if transform:
        params["transform"] = transform
    if properties:
        params["properties"] = properties
    return _call("spawn_actor_in_level", params)


def list_actors_in_level(
    mapPath: Optional[str] = None,
    classFilter: Optional[str] = None,
) -> Dict[str, Any]:
    params: Dict[str, Any] = {}
    if mapPath:
        params["mapPath"] = mapPath
    if classFilter:
        params["classFilter"] = classFilter
    return _call("list_actors_in_level", params)


def asset_exists(assetPath: str) -> Dict[str, Any]:
    return _call("asset_exists", {"assetPath": assetPath})


def delete_asset(assetPath: str, ifMissing: str = "skip") -> Dict[str, Any]:
    return _call("delete_asset", {"assetPath": assetPath, "ifMissing": ifMissing})
