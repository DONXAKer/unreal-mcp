"""
Recipe framework for MCP Content Pipeline.

Users declare recipes in <ProjectRoot>/<recipesDir>/*.py via the @recipe
and @arg decorators. At startup (or explicit reload), the framework
discovers them, builds typed pydantic signatures, and registers each
as a first-class MCP tool under namespace "<projectNamespace>.<name>".

See MCP-CONTENT-001 task for design decisions.
"""

from __future__ import annotations

import importlib.util
import inspect
import logging
import re
import sys
from collections.abc import Callable
from contextvars import ContextVar
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP

from tools.project_config import ProjectConfig, get_config, resolve_project_root
from tools.result_format import fail, ok

# --- rollback journal -------------------------------------------------------

# When a recipe with rollback_on_failure=True is active, this contextvar holds
# a mutable list to which every successful asset-creating primitive call
# appends `{command, assetPath, status}`. On recipe failure the framework
# walks the journal in reverse and deletes `created` entries.
_rollback_journal: ContextVar[list[dict[str, Any]] | None] = ContextVar(
    "_mcp_rollback_journal", default=None
)


def _journal_record(command: str, response: dict[str, Any]) -> None:
    """Append the call to the active rollback journal if one is open.

    Safe to call from primitives.* unconditionally — exits cheaply when no
    rollback scope is active. Only records `status` in (created, overwritten);
    callers that already report `skipped` are no-ops to rollback.
    """
    journal = _rollback_journal.get()
    if journal is None:
        return
    if not isinstance(response, dict):
        return
    status = response.get("status")
    if status not in ("created", "overwritten"):
        return
    asset_path = response.get("assetPath")
    if not asset_path:
        meta = response.get("meta")
        if isinstance(meta, dict):
            asset_path = meta.get("assetPath")
    if not asset_path:
        return
    journal.append({"command": command, "assetPath": asset_path, "status": status})


def _perform_rollback(journal: list[dict[str, Any]]) -> dict[str, Any]:
    """Walk journal in reverse and delete every newly-created asset.

    Overwrites can't be undone (we don't take backups before write) — surfaced
    in `skipped` rather than silently dropped. Failures during delete are
    collected into `errors` and don't stop the loop.
    """
    try:
        from tools.primitives import delete_asset
    except Exception:
        return {"deleted": [], "skipped": [], "errors": [{"error": "primitives.delete_asset import failed"}]}

    deleted: list[str] = []
    skipped: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    for entry in reversed(journal):
        if entry["status"] == "overwritten":
            skipped.append(entry)
            continue
        try:
            r = delete_asset(assetPath=entry["assetPath"], ifMissing="skip")
        except Exception as e:
            errors.append({"asset": entry["assetPath"], "error": f"{type(e).__name__}: {e}"})
            continue
        if r.get("ok"):
            deleted.append(entry["assetPath"])
        else:
            err = r.get("error") if isinstance(r, dict) else None
            errors.append({"asset": entry["assetPath"], "error": err})
    return {"deleted": deleted, "skipped": skipped, "errors": errors}

logger = logging.getLogger("UnrealMCP")


@dataclass
class RecipeArg:
    name: str
    type_: type
    required: bool = True
    default: Any = None
    description: str = ""


@dataclass
class RecipeSpec:
    name: str
    description: str
    func: Callable[..., Any]
    args: list[RecipeArg] = field(default_factory=list)
    produces: list[str] = field(default_factory=list)
    rollback_on_failure: bool = False

    def qualified_name(self, namespace: str) -> str:
        return f"{namespace}.{self.name}"


# Module-level staging attributes applied by decorators, harvested by discovery.
_STAGED_ARGS_ATTR = "__mcp_recipe_args__"
_STAGED_PRODUCES_ATTR = "__mcp_recipe_produces__"
_RECIPE_SPEC_ATTR = "__mcp_recipe_spec__"


