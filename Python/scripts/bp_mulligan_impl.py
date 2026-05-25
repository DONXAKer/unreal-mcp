#!/usr/bin/env python3
"""Blueprint implementation for WBP_MulliganCard SetSelectedVisual and WBP_Mulligan ForEachLoop chain."""

import socket
import json
import sys
import os

HOST = os.environ.get("UNREAL_HOST", "127.0.0.1")
PORT = int(os.environ.get("UNREAL_PORT", "55557"))


def send_cmd(cmd_type, params):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(15)
        s.connect((HOST, PORT))
        msg = json.dumps({"type": cmd_type, "params": params})
        s.sendall(msg.encode('utf-8'))
        data = b""
        while True:
            chunk = s.recv(8192)
            if not chunk:
                break
            data += chunk
            try:
                json.loads(data.decode('utf-8'))
                break
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
    return json.loads(data.decode('utf-8'))


def get_node_id(resp):
    if resp.get("status") == "success":
        r = resp.get("result", {})
        # Handle both direct node_id and nested structures
        if isinstance(r, dict):
            if "node_id" in r:
                return r["node_id"]
            # Try common nested patterns
            for key in ["id", "nodeId", "node_id"]:
                if key in r:
                    return r[key]
    return None


def add_node(bp, node_type, **kw):
    p = {"blueprint_name": bp, "node_type": node_type}
    p.update(kw)
    r = send_cmd("add_blueprint_node", p)
    nid = get_node_id(r)
    status = r.get("status", "?")
    result_info = r.get("result", r.get("error", ""))
    print(f"  add_node [{node_type}]: status={status} node_id={nid} info={str(result_info)[:120]}")
    return nid, r


def connect_nodes(bp, src, src_pin, tgt, tgt_pin, fn=""):
    p = {
        "blueprint_name": bp,
        "source_node_id": src,
        "source_pin_name": src_pin,
        "target_node_id": tgt,
        "target_pin_name": tgt_pin
    }
    if fn:
        p["function_name"] = fn
    r = send_cmd("connect_nodes", p)
    result = r.get("result", {})
    ok = False
    if isinstance(result, dict):
        ok = result.get("success", False)
    elif r.get("status") == "success":
        ok = True
    label = "OK" if ok else f"FAIL: {r.get('result', r.get('error', ''))}"
    print(f"  connect [{src}.{src_pin}] -> [{tgt}.{tgt_pin}]: {label}")
    return ok


def analyze(bp_path, graph="EventGraph"):
    r = send_cmd("analyze_blueprint_graph", {
        "blueprint_path": bp_path,
        "graph_name": graph,
        "include_pin_connections": True,
        "include_node_details": True
    })
    if r.get("status") == "success":
        return r.get("result", {}).get("graph_data", {})
    print(f"  analyze ERROR: {r.get('error', r)}")
    return {}


def print_nodes_with_pins(nodes, label=""):
    if label:
        print(f"\n--- {label} ---")
    for node in nodes:
        nid = node.get("node_id", "?")
        ntype = node.get("node_type", "?")
        title = node.get("title", "")
        print(f"  Node {nid} | type={ntype} | title={title}")
        for pin in node.get("pins", []):
            pname = pin.get("name", "")
            pdir = pin.get("direction", "")
            ptype = pin.get("pin_type", "")
            conns = pin.get("connections", [])
            print(f"    pin '{pname}' | dir={pdir} | type={ptype} | conns={conns}")


def find_node_by_id(nodes, nid):
    for node in nodes:
        if node.get("node_id") == nid:
            return node
    return None


def get_pins(nodes, nid):
    node = find_node_by_id(nodes, nid)
    if node:
        return node.get("pins", [])
    return []


def compile_bp(bp_name):
    r = send_cmd("compile_blueprint", {"blueprint_name": bp_name})
    ok = r.get("status") == "success"
    print(f"  compile [{bp_name}]: {'OK' if ok else r.get('error', r)}")
    return ok


# ===========================================================================
print("\n" + "="*70)
print("TASK 1: WBP_MulliganCard — SetSelectedVisual function graph")
print("="*70)

