"""Быстрый запуск headless-оппонента (testuser2) для PIE плейтеста."""
import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))
from tests._stomp_opponent import OpponentBot

TOKEN = "eyJhbGciOiJIUzUxMiJ9.eyJzdWIiOiIwMDAwMDAwMC0wMDAwLTAwMDAtMDAwMC0wMDAwMDAwMDAwMDQiLCJ1c2VybmFtZSI6InRlc3R1c2VyMiIsImlhdCI6MTc4MTg2MDU0MywiZXhwIjoxNzgxOTQ2OTQzfQ.hH2pKtg2FBCQDflKEYEz__Jnws5NyfNjY9ePcw4Wv-60XivyLJZA60IhytSEIypXGNrDz7s8-hM1UMB27ZCigg"

bot = OpponentBot(TOKEN, name="testuser2", verbose=True, full_play=False)
if not bot.start():
    print("FAIL: бот не подключился")
    sys.exit(1)

print("[run_opponent] matchmaking запущен, ожидаю игру...")
ok = bot.wait_battle(180)
print(f"[run_opponent] BATTLE достигнут: {ok}")

try:
    while True:
        time.sleep(5)
        print("[run_opponent] жду...")
except KeyboardInterrupt:
    pass
finally:
    bot.close()
    print("[run_opponent] завершён")
