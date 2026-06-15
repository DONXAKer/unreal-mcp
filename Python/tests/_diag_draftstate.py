"""Diag: Text_PickStatus + кол-во доступных кнопок у каждого клиента в драфте."""
from __future__ import annotations
import logging, sys
sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)
from unreal_mcp_server import UnrealConnection


def find_node(node, name):
    if not isinstance(node, dict):
        return None
    if node.get("name") == name:
        return node
    for ch in node.get("children", []) or []:
        r = find_node(ch, name)
        if r:
            return r
    return None


def main():
    conn = UnrealConnection(); conn.connect()
    for ctrl in (0, 1):
        r = conn.send_command("get_widget_tree", {"controller_index": ctrl})
        res = r.get("result", r) if isinstance(r, dict) else {}
        uws = res.get("user_widgets") or []
        drafts = [uw for uw in uws if "Draft" in uw.get("class", "")]
        print(f"=== controller {ctrl}: {len(uws)} widgets, {len(drafts)} draft ===")
        for uw in drafts:
            root = uw.get("root")
            status = find_node(root, "Text_PickStatus")
            prog = find_node(root, "Text_PickProgress")
            scroll = find_node(root, "Scroll_AvailableUnits")
            nbtn = len([c for c in (scroll.get("children") or []) if "Button" in c.get("class","")]) if scroll else 0
            print(f"  {uw.get('name')}: status={status.get('text') if status else '?'!r} "
                  f"progress={prog.get('text') if prog else '?'!r} avail_buttons={nbtn}")
    return 0


main()