print("\n[1.1] Analyze current SetSelectedVisual state")
sv_graph = analyze("/Game/UI/Mulligan/WBP_MulliganCard", graph="SetSelectedVisual")
sv_nodes = sv_graph.get("nodes", [])
print_nodes_with_pins(sv_nodes, "SetSelectedVisual current nodes")

# Find FunctionEntry
entry_id = None
for node in sv_nodes:
    ntype = node.get("node_type", "")
    nid = node.get("node_id", "")
    if "FunctionEntry" in ntype or "FunctionEntry" in nid:
        entry_id = nid
        break
print(f"\nFunctionEntry id: {entry_id}")

print("\n[1.2] Add Branch node to SetSelectedVisual")
branch_id, _ = add_node("WBP_MulliganCard", "branch",
                         function_name="SetSelectedVisual",
                         pos_x=500, pos_y=100)

print("\n[1.3] Add VariableGet CardButton x2")
varget1_id, _ = add_node("WBP_MulliganCard", "variable_get",
                          variable_name="CardButton",
                          function_name="SetSelectedVisual",
                          pos_x=700, pos_y=0)

varget2_id, _ = add_node("WBP_MulliganCard", "variable_get",
                          variable_name="CardButton",
                          function_name="SetSelectedVisual",
                          pos_x=700, pos_y=350)

print("\n[1.4] Add SetBackgroundColor (Yellow) and (White)")
setbg_y_id, setbg_y_resp = add_node("WBP_MulliganCard", "CallFunction",
                                     function_name="SetSelectedVisual",
                                     target_function="SetBackgroundColor",
                                     target_class="Button",
                                     pos_x=1000, pos_y=0)

setbg_w_id, setbg_w_resp = add_node("WBP_MulliganCard", "CallFunction",
                                     function_name="SetSelectedVisual",
                                     target_function="SetBackgroundColor",
                                     target_class="Button",
                                     pos_x=1000, pos_y=350)

print("\n[1.5] Analyze SetSelectedVisual AFTER adding nodes (to get real pin names)")
sv_graph2 = analyze("/Game/UI/Mulligan/WBP_MulliganCard", graph="SetSelectedVisual")
sv_nodes2 = sv_graph2.get("nodes", [])
print_nodes_with_pins(sv_nodes2, "SetSelectedVisual AFTER node additions")

# Re-resolve IDs in case add_node returned None
if not entry_id:
    for node in sv_nodes2:
        if "FunctionEntry" in node.get("node_type", ""):
            entry_id = node.get("node_id")
            break

if not branch_id:
    for node in sv_nodes2:
        if node.get("node_type") in ["K2Node_IfThenElse"]:
            branch_id = node.get("node_id")
            break

print(f"\nResolved IDs: entry={entry_id}, branch={branch_id}, varget1={varget1_id}, varget2={varget2_id}")
print(f"              setbg_y={setbg_y_id}, setbg_w={setbg_w_id}")

# Get real pin names from analysis
def get_pin_names(nodes, node_id):
    return {p.get("name"): p for p in get_pins(nodes, node_id)}

entry_pins = get_pin_names(sv_nodes2, entry_id) if entry_id else {}
branch_pins = get_pin_names(sv_nodes2, branch_id) if branch_id else {}
setbg_y_pins = get_pin_names(sv_nodes2, setbg_y_id) if setbg_y_id else {}
setbg_w_pins = get_pin_names(sv_nodes2, setbg_w_id) if setbg_w_id else {}
varget1_pins = get_pin_names(sv_nodes2, varget1_id) if varget1_id else {}
varget2_pins = get_pin_names(sv_nodes2, varget2_id) if varget2_id else {}

print(f"\nentry pins: {list(entry_pins.keys())}")
print(f"branch pins: {list(branch_pins.keys())}")
print(f"setbg_y pins: {list(setbg_y_pins.keys())}")
print(f"setbg_w pins: {list(setbg_w_pins.keys())}")
print(f"varget1 pins: {list(varget1_pins.keys())}")
print(f"varget2 pins: {list(varget2_pins.keys())}")

