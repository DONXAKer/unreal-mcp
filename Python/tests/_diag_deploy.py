"""Repro UI-клика деплоя: довести 2 клиента до Deployment и кликнуть карточку
юнита РЕАЛЬНЫМ кликом (click_widget_by_name по экранным координатам, через Slate
hit-test) — в отличие от wc_deploy_unit (прямой вызов субсистемы, минует UI).
Проверяем, сменился ли StatusText на «Выберите клетку…» (т.е. сработал ли
HandleUnitEntryClicked). Так воспроизводим баг «карточка подсвечивается, но клик
ничего не делает».

Запуск: uv run python tests/_diag_deploy.py   (нужны сервер :8081 + 2 редактора)
"""
from __future__ import annotations

import json
import logging
import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user
from tests.playtest_visual import (
    PORTS, _find_node, _send, _snake_draft, _start_pie, _wait_uw_all,
)
from unreal_mcp_server import UnrealConnection


def _find_first(node, predicate):
    return _find_node(node, predicate)


def _dump_text_nodes(node, out):
    if not isinstance(node, dict):
        return
    if "TextBlock" in node.get("class", "") and node.get("text"):
        out.append((node.get("name"), node.get("text")))
    for ch in node.get("children", []) or []:
        _dump_text_nodes(ch, out)


def main() -> int:
    ts = str(int(time.time()))
    short = ts[-6:]
    users = {"p1": f"dd_a_{short}", "p2": f"dd_b_{short}"}
    pwd = "Test1234"
    for side in PORTS:
        ensure_test_user(users[side], pwd, f"{users[side]}@x.com")
    conns = {s: UnrealConnection(port=p) for s, p in PORTS.items()}
    for s, conn in conns.items():
        if not conn.connect():
            print(f"FATAL: нет соединения {s}"); return 2

    for side, conn in conns.items():
        # Если PIE уже запущен (подняли вручную) — не стартуем заново.
        st = conn.send_command("pie_status", {})
        st = st.get("result", st) if isinstance(st, dict) else st
        if isinstance(st, dict) and st.get("is_running"):
            print(f"[{side}] PIE уже запущен — пропускаю pie_start")
            continue
        if not _start_pie(side, conn):
            print(f"FATAL: PIE не поднялся {side}"); return 1
    time.sleep(3)

    for side, conn in conns.items():
        _send(conn, "set_text_on_widget", {"widget_name": "LoginUsernameInput", "text": users[side], "controller_index": 0})
        _send(conn, "set_text_on_widget", {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})
    _wait_uw_all(conns, "WBP_MainMenu", timeout=30)
    draft_ok = False
    for _ in range(3):
        for conn in conns.values():
            _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
        if _wait_uw_all(conns, "WBP_Draft", timeout=30):
            draft_ok = True; break
    if not draft_ok:
        print("FATAL: draft не достигнут"); return 1
    _snake_draft(conns)
    if not _wait_uw_all(conns, "WBP_DeploymentScreen", timeout=45):
        print("FATAL: deployment не достигнут"); return 1
    time.sleep(2)

    c = conns["p1"]
    def s(cmd, p=None):
        r = c.send_command(cmd, p or {}); return r.get("result", r) if isinstance(r, dict) else r

    # Дерево WBP_DeploymentScreen — найти карточку-кнопку + StatusText
    tree = s("get_widget_tree", {"controller_index": 0})
    dep_uw = None
    for uw in (tree.get("user_widgets") or []):
        if "Deployment" in uw.get("class", ""):
            dep_uw = uw; break
    if not dep_uw:
        print("FATAL: WBP_DeploymentScreen не в дереве"); return 1

    # Карточка: UCatalogEntryButton (Button) внутри
    card = _find_first(dep_uw.get("root"), lambda n: "Button" in n.get("class", "") or "CatalogEntry" in n.get("class", ""))
    print("card widget:", card.get("name") if card else None, "class:", card.get("class") if card else None)

    def status_texts():
        t = s("get_widget_tree", {"controller_index": 0})
        for uw in (t.get("user_widgets") or []):
            if "Deployment" in uw.get("class", ""):
                out = []; _dump_text_nodes(uw.get("root"), out); return out
        return []

    def dep_state():
        return s("wc_get_deployment_state", {"controller_index": 0})

    def deployed_count():
        d = dep_state()
        return d.get("deployed_count", 0) if isinstance(d, dict) else 0

    print("deploy BEFORE:", json.dumps(dep_state(), ensure_ascii=False)[:120])

    if card:
        # 1) Выбрать юнит: invoke (программный OnClicked, UMG — работает).
        s("invoke_button_click", {"widget_name": card.get("name"), "controller_index": 0})
        time.sleep(0.8)
        before = deployed_count()
        # 2) Клик по КЛЕТКЕ: РЕАЛЬНЫЙ Slate-клик по пикселям через screen_click
        #    (3.4.0). После SHTI корень deployment-экрана пропускает клик в мир →
        #    контроллер OnLeftMouseClick → HandleCellSelection → TryDeployAtCell.
        #    Свип по сетке (нормализованные коорд.) — ищем первую клетку своей зоны,
        #    где deployed_count вырастет. Зона неизвестна заранее → проходим всю ширину.
        sweep = [(nx, ny) for ny in (0.40, 0.50, 0.60) for nx in (0.20, 0.30, 0.70, 0.80, 0.50)]
        placed_at = None
        for (nx, ny) in sweep:
            # down → пауза (редактор тикает САМ между TCP-командами) → up.
            # За кадры зажатия Enhanced Input даёт ETriggerEvent::Triggered.
            # tick_world ИЗ команды нельзя — re-entrant World->Tick роняет редактор.
            s("screen_click", {"x": nx, "y": ny, "normalized": True, "action": "down", "controller_index": 0})
            time.sleep(0.35)
            s("screen_click", {"x": nx, "y": ny, "normalized": True, "action": "up", "controller_index": 0})
            time.sleep(0.5)
            now = deployed_count()
            print(f"click ({nx},{ny}) -> deployed={now}")
            if now > before:
                placed_at = (nx, ny); break
        print("RESULT placed_at:", placed_at, "| deploy AFTER:", json.dumps(dep_state(), ensure_ascii=False)[:120])

    print("PIE оставлен в Deployment.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
