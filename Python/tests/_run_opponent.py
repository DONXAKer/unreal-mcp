"""Standalone-оппонент для ручной игры: headless STOMP-бот в очереди матчмейкинга.

Запуск (из unreal-mcp/Python):
    uv run python tests/_run_opponent.py [минуты_жизни]

Пользователь в UE-клиенте жмёт «Найти игру» — матчится с этим ботом.
Бот в full_play-режиме: драфтит, деплоит, ходит в бою.
"""
from __future__ import annotations

import sys
import time

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0].rsplit("/tests", 1)[0]))

from tests._fixtures import ensure_test_user, get_test_jwt
from tests._stomp_opponent import OpponentBot


def main() -> int:
    minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    login, pwd = "botplayer", "Test1234"
    ensure_test_user(login, pwd, "botplayer@example.com")
    jwt = get_test_jwt(login, pwd)

    bot = OpponentBot(jwt, name="bot", full_play=True)
    if not bot.start():
        print("FAIL: бот не подключился к серверу")
        return 1
    print("BOT READY: в очереди матчмейкинга, жми 'Найти игру' в клиенте "
          f"(живёт {minutes:.0f} мин)")

    deadline = time.time() + minutes * 60.0
    try:
        while time.time() < deadline:
            time.sleep(5)
    except KeyboardInterrupt:
        pass
    finally:
        bot.close()
    print("BOT EXIT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
