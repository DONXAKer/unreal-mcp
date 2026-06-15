"""Точечная проверка матчмейкинга (без UE): два свежих headless-клиента шлют
matchmaking/start, оба должны получить game-found с ОДНИМ gameId.

Проверяет, что после чистки зомби-игр (2026-06-01) пара различных аккаунтов
корректно матчится. НЕ играет до BATTLE — только подтверждает game-found.

Запуск:
    cd D:\\WarCard\\unreal-mcp\\Python
    python tests\\_verify_matchmaking.py
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tests._fixtures import ensure_test_user, get_test_jwt, is_server_alive
from tests._stomp_opponent import OpponentBot


def main() -> int:
    if not is_server_alive():
        print("FAIL: сервер не отвечает на /actuator/health")
        return 1

    suffix = str(int(time.time()) % 100000)
    creds = [
        (f"mm_a_{suffix}", "Test1234", f"mm_a_{suffix}@ex.com"),
        (f"mm_b_{suffix}", "Test1234", f"mm_b_{suffix}@ex.com"),
    ]

    bots: list[OpponentBot] = []
    for login, pwd, email in creds:
        ensure_test_user(login, pwd, email)
        jwt = get_test_jwt(login, pwd)
        bot = OpponentBot(jwt, name=login.split("_")[1])
        bots.append(bot)

    for b in bots:
        if not b.start():
            print(f"FAIL: бот {b.name} не подключился к STOMP")
            for x in bots:
                x.close()
            return 1
        time.sleep(0.8)

    # Ждём game-found у обоих (до 25с — мгновенный матч + фоновый processMatchmaking@5с)
    deadline = time.time() + 25.0
    while time.time() < deadline:
        if all(b.game_id for b in bots):
            break
        time.sleep(0.5)

    gids = {b.name: b.game_id for b in bots}
    print(f"game-found per bot: {gids}")

    ok = (
        all(b.game_id for b in bots)
        and len({b.game_id for b in bots}) == 1
    )

    for b in bots:
        b.close()

    if ok:
        print(f"=== MATCHMAKING PASS — оба бота в одной игре {bots[0].game_id} ===")
        return 0
    print("=== MATCHMAKING FAIL — game-found не у обоих / разные gameId ===")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
