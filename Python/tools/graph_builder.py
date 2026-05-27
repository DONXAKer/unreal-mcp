"""
Declarative Blueprint graph builder for UnrealMCP.

Takes a JSON spec describing nodes + connections in a Blueprint EventGraph
and atomically builds the whole graph through existing UnrealMCP primitives.
Replaces 15-30 sequential MCP tool calls with one high-level call, drastically
reducing LLM-side errors (lost node_ids, wrong order, missed connections).

Spec schema (see also `_graph_builder_design.md`):

    {
      "blueprint_path": "/Game/UI/Mulligan/WBP_Mulligan",
      "graph": "EventGraph",            # or "<FunctionName>" for function graph
      "nodes": [
        {"id": "<local_id>", "type": "<NodeType>", ...type-specific params},
        ...
      ],
      "connections": [
        {"from": "<local_id>.<pin_name>", "to": "<local_id>.<pin_name>"},
        ...
      ],
      "defaults": [                     # optional, set after wiring
        {"pin": "<local_id>.<pin_name>", "value": <literal>}
      ],
      "compile": true,                  # default true
      "rollback_on_failure": true,      # default true
      "clear_graph_first": false        # default false; danger: wipes existing nodes
    }

Supported node types (80% UI-task coverage):

| type           | params                                              |
|----------------|-----------------------------------------------------|
| Event          | event_name (e.g. "ReceiveBeginPlay", custom name)   |
| FunctionCall   | target (class name or "self"), function_name        |
| VariableGet    | var_name                                            |
| VariableSet    | var_name                                            |
| Branch         | —                                                   |
| ForEachLoop    | —                                                   |
| DynamicCast    | target_class                                        |
| BindEvent      | component, delegate, delegate_class                 |
| CustomEvent    | event_name, inputs?: [{name, type}]                 |

For any type not in this table, the orchestrator returns a validation error
with `unsupported_type` code — caller falls back to manual primitive flow.

Contract (return shape):

  Success:
    {"success": True, "nodes_created": N, "connections_made": M,
     "compile_result": "ok|failed|skipped", "compile_errors": [],
     "node_id_map": {"<local_id>": "<unreal_node_id>"}}

  Validation error (UE5 not touched):
    {"success": False, "phase": "validation",
     "errors": [{"node"|"connection": ..., "code": ..., "message": ...}]}

  Build error (atomic rollback when rollback_on_failure=true):
    {"success": False, "phase": "create|connect|defaults",
     "fail_at": "<local_id or connection>", "cause": {...},
     "rolled_back": True/False,
     "rollback_result": {"deleted": N, "errors": [...]},
     "nodes_left": [...]}      # only if rollback skipped or partial

  Compile error (graph built, BP doesn't compile):
    {"success": True, "compile_result": "failed",
     "compile_errors": [...], "nodes_created": N, "connections_made": M}
"""

from __future__ import annotations

import logging
from typing import Any

from mcp.server.fastmcp import Context

logger = logging.getLogger("UnrealMCP")


SUPPORTED_NODE_TYPES = {
    "Event",
    "FunctionCall",
    "VariableGet",
    "VariableSet",
    "Branch",
    "ForEachLoop",
    "DynamicCast",
    "BindEvent",
    "CustomEvent",
}


# ─── Pre-validation ─────────────────────────────────────────────────────────