def recipe(
    *,
    name: str,
    desc: str = "",
    produces: list[str] | None = None,
    rollback_on_failure: bool = False,
) -> Callable[..., Any]:
    """Mark a function as a content recipe discoverable by the framework.

    Args:
        name: Short tool name (namespaced at registration time).
        desc: One-line description surfaced to MCP clients.
        produces: Optional list of asset path templates this recipe creates
                  (e.g. ["{paths.textures}/T_CardArt_{card_id}"]). Used by
                  the post-call produces validator.
        rollback_on_failure: When True, every primitive that creates a fresh
                  asset within the recipe scope is journaled. If the recipe
                  raises or returns ok=False, the framework walks the
                  journal in reverse and calls delete_asset for each created
                  entry. Overwrites are NOT reverted (we don't keep backups).
                  Surfaced under meta.rollback / error.details.rollback.
    """
    def deco(fn: Callable[..., Any]) -> Callable[..., Any]:
        staged_args: list[RecipeArg] = list(getattr(fn, _STAGED_ARGS_ATTR, []))
        if desc:
            description = desc
        elif fn.__doc__:
            doc_lines = fn.__doc__.strip().splitlines()
            description = doc_lines[0] if doc_lines else name
        else:
            description = name
        spec = RecipeSpec(
            name=name,
            description=description,
            func=fn,
            args=staged_args,
            produces=list(produces or getattr(fn, _STAGED_PRODUCES_ATTR, []) or []),
            rollback_on_failure=rollback_on_failure,
        )
        setattr(fn, _RECIPE_SPEC_ATTR, spec)
        return fn

    return deco


def arg(
    name: str,
    type_: type,
    required: bool = True,
    default: Any = None,
    description: str = "",
) -> Callable[..., Any]:
    """Declare one typed argument for a @recipe-decorated function.

    Stack multiple @arg(...) above @recipe(...) — evaluation order means
    decorators closest to the function run first; the framework reverses
    the accumulated list so the declared order is preserved.
    """
    def deco(fn: Callable[..., Any]) -> Callable[..., Any]:
        staged = list(getattr(fn, _STAGED_ARGS_ATTR, []))
        staged.insert(0, RecipeArg(
            name=name,
            type_=type_,
            required=required,
            default=default,
            description=description,
        ))
        setattr(fn, _STAGED_ARGS_ATTR, staged)
        return fn

    return deco


class RecipeRegistry:
    """Owns the set of discovered recipes and their MCP tool handles.

    Re-registration after reload_recipes() is supported by removing stale
    entries from FastMCP's internal tool map.
    """

    def __init__(self, mcp: FastMCP):
        self.mcp = mcp
        self.registered: dict[str, RecipeSpec] = {}

    def clear(self) -> None:
        for qualified_name in list(self.registered.keys()):
            self._unregister_from_fastmcp(qualified_name)
        self.registered.clear()

    def _unregister_from_fastmcp(self, qualified_name: str) -> None:
        tool_manager = getattr(self.mcp, "_tool_manager", None)
        if tool_manager is None:
            return
        tools: dict[str, Any] = getattr(tool_manager, "_tools", {})
        tools.pop(qualified_name, None)

    def register(self, spec: RecipeSpec, namespace: str) -> str:
        qualified = spec.qualified_name(namespace)
        self.registered[qualified] = spec
        self._wire_tool(qualified, spec)
        return qualified

    def _wire_tool(self, qualified: str, spec: RecipeSpec) -> None:
        arg_defs = spec.args

        def tool_impl(**kwargs: Any) -> dict[str, Any]:
            # Open a rollback journal when the recipe opts in. ContextVars
            # are coroutine-safe so concurrent MCP calls each see their own
            # journal, never cross-contaminate.
            journal: list[dict[str, Any]] | None = (
                [] if spec.rollback_on_failure else None
            )
            token = _rollback_journal.set(journal)
            try:
                call_kwargs: dict[str, Any] = {}
                for a in arg_defs:
                    if a.name in kwargs:
                        call_kwargs[a.name] = kwargs[a.name]
                    elif a.required:
                        return fail(
                            "user",
                            "MISSING_ARG",
                            f"Recipe {qualified} missing required arg '{a.name}'",
                        )
                    else:
                        call_kwargs[a.name] = a.default
                result = spec.func(**call_kwargs)
                if not isinstance(result, dict):
                    return ok("created", "", raw=result)
                # Best-effort produces validation — only on success, never fails
                # the recipe call. Result is surfaced under meta.produces_check.
                if spec.produces and result.get("ok") is not False:
                    check = _validate_produces(spec.produces, call_kwargs)
                    if check is not None:
                        meta = result.setdefault("meta", {})
                        if isinstance(meta, dict):
                            meta["produces_check"] = check
                # Recipe reported failure → rollback what we journaled.
                if (
                    journal is not None
                    and journal
                    and result.get("ok") is False
                ):
                    summary = _perform_rollback(journal)
                    err = result.get("error")
                    if isinstance(err, dict):
                        err.setdefault("details", {})
                        if isinstance(err["details"], dict):
                            err["details"]["rollback"] = summary
                return result
            except Exception as e:
                logger.exception("Recipe %s failed", qualified)
                fail_resp = fail(
                    "ue_internal",
                    "RECIPE_EXCEPTION",
                    f"{type(e).__name__}: {e}",
                    recipe=qualified,
                )
                if journal is not None and journal:
                    summary = _perform_rollback(journal)
                    details = fail_resp["error"]["details"]
                    if isinstance(details, dict):
                        details["rollback"] = summary
                return fail_resp
            finally:
                _rollback_journal.reset(token)

        # FastMCP derives the tool schema from inspect.signature(fn), so we
        # must synthesize a real signature that matches the recipe's @arg
        # declarations — otherwise MCP clients see a tool with no arguments.
        params: list[inspect.Parameter] = []
        annotations: dict[str, Any] = {}
        for a in arg_defs:
            if a.required:
                params.append(
                    inspect.Parameter(
                        a.name,
                        inspect.Parameter.KEYWORD_ONLY,
                        annotation=a.type_,
                    )
                )
            else:
                params.append(
                    inspect.Parameter(
                        a.name,
                        inspect.Parameter.KEYWORD_ONLY,
                        annotation=a.type_,
                        default=a.default,
                    )
                )
            annotations[a.name] = a.type_
        annotations["return"] = dict

        tool_impl.__signature__ = inspect.Signature(parameters=params)  # type: ignore[attr-defined]
        tool_impl.__annotations__ = annotations
        tool_impl.__name__ = qualified.replace(".", "_")
        tool_impl.__doc__ = spec.description

        # FastMCP's ToolManager.add_tool silently keeps the existing entry if
        # the name is already registered, so hot reload wouldn't replace the
        # captured closure. Pop first, then register.
        tool_manager = getattr(self.mcp, "_tool_manager", None)
        if tool_manager is not None:
            store = getattr(tool_manager, "_tools", None)
            if isinstance(store, dict):
                store.pop(qualified, None)

        self.mcp.tool(name=qualified, description=spec.description)(tool_impl)


