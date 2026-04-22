"""
Test runner helper for wc.run_tests meta-recipe.

discover_test_modules(dir) scans the given directory for test_*.py files,
imports each, and wraps it in a unified run() interface.
"""

from __future__ import annotations

import importlib.util
import logging
import sys
import time
from pathlib import Path
from typing import Any, Dict, List

logger = logging.getLogger("UnrealMCP")


class _TestModule:
    def __init__(self, name: str, path: Path):
        self.name = name
        self.path = path
        self._mod = None

    def _load(self):
        if self._mod is not None:
            return
        mod_name = f"_mcp_tests.{self.name}"
        spec = importlib.util.spec_from_file_location(mod_name, self.path)
        if spec is None or spec.loader is None:
            raise ImportError(f"Cannot load {self.path}")
        mod = importlib.util.module_from_spec(spec)
        sys.modules[mod_name] = mod
        spec.loader.exec_module(mod)
        self._mod = mod

    def run(self) -> Dict[str, Any]:
        start = time.monotonic()
        try:
            self._load()
            if not hasattr(self._mod, "run"):
                return {
                    "ok": False,
                    "name": self.name,
                    "duration_ms": 0,
                    "error": f"{self.path.name} has no run() function",
                }
            result = self._mod.run()
            duration_ms = int((time.monotonic() - start) * 1000)
            if not isinstance(result, dict):
                result = {"ok": bool(result)}
            result.setdefault("name", self.name)
            result.setdefault("duration_ms", duration_ms)
            return result
        except Exception as exc:  # noqa: BLE001
            duration_ms = int((time.monotonic() - start) * 1000)
            logger.exception("Test %s raised an exception", self.name)
            return {
                "ok": False,
                "name": self.name,
                "duration_ms": duration_ms,
                "error": f"{type(exc).__name__}: {exc}",
            }


def discover_test_modules(tests_dir: str) -> List[_TestModule]:
    """Return a list of _TestModule wrappers for every test_*.py in tests_dir."""
    root = Path(tests_dir)
    if not root.is_dir():
        # Try resolving relative to this file's parent (Python/)
        alt = Path(__file__).resolve().parent.parent / tests_dir
        if alt.is_dir():
            root = alt
        else:
            logger.warning("Test dir not found: %s", tests_dir)
            return []

    modules = []
    for py_file in sorted(root.glob("test_*.py")):
        modules.append(_TestModule(py_file.stem, py_file))
    return modules
