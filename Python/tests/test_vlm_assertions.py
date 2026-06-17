"""TEST-VISUAL-002: unit-тесты VLM-ассертов per-phase.

Проверяют структуру VISUAL_CHECKS и механизм «pass:false роняет прогон».
Не требуют Anthropic SDK, живого Editor или PIE — чистые unit-тесты.

Запуск:
    uv run pytest tests/test_vlm_assertions.py -q
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).parent.parent))

from tests.playtest_visual import VISUAL_CHECKS, _add_check

# Критические фазы, для которых VLM-проверка обязательна.
REQUIRED_PHASES = {"login", "draft", "deployment", "battle", "game_result"}


def _fresh_manifest() -> dict[str, Any]:
    return {"phases": [], "logic_checks": []}


# ────────────────────────────────────────────────────────────────────────
# Структурные проверки VISUAL_CHECKS
# ────────────────────────────────────────────────────────────────────────

def test_visual_checks_covers_required_phases() -> None:
    """Все критически важные фазы имеют per-phase visual checks."""
    missing = REQUIRED_PHASES - VISUAL_CHECKS.keys()
    assert not missing, f"VISUAL_CHECKS не определены для фаз: {missing}"


def test_visual_checks_schema() -> None:
    """Каждая фаза в VISUAL_CHECKS — непустой список строк."""
    for phase, checks in VISUAL_CHECKS.items():
        assert isinstance(checks, list), f"{phase}: ожидался list, получен {type(checks)}"
        assert len(checks) > 0, f"{phase}: список проверок пустой"
        for c in checks:
            assert isinstance(c, str) and c.strip(), (
                f"{phase}: пустая или нестроковая проверка: {c!r}"
            )


def test_visual_checks_stale_widget_check_present() -> None:
    """Критические фазы содержат проверку на устаревшие (stale) виджеты."""
    stale_keyword = "устаревших виджет"
    for phase in ("login", "draft", "battle", "game_result"):
        checks = VISUAL_CHECKS.get(phase, [])
        has_stale_check = any(stale_keyword.lower() in c.lower() for c in checks)
        assert has_stale_check, (
            f"Фаза '{phase}' не содержит проверки на устаревшие виджеты. "
            f"Добавь пункт со словами 'устаревших виджетов' в VISUAL_CHECKS['{phase}']."
        )


# ────────────────────────────────────────────────────────────────────────
# Негативный кейс: stale-виджет → pass:false → rc=1
# ────────────────────────────────────────────────────────────────────────

def test_stale_widget_verdict_fails_run() -> None:
    """VLM-вердикт pass:false (stale-виджет поверх HUD) роняет прогон в rc=1.

    Сценарий: GamePhaseManager не скрыл WBP_Draft при переходе в Battle —
    виджет висит поверх WBP_BattleHUD. VLM обнаруживает это и возвращает
    pass:false. Harness должен считать прогон проваленным (rc=1).
    """
    manifest = _fresh_manifest()

    # Симулируем VLM-вердикт: stale-виджет обнаружен
    _add_check(
        manifest,
        "vlm:p1:battle:Нет устаревших виджетов других экранов поверх HUD боя",
        False,
        "WBP_Draft (экран черновика) виден поверх WBP_BattleHUD — "
        "GamePhaseManager не скрыл виджет при переходе Draft→Battle",
    )

    # Проверяем механизм: passed=False при любом pass:false
    passed = all(c["passed"] for c in manifest["logic_checks"])
    assert not passed, "Вердикт pass:false должен давать passed=False"

    rc = 0 if passed else 1
    assert rc == 1, "passed=False должен давать rc=1 (проваленный прогон)"


def test_multiple_stale_verdicts_all_fail() -> None:
    """Несколько pass:false вердиктов — все роняют прогон."""
    manifest = _fresh_manifest()
    _add_check(manifest, "vlm:p1:battle:stale_check", False, "stale draft виджет")
    _add_check(manifest, "vlm:p2:game_result:stale_check", False, "stale battle hud")
    # Даже один True среди False не спасёт — all() требует все True
    _add_check(manifest, "vlm:p1:login:login_visible", True, "виден экран входа")

    passed = all(c["passed"] for c in manifest["logic_checks"])
    assert not passed


# ────────────────────────────────────────────────────────────────────────
# Позитивный кейс: все pass:true → rc=0
# ────────────────────────────────────────────────────────────────────────

def test_all_vlm_pass_yields_rc0() -> None:
    """Все VLM-вердикты pass:true → прогон проходит (rc=0)."""
    manifest = _fresh_manifest()
    for phase in ("login", "draft", "battle"):
        for check in VISUAL_CHECKS.get(phase, [])[:2]:  # первые 2 из каждой фазы
            _add_check(manifest, f"vlm:p1:{phase}:{check[:40]}", True, "корректно")

    passed = all(c["passed"] for c in manifest["logic_checks"])
    assert passed
    rc = 0 if passed else 1
    assert rc == 0


# ────────────────────────────────────────────────────────────────────────
# Проверка интеграции: _add_check пишет в manifest["logic_checks"]
# ────────────────────────────────────────────────────────────────────────

def test_add_check_writes_to_manifest() -> None:
    """_add_check дописывает запись в manifest['logic_checks']."""
    manifest = _fresh_manifest()
    assert len(manifest["logic_checks"]) == 0

    _add_check(manifest, "vlm:p1:login:test", True, "тест")
    assert len(manifest["logic_checks"]) == 1
    entry = manifest["logic_checks"][0]
    assert entry["name"] == "vlm:p1:login:test"
    assert entry["passed"] is True
    assert entry["detail"] == "тест"


if __name__ == "__main__":
    import traceback
    tests = [
        test_visual_checks_covers_required_phases,
        test_visual_checks_schema,
        test_visual_checks_stale_widget_check_present,
        test_stale_widget_verdict_fails_run,
        test_multiple_stale_verdicts_all_fail,
        test_all_vlm_pass_yields_rc0,
        test_add_check_writes_to_manifest,
    ]
    passed_count = 0
    for t in tests:
        try:
            t()
            print(f"  PASS {t.__name__}")
            passed_count += 1
        except Exception:
            print(f"  FAIL {t.__name__}")
            traceback.print_exc()
    print(f"\n{'PASS' if passed_count == len(tests) else 'FAIL'} "
          f"({passed_count}/{len(tests)})")
    sys.exit(0 if passed_count == len(tests) else 1)
