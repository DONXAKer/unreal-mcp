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


# --- rollback ---------------------------------------------------------------

def test_journal_record_outside_recipe_is_noop() -> None:
    """No active scope → calling _journal_record never touches global state."""
    recipe_framework._rollback_journal.set(None)
    recipe_framework._journal_record("create_blueprint", {"status": "created", "assetPath": "/Game/X"})
    assert recipe_framework._rollback_journal.get() is None


def test_journal_record_captures_created_and_overwritten() -> None:
    """Only created/overwritten with assetPath land in the journal."""
    journal: list[dict[str, Any]] = []
    token = recipe_framework._rollback_journal.set(journal)
    try:
        recipe_framework._journal_record("a", {"status": "created", "assetPath": "/Game/A"})
        recipe_framework._journal_record("b", {"status": "overwritten", "assetPath": "/Game/B"})
        recipe_framework._journal_record("c", {"status": "skipped", "assetPath": "/Game/C"})
        recipe_framework._journal_record("d", {"status": "created"})  # no assetPath
        recipe_framework._journal_record("e", "not-a-dict")  # type: ignore[arg-type]
    finally:
        recipe_framework._rollback_journal.reset(token)
    assert journal == [
        {"command": "a", "assetPath": "/Game/A", "status": "created"},
        {"command": "b", "assetPath": "/Game/B", "status": "overwritten"},
    ]


def test_perform_rollback_deletes_created_skips_overwritten() -> None:
    """Walk journal in reverse: delete created assets, skip overwritten ones."""
    journal = [
        {"command": "create_blueprint", "assetPath": "/Game/A", "status": "created"},
        {"command": "set_datatable_row", "assetPath": "/Game/B", "status": "overwritten"},
        {"command": "create_material_instance", "assetPath": "/Game/C", "status": "created"},
    ]
    deleted_order: list[str] = []

    def fake_delete(*, assetPath: str, ifMissing: str = "fail") -> dict[str, Any]:
        deleted_order.append(assetPath)
        return ok("updated", assetPath)

    with patch("tools.primitives.delete_asset", side_effect=fake_delete):
        summary = recipe_framework._perform_rollback(journal)

    # Reverse order: C first, then A. B (overwritten) skipped.
    assert deleted_order == ["/Game/C", "/Game/A"]
    assert summary["deleted"] == ["/Game/C", "/Game/A"]
    assert summary["errors"] == []
    assert [s["assetPath"] for s in summary["skipped"]] == ["/Game/B"]


def test_perform_rollback_collects_errors() -> None:
    """delete_asset raising / returning fail goes into errors[], loop continues."""
    journal = [
        {"command": "x", "assetPath": "/Game/Good", "status": "created"},
        {"command": "y", "assetPath": "/Game/Bad", "status": "created"},
    ]

    def fake_delete(*, assetPath: str, ifMissing: str = "fail") -> dict[str, Any]:
        if assetPath == "/Game/Bad":
            raise RuntimeError("boom")
        return ok("updated", assetPath)

    with patch("tools.primitives.delete_asset", side_effect=fake_delete):
        summary = recipe_framework._perform_rollback(journal)

    assert summary["deleted"] == ["/Game/Good"]
    assert len(summary["errors"]) == 1
    assert summary["errors"][0]["asset"] == "/Game/Bad"
    assert "boom" in summary["errors"][0]["error"]