# Determine exec/bool pin names from FunctionEntry
entry_exec_out = "then"
entry_bool_out = "bIsSelected1"
for pname, pdata in entry_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["output", "Output"]:
        if ptype in ["exec", "Exec", "execution"] or "exec" in ptype.lower():
            entry_exec_out = pname
        elif ptype in ["bool", "Boolean"] or "bool" in ptype.lower():
            entry_bool_out = pname

# Branch exec input pin
branch_exec_in = "execute"
branch_cond_in = "Condition"
branch_true_out = "True"
branch_false_out = "False"
for pname, pdata in branch_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["input", "Input"]:
        if ptype in ["exec", "Exec"] or "exec" in ptype.lower():
            branch_exec_in = pname
        elif ptype in ["bool", "Boolean"] or "bool" in ptype.lower():
            branch_cond_in = pname
    elif pdir in ["output", "Output"]:
        if "true" in pname.lower():
            branch_true_out = pname
        elif "false" in pname.lower():
            branch_false_out = pname

# SetBackgroundColor: exec in, self in, InColor
setbg_exec_in = "execute"
setbg_self_in = "self"
setbg_color_in = "InColor"
for pname, pdata in setbg_y_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["input", "Input"]:
        if ptype in ["exec", "Exec"] or "exec" in ptype.lower():
            setbg_exec_in = pname
        elif pname.lower() in ["self", "target"]:
            setbg_self_in = pname
        elif "color" in pname.lower():
            setbg_color_in = pname

# VarGet CardButton output pin
varget1_out = "CardButton"
varget2_out = "CardButton"
for pname, pdata in varget1_pins.items():
    if pdata.get("direction", "") in ["output", "Output"]:
        varget1_out = pname
        break
for pname, pdata in varget2_pins.items():
    if pdata.get("direction", "") in ["output", "Output"]:
        varget2_out = pname
        break

print(f"\nResolved pin names:")
print(f"  FunctionEntry: exec_out='{entry_exec_out}' bool_out='{entry_bool_out}'")
print(f"  Branch: exec_in='{branch_exec_in}' cond_in='{branch_cond_in}' true='{branch_true_out}' false='{branch_false_out}'")
print(f"  SetBG: exec_in='{setbg_exec_in}' self_in='{setbg_self_in}' color_in='{setbg_color_in}'")
print(f"  VarGet1 out: '{varget1_out}', VarGet2 out: '{varget2_out}'")

print("\n[1.6] Wire connections in SetSelectedVisual")
fn = "SetSelectedVisual"
bp_card = "WBP_MulliganCard"

if entry_id and branch_id:
    connect_nodes(bp_card, entry_id, entry_exec_out, branch_id, branch_exec_in, fn=fn)
    connect_nodes(bp_card, entry_id, entry_bool_out, branch_id, branch_cond_in, fn=fn)

if branch_id and setbg_y_id:
    connect_nodes(bp_card, branch_id, branch_true_out, setbg_y_id, setbg_exec_in, fn=fn)

if branch_id and setbg_w_id:
    connect_nodes(bp_card, branch_id, branch_false_out, setbg_w_id, setbg_exec_in, fn=fn)

if varget1_id and setbg_y_id:
    connect_nodes(bp_card, varget1_id, varget1_out, setbg_y_id, setbg_self_in, fn=fn)

if varget2_id and setbg_w_id:
    connect_nodes(bp_card, varget2_id, varget2_out, setbg_w_id, setbg_self_in, fn=fn)

print("\n[1.7] Set default colors (Yellow and White)")
if setbg_y_id:
    r = send_cmd("set_node_pin_default_value", {
        "blueprint_name": bp_card,
        "node_id": setbg_y_id,
        "pin_name": setbg_color_in,
        "default_value": "(R=1.0,G=0.8,B=0.0,A=1.0)",
        "function_name": fn
    })
    print(f"  set yellow color: {r.get('status')} | {r.get('result', r.get('error', ''))}")

if setbg_w_id:
    r = send_cmd("set_node_pin_default_value", {
        "blueprint_name": bp_card,
        "node_id": setbg_w_id,
        "pin_name": setbg_color_in,
        "default_value": "(R=1.0,G=1.0,B=1.0,A=1.0)",
        "function_name": fn
    })
    print(f"  set white color: {r.get('status')} | {r.get('result', r.get('error', ''))}")