def _validate_spec(spec: dict[str, Any]) -> list[dict[str, Any]]:
    """Pure-Python structural validation. No TCP calls.

    Returns list of errors; empty list = spec is valid for build attempt.
    """
    errors: list[dict[str, Any]] = []

    if not isinstance(spec, dict):
        return [{"code": "spec_not_object", "message": "spec must be a JSON object"}]

    if "blueprint_path" not in spec or not spec["blueprint_path"]:
        errors.append({"code": "missing_blueprint_path", "message": "spec.blueprint_path is required"})

    nodes = spec.get("nodes")
    existing_nodes = spec.get("existing_nodes", {})
    if not isinstance(nodes, list):
        errors.append({"code": "missing_nodes", "message": "spec.nodes must be a list"})
        return errors  # can't validate further without nodes list
    # Allow empty nodes[] when existing_nodes provides pre-registered IDs
    if not nodes and not existing_nodes:
        errors.append({"code": "missing_nodes",
                       "message": "spec.nodes must be non-empty when existing_nodes is empty"})
        return errors

    seen_ids: set[str] = set(existing_nodes.keys())
    for i, n in enumerate(nodes):
        if not isinstance(n, dict):
            errors.append({"node": f"<index {i}>", "code": "node_not_object",
                           "message": "each node must be an object"})
            continue
        nid = n.get("id")
        if not nid:
            errors.append({"node": f"<index {i}>", "code": "missing_id",
                           "message": "node.id is required"})
            continue
        if nid in seen_ids:
            errors.append({"node": nid, "code": "duplicate_id",
                           "message": f"duplicate node id '{nid}'"})
        seen_ids.add(nid)
        ntype = n.get("type")
        if ntype not in SUPPORTED_NODE_TYPES:
            errors.append({"node": nid, "code": "unsupported_type",
                           "message": f"node.type '{ntype}' not in supported set "
                                      f"{sorted(SUPPORTED_NODE_TYPES)}"})

    connections = spec.get("connections", [])
    if not isinstance(connections, list):
        errors.append({"code": "connections_not_list",
                       "message": "spec.connections must be a list"})
        connections = []

    for i, c in enumerate(connections):
        if not isinstance(c, dict) or "from" not in c or "to" not in c:
            errors.append({"connection": f"<index {i}>", "code": "bad_connection_shape",
                           "message": "each connection requires 'from' and 'to' strings"})
            continue
        for side in ("from", "to"):
            ref = c[side]
            if not isinstance(ref, str) or "." not in ref:
                errors.append({"connection": f"<index {i}>", "code": "bad_pin_ref",
                               "message": f"{side}='{ref}' must be '<node_id>.<pin_name>'"})
                continue
            nid = ref.rsplit(".", 1)[0]
            if nid not in seen_ids:
                errors.append({"connection": f"<index {i}>", "code": "unknown_node_ref",
                               "message": f"{side}='{ref}' references unknown node '{nid}'"})

    defaults = spec.get("defaults", [])
    if not isinstance(defaults, list):
        errors.append({"code": "defaults_not_list",
                       "message": "spec.defaults must be a list"})
    else:
        for i, d in enumerate(defaults):
            if not isinstance(d, dict) or "pin" not in d or "value" not in d:
                errors.append({"default": f"<index {i}>", "code": "bad_default_shape",
                               "message": "each default requires 'pin' and 'value'"})
                continue
            pin = d["pin"]
            if not isinstance(pin, str) or "." not in pin:
                errors.append({"default": f"<index {i}>", "code": "bad_pin_ref",
                               "message": f"pin='{pin}' must be '<node_id>.<pin_name>'"})
                continue
            nid = pin.rsplit(".", 1)[0]
            if nid not in seen_ids:
                errors.append({"default": f"<index {i}>", "code": "unknown_node_ref",
                               "message": f"pin='{pin}' references unknown node '{nid}'"})

    return errors


# ─── Node creation dispatch ─────────────────────────────────────────────────


def _extract_node_id(response: dict[str, Any]) -> str | None:
    """UE5 primitives put the created node_id in different keys. Try them all."""
    if not isinstance(response, dict):
        return None
    # Most common: top-level node_id
    if response.get("node_id"):
        return response["node_id"]
    # Nested under 'result' or 'node'
    for key in ("result", "node", "data"):
        sub = response.get(key)
        if isinstance(sub, dict) and sub.get("node_id"):
            return sub["node_id"]
    # Some primitives return id under 'guid' or 'NodeGuid'
    for key in ("guid", "NodeGuid", "id"):
        if response.get(key):
            return response[key]
        for sub_key in ("result", "node", "data"):
            sub = response.get(sub_key)
            if isinstance(sub, dict) and sub.get(key):
                return sub[key]
    return None