def test_recipe_rollback_on_exception_triggers_journal_walk() -> None:
    """Recipe with rollback_on_failure=True raises → rollback runs, summary in
    error.details.rollback."""
    server = _fresh_registry()

    @recipe_framework.recipe(name="bad_recipe", desc="will fail", rollback_on_failure=True)
    def bad_recipe() -> dict[str, Any]:
        # Simulate two successful primitive calls before the recipe blows up.
        recipe_framework._journal_record(
            "create_blueprint",
            {"status": "created", "assetPath": "/Game/Half_BP"},
        )
        recipe_framework._journal_record(
            "create_material_instance",
            {"status": "created", "assetPath": "/Game/Half_MI"},
        )
        raise RuntimeError("midway crash")

    spec = getattr(bad_recipe, recipe_framework._RECIPE_SPEC_ATTR)
    recipe_framework._registry.register(spec, namespace="wc")  # type: ignore[union-attr]
    tool = server._tool_manager._tools["wc.bad_recipe"]

    deleted: list[str] = []

    def fake_delete(*, assetPath: str, ifMissing: str = "fail") -> dict[str, Any]:
        deleted.append(assetPath)
        return ok("updated", assetPath)

    with patch("tools.primitives.delete_asset", side_effect=fake_delete):
        out = tool.fn()

    assert out["ok"] is False
    assert out["error"]["code"] == "RECIPE_EXCEPTION"
    assert "midway crash" in out["error"]["message"]
    rb = out["error"]["details"]["rollback"]
    # Reverse: MI deleted first, then BP.
    assert deleted == ["/Game/Half_MI", "/Game/Half_BP"]
    assert rb["deleted"] == ["/Game/Half_MI", "/Game/Half_BP"]


def test_recipe_rollback_skipped_when_opt_out() -> None:
    """rollback_on_failure=False (default) → recipe failure leaves journal empty."""
    _fresh_registry()

    @recipe_framework.recipe(name="bad_no_rollback", desc="no opt-in")
    def bad_no_rollback() -> dict[str, Any]:
        recipe_framework._journal_record(
            "create_blueprint",
            {"status": "created", "assetPath": "/Game/Leaked"},
        )
        raise RuntimeError("crash")

    spec = getattr(bad_no_rollback, recipe_framework._RECIPE_SPEC_ATTR)
    recipe_framework._registry.register(spec, namespace="wc")  # type: ignore[union-attr]

    delete_called = False

    def fake_delete(*, assetPath: str, ifMissing: str = "fail") -> dict[str, Any]:
        nonlocal delete_called
        delete_called = True
        return ok("updated", assetPath)

    with patch("tools.primitives.delete_asset", side_effect=fake_delete):
        # _journal_record was a no-op because journal contextvar was None
        # (rollback_on_failure default). So delete_asset is never invoked.
        server_mcp = recipe_framework._registry.mcp  # type: ignore[union-attr]
        wrapped = server_mcp._tool_manager._tools["wc.bad_no_rollback"].fn
        out = wrapped()
    assert out["ok"] is False
    assert delete_called is False
    assert "rollback" not in out.get("error", {}).get("details", {})


def test_recipe_rollback_on_ok_false_result() -> None:
    """Recipe returns ok=False (no exception) → rollback still runs."""
    server = _fresh_registry()

    @recipe_framework.recipe(name="soft_fail", desc="ok=false path", rollback_on_failure=True)
    def soft_fail() -> dict[str, Any]:
        recipe_framework._journal_record(
            "create_blueprint",
            {"status": "created", "assetPath": "/Game/SF_BP"},
        )
        return {"ok": False, "error": {"category": "ue_internal", "code": "X", "message": "soft", "details": {}}}

    spec = getattr(soft_fail, recipe_framework._RECIPE_SPEC_ATTR)
    recipe_framework._registry.register(spec, namespace="wc")  # type: ignore[union-attr]
    tool = server._tool_manager._tools["wc.soft_fail"]

    deleted: list[str] = []

    def fake_delete(*, assetPath: str, ifMissing: str = "fail") -> dict[str, Any]:
        deleted.append(assetPath)
        return ok("updated", assetPath)

    with patch("tools.primitives.delete_asset", side_effect=fake_delete):
        out = tool.fn()

    assert out["ok"] is False
    assert deleted == ["/Game/SF_BP"]
    assert out["error"]["details"]["rollback"]["deleted"] == ["/Game/SF_BP"]