# ===========================================================================
print("\n" + "="*70)
print("TASK 2: WBP_Mulligan — EventGraph ForEachLoop chain")
print("="*70)

print("\n[2.1] Analyze current WBP_Mulligan EventGraph")
mull_graph = analyze("/Game/UI/Mulligan/WBP_Mulligan", graph="EventGraph")
mull_nodes = mull_graph.get("nodes", [])
print_nodes_with_pins(mull_nodes, "WBP_Mulligan EventGraph current nodes")

# Identify existing nodes
populate_id = None
get_children_id = None
set_vis_id = None
contains_id = None

for node in mull_nodes:
    nid = node.get("node_id", "")
    title = node.get("title", "")
    ntype = node.get("node_type", "")
    t = title.lower() + " " + ntype.lower()
    if "populatecards" in t or "populate_cards" in t or "PopulateCardsInContainer" in title:
        populate_id = nid
    if "getallchildren" in t or "get_all_children" in t or "GetAllChildren" in title:
        get_children_id = nid
    if "setselectedvisual" in t or "set_selected_visual" in t or "SetSelectedVisual" in title:
        set_vis_id = nid
    if "containsitem" in t or "contains_item" in t or "ContainsItem" in title:
        contains_id = nid

print(f"\nExisting nodes found:")
print(f"  PopulateCards = {populate_id}")
print(f"  GetAllChildren = {get_children_id}")
print(f"  SetSelectedVisual = {set_vis_id}")
print(f"  ContainsItem = {contains_id}")

print("\n[2.2] Add ForEachLoop macro node")
bp_mull = "WBP_Mulligan"

foreach_id = None
for attempt_type in ["macro", "ForEachLoop", "macro_instance"]:
    kw = {"pos_x": 1200, "pos_y": 100}
    if attempt_type in ["macro", "macro_instance"]:
        kw["macro_name"] = "ForEachLoop"
    foreach_id, foreach_r = add_node(bp_mull, attempt_type, **kw)
    if foreach_id:
        print(f"  ForEachLoop created with type={attempt_type}")
        break
print(f"ForEachLoop id: {foreach_id}")

print("\n[2.3] Add DynamicCast to WBP_MulliganCard_C")
dyncast_id = None
for attempt_type, kw in [
    ("dynamic_cast", {"cast_target_class": "WBP_MulliganCard_C", "pos_x": 1600, "pos_y": 100}),
    ("DynamicCast",  {"class_name": "WBP_MulliganCard_C", "pos_x": 1600, "pos_y": 100}),
    ("cast",         {"target_class": "WBP_MulliganCard_C", "pos_x": 1600, "pos_y": 100}),
]:
    dyncast_id, dyncast_r = add_node(bp_mull, attempt_type, **kw)
    if dyncast_id:
        print(f"  DynamicCast created with type={attempt_type}")
        break
print(f"DynamicCast id: {dyncast_id}")

print("\n[2.4] Add VariableGet SelectedCardIndices")
varget_sel_id, _ = add_node(bp_mull, "variable_get",
                             variable_name="SelectedCardIndices",
                             pos_x=1200, pos_y=400)
print(f"VariableGet SelectedCardIndices id: {varget_sel_id}")

print("\n[2.5] Analyze EventGraph AFTER additions (real pin names)")
mull_graph2 = analyze("/Game/UI/Mulligan/WBP_Mulligan", graph="EventGraph")
mull_nodes2 = mull_graph2.get("nodes", [])

# Find new nodes in updated graph
print_nodes_with_pins(
    [n for n in mull_nodes2 if n.get("node_id") in [foreach_id, dyncast_id, varget_sel_id]],
    "New nodes pin details"
)

# Also re-find existing nodes
for node in mull_nodes2:
    nid = node.get("node_id", "")
    title = node.get("title", "")
    ntype = node.get("node_type", "")
    t = title.lower() + " " + ntype.lower()
    if "populatecards" in t or "PopulateCardsInContainer" in title:
        populate_id = nid
    if "getallchildren" in t or "GetAllChildren" in title:
        get_children_id = nid
    if "setselectedvisual" in t or "SetSelectedVisual" in title:
        set_vis_id = nid
    if "containsitem" in t or "ContainsItem" in title:
        contains_id = nid

