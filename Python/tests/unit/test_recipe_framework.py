"""
Unit tests for tools/recipe_framework — list_recipes_impl + produces validator.

No live Unreal Editor required — uses FastMCP test instance + mocked
primitives.asset_exists for the produces-validation paths.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any
from unittest.mock import patch

from mcp.server.fastmcp import FastMCP

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import project_config, recipe_framework
from tools.project_config import ProjectConfig
from tools.result_format import ok


def _fresh_registry() -> FastMCP:
    """Build a fresh FastMCP + initialize a clean registry singleton."""
    server = FastMCP("test")
    recipe_framework.init_registry(server)
    return server


def test_list_recipes_uninitialized_registry() -> None:
    """Hitting list_recipes before init_registry returns a fail envelope."""
    recipe_framework._registry = None
    out = recipe_framework.list_recipes_impl()
    assert out["ok"] is False
    assert out["error"]["code"] == "REGISTRY_NOT_INITIALIZED"


def test_list_recipes_empty_registry() -> None:
    """Fresh registry with no recipes returns count=0, recipes=[]."""
    _fresh_registry()
    out = recipe_framework.list_recipes_impl()
    assert out["ok"] is True
    assert out["meta"]["count"] == 0
    assert out["meta"]["recipes"] == []


def test_list_recipes_returns_full_metadata() -> None:
    """Registered recipes show qualified name + args + produces."""
    _fresh_registry()

    @recipe_framework.recipe(
        name="demo_card",
        desc="A demo recipe",
        produces=["/Game/T_Card_{card_id}", "/Game/MI_Card_{card_id}"],
    )
    @recipe_framework.arg("card_id", int, required=True, description="Card id")
    @recipe_framework.arg("note", str, required=False, default="hello")
    def demo_card(card_id: int, note: str) -> dict[str, Any]:
        return ok("created", f"/Game/T_Card_{card_id}")

    spec = getattr(demo_card, recipe_framework._RECIPE_SPEC_ATTR)
    qualified = recipe_framework._registry.register(spec, namespace="wc")  # type: ignore[union-attr]
    assert qualified == "wc.demo_card"

    out = recipe_framework.list_recipes_impl()
    assert out["ok"] is True
    assert out["meta"]["count"] == 1

    r = out["meta"]["recipes"][0]
    assert r["name"] == "wc.demo_card"
    assert r["description"] == "A demo recipe"
    assert r["produces"] == ["/Game/T_Card_{card_id}", "/Game/MI_Card_{card_id}"]

    arg_names = [a["name"] for a in r["args"]]
    assert arg_names == ["card_id", "note"]

    card_arg = r["args"][0]
    assert card_arg["type"] == "int"
    assert card_arg["required"] is True
    assert card_arg["description"] == "Card id"

    note_arg = r["args"][1]
    assert note_arg["required"] is False
    assert note_arg["default"] == "hello"


def test_list_recipes_sorted_by_qualified_name() -> None:
    """Recipes are returned in alphabetical order by qualified name."""
    _fresh_registry()

    @recipe_framework.recipe(name="zeta", desc="z")
    def zeta() -> dict[str, Any]:
        return ok("created", "/Game/z")

    @recipe_framework.recipe(name="alpha", desc="a")
    def alpha() -> dict[str, Any]:
        return ok("created", "/Game/a")

    for fn in (zeta, alpha):
        spec = getattr(fn, recipe_framework._RECIPE_SPEC_ATTR)
        recipe_framework._registry.register(spec, namespace="wc")  # type: ignore[union-attr]

    out = recipe_framework.list_recipes_impl()
    names = [r["name"] for r in out["meta"]["recipes"]]
    assert names == ["wc.alpha", "wc.zeta"]


# --- produces validator ------------------------------------------------------

def _stub_cfg() -> ProjectConfig:
    """Build a typical ProjectConfig (no file I/O)."""
    return ProjectConfig(project_name="WarCard")


def test_resolve_template_paths_naming_assetroot() -> None:
    cfg = _stub_cfg()
    out = recipe_framework._resolve_template(
        "{paths.cards}/{naming.texture}CardArt_{card_id}",
        cfg,
        {"card_id": 7},
    )
    assert out == "/Game/Cards/T_CardArt_7"


def test_resolve_template_unknown_placeholder_survives() -> None:
    cfg = _stub_cfg()
    out = recipe_framework._resolve_template(
        "{paths.bogus}/{naming.also_bogus}/{unknown_arg}",
        cfg,
        {},
    )
    # bogus path/naming keys + unresolved arg → original placeholders kept.
    assert "{paths.bogus}" in out
    assert "{naming.also_bogus}" in out
    assert "{unknown_arg}" in out


def test_validate_produces_all_present() -> None:
    """Mock asset_exists → exists=True for every path; missing[] is empty."""
    project_config._cached_config = _stub_cfg()
    fake_exists = lambda assetPath: {"ok": True, "meta": {"exists": True}}  # noqa: E731
    with patch("tools.primitives.asset_exists", side_effect=fake_exists):
        check = recipe_framework._validate_produces(
            ["{paths.cards}/T_Card_{id}", "{paths.cards}/MI_Card_{id}"],
            {"id": 1},
        )
    assert check is not None
    assert check["status"] == "ok"
    assert check["checked"] == 2
    assert check["missing"] == []
    assert check["resolved"] == ["/Game/Cards/T_Card_1", "/Game/Cards/MI_Card_1"]
    project_config._cached_config = None


def test_validate_produces_some_missing() -> None:
    """First asset exists, second doesn't — missing[] captures the second."""
    project_config._cached_config = _stub_cfg()
    seen: list[str] = []

    def fake_exists(assetPath: str) -> dict[str, Any]:
        seen.append(assetPath)
        return {"ok": True, "meta": {"exists": assetPath.endswith("_1")}}

    with patch("tools.primitives.asset_exists", side_effect=fake_exists):
        check = recipe_framework._validate_produces(
            ["{paths.cards}/T_Card_{id}", "{paths.cards}/MI_Card_{id}_extra"],
            {"id": 1},
        )
    assert check is not None
    assert check["status"] == "missing"
    assert check["missing"] == ["/Game/Cards/MI_Card_1_extra"]
    assert seen == ["/Game/Cards/T_Card_1", "/Game/Cards/MI_Card_1_extra"]
    project_config._cached_config = None


