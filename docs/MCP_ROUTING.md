# Маршрутизация MCP: unreal-mcp vs unreal-ue58

Краткий контракт для задач UITERM / PIE-приёмки (2026-10-13).

| Нужно | Сервер | Примечание |
|-------|--------|------------|
| BP / UMG create-edit, materials, mesh, DT, recipes | **`unreal-mcp`** (stdio → `:55557`) | Content не на Epic MCP |
| `get_umg_hierarchy`, `get_widget_tree`, `find_widget`, `wait_for_*` | **`unreal-mcp` only** | На `unreal-ue58` нет |
| `pie_start` / `pie_screenshot` / `screen_click` / `simulate_key` | оба | Предпочтительно не смешивать в одном прогоне без нужды |
| `wc_*` selection/deploy/battle + `wc_play_card` / `wc_select_battle_unit` | **`unreal-ue58`** (`:8137`) | с 1.4.0 — полный parity 15 команд |
| `asset_exists` / `get_asset_references` / `rename_asset` / `delete_asset` | **`unreal-mcp`** | гигиена ассетов, PIE остановлен |
| Lookdev кадр без гизмо | `viewport_capture(game_view=True)` | UnrealMCP ≥ 3.28 |

**Правило UITERM:** claims с hierarchy/tree → `mcp__unreal-mcp__*`. Не ожидать тех же tools на HTTP Epic-сервере.
