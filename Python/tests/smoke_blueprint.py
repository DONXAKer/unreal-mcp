"""Smoke test: end-to-end Blueprint authoring via UnrealMCP plugin.

Run:
    python -m tests.smoke_blueprint              # creates and cleans up
    python -m tests.smoke_blueprint --no-cleanup # leave assets for inspection

Steps:
    1. create_blueprint(BP_SmokeTest, Actor, /Game/Smoke)
    2. create_variable(Health: Float)
    3. set_variable_default_value(Health = 100.0)
    4. list_blueprint_variables — assert Health present
    5. add_component_to_blueprint(StaticMeshComponent "Mesh")
    6. list_components — assert Mesh present
    7. create_function(DoSmokeTest)
    8. add_event_node(ReceiveBeginPlay)
    9. compile_blueprint
   10. (cleanup) delete_blueprint_variable(Health)

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

BP_NAME = "BP_SmokeTest"
BP_FOLDER = "/Game/Smoke"


def _find_variable(result: dict[str, Any], var_name: str) -> bool:
    """list_blueprint_variables payload shape varies; do a tolerant scan."""
    if not isinstance(result, dict):
        return False
    variables = result.get("variables") or result.get("Variables") or []
    if isinstance(variables, list):
        for item in variables:
            if isinstance(item, dict):
                name = item.get("name") or item.get("Name") or item.get("variable_name")
            else:
                name = item
            if name == var_name:
                return True
    return False


def _find_component(result: dict[str, Any], comp_name: str) -> bool:
    if not isinstance(result, dict):
        return False
    components = result.get("components") or result.get("Components") or []
    if isinstance(components, list):
        for item in components:
            if isinstance(item, dict):
                name = item.get("name") or item.get("Name") or item.get("component_name")
            else:
                name = item
            if name == comp_name:
                return True
    return False


def main(argv: list[str]) -> int:
    no_cleanup = parse_no_cleanup(argv)

    def step_create_bp():
        resp = send_command("create_blueprint", {
            "name": BP_NAME,
            "parent_class": "Actor",
            "folder_path": BP_FOLDER,
        })
        assert_success(resp, 1, "create_blueprint")

    def step_create_var():
        resp = send_command("create_variable", {
            "blueprint_name": BP_NAME,
            "variable_name": "Health",
            "variable_type": "Float",
            "is_exposed": False,
        })
        assert_success(resp, 2, "create_variable")

    def step_set_default():
        resp = send_command("set_variable_default_value", {
            "blueprint_name": BP_NAME,
            "variable_name": "Health",
            "default_value": "100.0",
        })
        assert_success(resp, 3, "set_variable_default_value")

    def step_list_vars():
        resp = send_command("list_blueprint_variables", {"blueprint_name": BP_NAME})
        result = assert_success(resp, 4, "list_blueprint_variables")
        if not _find_variable(result, "Health"):
            raise SmokeFailure(4, "list_blueprint_variables", "Health variable not found in listing", resp)

    def step_add_component():
        resp = send_command("add_component_to_blueprint", {
            "blueprint_name": BP_NAME,
            "component_type": "StaticMeshComponent",
            "component_name": "Mesh",
        })
        assert_success(resp, 5, "add_component_to_blueprint")

    def step_list_components():
        resp = send_command("list_components", {"blueprint_name": BP_NAME})
        result = assert_success(resp, 6, "list_components")
        if not _find_component(result, "Mesh"):
            raise SmokeFailure(6, "list_components", "Mesh component not found in listing", resp)

    def step_create_function():
        resp = send_command("create_function", {
            "blueprint_name": BP_NAME,
            "function_name": "DoSmokeTest",
        })
        assert_success(resp, 7, "create_function")

    def step_add_event():
        resp = send_command("add_event_node", {
            "blueprint_name": BP_NAME,
            "event_name": "ReceiveBeginPlay",
        })
        assert_success(resp, 8, "add_event_node")

    def step_compile():
        resp = send_command("compile_blueprint", {"blueprint_name": BP_NAME})
        assert_success(resp, 9, "compile_blueprint")

    steps = [
        ("create_blueprint",            step_create_bp),
        ("create_variable",             step_create_var),
        ("set_variable_default_value",  step_set_default),
        ("list_blueprint_variables",    step_list_vars),
        ("add_component_to_blueprint",  step_add_component),
        ("list_components",             step_list_components),
        ("create_function",             step_create_function),
        ("add_event_node",              step_add_event),
        ("compile_blueprint",           step_compile),
    ]

    if not no_cleanup:
        def step_cleanup_var():
            resp = send_command("delete_blueprint_variable", {
                "blueprint_name": BP_NAME,
                "variable_name": "Health",
            })
            assert_success(resp, 10, "delete_blueprint_variable")

        steps.append(("delete_blueprint_variable (cleanup)", step_cleanup_var))

    return run_steps("smoke_blueprint", steps)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
