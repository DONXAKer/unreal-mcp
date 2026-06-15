"""E2E смок: draft phase — выбор юнитов из каталога.

STATUS: SKELETON. См. smoke_pie_matchmaking.py — требует упорядоченного
прохождения предыдущих смоков и discovery виджетов WBP_Draft / WBP_DraftUnitEntry.

Шаги (заготовка):
    1. pie_status / pie_start если нужно
    2. Дойти до фазы Draft (либо через smoke_pie_matchmaking, либо через
       инжекцию состояния через MCP set_actor_property на GamePhaseManager)
    3. wait WBP_Draft_C_0
    4. get_widget_tree -> найти первый WBP_DraftUnitEntry
    5. click on entry
    6. (повторить N раз — обычно 5 юнитов)
    7. wait WBP_Deployment_C_0 или WBP_UnitSelection_C_0
"""

from __future__ import annotations

import sys


def main(argv: list[str]) -> int:
    print("SKIP: smoke_pie_draft — skeleton. Требует discovery виджетов WBP_Draft + работающий matchmaking smoke.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