# --- produces[] template resolution + asset-existence validation ------------

class _SafeDict(dict[str, Any]):
    """str.format_map() helper: leaves `{unknown}` placeholders intact instead of
    raising KeyError so partially-resolved templates survive."""

    def __missing__(self, key: str) -> str:
        return "{" + key + "}"


def _resolve_template(template: str, cfg: ProjectConfig, args: dict[str, Any]) -> str:
    """Expand {paths.X} / {naming.X} / {assetRoot} / {arg_name} placeholders.

    Unknown placeholders survive unchanged — they're a soft contract, not a
    hard one, so a partially-resolved template still gives a meaningful
    diagnostic in produces_check.missing.
    """

    def _paths(m: re.Match[str]) -> str:
        return str(getattr(cfg.paths, m.group(1), m.group(0)))

    def _naming(m: re.Match[str]) -> str:
        return str(getattr(cfg.naming, m.group(1), m.group(0)))

    template = re.sub(r"\{paths\.(\w+)\}", _paths, template)
    template = re.sub(r"\{naming\.(\w+)\}", _naming, template)
    template = template.replace("{assetRoot}", cfg.asset_root)
    try:
        template = template.format_map(_SafeDict(**args))
    except Exception:
        # Unrecoverable format error — return what we have so far so the
        # missing[] entry still points to *something* the caller can act on.
        pass
    return template


def _validate_produces(
    produces: list[str],
    call_kwargs: dict[str, Any],
) -> dict[str, Any] | None:
    """Best-effort check: each produces template must resolve to an asset that
    exists in the registry after the recipe finishes.

    Returns `{checked, missing, resolved, status}` for surfacing under
    `meta.produces_check`, or None if validation must be skipped (no config,
    bridge unreachable, etc.) — never raises, never fails the recipe.
    """
    cfg = get_config()
    if cfg is None:
        return None

    try:
        from tools.primitives import asset_exists
    except Exception:
        logger.warning("produces_check skipped: primitives.asset_exists import failed")
        return None

    resolved: list[str] = []
    missing: list[str] = []
    for template in produces:
        try:
            path = _resolve_template(template, cfg, call_kwargs)
        except Exception:
            logger.exception("produces_check: failed to resolve template %s", template)
            continue
        resolved.append(path)
        # `{...}` left in path → template unresolved; treat as missing.
        if "{" in path or "}" in path:
            missing.append(path)
            continue
        try:
            r = asset_exists(assetPath=path)
        except Exception:
            logger.warning("produces_check: bridge unreachable for %s", path)
            return None
        if r.get("ok") is False:
            # Bridge errored on this check; surface as missing rather than
            # silently swallowing.
            missing.append(path)
            continue
        if not r.get("meta", {}).get("exists", False):
            missing.append(path)

    return {
        "status": "ok" if not missing else "missing",
        "checked": len(resolved),
        "resolved": resolved,
        "missing": missing,
    }


