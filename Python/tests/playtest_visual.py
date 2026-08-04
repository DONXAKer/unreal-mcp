"""2-player ВИЗУАЛЬНЫЙ playtest: два отдельных UnrealEditor (порты 55557/55558),
каждый PIE n=1, controller 0 — проходят все фазы игры; на каждой фазе с ОБОИХ
клиентов снимается скриншот + JSON-снимок состояния. Для каждого кадра гоняется
pixel-diff против baseline (_screenshot_diff), а всё вместе складывается в
Saved/PlaytestRuns/<ts>/ с manifest.json — его читает vision-агент
`visual-playtest-reviewer`, который «смотрит» PNG и ловит ГРАФИЧЕСКИЕ баги
(съехавший layout, пропавшие портреты/юниты, наложения, чёрный экран), а сам
harness ловит ЛОГИЧЕСКИЕ (не та фаза, юнит не на той клетке, доска не меняется).

Почему два Editor-процесса, а не PIE n=2: multi-client PIE флачит на
widget-enumeration (memory project_multiclient_pie_limitation). Здесь каждый
игрок — отдельный Editor (controller 0), enumeration стабилен; сервер матчит
двух реальных клиентов сам (бот не нужен). Требует UnrealMCP 3.2.0+
(конфигурируемый порт: Editor #2 запущен с `-MCPPort=55558`).

Запуск из D:\\WarCard\\unreal-mcp\\Python (оба редактора уже подняты skill'ом):
    uv run python tests/playtest_visual.py [<ts>]
    # первый прогон для сидирования эталонов:
    $env:WC_UPDATE_BASELINES="1"; uv run python tests/playtest_visual.py

Требования: WarCard сервер :8081; два Editor-инстанса с MCP на 55557 и 55558.
"""

from __future__ import annotations

import json
import logging
import re
import shutil
import sys
import threading
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

# Подавляем шумный INFO unreal_mcp_server (печатает полный JSON, крашится на
# Windows cp1251 на Unicode-символах в widget tree).
logging.getLogger("UnrealMCP").setLevel(logging.ERROR)

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import (
    StompWs,
    _ATTACK_CARD_TYPES,
    _DMG_KEY_BY_TYPE,
    _jwt_subject,
)
from tests._screenshot_diff import (
    SCREENSHOTS_DIR,
    BaselineMissing,
    ScreenshotMismatch,
    assert_screenshot_matches,
)
from tests.smoke_pie_full_game import (
    UNITS_TO_PICK,
    _battle_units,
    _deploy_client,
    _find_node,
    _node_first_text,
    _send,
    _suffix,
)
from unreal_mcp_server import UnrealConnection

# Порт на сторону. Editor #1 — дефолт 55557, Editor #2 — `-MCPPort=55558`.
PORTS = {"p1": 55557, "p2": 55558}

RUNS_ROOT = Path("D:/WarCard/client/Saved/PlaytestRuns")

# Пороги pixel-diff: статичные экраны строже, боевые мягче (анимации/таймеры).
THRESHOLDS = {
    "login": 0.97,
    "main_menu": 0.97,
    "matchmaking": 0.93,
    "dice_roll_1": 0.93,
    "draft": 0.95,
    "deployment": 0.95,
    "dice_roll_2": 0.93,
    "mulligan": 0.95,
    "battle": 0.90,
    "battle_field": 0.88,
    "battle_after": 0.88,
    "battle_attack": 0.88,
    "battle_death":  0.85,
    "game_result": 0.95,
}

# Ожидаемый виджет-маркер фазы — агент сверяет картинку с этим контекстом.
EXPECTED_WIDGET = {
    "login": "WBP_Login",
    "main_menu": "WBP_MainMenu",
    "matchmaking": "WBP_Matchmaking",
    "dice_roll_1": "WBP_DiceRoll",
    "draft": "WBP_Draft",
    "deployment": "WBP_DeploymentScreen",
    "dice_roll_2": "WBP_DiceRoll",
    "mulligan": "WBP_Mulligan",
    "battle": "WBP_BattleHUD",
    "battle_field": "WBP_BattleHUD",
    "battle_after": "WBP_BattleHUD",
    "battle_attack": "WBP_BattleHUD",
    "battle_death":  "WBP_BattleHUD",
    "game_result": "WBP_GameResult",
}


# Ожидаемые визуальные факты по каждой фазе (per-phase VLM contract).
# Используются _run_vlm_checks(): каждый пункт — проверяемое утверждение о кадре.
VISUAL_CHECKS: dict[str, list[str]] = {
    "login": [
        "Поля ввода логина и пароля видны и не обрезаны",
        "Кнопка входа (LoginButton) видна",
        "Нет устаревших виджетов других экранов поверх экрана логина",
    ],
    "main_menu": [
        "Кнопка поиска игры (FindGame) видна и не обрезана",
        "Нет устаревших виджетов поверх главного меню",
    ],
    "matchmaking": [
        "Виден индикатор/текст поиска игры",
        "Нет устаревших виджетов поверх экрана матчмейкинга",
    ],
    "draft": [
        "Список юнитов для выбора содержит карточки — не пустой скроллбокс",
        "Карточки юнитов содержат имя (не плейсхолдер 'Text Block')",
        "Нет устаревших виджетов других экранов поверх драфта",
    ],
    "deployment": [
        "Сетка расстановки (GameBoard) видна — не чёрный прямоугольник",
        "Список юнитов для расстановки виден в боковой панели",
        "Нет устаревших виджетов поверх экрана расстановки",
    ],
    "mulligan": [
        "Карты в руке видны (не пустой экран без карт)",
        "Кнопки AcceptHand или ConfirmMulligan видны",
        "Нет устаревших виджетов поверх экрана муллигана",
    ],
    "battle": [
        "AP-счётчик (очки действия) виден и содержит числовое значение",
        "Индикатор хода (My Turn / Opponent's Turn) виден",
        "Кнопка EndTurn видна",
        "Нет устаревших виджетов других экранов поверх HUD боя",
    ],
    "battle_field": [
        "Поле боя (сетка 8×8) видно — не чёрный экран",
        "Юниты-актёры присутствуют на сетке",
    ],
    "game_result": [
        "Заголовок результата (Victory / Defeat / Победа / Поражение) виден",
        "Итоговый счёт (Score / totalScore) отображён числом",
        "Статистика (уничтоженных юнитов или нанесённого урона) видна",
        "Кнопка возврата в главное меню видна",
        "Нет устаревших виджетов поверх экрана результата",
    ],
}