print(f"\nRe-resolved existing: populate={populate_id}, get_children={get_children_id}")
print(f"                       set_vis={set_vis_id}, contains={contains_id}")

# Determine real pin names for ForEachLoop
foreach_exec_in   = "execute"
foreach_array_in  = "Array"
foreach_body_out  = "Loop Body"
foreach_elem_out  = "Array Element"
foreach_idx_out   = "Array Index"

foreach_node_pins = get_pin_names(mull_nodes2, foreach_id) if foreach_id else {}
for pname, pdata in foreach_node_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    pl = pname.lower()
    if pdir in ["input", "Input"]:
        if "exec" in ptype.lower() or ptype in ["exec", "Exec"]:
            foreach_exec_in = pname
        elif "array" in pl:
            foreach_array_in = pname
    elif pdir in ["output", "Output"]:
        if "body" in pl:
            foreach_body_out = pname
        elif "element" in pl:
            foreach_elem_out = pname
        elif "index" in pl:
            foreach_idx_out = pname

# Determine real pin names for DynamicCast
dyncast_exec_in  = "execute"
dyncast_obj_in   = "Object"
dyncast_then_out = "then"
dyncast_typed_out = None

dyncast_node_pins = get_pin_names(mull_nodes2, dyncast_id) if dyncast_id else {}
for pname, pdata in dyncast_node_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["output", "Output"]:
        if pname.lower().startswith("as ") or pname.lower().startswith("as_"):
            dyncast_typed_out = pname
        elif "mulligan" in pname.lower() or "wbp" in pname.lower():
            dyncast_typed_out = pname

# Fallback: any non-exec output that is not "then" or "Cast Failed"
if not dyncast_typed_out:
    for pname, pdata in dyncast_node_pins.items():
        pdir = pdata.get("direction", "")
        ptype = pdata.get("pin_type", "")
        if (pdir in ["output", "Output"] and
                pname not in ["then", "Cast Failed", ""] and
                "exec" not in ptype.lower()):
            dyncast_typed_out = pname
            break

# VariableGet SelectedCardIndices output pin
varget_sel_out = "SelectedCardIndices"
varget_sel_node_pins = get_pin_names(mull_nodes2, varget_sel_id) if varget_sel_id else {}
for pname, pdata in varget_sel_node_pins.items():
    if pdata.get("direction", "") in ["output", "Output"]:
        varget_sel_out = pname
        break

# SetSelectedVisual and ContainsItem pin names
set_vis_pins = get_pin_names(mull_nodes2, set_vis_id) if set_vis_id else {}
contains_pins = get_pin_names(mull_nodes2, contains_id) if contains_id else {}

set_vis_exec_in = "execute"
set_vis_self_in = "self"
set_vis_bool_in = "bIsSelected"
for pname, pdata in set_vis_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["input", "Input"]:
        if "exec" in ptype.lower():
            set_vis_exec_in = pname
        elif pname.lower() in ["self", "target"]:
            set_vis_self_in = pname
        elif "selected" in pname.lower() or "bool" in ptype.lower():
            set_vis_bool_in = pname

contains_arr_in = "TargetArray"
contains_item_in = "ItemToFind"
contains_ret_out = "ReturnValue"
for pname, pdata in contains_pins.items():
    pdir = pdata.get("direction", "")
    pl = pname.lower()
    if pdir in ["input", "Input"]:
        if "array" in pl or "target" in pl:
            contains_arr_in = pname
        elif "item" in pl or "find" in pl:
            contains_item_in = pname
    elif pdir in ["output", "Output"]:
        if "return" in pl or "value" in pl or "result" in pl:
            contains_ret_out = pname

