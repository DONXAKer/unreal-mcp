"""E2E смок: deployment phase — расстановка юнитов на сетку.

STATUS: SKELETON. См. smoke_pie_matchmaking.py.

Шаги (заготовка):
    1. Дойти до фазы Deployment
    2. wait WBP_DeploymentScreen_C_0
    3. Для каждого юнита из state['draft_picks']:
         - click WBP_UnitCard with this unit id
         - click GridCell at (x, y)  (потребуется новая MCP-команда
           click_world_position по 3D-координате — TODO в плагине)
    4. click ConfirmDeploymentButton
    5. wait WBP_BattleHUD_C_0
"""

from __future__ import annotations

import sys


def main(argv: list[str]) -> int:
    print("SKIP: smoke_pie_deployment — skeleton. Требует MCP-команды click_world_position для клика по GridCell.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
