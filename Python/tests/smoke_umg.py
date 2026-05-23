"""Smoke test: UMG Widget Blueprint authoring.

Run:
    python -m tests.smoke_umg              # default, asset stays in /Game/Smoke
    python -m tests.smoke_umg --no-cleanup # alias — UMG smoke does not clean up

Steps:
    1. create_umg_widget_blueprint(WBP_SmokeTest, /Game/Smoke)
    2. add_panel_widget_to_widget(CanvasPanel "RootCanvas")
    3. add_text_block_to_widget(parent=RootCanvas, widget=Label, "Hello")
    4. add_button_to_widget(parent=RootCanvas, widget=Btn)
    5. get_umg_hierarchy — assert Label and Btn present

Exit 0 on success, 1 on any failure.
"""

from __future__ import annotations

import sys
from typing import Any

from tests._smoke_common import (
    SmokeFailure,
    assert_success,
    parse_no_cleanup,
    run_steps,
    send_command,
)

WIDGET_NAME = "WBP_SmokeTest"
WIDGET_PATH = "/Game/Smoke"


def _hierarchy_contains(payload: dict[str, Any], target_name: str) -> bool:
    """Tolerant DFS over a UMG hierarchy payload looking for a widget by name."""
    if isinstance(payload, dict):
        name = payload.get("name") or payload.get("Name") or payload.get("widget_name")
        if name == target_name:
            return True
        for value in payload.values():
            if _hierarchy_contains(value, target_name):
                return True
    elif isinstance(payload, list):
        for item in payload:
            if _hierarchy_contains(item, target_name):
                return True
    return False


def main(argv: list[str]) -> int:
    _no_cleanup = parse_no_cleanup(argv)

    def step_create():
        resp = send_command("create_umg_widget_blueprint", {
            "widget_name": WIDGET_NAME,
            "path": WIDGET_PATH,
        })
        assert_success(resp, 1, "create_umg_widget_blueprint")

    def step_panel():
        resp = send_command("add_panel_widget_to_widget", {
            "blueprint_name": WIDGET_NAME,
            "parent_name": None,
            "widget_type": "CanvasPanel",
            "widget_name": "RootCanvas",
        })
        assert_success(resp, 2, "add_panel_widget_to_widget")

    def step_text():
        resp = send_command("add_text_block_to_widget", {
            "blueprint_name": WIDGET_NAME,
            "parent_widget": "RootCanvas",
            "widget_name": "Label",
            "text": "Hello",
        })
        assert_success(resp, 3, "add_text_block_to_widget")

    def step_button():
        resp = send_command("add_button_to_widget", {
            "blueprint_name": WIDGET_NAME,
            "parent_widget": "RootCanvas",
            "widget_name": "Btn",
        })
        assert_success(resp, 4, "add_button_to_widget")

    def step_hierarchy():
        resp = send_command("get_umg_hierarchy", {"blueprint_name": WIDGET_NAME})
        result = assert_success(resp, 5, "get_umg_hierarchy")
        if not _hierarchy_contains(result, "Label"):
            raise SmokeFailure(5, "get_umg_hierarchy", "Label not found in hierarchy", resp)
        if not _hierarchy_contains(result, "Btn"):
            raise SmokeFailure(5, "get_umg_hierarchy", "Btn not found in hierarchy", resp)

    steps = [
        ("create_umg_widget_blueprint", step_create),
        ("add_panel_widget_to_widget",  step_panel),
        ("add_text_block_to_widget",    step_text),
        ("add_button_to_widget",        step_button),
        ("get_umg_hierarchy",           step_hierarchy),
    ]
    return run_steps("smoke_umg", steps)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