print(f"\nResolved ForEachLoop pins:")
print(f"  exec_in='{foreach_exec_in}' array_in='{foreach_array_in}'")
print(f"  body_out='{foreach_body_out}' elem_out='{foreach_elem_out}' idx_out='{foreach_idx_out}'")
print(f"\nResolved DynamicCast pins:")
print(f"  exec_in='{dyncast_exec_in}' obj_in='{dyncast_obj_in}'")
print(f"  then_out='{dyncast_then_out}' typed_out='{dyncast_typed_out}'")
print(f"\nResolved other pins:")
print(f"  varget_sel_out='{varget_sel_out}'")
print(f"  set_vis_exec='{set_vis_exec_in}' set_vis_self='{set_vis_self_in}' set_vis_bool='{set_vis_bool_in}'")
print(f"  contains_arr='{contains_arr_in}' contains_item='{contains_item_in}' contains_ret='{contains_ret_out}'")

# Also get populate "then" output pin name
populate_pins = get_pin_names(mull_nodes2, populate_id) if populate_id else {}
populate_then_out = "then"
for pname, pdata in populate_pins.items():
    pdir = pdata.get("direction", "")
    ptype = pdata.get("pin_type", "")
    if pdir in ["output", "Output"] and "exec" in ptype.lower():
        populate_then_out = pname
        break

# GetAllChildren ReturnValue output pin
get_children_pins = get_pin_names(mull_nodes2, get_children_id) if get_children_id else {}
get_children_ret_out = "ReturnValue"
for pname, pdata in get_children_pins.items():
    pdir = pdata.get("direction", "")
    pl = pname.lower()
    if pdir in ["output", "Output"] and ("return" in pl or "value" in pl or "result" in pl or "children" in pl):
        if "exec" not in pdata.get("pin_type", "").lower():
            get_children_ret_out = pname
            break

print(f"\n  populate_then='{populate_then_out}' get_children_ret='{get_children_ret_out}'")

print("\n[2.6] Wire connections in WBP_Mulligan EventGraph")

# PopulateCards.then -> ForEachLoop.execute
if populate_id and foreach_id:
    connect_nodes(bp_mull, populate_id, populate_then_out, foreach_id, foreach_exec_in)

# GetAllChildren.ReturnValue -> ForEachLoop.Array
if get_children_id and foreach_id:
    connect_nodes(bp_mull, get_children_id, get_children_ret_out, foreach_id, foreach_array_in)

# ForEachLoop.Loop Body -> DynamicCast.execute
if foreach_id and dyncast_id:
    connect_nodes(bp_mull, foreach_id, foreach_body_out, dyncast_id, dyncast_exec_in)

# ForEachLoop.Array Element -> DynamicCast.Object
if foreach_id and dyncast_id:
    connect_nodes(bp_mull, foreach_id, foreach_elem_out, dyncast_id, dyncast_obj_in)

# DynamicCast.then -> SetSelectedVisual.execute
if dyncast_id and set_vis_id:
    connect_nodes(bp_mull, dyncast_id, dyncast_then_out, set_vis_id, set_vis_exec_in)

# DynamicCast.typed -> SetSelectedVisual.self
if dyncast_id and set_vis_id and dyncast_typed_out:
    connect_nodes(bp_mull, dyncast_id, dyncast_typed_out, set_vis_id, set_vis_self_in)
else:
    print(f"  SKIP DynamicCast typed->SetVis.self: typed_out={dyncast_typed_out}, dyncast={dyncast_id}, set_vis={set_vis_id}")

# SelectedCardIndices -> ContainsItem.TargetArray
if varget_sel_id and contains_id:
    connect_nodes(bp_mull, varget_sel_id, varget_sel_out, contains_id, contains_arr_in)

# ForEachLoop.Array Index -> ContainsItem.ItemToFind
if foreach_id and contains_id:
    connect_nodes(bp_mull, foreach_id, foreach_idx_out, contains_id, contains_item_in)

# ContainsItem.ReturnValue -> SetSelectedVisual.bIsSelected
if contains_id and set_vis_id:
    connect_nodes(bp_mull, contains_id, contains_ret_out, set_vis_id, set_vis_bool_in)


# ===========================================================================
print("\n" + "="*70)
print("FINAL: Compile both blueprints")
print("="*70)

compile_bp("WBP_MulliganCard")
compile_bp("WBP_Mulligan")

print("\n=== ALL DONE ===")
