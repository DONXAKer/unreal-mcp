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
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

from mcp.server.fastmcp import FastMCP

from tools.project_config import get_config, resolve_project_root
from tools.result_format import fail, ok

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
    args: List[RecipeArg] = field(default_factory=list)
    produces: List[str] = field(default_factory=list)

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
    produces: Optional[List[str]] = None,
) -> Callable:
    """Mark a function as a content recipe discoverable by the framework.

    Args:
        name: Short tool name (namespaced at registration time).
        desc: One-line description surfaced to MCP clients.
        produces: Optional list of asset path templates this recipe creates
                  (e.g. ["{paths.textures}/T_CardArt_{card_id}"]). Consumed
                  by the contract-validator in MCP-CONTENT-006.
    """
    def deco(fn: Callable) -> Callable:
        staged_args: List[RecipeArg] = list(getattr(fn, _STAGED_ARGS_ATTR, []))
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
) -> Callable:
    """Declare one typed argument for a @recipe-decorated function.

    Stack multiple @arg(...) above @recipe(...) — evaluation order means
    decorators closest to the function run first; the framework reverses
    the accumulated list so the declared order is preserved.
    """
    def deco(fn: Callable) -> Callable:
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
        self.registered: Dict[str, RecipeSpec] = {}

    def clear(self) -> None:
        for qualified_name in list(self.registered.keys()):
            self._unregister_from_fastmcp(qualified_name)
        self.registered.clear()

    def _unregister_from_fastmcp(self, qualified_name: str) -> None:
        tool_manager = getattr(self.mcp, "_tool_manager", None)
        if tool_manager is None:
            return
        tools: Dict[str, Any] = getattr(tool_manager, "_tools", {})
        tools.pop(qualified_name, None)

    def register(self, spec: RecipeSpec, namespace: str) -> str:
        qualified = spec.qualified_name(namespace)
        self.registered[qualified] = spec
        self._wire_tool(qualified, spec)
        return qualified

    def _wire_tool(self, qualified: str, spec: RecipeSpec) -> None:
        arg_defs = spec.args

        def tool_impl(**kwargs: Any) -> Dict[str, Any]:
            try:
                call_kwargs: Dict[str, Any] = {}
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
                return result
            except Exception as e:  # noqa: BLE001 — surface anything to the caller.
                logger.exception("Recipe %s failed", qualified)
                return fail(
                    "ue_internal",
                    "RECIPE_EXCEPTION",
                    f"{type(e).__name__}: {e}",
                    recipe=qualified,
                )

        # FastMCP derives the tool schema from inspect.signature(fn), so we
        # must synthesize a real signature that matches the recipe's @arg
        # declarations — otherwise MCP clients see a tool with no arguments.
        params: List[inspect.Parameter] = []
        annotations: Dict[str, Any] = {}
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

        self.mcp.tool(name=qualified, description=spec.description)(tool_impl)


_registry: Optional[RecipeRegistry] = None


def init_registry(mcp: FastMCP) -> RecipeRegistry:
    """Initialize the singleton registry bound to this MCP server instance."""
    global _registry
    _registry = RecipeRegistry(mcp)
    return _registry


def get_registry() -> Optional[RecipeRegistry]:
    return _registry


def discover_recipes(recipes_dir: Path) -> List[RecipeSpec]:
    """Import every *.py under `recipes_dir` and collect @recipe specs."""
    if not recipes_dir.is_dir():
        logger.warning("Recipes dir does not exist: %s", recipes_dir)
        return []

    specs: List[RecipeSpec] = []
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
        except Exception:  # noqa: BLE001
            logger.exception("Failed importing recipes from %s", py_file)
            continue
        for _, member in inspect.getmembers(mod):
            rs: Optional[RecipeSpec] = getattr(member, _RECIPE_SPEC_ATTR, None)
            if rs is not None:
                specs.append(rs)
    return specs


def register_all_recipes() -> Tuple[int, List[str], List[str]]:
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
    registered: List[str] = []
    errors: List[str] = []
    for s in specs:
        try:
            qualified = _registry.register(s, cfg.namespace)
            registered.append(qualified)
        except Exception as e:  # noqa: BLE001
            logger.exception("Failed to register recipe %s", s.name)
            errors.append(f"{s.name}: {e}")
    return len(registered), registered, errors


def reload_recipes_impl() -> Dict[str, Any]:
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