def test_validate_produces_unresolved_template_counts_as_missing() -> None:
    """If a placeholder can't be resolved, the path is flagged as missing
    without ever hitting the bridge — the recipe forgot to declare an @arg."""
    project_config._cached_config = _stub_cfg()
    called = False

    def fake_exists(assetPath: str) -> dict[str, Any]:
        nonlocal called
        called = True
        return {"ok": True, "meta": {"exists": True}}

    with patch("tools.primitives.asset_exists", side_effect=fake_exists):
        check = recipe_framework._validate_produces(
            ["{paths.cards}/T_Card_{missing_arg}"],
            {},
        )
    assert check is not None
    assert check["status"] == "missing"
    assert check["missing"] == ["/Game/Cards/T_Card_{missing_arg}"]
    assert called is False  # short-circuited before bridge call
    project_config._cached_config = None


def test_validate_produces_no_config_returns_none() -> None:
    """No mcp-project.json → skip validation gracefully (returns None)."""
    project_config._cached_config = None
    # resolve_project_root() will not find a config either; force the path:
    with patch("tools.recipe_framework.get_config", return_value=None):
        check = recipe_framework._validate_produces(["/Game/X"], {})
    assert check is None


def test_validate_produces_bridge_unreachable_returns_none() -> None:
    """asset_exists raising → degrade gracefully (skip the whole check)."""
    project_config._cached_config = _stub_cfg()

    def boom(assetPath: str) -> dict[str, Any]:
        raise RuntimeError("bridge unreachable")

    with patch("tools.primitives.asset_exists", side_effect=boom):
        check = recipe_framework._validate_produces(["/Game/X"], {})
    assert check is None
    project_config._cached_config = None
