"""FIX-VFX-ATTRIB-001 runtime check: attacks must still render, and every
sequence must match a server combat event pair.

The stock smoke_fullgame_solo never lands an attack: both deploy zones are the
extreme columns (x=0 vs x=7 -> distance 7 > every range, max 3), so "attacks
ok=N" there only counts dispatched commands, all of which the server rejects
with OUT_OF_RANGE. Deploying closer is not an option either - the zone IS the
extreme column, x=2 is refused outright. So this harness walks a unit in one
tile per turn (the bot passes, so the whole gap is closed from our side) and
attacks once the gap is inside range.

What it asserts: the number of "CombatVFX: sequence" lines equals the number of
server ATTACK events, and the pairs match. Before FIX-VFX-ATTRIB-001 the
attacker also played a local sequence built from the mutable SelectedTarget slot,
so a second attack in flight made it fire on the wrong pair.

Run from unreal-mcp/Python (live Editor + server on :8081 required):
    uv run python tests/smoke_attack_vfx.py

Exit codes: 0 PASS, 1 FAIL, 2 INCONCLUSIVE (no attack landed - the run says so
rather than reporting a green that proves nothing).
ASCII output only.
"""
from __future__ import annotations

import os
import re
import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

import logging

logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests import _stomp_opponent
from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot
from tests.smoke_fullgame_solo import (
    _pie_restart_n1,
    _ue_draft,
    _ue_login_findgame,
    _wait_battle_reached,
    _wait_pie_widget,
)
from tests.smoke_pie_full_game import _battle_units, _deploy_client, _has_uw_any, _send
from unreal_mcp_server import UnrealConnection

LOG = r"D:\WarCard\client\Saved\Logs\Client.log"

# Measured the hard way: the deployment zone is the EXTREME column only
# (red x=0, blue x=7) - placing at x=2 is rejected outright (0/5 deployed).
# So the two lines start 7 tiles apart, beyond every unit's range (max 3), and
# the only way to land an attack is to walk in. The bot passes its turns, so
# the whole gap has to be closed from our side.
_stomp_opponent._ZONE_CELLS["blue"] = [(7, 0), (7, 1), (7, 2), (7, 3), (7, 4)]
ATTACK_RANGE_GUESS = 3
MAX_STEP = 4


def _log_mark() -> int:
    try:
        return os.path.getsize(LOG)
    except OSError:
        return 0


def _log_since(offset: int) -> str:
    with open(LOG, encoding="utf-8", errors="replace") as f:
        f.seek(0)
        return f.read()[offset:]


def _dist(a, b) -> int:
    return abs(a["gridX"] - b["gridX"]) + abs(a["gridY"] - b["gridY"])


def _advance(conn, unit, foe, occupied) -> bool:
    """Walk `unit` one tile toward `foe`, verifying it actually moved.

    wc_free_move's `ok` is transport-level only: the command was dispatched, not
    accepted. A 4-tile hop is silently rejected by the server (beyond movement
    range) while the call still reports ok - that is how twelve rounds of this
    check "moved" without ever closing the 7-tile gap. So: single step, then
    re-read the board and compare.
    """
    nx, ny = unit["gridX"] + 1, unit["gridY"]
    if nx > 7 or (nx, ny) in occupied:
        return False
    _send(conn, "wc_free_move",
          {"controller_index": 0, "unit_id": unit["unitId"], "x": nx, "y": ny})
    time.sleep(1.5)
    after = {u["unitId"]: (u["gridX"], u["gridY"]) for u in _battle_units(conn, 0)}
    moved = after.get(unit["unitId"]) == (nx, ny)
    verdict = "ok" if moved else "REJECTED"
    print(f'    move {unit["unitId"]} -> ({nx},{ny}): {verdict}')
    return moved