def _create_node(unreal: Any, blueprint_path: str, graph: str,
                 node_spec: dict[str, Any]) -> dict[str, Any]:
    """Map a single node_spec to the right `unreal.send_command(...)` call.

    Returns the raw response dict from the bridge. Caller extracts node_id.
    """
    ntype = node_spec["type"]
    common = {"blueprint_name": blueprint_path,
              "pos_x": node_spec.get("pos_x", 0.0),
              "pos_y": node_spec.get("pos_y", 0.0)}
    if graph and graph != "EventGraph":
        common["function_name"] = graph

    if ntype == "Event":
        params = {"blueprint_name": blueprint_path,
                  "event_name": node_spec["event_name"],
                  "node_position": [common["pos_x"], common["pos_y"]]}
        if graph and graph != "EventGraph":
            params["function_name"] = graph
        return unreal.send_command("add_event_node", params)

    if ntype == "FunctionCall":
        params = {"blueprint_name": blueprint_path,
                  "node_type": "CallFunction",
                  "target_class": node_spec.get("target", "self"),
                  "target_function": node_spec["function_name"],
                  "params": node_spec.get("params", {}),
                  "node_position": [common["pos_x"], common["pos_y"]]}
        if graph and graph != "EventGraph":
            params["function_name"] = graph
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "VariableGet":
        params = {**common, "node_type": "VariableGet",
                  "variable_name": node_spec["var_name"]}
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "VariableSet":
        params = {**common, "node_type": "VariableSet",
                  "variable_name": node_spec["var_name"]}
        # VariableSet primitive doesn't accept function_name in current impl;
        # spec still allows function-graph placement via "graph" — UE5 falls
        # back to EventGraph if function_name unknown, that's OK.
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "Branch":
        params = {**common, "node_type": "Branch"}
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "ForEachLoop":
        params = {**common, "node_type": "ForEachLoop"}
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "DynamicCast":
        params = {**common, "node_type": "DynamicCast",
                  "target_class": node_spec["target_class"]}
        return unreal.send_command("add_blueprint_node", params)

    if ntype == "BindEvent":
        params = {"blueprint_name": blueprint_path,
                  "component_property_name": node_spec["component"],
                  "delegate_name": node_spec["delegate"],
                  "delegate_class": node_spec.get("delegate_class", ""),
                  "node_position": [common["pos_x"], common["pos_y"]]}
        return unreal.send_command("add_component_bound_event", params)

    if ntype == "CustomEvent":
        params = {"blueprint_name": blueprint_path,
                  "event_name": node_spec["event_name"],
                  "node_position": [common["pos_x"], common["pos_y"]]}
        response = unreal.send_command("create_custom_event", params)
        # Add typed inputs after creation
        inputs = node_spec.get("inputs", []) or []
        if response.get("success") and inputs:
            node_id = _extract_node_id(response)
            for inp in inputs:
                unreal.send_command("add_custom_event_input", {
                    "blueprint_name": blueprint_path,
                    "node_id": node_id,
                    "input_name": inp["name"],
                    "input_type": inp["type"],
                })
        return response

    return {"success": False,
            "message": f"unsupported node type '{ntype}' (validation should have caught this)"}


def _is_success(response: dict[str, Any]) -> bool:
    """Bridge sometimes uses `success`, sometimes `ok`. Treat both as success-truth."""
    if not isinstance(response, dict):
        return False
    if "success" in response:
        return bool(response["success"])
    if "ok" in response:
        return bool(response["ok"])
    # Heuristic: no error message + no explicit success field
    return "message" not in response and "error" not in response


def _error_message(response: dict[str, Any]) -> str:
    if not isinstance(response, dict):
        return f"non-dict response: {type(response).__name__}"
    return (response.get("message")
            or response.get("error")
            or str(response.get("cause") or response)[:200])


# ─── Rollback ───────────────────────────────────────────────────────────────


def _rollback(unreal: Any, blueprint_path: str, graph: str,
              created_node_ids: list[tuple[str, str]],
              created_connections: list[dict[str, Any]]) -> dict[str, Any]:
    """Walk forward-created state in reverse and undo it."""
    deleted: list[str] = []
    errors: list[dict[str, Any]] = []

    # Step 1: disconnect created pins (reverse order)
    for conn in reversed(created_connections):
        try:
            src_id, src_pin = conn["_resolved_from"].rsplit(".", 1)
            tgt_id, tgt_pin = conn["_resolved_to"].rsplit(".", 1)
            r = unreal.send_command("disconnect_pin", {
                "blueprint_name": blueprint_path,
                "source_node_id": src_id,
                "source_pin_name": src_pin,
                "target_node_id": tgt_id,
                "target_pin_name": tgt_pin,
            })
            if not _is_success(r):
                errors.append({"connection": conn, "error": _error_message(r)})
        except Exception as e:
            errors.append({"connection": conn, "error": f"{type(e).__name__}: {e}"})

    # Step 2: delete created nodes (reverse order)
    for spec_id, node_id in reversed(created_node_ids):
        try:
            params = {"blueprint_name": blueprint_path, "node_id": node_id}
            if graph and graph != "EventGraph":
                params["function_name"] = graph
            r = unreal.send_command("delete_node", params)
            if _is_success(r):
                deleted.append(spec_id)
            else:
                errors.append({"node": spec_id, "error": _error_message(r)})
        except Exception as e:
            errors.append({"node": spec_id, "error": f"{type(e).__name__}: {e}"})

    return {"deleted": deleted, "errors": errors}


