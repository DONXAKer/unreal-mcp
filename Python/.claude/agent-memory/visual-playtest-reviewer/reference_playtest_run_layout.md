---
name: reference-playtest-run-layout
description: Структура run-папки визуального playtest (manifest.json, фазы, logic_checks) и канон фаз игры WarCard
metadata:
  type: reference
---

Run-папки: `D:\WarCard\client\Saved\PlaytestRuns\<ts>\` с `manifest.json` + `p1/<phase>.png` + `p2/<phase>.png`.

Канон фаз (порядок захвата): login → main_menu → draft → deployment → mulligan → battle → battle_field → battle_after → game_result. 9 фаз × 2 стороны = 18 кадров.
- `battle_field` снимается с `show_ui:false` — ожидается чистая 3D-сцена поля без HUD.
- Transient-кадры могут отсутствовать на переходах (напр. p1/main_menu, p2/draft) — это не баг сам по себе.

Соответствие фаза → expected_widget:
login=WBP_Login, main_menu=WBP_MainMenu, draft=WBP_Draft, deployment=WBP_DeploymentScreen,
mulligan=WBP_Mulligan (на практике может быть проскочен → WBP_ActionCardHand+WBP_BattleHUD),
battle/battle_field/battle_after=WBP_BattleHUD, game_result=WBP_GameResult.

manifest содержит `state.battle_units[]` (unitId, gridX/Y, hp, playerId, alive) — сверять позиции/HP с картинкой поля.
`logic_checks[]` ключевые: draft_complete, deploy_p1/p2, battle_reached, board_changed_after_moves (turns=N), game_result_reached_both.

Намеренные плейсхолдеры (помечать low, прогон не роняют): портреты-прямоугольники на карточках драфта (коммит eb1bf31 — placeholder by design).
См. [[reference-style-guide]].