def main() -> int:
    ts = int(time.time()) % 1000000
    ue_user, bot_user = f"vf_ue_{ts}", f"vf_bot_{ts}"
    pwd = "Test1234"
    ensure_test_user(ue_user, pwd, f"{ue_user}@x.com")
    ensure_test_user(bot_user, pwd, f"{bot_user}@x.com")

    bot = OpponentBot(get_test_jwt(bot_user, pwd), name="opp")
    if not bot.start():
        print("FAIL: bot did not connect")
        return 1
    time.sleep(2.0)

    conn = UnrealConnection()
    conn.connect()
    try:
        if not _pie_restart_n1(conn):
            print("FAIL: PIE did not start")
            return 1
        if not _ue_login_findgame(conn, ue_user, pwd):
            print("FAIL: draft not reached")
            return 1
        _ue_draft(conn)

        dl = time.monotonic() + 45
        while time.monotonic() < dl:
            if _has_uw_any(conn, "WBP_DeploymentScreen"):
                break
            time.sleep(1.5)
        n = _deploy_client(conn, 0)
        print(f"  deployed: {n}/5")
        _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        if _wait_pie_widget(conn, ("Mulligan",), 40, "mulligan"):
            time.sleep(1.5)
            _send(conn, "invoke_button_click",
                  {"widget_name": "AcceptHandButton", "controller_index": 0})

        if not _wait_battle_reached(conn, 60):
            print("FAIL: battle not reached")
            return 1

        mark = _log_mark()
        attacks_sent = 0
        for rnd in range(16):
            dl = time.monotonic() + 25
            while time.monotonic() < dl:
                if _send(conn, "wc_get_battle_state", {"controller_index": 0}).get("my_turn"):
                    break
                time.sleep(1.0)
            else:
                print(f"  turn never came (round {rnd})")
                break

            units = [u for u in _battle_units(conn, 0) if u.get("alive")]
            mine = [u for u in units if u.get("unitId", "").endswith("-red")]
            foes = [u for u in units if not u.get("unitId", "").endswith("-red")]
            if not mine or not foes:
                break
            occupied = {(u["gridX"], u["gridY"]) for u in units}

            pairs = sorted(((a, min(foes, key=lambda e, a=a: _dist(a, e))) for a in mine),
                           key=lambda p: _dist(p[0], p[1]))
            best_a, best_f = pairs[0]
            gap = _dist(best_a, best_f)
            print(f'  round {rnd}: closest pair {best_a["unitId"]} -> '
                  f'{best_f["unitId"]}, gap {gap}')

            if gap > ATTACK_RANGE_GUESS:
                _advance(conn, best_a, best_f, occupied)
            else:
                # Two attacks back to back, no waiting for the first result:
                # exactly the race that used to play the sequence on the wrong pair.
                # Order matters: QueuePendingExecute has ONE slot, so the second
                # AttackUnit overwrites the first and only the LAST command ever
                # reaches the server. Fire the far pair first and the in-range
                # pair last, otherwise the surviving command is the hopeless one
                # and the server answers OUT_OF_RANGE (that is what sank run 3).
                for a, f in reversed(pairs[:2]):
                    _send(conn, "wc_attack", {"controller_index": 0,
                                              "attacker_unit_id": a["unitId"],
                                              "target_unit_id": f["unitId"],
                                              "x": f["gridX"], "y": f["gridY"]})
                    attacks_sent += 1
                time.sleep(5.0)

            _send(conn, "wc_end_turn", {"controller_index": 0})
            time.sleep(2.0)
            if attacks_sent >= 4:
                break

        time.sleep(2.0)
        tail = _log_since(mark)
        seq = re.findall(r"CombatVFX[^\n]*?([\w\-]+) -> ([\w\-]+)", tail)
        ev = re.findall(r"ATTACK ([\w\-]+) -> ([\w\-]+)", tail)
        rejected = tail.count("OUT_OF_RANGE")
        print("")
        print(f"  attacks dispatched : {attacks_sent}")
        print(f"  server OUT_OF_RANGE: {rejected}")
        print(f"  combat events      : {len(ev)} {ev}")
        print(f"  vfx sequences      : {len(seq)} {seq}")

        if not ev:
            print("=== RESULT: INCONCLUSIVE - no attack landed ===")
            return 2
        ok = len(seq) == len(ev) and sorted(seq) == sorted(ev)
        print("=== RESULT: %s ===" % ("PASS - every sequence matches a server pair"
                                      if ok else "FAIL - sequences differ from events"))
        return 0 if ok else 1
    finally:
        _send(conn, "wc_surrender", {"controller_index": 0})
        bot.close()


if __name__ == "__main__":
    raise SystemExit(main())
