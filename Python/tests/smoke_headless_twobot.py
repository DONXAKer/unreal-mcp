"""TEST-E2E-001: полный матч двух headless STOMP-ботов до GameResult (без UE/PIE).

Два `OpponentBot(full_play=True)` (см. tests/_stomp_opponent.py) проходят весь
игровой цикл целиком через STOMP, без Unreal Editor:

    login → matchmaking → dice → draft (snake) → deployment → mulligan →
    battle (free-move + attack + end-turn) → surrender → game-result

Покрывает механики, ранее не затронутые e2e нигде:
  * MULLIGAN  — явная команда /app/game/mulligan (keep-all), а не только phase-ready;
  * free-move — /app/game/free-move (≥1 успешное движение);
  * attack    — /app/game/select-action-card + /app/game/execute-action (≥1 атака);
  * финиш     — /app/game/surrender с зафиксированным победителем.

Детерминизм: бот-«финишер» сдаётся, как только оба механики реально
продемонстрированы (BattleCoordinator), либо по hard-cap ходов — поэтому матч
гарантированно завершается, а не зависает на RNG-добивании HP.

Запуск:
    cd unreal-mcp/Python
    uv run python tests/smoke_headless_twobot.py

Требует живой сервер на :8081. Exit 0 — партия дошла до победителя с
продемонстрированными механиками; ненулевой — зависание / незавершение / пропуск
обязательной механики.
"""

from __future__ import annotations

import os
import sys
import time

# tests/ — пакет; добавляем Python/ в sys.path для абсолютного импорта.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tests._fixtures import ensure_test_user, get_test_jwt, is_server_alive
from tests._stomp_opponent import BattleCoordinator, OpponentBot

# Тайминги. Дорога до BATTLE включает ожидание в matchmaking-очереди (~до 60с в
# solo, но здесь оба бота заходят сразу — обычно секунды) + dice/draft/deploy.
BATTLE_TIMEOUT_S = 120.0   # login → BATTLE
GAME_OVER_TIMEOUT_S = 180.0  # BATTLE → game-result (победитель)


def run() -> int:
    if not is_server_alive():
        print("FAIL: сервер не отвечает на :8081 (/actuator/health)")
        return 1

    suffix = str(int(time.time()) % 100000)
    creds = [
        (f"e2e_a_{suffix}", "Test1234", f"e2e_a_{suffix}@ex.com"),
        (f"e2e_b_{suffix}", "Test1234", f"e2e_b_{suffix}@ex.com"),
    ]
    coordinator = BattleCoordinator()
    bots: list[OpponentBot] = []
    for i, (login, pwd, email) in enumerate(creds):
        ensure_test_user(login, pwd, email)
        jwt = get_test_jwt(login, pwd)
        bots.append(OpponentBot(
            jwt, name=login.split("_")[1], full_play=True,
            coordinator=coordinator, is_finisher=(i == 0),  # bot 'a' сдаётся в финале
        ))

    try:
        for b in bots:
            if not b.start():
                print("FAIL: бот не подключился (нет CONNECTED)")
                return 1
            time.sleep(1.0)

        if not all(b.wait_battle(BATTLE_TIMEOUT_S) for b in bots):
            print(f"FAIL: BATTLE не достигнут за {BATTLE_TIMEOUT_S:.0f}s")
            return 1
        print("OK: оба бота вошли в BATTLE — играем до результата")

        game_over = all(b.wait_game_over(GAME_OVER_TIMEOUT_S) for b in bots)
        winner = next((b.winner_id for b in bots if b.winner_id), None)
        moves = coordinator.moves_done
        attacks = coordinator.attacks_done
    finally:
        for b in bots:
            b.close()

    print(f"--- ИТОГ: game_over={game_over}, winner={winner}, "
          f"moves={moves}, attacks={attacks} ---")

    problems = []
    if not game_over:
        problems.append(f"партия не завершилась за {GAME_OVER_TIMEOUT_S:.0f}s")
    if moves < 1:
        problems.append("ни одного успешного free-move")
    if attacks < 1:
        problems.append("ни одной успешной атаки (execute-action)")
    if winner is None:
        problems.append("победитель не зафиксирован")

    if problems:
        print("=== TWOBOT E2E: FAIL — " + "; ".join(problems) + " ===")
        return 1
    print("=== TWOBOT E2E: PASS — полный матч пройден "
          f"(moves={moves}, attacks={attacks}, winner={winner[:8]}…) ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