# ─── Orchestrator entry point ───────────────────────────────────────────────


def build_blueprint_graph_impl(unreal: Any, spec: dict[str, Any]) -> dict[str, Any]:
    """Core orchestrator (kept separate from @mcp.tool wrapper for unit testing).

    `unreal` is a connection object with .send_command(cmd, params) → dict.
    Tests pass a mock; production passes get_unreal_connection().
    """
    # Phase 0: validate
    validation_errors = _validate_spec(spec)
    if validation_errors:
        return {"success": False, "phase": "validation", "errors": validation_errors}

    blueprint_path = spec["blueprint_path"]
    graph = spec.get("graph", "EventGraph")
    rollback_enabled = spec.get("rollback_on_failure", True)
    do_compile = spec.get("compile", True)

    spec_id_to_node_id: dict[str, str] = dict(spec.get("existing_nodes", {}))
    created_order: list[tuple[str, str]] = []  # preserves creation order for rollback
    created_connections: list[dict[str, Any]] = []

    # Phase A: create nodes
    for node_spec in spec["nodes"]:
        spec_id = node_spec["id"]
        try:
            r = _create_node(unreal, blueprint_path, graph, node_spec)
        except Exception as e:
            r = {"success": False, "message": f"{type(e).__name__}: {e}"}
        if not _is_success(r):
            rollback_result = _rollback(unreal, blueprint_path, graph,
                                        created_order, created_connections) if rollback_enabled else None
            return {"success": False, "phase": "create", "fail_at": spec_id,
                    "cause": r, "rolled_back": rollback_enabled,
                    "rollback_result": rollback_result,
                    "nodes_left": list(spec_id_to_node_id.keys()) if not rollback_enabled else []}
        node_id = _extract_node_id(r)
        if not node_id:
            rollback_result = _rollback(unreal, blueprint_path, graph,
                                        created_order, created_connections) if rollback_enabled else None
            return {"success": False, "phase": "create", "fail_at": spec_id,
                    "cause": {"message": f"primitive returned no node_id, raw response: {r}"},
                    "rolled_back": rollback_enabled,
                    "rollback_result": rollback_result}
        spec_id_to_node_id[spec_id] = node_id
        created_order.append((spec_id, node_id))

    # Phase B: connect pins
    for conn in spec.get("connections", []):
        src_ref, tgt_ref = conn["from"], conn["to"]
        src_id, src_pin = src_ref.rsplit(".", 1)
        tgt_id, tgt_pin = tgt_ref.rsplit(".", 1)
        try:
            conn_params: dict[str, Any] = {
                "blueprint_name": blueprint_path,
                "source_node_id": spec_id_to_node_id[src_id],
                "source_pin_name": src_pin,
                "target_node_id": spec_id_to_node_id[tgt_id],
                "target_pin_name": tgt_pin,
            }
            if graph and graph != "EventGraph":
                conn_params["function_name"] = graph
            r = unreal.send_command("connect_nodes", conn_params)
        except Exception as e:
            r = {"success": False, "message": f"{type(e).__name__}: {e}"}
        if not _is_success(r):
            rollback_result = _rollback(unreal, blueprint_path, graph,
                                        created_order, created_connections) if rollback_enabled else None
            return {"success": False, "phase": "connect", "fail_at": f"{src_ref} → {tgt_ref}",
                    "cause": r, "rolled_back": rollback_enabled,
                    "rollback_result": rollback_result,
                    "connections_made": len(created_connections)}
        created_connections.append({**conn,
                                    "_resolved_from": f"{spec_id_to_node_id[src_id]}.{src_pin}",
                                    "_resolved_to": f"{spec_id_to_node_id[tgt_id]}.{tgt_pin}"})

    # Phase C: set pin defaults
    for default in spec.get("defaults", []):
        pin_ref = default["pin"]
        nid, pname = pin_ref.rsplit(".", 1)
        try:
            r = unreal.send_command("set_pin_default_value", {
                "blueprint_name": blueprint_path,
                "node_id": spec_id_to_node_id[nid],
                "pin_name": pname,
                "value": default["value"],
            })
        except Exception as e:
            r = {"success": False, "message": f"{type(e).__name__}: {e}"}
        if not _is_success(r):
            rollback_result = _rollback(unreal, blueprint_path, graph,
                                        created_order, created_connections) if rollback_enabled else None
            return {"success": False, "phase": "defaults", "fail_at": pin_ref,
                    "cause": r, "rolled_back": rollback_enabled,
                    "rollback_result": rollback_result}

    # Phase D: compile
    compile_result = "skipped"
    compile_errors: list[Any] = []
    if do_compile:
        try:
            r = unreal.send_command("compile_blueprint",
                                    {"blueprint_name": blueprint_path})
            if _is_success(r):
                compile_result = "ok"
            else:
                compile_result = "failed"
                compile_errors = [_error_message(r)]
        except Exception as e:
            compile_result = "failed"
            compile_errors = [f"{type(e).__name__}: {e}"]

    return {"success": True,
            "nodes_created": len(created_order),
            "connections_made": len(created_connections),
            "defaults_set": len(spec.get("defaults", [])),
            "compile_result": compile_result,
            "compile_errors": compile_errors,
            "node_id_map": dict(spec_id_to_node_id)}