_registry: RecipeRegistry | None = None


def init_registry(mcp: FastMCP) -> RecipeRegistry:
    """Initialize the singleton registry bound to this MCP server instance."""
    global _registry
    _registry = RecipeRegistry(mcp)
    return _registry


def get_registry() -> RecipeRegistry | None:
    return _registry


def discover_recipes(recipes_dir: Path) -> list[RecipeSpec]:
    """Import every *.py under `recipes_dir` and collect @recipe specs."""
    if not recipes_dir.is_dir():
        logger.warning("Recipes dir does not exist: %s", recipes_dir)
        return []

    specs: list[RecipeSpec] = []
    for py_file in sorted(recipes_dir.glob("*.py")):
        if py_file.name.startswith("_"):
            continue
        module_name = f"_mcp_recipes.{py_file.stem}"
        spec = importlib.util.spec_from_file_location(module_name, py_file)
        if spec is None or spec.loader is None:
            logger.warning("Could not load recipe module %s", py_file)
            continue
        mod = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = mod
        try:
            spec.loader.exec_module(mod)
        except Exception:
            logger.exception("Failed importing recipes from %s", py_file)
            continue
        for _, member in inspect.getmembers(mod):
            rs: RecipeSpec | None = getattr(member, _RECIPE_SPEC_ATTR, None)
            if rs is not None:
                specs.append(rs)
    return specs


def register_all_recipes() -> tuple[int, list[str], list[str]]:
    """Discover and register every recipe under the configured recipesDir.

    Returns: (count_registered, registered_names, errors).
    """
    if _registry is None:
        return 0, [], ["registry-not-initialized"]

    cfg = get_config()
    if cfg is None:
        return 0, [], ["no-project-config"]

    root = resolve_project_root()
    if root is None:
        return 0, [], ["no-project-root"]

    recipes_dir = (root / cfg.recipes_dir).resolve()
    _registry.clear()
    specs = discover_recipes(recipes_dir)
    registered: list[str] = []
    errors: list[str] = []
    for s in specs:
        try:
            qualified = _registry.register(s, cfg.namespace)
            registered.append(qualified)
        except Exception as e:
            logger.exception("Failed to register recipe %s", s.name)
            errors.append(f"{s.name}: {e}")
    return len(registered), registered, errors


def reload_recipes_impl() -> dict[str, Any]:
    """MCP-exposed hot reload of recipes. Returns unified-format result."""
    count, names, errors = register_all_recipes()
    if errors and count == 0:
        return fail("config", "NO_RECIPES_REGISTERED", "; ".join(errors))
    return ok(
        "updated",
        "",
        count=count,
        registered=names,
        errors=errors,
    )


def list_recipes_impl() -> dict[str, Any]:
    """MCP-exposed introspection: full metadata for every registered recipe.

    Returns each recipe's qualified name, description, declared args (with
    type + required flag + default), and `produces[]` templates. Used by
    MCP clients (and contract-validators) to discover the recipe surface
    without re-running discovery.
    """
    if _registry is None:
        return fail("config", "REGISTRY_NOT_INITIALIZED", "Recipe registry not initialized")

    recipes: list[dict[str, Any]] = []
    for qualified, spec in sorted(_registry.registered.items()):
        recipes.append(
            {
                "name": qualified,
                "description": spec.description,
                "args": [
                    {
                        "name": a.name,
                        "type": a.type_.__name__,
                        "required": a.required,
                        "default": a.default,
                        "description": a.description,
                    }
                    for a in spec.args
                ],
                "produces": list(spec.produces),
            }
        )
    return ok("updated", "", count=len(recipes), recipes=recipes)