# ────────────────────────────────────────────────────────────────────────
# Single-connection (controller 0) хелперы — в каждом редакторе один клиент.
# ────────────────────────────────────────────────────────────────────────

def _uw_classes(conn: UnrealConnection) -> list[str]:
    tree = _send(conn, "get_widget_tree", {"controller_index": 0})
    return [uw.get("class", "") for uw in (tree.get("user_widgets") or [])
            if uw.get("is_in_viewport")]


def _has_uw(conn: UnrealConnection, substr: str) -> bool:
    if any(substr in c for c in _uw_classes(conn)):
        return True
    # Fallback: current_widget из pie_status (get_widget_tree иногда не
    # помечает виджет is_in_viewport=True во втором PIE-окне).
    status = _send(conn, "pie_status", {})
    clients = status.get("clients") or []
    cw = (clients[0].get("current_widget") or "") if clients else ""
    return bool(substr in cw)


def _wait_uw(conn: UnrealConnection, substr: str, timeout: float = 30.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _has_uw(conn, substr):
            return True
        time.sleep(1.0)
    return False


def _wait_uw_all(conns: dict[str, UnrealConnection], substr: str, timeout: float = 40.0) -> bool:
    deadline = time.monotonic() + timeout
    pending = set(conns)
    while time.monotonic() < deadline and pending:
        for side in list(pending):
            if _has_uw(conns[side], substr):
                pending.discard(side)
        if not pending:
            return True
        time.sleep(1.0)
    return not pending


def _draft_entries(conn: UnrealConnection) -> list[tuple[str, str]]:
    """(имя кнопки, имя юнита) для CatalogEntryButton в Scroll_AvailableUnits."""
    out: list[tuple[str, str]] = []
    tree = _send(conn, "get_widget_tree", {"controller_index": 0})
    for uw in (tree.get("user_widgets") or []):
        if "Draft" not in uw.get("class", ""):
            continue
        scroll = _find_node(uw.get("root"), lambda n: n.get("name") == "Scroll_AvailableUnits")
        if not scroll:
            continue
        for ch in (scroll.get("children") or []):
            if "Button" in ch.get("class", ""):
                out.append((ch.get("name"), _node_first_text(ch)))
    return out


def _collect_state(conn: UnrealConnection) -> dict[str, Any]:
    """Богатый снимок состояния клиента (current_widget + что отдаст phase-subsystem)."""
    out: dict[str, Any] = {}
    status = _send(conn, "pie_status", {})
    clients = status.get("clients") or []
    out["current_widget"] = clients[0].get("current_widget") if clients else None
    out["pie_running"] = status.get("is_running")
    out["viewport_widgets"] = _uw_classes(conn)
    for cmd, key in (
        ("wc_get_selection_state", "selection"),
        ("wc_get_deployment_state", "deployment"),
        ("wc_get_battle_state", "battle"),
    ):
        r = _send(conn, cmd, {"controller_index": 0})
        if isinstance(r, dict) and not r.get("error"):
            out[key] = r
    bu = _battle_units(conn, 0)
    if bu:
        out["battle_units"] = bu
    return out


# ────────────────────────────────────────────────────────────────────────
# Захват кадра: скриншот + state + копия в run-папку + pixel-diff.
# ────────────────────────────────────────────────────────────────────────

def _capture(side: str, conn: UnrealConnection, phase: str, run_dir: Path,
             manifest: dict[str, Any], *, show_ui: bool = True,
             do_diff: bool = True) -> dict[str, Any]:
    fname = f"pt_{side}_{phase}.png"
    src = SCREENSHOTS_DIR / fname
    # Снимаем заново — удаляем прошлый файл, чтобы дождаться именно свежего.
    try:
        src.unlink()
    except FileNotFoundError:
        pass
    _send(conn, "pie_screenshot", {"filename": fname, "show_ui": show_ui})
    # Скрин пишется на redraw'е (плагин 3.2.1 флашит синхронно) — но ждём файл
    # с запасом на случай задержки ФС/тика.
    deadline = time.monotonic() + 6.0
    while not src.is_file() and time.monotonic() < deadline:
        time.sleep(0.5)

    state = _collect_state(conn)

    # Копия из общей Saved/Screenshots в архив прогона.
    side_dir = run_dir / side
    side_dir.mkdir(parents=True, exist_ok=True)
    dst = side_dir / f"{phase}.png"
    copied = False
    if src.is_file():
        try:
            shutil.copyfile(src, dst)
            copied = True
        except Exception as exc:
            print(f"    [{side}] copy fail {fname}: {exc}")

    diff_result: dict[str, Any] | None = None
    if do_diff and copied:
        threshold = THRESHOLDS.get(phase, 0.95)
        try:
            assert_screenshot_matches(fname, threshold)
            diff_result = {"status": "ok", "threshold": threshold}
        except BaselineMissing:
            diff_result = {"status": "baseline_missing", "threshold": threshold}
        except ScreenshotMismatch as exc:
            diff_result = {"status": "mismatch", "threshold": threshold, "detail": str(exc)}
        except Exception as exc:
            diff_result = {"status": "error", "detail": str(exc)}

    expected = EXPECTED_WIDGET.get(phase, "")
    widget_ok = bool(expected) and any(expected in c for c in state.get("viewport_widgets", []))

    entry = {
        "phase": phase,
        "side": side,
        "expected_widget": expected,
        "widget_present": widget_ok,
        "screenshot": str(dst) if copied else None,
        "screenshot_flat": fname,
        "screenshot_present": copied,
        "show_ui": show_ui,
        "state": state,
        "pixel_diff": diff_result,
    }
    manifest["phases"].append(entry)
    dstat = diff_result.get("status") if diff_result else "—"
    print(f"  [{side}] {phase}: widget={state.get('current_widget')} "
          f"expect={expected}({'ok' if widget_ok else 'MISS'}) "
          f"diff={dstat} png={'ok' if copied else 'MISSING'}")
    return entry


def _capture_both(conns: dict[str, UnrealConnection], phase: str, run_dir: Path,
                  manifest: dict[str, Any], *, show_ui: bool = True,
                  do_diff: bool = True) -> None:
    for side, conn in conns.items():
        _capture(side, conn, phase, run_dir, manifest, show_ui=show_ui, do_diff=do_diff)


def _warm_render(conns: dict[str, UnrealConnection], rounds: int = 4) -> None:
    """Прогрев рендера перед захватом 3D-сцены. Фоновое new_window-окно
    отрисовывает поле боя НЕ с первого кадра (троттлинг фона + стриминг сцены),
    поэтому ранний pie_screenshot выходит чёрным. Форсируем несколько Draw'ов
    throwaway-снимками, чтобы сцена успела сойтись до реального захвата."""
    for _ in range(rounds):
        for conn in conns.values():
            _send(conn, "pie_screenshot", {"filename": "_warmup.png", "show_ui": False})
        time.sleep(1.2)


def _add_check(manifest: dict[str, Any], name: str, passed: bool, detail: str = "") -> None:
    manifest["logic_checks"].append({"name": name, "passed": bool(passed), "detail": detail})
    print(f"  CHECK {'PASS' if passed else 'FAIL'}: {name} {('— ' + detail) if detail else ''}")


def _run_vlm_checks(run_dir: Path, manifest: dict[str, Any]) -> None:  # noqa: ARG001
    """VLM-ревью каждой фазы через Anthropic SDK (claude-haiku).

    Для каждой пары (phase, side) со скриншотом отправляет изображение +
    список пунктов из VISUAL_CHECKS. Полученные вердикты
    [{check, phase, pass, evidence}] дописываются в manifest['logic_checks'].
    Если anthropic не установлен — вердикты пропускаются (graceful skip).
    """
    try:
        import anthropic
        import base64
    except ImportError:
        print("  [VLM] anthropic SDK не установлен — VLM-чеки пропущены "
              "(установи: uv add anthropic --dev)")
        return

    client = anthropic.Anthropic()
    checked = 0

    for entry in manifest["phases"]:
        phase = entry["phase"]
        side = entry["side"]
        checks = VISUAL_CHECKS.get(phase)
        if not checks or not entry.get("screenshot_present"):
            continue

        png_path = Path(entry.get("screenshot") or "")
        if not png_path.is_file():
            _add_check(manifest, f"vlm:{side}:{phase}:screenshot_missing", False,
                       f"файл не найден: {png_path}")
            continue

        with png_path.open("rb") as fh:
            image_b64 = base64.standard_b64encode(fh.read()).decode()

        checks_text = "\n".join(f"- {c}" for c in checks)
        prompt = (
            f"Фаза игры: {phase}, сторона: {side}.\n"
            f"Проверь следующие пункты на скриншоте.\n"
            f"Верни ТОЛЬКО JSON-массив без markdown и без пояснений:\n"
            f'[{{"check":"...","phase":"{phase}","pass":true/false,"evidence":"..."}}]\n\n'
            f"Пункты для проверки:\n{checks_text}"
        )

        try:
            resp = client.messages.create(
                model="claude-haiku-4-5-20251001",
                max_tokens=1024,
                messages=[{
                    "role": "user",
                    "content": [
                        {
                            "type": "image",
                            "source": {
                                "type": "base64",
                                "media_type": "image/png",
                                "data": image_b64,
                            },
                        },
                        {"type": "text", "text": prompt},
                    ],
                }],
            )
        except Exception as exc:  # noqa: BLE001
            # API error (нет ключа, rate limit и т.п.) — пропускаем, не роняем summary
            print(f"  [VLM] {side}:{phase} пропущен: {str(exc)[:120]}")
            continue

        raw = resp.content[0].text.strip() if resp.content else ""
        # Модель иногда оборачивает JSON в ```json ... ``` — извлекаем массив.
        m = re.search(r"\[.*?\]", raw, re.DOTALL)
        if not m:
            _add_check(manifest, f"vlm:{side}:{phase}:parse_error", False,
                       f"VLM не вернул JSON-массив: {raw[:200]}")
            continue

        try:
            verdicts: list[dict[str, Any]] = json.loads(m.group(0))
        except json.JSONDecodeError as exc:
            _add_check(manifest, f"vlm:{side}:{phase}:json_error", False,
                       f"{exc}: {raw[:200]}")
            continue

        for v in verdicts:
            check_name = f"vlm:{side}:{phase}:{v.get('check', '')[:50]}"
            _add_check(manifest, check_name,
                       bool(v.get("pass", False)),
                       v.get("evidence", ""))
        checked += len(verdicts)

    print(f"  [VLM] итого {checked} VLM-вердиктов записано в logic_checks")


# ────────────────────────────────────────────────────────────────────────
# STOMP-инфраструктура для боевой фазы: захват game_id + BattlePlayer.
# ────────────────────────────────────────────────────────────────────────

def _start_game_id_spy(token: str, holder: list[str], event: threading.Event) -> StompWs:
    """Подключить фоновый STOMP-клиент, который захватит gameId из game-found.
    Вызывать ДО _login_findgame() — сервер пришлёт game-found всем STOMP-сессиям
    одного Principal (одинаковый JWT → один playerId)."""
    spy = StompWs("127.0.0.1", 8081)

    def _on(dest: str, body: dict[str, Any]) -> None:
        if dest.endswith("/game-found") and not holder:
            gid = body.get("gameId")
            if gid:
                holder.append(str(gid))
                event.set()

    spy.on_message = _on
    if spy.connect(token):
        spy.subscribe("/user/queue/matchmaking/game-found")
    return spy


class BattlePlayer:
    """Тонкий синхронный STOMP-клиент одного игрока в BATTLE.
    Треды-безопасен на чтение (recv-тред StompWs) + запись из основного треда."""

    def __init__(self, token: str, game_id: str,
                 server_host: str = "127.0.0.1", server_port: int = 8081):
        self.my_id = _jwt_subject(token)
        self._token = token
        self._gid = game_id
        self._stomp = StompWs(server_host, server_port)
        self._stomp.on_message = self._on_msg
        self._lock = threading.Lock()
        self._hand: list[dict[str, Any]] = []
        self._state: dict[str, Any] = {}
        self._turn_ev = threading.Event()
        self._pending: dict[str, tuple[threading.Event, dict[str, Any]]] = {}

    def connect(self) -> bool:
        if not self._stomp.connect(self._token):
            return False
        gid = self._gid
        self._stomp.subscribe(f"/topic/game/{gid}/state")
        self._stomp.subscribe(f"/user/topic/game/{gid}/cards-update")
        self._stomp.subscribe(f"/topic/game/{gid}/turn-changed")
        self._stomp.subscribe(f"/topic/game/{gid}/result")
        return True

    def _on_msg(self, dest: str, body: dict[str, Any]) -> None:
        if dest.endswith("/state"):
            with self._lock:
                self._state = body
        elif dest.endswith("/cards-update"):
            cards = body.get("handCards") or []
            with self._lock:
                self._hand = [{"id": c.get("id", ""), "type": c.get("actionType", "")}
                              for c in cards if isinstance(c, dict)]
        elif dest.endswith("/turn-changed"):
            if body.get("activePlayerId") == self.my_id:
                self._turn_ev.set()
        elif dest.endswith("/result"):
            ct = body.get("commandType", "")
            with self._lock:
                entry = self._pending.pop(ct, None)
            if entry is not None:
                ev, h = entry
                h["success"] = bool(body.get("success"))
                h["message"] = str(body.get("message", ""))
                h["data"] = body.get("data")
                ev.set()

    def wait_my_turn(self, timeout: float = 25.0) -> bool:
        self._turn_ev.clear()
        return self._turn_ev.wait(timeout)

    def get_state(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._state)

    def get_hand(self) -> list[dict[str, Any]]:
        with self._lock:
            return list(self._hand)

    def my_units(self) -> list[dict[str, Any]]:
        st = self.get_state()
        return [u for u in st.get("units", [])
                if u.get("ownerId") == self.my_id and u.get("isAlive", True)]

    def foe_units(self) -> list[dict[str, Any]]:
        st = self.get_state()
        return [u for u in st.get("units", [])
                if u.get("ownerId") and u.get("ownerId") != self.my_id
                and u.get("isAlive", True)]

    def execute_attack(self, card: dict[str, Any]) -> bool:
        """Двухэтапная атака: select-action-card + execute-action."""
        mine = self.my_units()
        foes = self.foe_units()
        if not mine or not foes or not card.get("id"):
            return False
        # Ближайшая пара (attacker, foe) — минимизируем расстояние между конкретным
        # атакующим и конкретной целью, чтобы сервер не отверг атаку «вне дальности».
        best_dist = 9999
        attacker, foe = mine[0], foes[0]
        for a in mine:
            for f in foes:
                d = (abs(int(a.get("gridX", 0)) - int(f.get("gridX", 0)))
                     + abs(int(a.get("gridY", 0)) - int(f.get("gridY", 0))))
                if d < best_dist:
                    best_dist, attacker, foe = d, a, f
        gid = self._gid

        # 1. Выбрать карту
        ev_s = threading.Event()
        h_s: dict[str, Any] = {}
        with self._lock:
            self._pending["select-action-card"] = (ev_s, h_s)
        self._stomp.send_json("/app/game/select-action-card",
                              {"gameId": gid, "actionCardId": card["id"]})
        ev_s.wait(3.0)

        # 2. Выполнить атаку
        ev_e = threading.Event()
        h_e: dict[str, Any] = {}
        with self._lock:
            self._pending["execute-action"] = (ev_e, h_e)
        self._stomp.send_json("/app/game/execute-action", {
            "gameId": gid,
            "actionCardId": card["id"],
            "targetX": int(foe.get("gridX", 0)),
            "targetY": int(foe.get("gridY", 0)),
            "targetUnitId": foe.get("unitId", ""),
            "attackerUnitId": attacker.get("unitId", ""),
        })
        ev_e.wait(5.0)
        return bool(h_e.get("success"))

    def execute_move(self, card: dict[str, Any], unit_id: str, tx: int, ty: int) -> bool:
        """Двухэтапное перемещение: select-action-card + execute-action (MOVE/FORCED_MARCH)."""
        gid = self._gid

        ev_s = threading.Event()
        h_s: dict[str, Any] = {}
        with self._lock:
            self._pending["select-action-card"] = (ev_s, h_s)
        self._stomp.send_json("/app/game/select-action-card",
                              {"gameId": gid, "actionCardId": card["id"]})
        ev_s.wait(3.0)

        ev_e = threading.Event()
        h_e: dict[str, Any] = {}
        with self._lock:
            self._pending["execute-action"] = (ev_e, h_e)
        self._stomp.send_json("/app/game/execute-action", {
            "gameId": gid,
            "actionCardId": card["id"],
            "targetX": tx,
            "targetY": ty,
            "targetUnitId": unit_id,
            "attackerUnitId": "",
        })
        ev_e.wait(5.0)
        ok = bool(h_e.get("success"))
        if not ok:
            print(f"    execute_move failed: {h_e.get('message', '')}")
        return ok

    def end_turn(self) -> None:
        self._stomp.send_json("/app/game/end-turn", {"gameId": self._gid})

    def close(self) -> None:
        self._stomp.close()


def _play_battle_to_victory(
    conns: dict[str, UnrealConnection],
    run_dir: Path,
    manifest: dict[str, Any],
    p1_token: str,
    p2_token: str,
    p1_id: str,
    p2_id: str,
    game_id: str,
) -> tuple[int, bool]:
    """Играем BATTLE через STOMP execute-action до ALL_UNITS_DESTROYED.
    Возвращает (cards_executed, first_death_captured)."""
    p1_bp = BattlePlayer(p1_token, game_id)
    p2_bp = BattlePlayer(p2_token, game_id)

    if not p1_bp.connect():
        print("  WARN: BattlePlayer p1 STOMP не подключился")
    if not p2_bp.connect():
        print("  WARN: BattlePlayer p2 STOMP не подключился")

    time.sleep(1.5)  # дать recv-тредам осесть + получить первый /state

    cards_executed = 0
    first_death_captured = False
    natural_win = False
    MAX_TURNS = 60

    try:
        for turn_n in range(MAX_TURNS):
            # Определяем активного игрока: my_turn per-side через MCP
            st_p1 = _send(conns["p1"], "wc_get_battle_state", {"controller_index": 0})
            st_p2 = _send(conns["p2"], "wc_get_battle_state", {"controller_index": 0})
            p1_active = bool(st_p1.get("my_turn") or st_p1.get("myTurn"))
            p2_active = bool(st_p2.get("my_turn") or st_p2.get("myTurn"))

            # Читаем юниты из p1's перспективы (общее поле)
            all_units = _battle_units(conns["p1"], 0)
            red_alive = [u for u in all_units
                         if u.get("alive") and _suffix(u.get("unitId", "")) == "red"]
            blue_alive = [u for u in all_units
                          if u.get("alive") and _suffix(u.get("unitId", "")) == "blue"]

            # Первая гибель юнита — проверяем ДО проверки победы,
            # иначе при резком переходе total 10→0 блок никогда не сработает.
            total_alive = len(red_alive) + len(blue_alive)
            if total_alive < 10 and not first_death_captured:
                print(f"  первый юнит погиб! живых={total_alive}")
                _capture_both(conns, "battle_death", run_dir, manifest, show_ui=False)
                first_death_captured = True

            # Победа? Double-check через 1.5s — _battle_units может вернуть
            # временно пустой список (transient MCP ответ), что даст false positive.
            if not red_alive or not blue_alive:
                time.sleep(1.5)
                confirm = _battle_units(conns["p1"], 0)
                red_confirm = [u for u in confirm
                               if u.get("alive") and _suffix(u.get("unitId", "")) == "red"]
                blue_confirm = [u for u in confirm
                                if u.get("alive") and _suffix(u.get("unitId", "")) == "blue"]
                if not red_confirm or not blue_confirm:
                    if not first_death_captured:
                        _capture_both(conns, "battle_death", run_dir, manifest, show_ui=False)
                        first_death_captured = True
                    print(f"  ход {turn_n}: победа! red={len(red_confirm)} blue={len(blue_confirm)}")
                    natural_win = True
                    break
                # Первый чек был ложным — обновляем живых и продолжаем
                red_alive, blue_alive = red_confirm, blue_confirm

            # Выбираем активного бота
            if p1_active:
                active_bp, active_conn, active_side = p1_bp, conns["p1"], "p1"
            elif p2_active:
                active_bp, active_conn, active_side = p2_bp, conns["p2"], "p2"
            else:
                time.sleep(1.5)
                continue

            # Ждём подтверждения хода из STOMP (turn-changed)
            active_bp.wait_my_turn(timeout=3.0)
            time.sleep(0.4)  # cards-update приходит чуть позже turn-changed

            # Движение через карты MOVE/FORCED_MARCH из руки (тратит AP на сервере).
            # Сдвигаем ближайшего к врагу юнита на 1 клетку в его сторону за итерацию.
            _move_types = {"MOVE", "FORCED_MARCH"}
            _moved_n = 0
            for _attempt in range(3):  # до 2 удачных перемещений за ход
                if _moved_n >= 2:
                    break
                move_card = next(
                    (c for c in active_bp.get_hand() if c.get("type") in _move_types), None)
                if not move_card:
                    break  # нет карт движения в руке
                _st_my = active_bp.my_units()
                _st_foe = active_bp.foe_units()
                if not _st_my or not _st_foe:
                    break
                _best_u, _near_f, _best_dist = None, None, 9999
                for _u in _st_my:
                    for _f in _st_foe:
                        _d = (abs(int(_u.get("gridX", 0)) - int(_f.get("gridX", 0))) +
                              abs(int(_u.get("gridY", 0)) - int(_f.get("gridY", 0))))
                        if _d < _best_dist:
                            _best_dist, _best_u, _near_f = _d, _u, _f
                if _best_u is None or _best_dist <= 1:
                    break  # все юниты уже в дальности атаки
                _dx = int(_near_f.get("gridX", 0)) - int(_best_u.get("gridX", 0))
                _dy = int(_near_f.get("gridY", 0)) - int(_best_u.get("gridY", 0))
                _nx = int(_best_u.get("gridX", 0)) + (1 if _dx > 0 else -1 if _dx < 0 else 0)
                _ny = int(_best_u.get("gridY", 0)) + (1 if _dy > 0 else -1 if _dy < 0 else 0)
                ok_mv = active_bp.execute_move(
                    move_card, _best_u.get("unitId", ""), _nx, _ny)
                if ok_mv:
                    _moved_n += 1
                    print(f"    move {_best_u.get('unitId','')} -> ({_nx},{_ny})")
                    time.sleep(0.5)  # дать STOMP state обновиться после move
                else:
                    break  # AP=0 или клетка занята — прекращаем движение

            # Атаки: сервер контролирует AP через execute-action result.
            attacked_this_turn = False
            while True:
                hand = active_bp.get_hand()
                card = next((c for c in hand if c.get("type") in _ATTACK_CARD_TYPES), None)
                if not card:
                    break  # нет карт атаки
                if not active_bp.foe_units():
                    break  # нет врагов
                ok_atk = active_bp.execute_attack(card)
                if ok_atk:
                    cards_executed += 1
                    attacked_this_turn = True
                    print(f"  ход {turn_n}: {active_side} атакует ({card['type']})")
                else:
                    break  # сервер отказал (AP=0 или ошибка)
                time.sleep(0.3)

            # Скриншот после первой атаки в игре (оба экрана, однократно)
            if attacked_this_turn and "battle_attack" not in {p["phase"] for p in manifest.get("phases", [])}:
                other_side = "p2" if active_side == "p1" else "p1"
                _capture(active_side, active_conn,
                         "battle_attack", run_dir, manifest, show_ui=True)
                _capture(other_side, conns[other_side],
                         "battle_attack", run_dir, manifest, show_ui=True)

            active_bp.end_turn()
            print(f"  ход {turn_n}: {active_side} end-turn (карт всего={cards_executed})")
            time.sleep(1.0)
    finally:
        p1_bp.close()
        p2_bp.close()

    return cards_executed, first_death_captured, natural_win


# ────────────────────────────────────────────────────────────────────────
# PIE lifecycle / драйверы фаз (через два соединения).
# ────────────────────────────────────────────────────────────────────────

def _start_pie(side: str, conn: UnrealConnection) -> bool:
    _send(conn, "pie_stop", {})
    dl = time.monotonic() + 25
    while time.monotonic() < dl:
        if not (_send(conn, "pie_status", {}) or {}).get("is_running"):
            break
        time.sleep(1.0)
    time.sleep(1.5)
    for _ in range(3):
        # mode="new_window" (плагин 3.3.0+) — PIE в отдельном presented-окне;
        # Slate-readback (3.3.1) снимает с него реальный кадр (не чёрный).
        _send(conn, "pie_start", {"num_clients": 1, "mode": "new_window"})
        dl = time.monotonic() + 30
        while time.monotonic() < dl:
            st = _send(conn, "pie_status", {})
            cs = st.get("clients") or []
            if st.get("is_running") and cs and cs[0].get("current_widget"):
                print(f"  [{side}] PIE up, widget={cs[0].get('current_widget')}")
                return True
            time.sleep(1.5)
        print(f"  [{side}] pie_start не поднял PIE — повтор")
    return False


def _login_findgame(conns: dict[str, UnrealConnection], users: dict[str, str], pwd: str) -> None:
    for side, conn in conns.items():
        _send(conn, "set_text_on_widget",
              {"widget_name": "LoginUsernameInput", "text": users[side], "controller_index": 0})
        _send(conn, "set_text_on_widget",
              {"widget_name": "LoginPasswordInput", "text": pwd, "controller_index": 0})
        _send(conn, "invoke_button_click", {"widget_name": "LoginButton", "controller_index": 0})


def _snake_draft(conns: dict[str, UnrealConnection]) -> dict[str, int]:
    counts = {side: 0 for side in conns}
    for pick in range(10):
        done = False
        for attempt in range(30):
            for side, conn in conns.items():
                entries_before = _draft_entries(conn)
                if not entries_before:
                    continue
                btn = entries_before[0][0]
                r = _send(conn, "invoke_button_click",
                          {"widget_name": btn, "controller_index": 0})
                if not (isinstance(r, dict) and r.get("ok") and not r.get("error")):
                    continue
                # Проверяем что сервер принял пик: каталог должен уменьшиться.
                # Сервер отклоняет пик не своего хода — тогда каталог не меняется.
                time.sleep(0.6)
                entries_after = _draft_entries(conn)
                if len(entries_after) < len(entries_before):
                    counts[side] += 1
                    print(f"  pick {pick}: {side} -> {btn}")
                    done = True
                    break
            if done:
                break
            time.sleep(0.4)
        if not done:
            print(f"  pick {pick}: не удалось — стоп драфта")
            break
        time.sleep(2.0)  # сервер broadcast draft-update + UI rebuild (два Editor = медленнее)
    return counts


def _active_side(conns: dict[str, UnrealConnection], wait: float = 0.0) -> str | None:
    """Сторона с my_turn=True. Опционально ждём wait секунд (бой/dice_roll_2
    могут назначить ход не сразу)."""
    deadline = time.monotonic() + wait
    while True:
        for side, conn in conns.items():
            st = _send(conn, "wc_get_battle_state", {"controller_index": 0})
            if st.get("my_turn"):
                return side
        if time.monotonic() >= deadline:
            return None
        time.sleep(1.0)


def _battle_moves(conns: dict[str, UnrealConnection], max_turns: int = 8) -> int:
    """Детерминированный бой без LLM: активный клиент двигает юниты к врагу + end-turn."""
    turns = 0
    for turn in range(max_turns):
        # Ждём, пока чей-то my_turn станет True, и действуем ИМЕННО за эту сторону.
        # (Не перечитываем my_turn повторно во вложенном цикле — reflection-чтение
        # IsMyTurn флакает, из-за чего ход мог теряться: turns=0 при активной стороне.)
        active_side = _active_side(conns, wait=20.0 if turn == 0 else 5.0)
        if active_side is None:
            break
        conn = conns[active_side]
        units = [u for u in _battle_units(conn, 0) if u.get("alive")]
        for u in units:
            enemies = [e for e in units
                       if _suffix(e.get("unitId", "")) != _suffix(u.get("unitId", ""))]
            if not enemies:
                continue
            tgt = min(enemies, key=lambda e: abs(e["gridX"] - u["gridX"]) + abs(e["gridY"] - u["gridY"]))
            _send(conn, "wc_attack", {"controller_index": 0, "attacker_unit_id": u["unitId"],
                                      "target_unit_id": tgt["unitId"], "x": tgt["gridX"], "y": tgt["gridY"]})
            nx = u["gridX"] + (1 if tgt["gridX"] > u["gridX"] else -1 if tgt["gridX"] < u["gridX"] else 0)
            ny = u["gridY"] + (1 if tgt["gridY"] > u["gridY"] else -1 if tgt["gridY"] < u["gridY"] else 0)
            _send(conn, "wc_free_move", {"controller_index": 0, "unit_id": u["unitId"], "x": nx, "y": ny})
        _send(conn, "wc_end_turn", {"controller_index": 0})
        turns += 1
        print(f"  ход {turn}: {active_side} отыграл ({len(units)} юнитов)")
        time.sleep(1.2)
    return turns


# ────────────────────────────────────────────────────────────────────────

def main() -> int:
    ts = sys.argv[1] if len(sys.argv) > 1 else str(int(time.time()))
    pwd = "Test1234"
    short = ts[-6:]
    users = {"p1": f"pt_a_{short}", "p2": f"pt_b_{short}"}

    run_dir = RUNS_ROOT / ts
    run_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "ts": ts, "users": users, "ports": PORTS,
        "phases": [], "logic_checks": [],
    }

    print("=== playtest_visual (2 Editor-процесса) ===")
    print(f"ts={ts} users={users} run_dir={run_dir}\n")

    for side in PORTS:
        ensure_test_user(users[side], pwd, f"{users[side]}@x.com")

    p1_token = get_test_jwt(users["p1"], pwd)
    p2_token = get_test_jwt(users["p2"], pwd)
    p1_id = _jwt_subject(p1_token)
    p2_id = _jwt_subject(p2_token)

    # Подключение к обоим редакторам.
    conns: dict[str, UnrealConnection] = {}
    for side, port in PORTS.items():
        conn = UnrealConnection(port=port)
        if not conn.connect():
            print(f"FATAL: нет MCP-соединения с {side} на 127.0.0.1:{port}. "
                  f"Запущен ли второй Editor с -MCPPort={port}? Собран ли плагин 3.2.0+?")
            return 2
        conns[side] = conn
    print("оба MCP-соединения установлены\n")

    rc = 0
    _gid_holder: list[str] = []
    _gid_ev = threading.Event()
    _gid_spy: StompWs | None = None
    try:
        # 1. PIE n=1 в каждом редакторе.
        print("--- PIE setup ---")
        for side, conn in conns.items():
            if not _start_pie(side, conn):
                print(f"FATAL: PIE не поднялся в {side}")
                return 1
            # Глушим AddOnScreenDebugMessage из C++ (попадают в скриншоты)
            _send(conn, "execute_console_command",
                  {"command": "DisableAllScreenMessages", "controller_index": 0})
        time.sleep(3)
        _capture_both(conns, "login", run_dir, manifest)

        # Запускаем STOMP-шпиона ДО логина, чтобы перехватить game-found.
        _gid_spy = _start_game_id_spy(p1_token, _gid_holder, _gid_ev)

        # 2. Login → MainMenu.
        print("\n--- login ---")
        _login_findgame(conns, users, pwd)
        if not _wait_uw_all(conns, "WBP_MainMenu", timeout=30):
            print("  WARN: MainMenu не у всех — продолжаем (best-effort)")
        _capture_both(conns, "main_menu", run_dir, manifest)

        # 3. FindGame → Matchmaking (transient) → Draft. Матчмейкинг двух
        #    реальных клиентов иногда не сводится с первого клика — ретраим.
        print("\n--- FindGame -> matchmaking -> draft ---")
        draft_ok = False
        for attempt in range(3):
            for conn in conns.values():
                _send(conn, "invoke_button_click", {"widget_name": "FindGameButton", "controller_index": 0})
            if attempt == 0:
                # matchmaking / dice_roll_1 — best-effort кадры (фазы краткие).
                if _wait_uw_all(conns, "WBP_Matchmaking", timeout=6):
                    _capture_both(conns, "matchmaking", run_dir, manifest)
                if _wait_uw_all(conns, "WBP_DiceRoll", timeout=8):
                    _capture_both(conns, "dice_roll_1", run_dir, manifest)
            if _wait_uw_all(conns, "WBP_Draft", timeout=30):
                draft_ok = True
                break
            print(f"  попытка {attempt}: draft ещё нет — повтор FindGame")
        _add_check(manifest, "draft_reached", draft_ok)
        if not draft_ok:
            print("=== draft не достигнут -> stop ===")
            rc = 1
            return rc
        _capture_both(conns, "draft", run_dir, manifest)

        # 4. Snake draft.
        print("\n--- snake draft ---")
        counts = _snake_draft(conns)
        total = sum(counts.values())
        _add_check(manifest, "draft_complete", total >= 10, f"picks={counts} total={total}/10")

        # 5. Deployment.
        print("\n--- deployment ---")
        deploy_ok = _wait_uw_all(conns, "WBP_DeploymentScreen", timeout=45)
        _add_check(manifest, "deployment_reached", deploy_ok)
        _warm_render(conns)  # дать сетке расстановки отрисоваться (иначе ложно-чёрный кадр)
        _capture_both(conns, "deployment", run_dir, manifest)
        for side, conn in conns.items():
            n = _deploy_client(conn, 0)
            stt = _send(conn, "wc_get_deployment_state", {"controller_index": 0})
            _add_check(manifest, f"deploy_{side}", stt.get("deployed_count") == UNITS_TO_PICK,
                       f"deployed={stt.get('deployed_count')} ready={stt.get('ready')} placed={n}")
        for conn in conns.values():
            _send(conn, "wc_confirm_deployment", {"controller_index": 0})

        # mulligan — оба игрока должны кликнуть AcceptHandButton.
        if _wait_uw_all(conns, "WBP_Mulligan", timeout=20):
            _capture_both(conns, "mulligan", run_dir, manifest)
            time.sleep(1.5)  # дать руке прогрузиться (OnHandCardsUpdated)
            for conn in conns.values():
                _send(conn, "invoke_button_click",
                      {"widget_name": "AcceptHandButton", "controller_index": 0})
            print("  AcceptHandButton кликнут для обоих клиентов")

        # 6. Battle HUD. BattleHUD OR ActionCardHand = клиент в бою
        #    (get_widget_tree может не ловить BattleHUD через is_in_viewport — текущий_widget надёжнее).
        print("\n--- battle ---")
        _bt_deadline = time.monotonic() + 60
        _bt_pending: set[str] = set(conns)
        while time.monotonic() < _bt_deadline and _bt_pending:
            for _side in list(_bt_pending):
                if (_has_uw(conns[_side], "BattleHUD")
                        or _has_uw(conns[_side], "ActionCardHand")):
                    _bt_pending.discard(_side)
            if not _bt_pending:
                break
            time.sleep(1.0)
        battle_ok = not _bt_pending
        _add_check(manifest, "battle_reached", battle_ok)
        time.sleep(2.5)  # дать /state-броадкасту заспавнить юнит-актёры
        _warm_render(conns, rounds=5)  # прогрев: поле боя рендерится не с первого кадра
        _capture_both(conns, "battle", run_dir, manifest)
        _capture_both(conns, "battle_field", run_dir, manifest, show_ui=False)

        # 7. Ждём game_id (шпион поймал его при matchmaking/game-found).
        print("\n--- ожидаем game_id ---")
        gid_ok = _gid_ev.wait(timeout=90.0)
        game_id = _gid_holder[0] if _gid_holder else ""
        _add_check(manifest, "game_id_captured", gid_ok and bool(game_id),
                   f"gameId={game_id}")

        # 8. Играем через execute-action до победы ALL_UNITS_DESTROYED.
        print("\n--- battle to victory ---")
        cards_executed, first_death, natural_win = _play_battle_to_victory(
            conns, run_dir, manifest,
            p1_token, p2_token, p1_id, p2_id, game_id,
        )
        _add_check(manifest, "cards_executed_gt0", cards_executed > 0,
                   f"cards_executed={cards_executed}")
        _add_check(manifest, "first_death_captured", first_death)
        _add_check(manifest, "natural_win", natural_win,
                   "ALL_UNITS_DESTROYED до MAX_TURNS" if natural_win else "MAX_TURNS исчерпан или surrender")

        # 9. Ждём WBP_GameResult (естественный конец — ALL_UNITS_DESTROYED).
        #    Таймаут большой: сервер завершает игру по MAX_TURNS~70 если не убиты все юниты.
        print("\n--- ожидаем GameResult ---")
        result_ok = _wait_uw_all(conns, "WBP_GameResult", timeout=120)
        if not result_ok:
            print("  game_result timeout: принудительная капитуляция p1 для завершения игры")
            _send(conns["p1"], "wc_surrender", {"controller_index": 0})
            # После surrender UE клиент обновляется не сразу — ждём до 60s
            result_ok = _wait_uw_all(conns, "WBP_GameResult", timeout=60)
        _add_check(manifest, "game_result_reached_both", result_ok)
        _capture_both(conns, "game_result", run_dir, manifest)

        # VLM-ревью скриншотов: дописывает вердикты в manifest["logic_checks"].
        print("\n--- VLM visual checks ---")
        _run_vlm_checks(run_dir, manifest)

        passed = all(c["passed"] for c in manifest["logic_checks"])
        manifest["summary"] = {
            "logic_passed": passed,
            "phases_captured": len(manifest["phases"]),
            "checks": len(manifest["logic_checks"]),
        }
        rc = 0 if passed else 1
    finally:
        if _gid_spy is not None:
            _gid_spy.close()
        (run_dir / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nmanifest -> {run_dir / 'manifest.json'}")
        print(f"кадров: {len(manifest['phases'])}, проверок: {len(manifest['logic_checks'])}")

    print(f"\n=== RESULT: {'PASS' if rc == 0 else 'FAIL'} (run {ts}) ===")
    return rc


if __name__ == "__main__":
    sys.exit(main())