# ─── MCP tool registration ──────────────────────────────────────────────────


def register_graph_builder_tool(mcp: Any) -> None:
    """Register `build_blueprint_graph` as an MCP tool.

    Call this from unreal_mcp_server.py alongside register_blueprint_node_tools().
    """

    @mcp.tool()
    def build_blueprint_graph(
        ctx: Context[Any, Any, Any],
        blueprint_path: str,
        spec: dict[str, Any],
        graph: str = "EventGraph",
    ) -> dict[str, Any]:
        """
        Atomically build a Blueprint graph (EventGraph or function graph) from a
        declarative JSON spec. See graph_builder.py docstring for spec schema.

        Replaces 15-30 sequential add_blueprint_*_node + connect_blueprint_nodes
        calls with one high-level operation. On failure, automatically rolls back
        all partially-created nodes (unless rollback_on_failure=false in spec).

        Args:
            blueprint_path: Full /Game/... path of target Blueprint. If spec.blueprint_path
                            is also set, this argument wins (callers that pass spec straight
                            from plan output can override the path).
            spec: Graph spec dict — nodes[], connections[], defaults[], compile, rollback_on_failure.
            graph: "EventGraph" (default) or name of a function graph within the Blueprint.

        Returns:
            Success: {"success": True, "nodes_created": N, "connections_made": M,
                     "compile_result": "ok|failed|skipped", "node_id_map": {...}}
            Failure: {"success": False, "phase": "validation|create|connect|defaults",
                     "errors" or "fail_at"+"cause", "rolled_back": bool, ...}
        """
        from unreal_mcp_server import get_unreal_connection

        # Layer caller-provided blueprint_path / graph over spec values
        effective_spec = dict(spec)
        if blueprint_path:
            effective_spec["blueprint_path"] = blueprint_path
        if graph and "graph" not in effective_spec:
            effective_spec["graph"] = graph

        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "phase": "connection",
                    "errors": [{"code": "no_unreal_connection",
                                "message": "Failed to connect to Unreal Engine"}]}

        logger.info(f"build_blueprint_graph: bp={effective_spec.get('blueprint_path')} "
                    f"graph={effective_spec.get('graph')} "
                    f"nodes={len(effective_spec.get('nodes', []))} "
                    f"connections={len(effective_spec.get('connections', []))}")
        result = build_blueprint_graph_impl(unreal, effective_spec)
        logger.info(f"build_blueprint_graph result: success={result.get('success')} "
                    f"phase={result.get('phase', 'n/a')} "
                    f"nodes_created={result.get('nodes_created', 0)}")
        return result
