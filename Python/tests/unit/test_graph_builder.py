"""
Unit tests for tools/graph_builder — _validate_spec + build_blueprint_graph_impl.

No live Unreal Editor required. All unreal.send_command calls are mocked.
Run with: uv run pytest tests/unit/test_graph_builder.py -v
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any
from unittest.mock import MagicMock, call

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.graph_builder import (
    _validate_spec,
    build_blueprint_graph_impl,
)

BP = "/Game/UI/Mulligan/WBP_Mulligan"


# ─── Helpers ─────────────────────────────────────────────────────────────────

def _mock_unreal(*responses: dict[str, Any]) -> MagicMock:
    """Return a mock whose send_command yields responses in order."""
    m = MagicMock()
    m.send_command.side_effect = list(responses)
    return m


def _ok_node(node_id: str = "node-001") -> dict[str, Any]:
    return {"success": True, "node_id": node_id}


def _fail(msg: str = "err") -> dict[str, Any]:
    return {"success": False, "message": msg}


# ─── _validate_spec ────────────────────────────────────────────────────────


def test_validate_empty_spec_errors() -> None:
    errors = _validate_spec({})
    codes = {e.get("code") for e in errors}
    assert "missing_blueprint_path" in codes
    assert "missing_nodes" in codes


def test_validate_missing_blueprint_path() -> None:
    spec = {"nodes": [{"id": "n1", "type": "Event", "event_name": "BeginPlay"}]}
    errors = _validate_spec(spec)
    assert any(e.get("code") == "missing_blueprint_path" for e in errors)


def test_validate_unsupported_node_type() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "MagicNode"}],
    }
    errors = _validate_spec(spec)
    assert any(e.get("code") == "unsupported_type" and e.get("node") == "n1"
               for e in errors)


def test_validate_duplicate_node_id() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [
            {"id": "n1", "type": "Branch"},
            {"id": "n1", "type": "Branch"},
        ],
    }
    errors = _validate_spec(spec)
    assert any(e.get("code") == "duplicate_id" for e in errors)


def test_validate_connection_unknown_node_ref() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [{"from": "ghost.then", "to": "n1.exec"}],
    }
    errors = _validate_spec(spec)
    assert any(e.get("code") == "unknown_node_ref" for e in errors)


def test_validate_bad_pin_ref_format() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [{"from": "noDot", "to": "n1.exec"}],
    }
    errors = _validate_spec(spec)
    assert any(e.get("code") == "bad_pin_ref" for e in errors)


def test_validate_valid_spec_no_errors() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [
            {"id": "ev", "type": "Event", "event_name": "ReceiveBeginPlay"},
            {"id": "br", "type": "Branch"},
        ],
        "connections": [{"from": "ev.then", "to": "br.exec"}],
    }
    assert _validate_spec(spec) == []


def test_validate_existing_nodes_allows_connection_to_them() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "existing_nodes": {"pre": "GUID-000"},
        "connections": [{"from": "pre.return", "to": "n1.condition"}],
    }
    assert _validate_spec(spec) == []


# ─── build_blueprint_graph_impl — node creation ──────────────────────────────


def _single_node_spec(node_type: str, extra: dict = None) -> dict[str, Any]:
    node = {"id": "n1", "type": node_type}
    if extra:
        node.update(extra)
    return {
        "blueprint_path": BP,
        "nodes": [node],
        "connections": [],
        "compile": False,
    }


@pytest.mark.parametrize("node_type,extra,expected_cmd", [
    ("Event",       {"event_name": "ReceiveBeginPlay"}, "add_event_node"),
    ("FunctionCall", {"function_name": "GetAllChildren", "target": "self"}, "add_blueprint_node"),
    ("VariableGet", {"var_name": "MyVar"}, "add_blueprint_node"),
    ("VariableSet", {"var_name": "MyVar"}, "add_blueprint_node"),
    ("Branch",      {},                    "add_blueprint_node"),
    ("ForEachLoop", {},                    "add_blueprint_node"),
    ("DynamicCast", {"target_class": "WBP_CardWidget"}, "add_blueprint_node"),
    ("BindEvent",   {"component": "Button", "delegate": "OnClicked", "delegate_class": ""}, "add_component_bound_event"),
    ("CustomEvent", {"event_name": "OnCardSelected"}, "create_custom_event"),
])
def test_node_type_dispatches_correct_command(node_type, extra, expected_cmd) -> None:
    spec = _single_node_spec(node_type, extra)
    unreal = _mock_unreal(_ok_node("node-abc"))
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True
    assert result["nodes_created"] == 1
    assert "node-abc" in result["node_id_map"].values()
    # Verify the correct primitive was called
    first_call_cmd = unreal.send_command.call_args_list[0][0][0]
    assert first_call_cmd == expected_cmd, (
        f"For {node_type}: expected command '{expected_cmd}', got '{first_call_cmd}'"
    )


def test_custom_event_with_inputs_calls_add_input() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "ce", "type": "CustomEvent",
                   "event_name": "OnCardSelected",
                   "inputs": [{"name": "CardId", "type": "string"}]}],
        "connections": [],
        "compile": False,
    }
    unreal = MagicMock()
    unreal.send_command.return_value = _ok_node("node-ce")
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True
    calls = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "create_custom_event" in calls
    assert "add_custom_event_input" in calls


# ─── build_blueprint_graph_impl — connections ────────────────────────────────


def test_connections_built_after_nodes() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [
            {"id": "ev", "type": "Event", "event_name": "ReceiveBeginPlay"},
            {"id": "br", "type": "Branch"},
        ],
        "connections": [{"from": "ev.then", "to": "br.exec"}],
        "compile": False,
    }
    unreal = _mock_unreal(_ok_node("node-1"), _ok_node("node-2"), {"success": True})
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True
    assert result["nodes_created"] == 2
    assert result["connections_made"] == 1
    # 3rd call should be connect_nodes
    third_cmd = unreal.send_command.call_args_list[2][0][0]
    assert third_cmd == "connect_nodes"


# ─── build_blueprint_graph_impl — rollback ────────────────────────────────────


def test_rollback_on_node_creation_failure() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [
            {"id": "n1", "type": "Branch"},
            {"id": "n2", "type": "Branch"},  # this one fails
        ],
        "connections": [],
        "compile": False,
        "rollback_on_failure": True,
    }
    # n1 succeeds, n2 fails, then rollback calls delete_node for n1
    unreal = _mock_unreal(
        _ok_node("node-1"),   # n1 create: OK
        _fail("oops"),        # n2 create: FAIL → trigger rollback
        {"success": True},    # delete n1 in rollback
    )
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is False
    assert result["phase"] == "create"
    assert result["fail_at"] == "n2"
    assert result["rolled_back"] is True
    # Rollback should have issued delete_node
    cmds = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "delete_node" in cmds


def test_no_rollback_when_disabled() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [],
        "compile": False,
        "rollback_on_failure": False,
    }
    unreal = _mock_unreal(_fail("intentional"))
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is False
    assert result["rolled_back"] is False
    # No delete_node called since rollback disabled
    cmds = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "delete_node" not in cmds


def test_rollback_on_connect_failure() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [
            {"id": "n1", "type": "Branch"},
            {"id": "n2", "type": "Branch"},
        ],
        "connections": [{"from": "n1.then", "to": "n2.exec"}],
        "compile": False,
    }
    unreal = _mock_unreal(
        _ok_node("node-1"),     # n1 create OK
        _ok_node("node-2"),     # n2 create OK
        _fail("pin not found"), # connect FAIL → rollback
        {"success": True},      # disconnect n1->n2 (nothing yet)
        {"success": True},      # delete node-2
        {"success": True},      # delete node-1
    )
    result = build_blueprint_graph_impl(unreal, spec)
    assert result["success"] is False
    assert result["phase"] == "connect"


# ─── build_blueprint_graph_impl — defaults ───────────────────────────────────


def test_defaults_set_after_connections() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [],
        "defaults": [{"pin": "n1.condition", "value": "true"}],
        "compile": False,
    }
    unreal = _mock_unreal(_ok_node("node-1"), {"success": True})
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True
    assert result["defaults_set"] == 1
    cmds = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "set_pin_default_value" in cmds


# ─── build_blueprint_graph_impl — compile ─────────────────────────────────────


def test_compile_called_by_default() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [],
    }
    unreal = _mock_unreal(_ok_node("node-1"), {"success": True})
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["compile_result"] == "ok"
    cmds = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "compile_blueprint" in cmds


def test_compile_skipped_when_disabled() -> None:
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [],
        "compile": False,
    }
    unreal = _mock_unreal(_ok_node("node-1"))
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["compile_result"] == "skipped"
    cmds = [c[0][0] for c in unreal.send_command.call_args_list]
    assert "compile_blueprint" not in cmds


def test_compile_failure_reported_in_result_success_true() -> None:
    """Compile failure → compile_result=failed, but success=True (graph was built)."""
    spec = {
        "blueprint_path": BP,
        "nodes": [{"id": "n1", "type": "Branch"}],
        "connections": [],
        "compile": True,
    }
    unreal = _mock_unreal(_ok_node("node-1"), _fail("orphan node detected"))
    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True
    assert result["compile_result"] == "failed"
    assert len(result["compile_errors"]) > 0


# ─── Integration scenario: MULLIGAN visual highlight chain ────────────────────


def test_mulligan_visual_highlight_chain() -> None:
    """
    Reference scenario from FEAT-MULLIGAN-002:
    OnCardDisplayUpdated(CardData) → ForEachLoop(Cards) → GetWidgetAt(index)
    → DynamicCast(WBP_CardWidget) → SetIsSelected(true).
    Five nodes, four connections. compile=False.
    """
    spec = {
        "blueprint_path": "/Game/UI/Mulligan/WBP_Mulligan",
        "graph": "EventGraph",
        "nodes": [
            {"id": "ev",   "type": "Event",      "event_name": "OnCardDisplayUpdated"},
            {"id": "loop", "type": "ForEachLoop"},
            {"id": "get",  "type": "FunctionCall", "function_name": "GetChildAt", "target": "self"},
            {"id": "cast", "type": "DynamicCast", "target_class": "WBP_CardWidget"},
            {"id": "sel",  "type": "FunctionCall", "function_name": "SetIsSelected", "target": "self"},
        ],
        "connections": [
            {"from": "ev.then",         "to": "loop.exec"},
            {"from": "loop.loopbody",   "to": "get.exec"},
            {"from": "get.return",      "to": "cast.object"},
            {"from": "cast.success",    "to": "sel.exec"},
        ],
        "compile": False,
    }

    def _create_resp(cmd, params):
        node_map = {
            "add_event_node":     "guid-ev",
            "add_blueprint_node": None,  # dynamic
            "connect_nodes":      {"success": True},
        }
        if cmd == "add_event_node":
            return {"success": True, "node_id": "guid-ev"}
        if cmd == "add_blueprint_node":
            ntype = params.get("node_type", "")
            ids = {"ForEachLoop": "guid-loop", "CallFunction": "guid-fn",
                   "DynamicCast": "guid-cast"}
            # ForEachLoop, then two FunctionCall, then DynamicCast
            return {"success": True, "node_id": ids.get(ntype, "guid-fn2")}
        if cmd == "connect_nodes":
            return {"success": True}
        return {"success": False, "message": f"unexpected cmd {cmd}"}

    unreal = MagicMock()
    # Supply responses for 5 creates + 4 connects
    unreal.send_command.side_effect = [
        {"success": True, "node_id": "guid-ev"},    # Event
        {"success": True, "node_id": "guid-loop"},  # ForEachLoop
        {"success": True, "node_id": "guid-get"},   # FunctionCall GetChildAt
        {"success": True, "node_id": "guid-cast"},  # DynamicCast
        {"success": True, "node_id": "guid-sel"},   # FunctionCall SetIsSelected
        {"success": True},  # connect ev → loop
        {"success": True},  # connect loop → get
        {"success": True},  # connect get → cast
        {"success": True},  # connect cast → sel
    ]

    result = build_blueprint_graph_impl(unreal, spec)

    assert result["success"] is True, f"Expected success, got: {result}"
    assert result["nodes_created"] == 5
    assert result["connections_made"] == 4
    assert result["compile_result"] == "skipped"
    assert result["node_id_map"]["ev"] == "guid-ev"
    assert result["node_id_map"]["cast"] == "guid-cast"
    assert unreal.send_command.call_count == 9
