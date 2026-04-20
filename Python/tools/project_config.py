"""
Project config loader for MCP Content Pipeline.

Reads <ProjectRoot>/mcp-project.json into a typed ProjectConfig pydantic
model. Used by recipes and content primitives to resolve naming prefixes,
asset paths, and per-asset-type defaults without hardcoding.

See MCP-CONTENT-001 task for schema and decisions.
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
from typing import Any, Dict, Optional

from pydantic import BaseModel, Field, ValidationError

logger = logging.getLogger("UnrealMCP")


CONFIG_FILENAME = "mcp-project.json"


class NamingConfig(BaseModel):
    blueprint: str = "BP_"
    material: str = "M_"
    material_instance: str = Field("MI_", alias="materialInstance")
    texture: str = "T_"
    static_mesh: str = Field("SM_", alias="staticMesh")
    data_table: str = Field("DT_", alias="dataTable")
    niagara: str = "NS_"
    sound_wave: str = Field("SW_", alias="soundWave")

    model_config = {"populate_by_name": True, "extra": "allow"}


class PathsConfig(BaseModel):
    cards: str = "/Game/Cards"
    levels: str = "/Game/Maps"
    materials: str = "/Game/Art/Materials"
    textures: str = "/Game/Art/Textures"
    meshes: str = "/Game/Art/Meshes"
    data_tables: str = Field("/Game/Data", alias="dataTables")
    niagara: str = "/Game/VFX"
    sounds: str = "/Game/Audio"

    model_config = {"populate_by_name": True, "extra": "allow"}


class TextureDefaults(BaseModel):
    s_rgb: bool = Field(True, alias="sRGB")
    compression: str = "BC7"
    mip_gen: str = Field("FromTexture", alias="mipGen")

    model_config = {"populate_by_name": True, "extra": "allow"}


class MaterialDefaults(BaseModel):
    master_material: Optional[str] = Field(None, alias="masterMaterial")

    model_config = {"populate_by_name": True, "extra": "allow"}


class DefaultsConfig(BaseModel):
    texture: TextureDefaults = Field(default_factory=TextureDefaults)
    material: MaterialDefaults = Field(default_factory=MaterialDefaults)

    model_config = {"extra": "allow"}


class ProjectConfig(BaseModel):
    project_name: str = Field(..., alias="projectName")
    asset_root: str = Field("/Game", alias="assetRoot")
    naming: NamingConfig = Field(default_factory=NamingConfig)
    paths: PathsConfig = Field(default_factory=PathsConfig)
    defaults: DefaultsConfig = Field(default_factory=DefaultsConfig)
    recipes_dir: str = Field("Content/Python/recipes", alias="recipesDir")

    model_config = {"populate_by_name": True, "extra": "allow"}

    @property
    def namespace(self) -> str:
        """Tool-name namespace derived from projectName.

        Examples:
          "WarCard"      -> "wc"  (uppercase-letter extraction)
          "Project Name" -> "pn"  (space-split initials)
          "warcard"      -> "warcard"  (already lowercase, no signal — use whole)
        """
        name = self.project_name.strip()
        if not name:
            return "mcp"
        uppercase = [ch for ch in name if ch.isupper()]
        if len(uppercase) >= 2:
            return "".join(uppercase).lower()
        if " " in name:
            return "".join(w[0] for w in name.split() if w).lower()
        return name.lower()


_cached_config: Optional[ProjectConfig] = None
_cached_config_path: Optional[Path] = None


def resolve_project_root(explicit: Optional[str] = None) -> Optional[Path]:
    """Resolve the UE project root that owns the mcp-project.json.

    Search order:
      1. Explicit argument (if passed).
      2. $MCP_PROJECT_ROOT env var.
      3. Walk up from CWD looking for a `*.uproject` sibling.
      4. Walk up from this module's location (plugin lives inside the
         project, so climbing from Python/tools/project_config.py hits
         the .uproject reliably regardless of MCP-server CWD and env).
    """
    if explicit:
        root = Path(explicit).resolve()
        return root if root.is_dir() else None

    env_root = os.environ.get("MCP_PROJECT_ROOT")
    if env_root:
        root = Path(env_root).resolve()
        if root.is_dir():
            return root

    def _find_root(start: Path) -> Optional[Path]:
        # Walk up; at each level also scan immediate children so monorepos
        # (e.g. D:/WarCard/client/ when the MCP server runs from a sibling
        # unreal-mcp/ dir) are discoverable without env vars. Prefer a dir
        # with mcp-project.json — in a monorepo multiple .uproject files
        # can exist (e.g. the plugin's own test project) and only one owns
        # the content config.
        for parent in (start, *start.parents):
            if (parent / CONFIG_FILENAME).is_file():
                return parent
            try:
                for child in parent.iterdir():
                    if child.is_dir() and (child / CONFIG_FILENAME).is_file():
                        return child
            except (PermissionError, OSError):
                continue
        for parent in (start, *start.parents):
            if any(parent.glob("*.uproject")):
                return parent
            try:
                for child in parent.iterdir():
                    if child.is_dir() and any(child.glob("*.uproject")):
                        return child
            except (PermissionError, OSError):
                continue
        return None

    cwd_hit = _find_root(Path.cwd().resolve())
    if cwd_hit is not None:
        return cwd_hit

    return _find_root(Path(__file__).resolve().parent)


def load_config(project_root: Optional[str] = None) -> Optional[ProjectConfig]:
    """Load (or reload) mcp-project.json from the resolved project root.

    Returns None if no config file is found — callers must handle this
    gracefully (warn, fall back to hard-coded defaults, or skip).
    """
    global _cached_config, _cached_config_path

    root = resolve_project_root(project_root)
    if root is None:
        logger.warning("Could not resolve project root for %s", CONFIG_FILENAME)
        return None

    config_path = root / CONFIG_FILENAME
    if not config_path.is_file():
        logger.warning("No %s at %s", CONFIG_FILENAME, config_path)
        return None

    try:
        raw: Dict[str, Any] = json.loads(config_path.read_text(encoding="utf-8"))
        cfg = ProjectConfig.model_validate(raw)
    except (json.JSONDecodeError, ValidationError) as e:
        logger.error("Invalid %s: %s", config_path, e)
        return None

    _cached_config = cfg
    _cached_config_path = config_path
    logger.info(
        "Loaded %s (projectName=%s, namespace=%s)",
        config_path,
        cfg.project_name,
        cfg.namespace,
    )
    return cfg


def get_config() -> Optional[ProjectConfig]:
    """Return the cached ProjectConfig, loading it lazily if needed."""
    global _cached_config
    if _cached_config is None:
        _cached_config = load_config()
    return _cached_config


def reload_config() -> Dict[str, Any]:
    """MCP-exposed hot reload of mcp-project.json.

    Returns a unified-format result so callers see a consistent shape.
    """
    from tools.result_format import fail, ok

    global _cached_config, _cached_config_path
    _cached_config = None
    _cached_config_path = None

    cfg = load_config()
    if cfg is None:
        return fail(
            "config",
            "NO_PROJECT_CONFIG",
            f"{CONFIG_FILENAME} not found or invalid",
        )
    return ok(
        "updated",
        str(_cached_config_path) if _cached_config_path else "",
        projectName=cfg.project_name,
        namespace=cfg.namespace,
        recipesDir=cfg.recipes_dir,
    )
