# Changelog

All notable changes to UnrealMCP (C++ plugin + Python MCP server) are documented here. History maintained from **v2.0.0** onward.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); plugin follows [SemVer](https://semver.org/) (`VersionName` in `UnrealMCP.uplugin`).

Bump rules:
- **patch** — C++ bug fixes that don't change command signatures or add commands
- **minor** — new commands, new Python tools, new spec fields (backward-compatible)
- **major** — removed commands, changed signatures, removed Python primitives

---

## [Unreleased]

Pending work; will be cut into the next minor or patch release.

---

## [2.18.0] — 2026-05-28

Battle-фаза в MCP: команды боя + догнан e2e полной игры (FEAT-BATTLE / FIX-UI-008).

### Added

- `wc_surrender` — капитуляция клиента (`UActionCardSubsystem::Surrender()` через
  reflection). Детерминированный game-over для e2e: сервер выставляет `bGameEnded`
  → оба клиента переходят в GameResult. Возврат `{ok, surrendered, controller_index}`.
- `wc_end_turn` — завершить ход (`EndTurn()`). Возврат `{ok, ended, controller_index}`.
- `wc_get_battle_state` — снимок боя `{ok, my_turn, ap, max_ap, controller_index}`
  (`IsMyTurn()`/`GetCurrentAP()`/`GetMaxAP()`).
- Все три — C++ `WarCardGameCommands` (reflection через `ResolveSubsystem`
  `/Script/Client.ActionCardSubsystem` + `InvokeFunction`, без client-include) +
  Python-обёртки `warcard_tools.py`. Раньше WarCard MCP покрывал только selection +
  deployment; battle — ничего.

### Fixed

- `get_actors_in_level` и `find_actors_by_name` (Python `tools/editor_tools.py`)
  больше не падают с Pydantic "Input should be a valid list" /
  `INVALID_RESPONSE_TYPE`. Они возвращали голый `list`, а
  `wrap_with_envelope._normalize_outbound` принимает только `dict` → любой
  успешный ответ превращался в ошибку "Tool returned non-dict (list)", а
  annotation `-> list[...]` дополнительно ломала output-схему FastMCP. Теперь оба
  несут результат внутри dict (`{success, actors, count}` → `meta.actors`),
  annotation `-> dict[str, Any]`. C++ не затронут.

### Why

- `smoke_pie_full_game.py` упирался в Battle HUD — не было MCP-команд для боя.
  Теперь тест идёт login→matchmaking→draft→deployment→battle→surrender→GameResult.
- Рестарт MCP-сервера нужен для Python-обёрток (новые tools + get_actors fix);
  C++-команды доступны после пересборки ClientEditor.

---

## [2.17.4] — 2026-05-28

`GetPIEWorldForClient` через `PC->GetWorld()` — стабильный резолв мира клиента (FIX-UI-008).

### Fixed

- `FUnrealMCPPIEUtils::GetPIEWorldForClient(index)` в multi-world теперь берёт мир
  через `GetPlayerControllerByIndex(index)->GetWorld()` (как `DescribeClient`,
  который НАДЁЖНО находит виджеты обоих клиентов в `pie_status`), а не напрямую
  `Contexts[index]->World()`. Прямой контекст периодически указывал на мир без
  виджетов клиента в listen-server PIE → `get_widget_tree(controller_index)` /
  `invoke_button_click(controller_index)` возвращали пусто/не находили кнопку,
  драфт-пики срывались в середине. Фоллбэк на `Contexts[index]->World()` сохранён.

---

## [2.17.3] — 2026-05-28

Multi-world: поиск виджетов по ВСЕМ PIE-мирам без `controller_index` (FIX-UI-008).

### Added

- `get_widget_tree` и `invoke_button_click` БЕЗ `controller_index` теперь ищут
  виджеты во ВСЕХ PIE-мирах (`UWorld::WorldType == PIE`), а не только в мире,
  резолвнутом по индексу. Причина: в listen-server multi-world резолв
  `GetPIEWorldForClient(index)` периодически нестабилен — `get_widget_tree(controller_index=0)`
  возвращал пустой список, хотя виджеты клиента существуют (их находил DescribeClient
  через `PC->GetWorld()`). Имена виджетов драфта (`CatalogEntryButton_N`) глобально
  уникальны, поэтому поиск по всем мирам надёжно находит нужную кнопку. С явным
  `controller_index` поведение прежнее (скоуп по миру клиента).

### Fixed

- `FindWidgetByName` (UMGRuntime/UMGTest): `PlayWorld == nullptr` → матч по любому
  PIE-миру. `invoke_button_click` без `controller_index` не требует PlayerController.

---

## [2.17.2] — 2026-05-28

Multi-world: `get_widget_tree`/`find_widget`/`invoke_button_click`/`set_text_on_widget` периодически не видели виджеты второго клиента (FIX-UI-008).

### Fixed

- OwningPlayer-фильтр (`MCP-PLUGIN-005`, для split-screen) теперь применяется ТОЛЬКО в single-world PIE. В multi-world (`PIE_ListenServer`, каждый клиент в своём `UWorld`) разделение клиентов делает фильтр по `World`, а фильтр по `OwningPlayer` в listen-server world (где несколько PlayerController'ов) периодически резал ВСЕ виджеты клиента → `get_widget_tree(controller_index)` возвращал пустой список, `invoke_button_click`/`set_text_on_widget` не находили кнопку. Затронуты `UMGTestCommands.cpp` (`HandleGetWidgetTree`, `FindWidgetByName`) и `UMGRuntimeCommands.cpp` (`FindWidgetByName`).
- Добавлен `FUnrealMCPPIEUtils::GetNumPIEWorldContexts()` — число отдельных клиентских PIE-контекстов; используется как признак multi-world.
- `ping.plugin_version` / `pie_status.plugin_version` синхронизированы с `VersionName`.

---

## [2.17.1] — 2026-05-28

Исправление multi-client PIE: `PIE_Standalone` не создавал отдельные миры (FIX-UI-008 v2).

### Fixed

- `PIECommands.cpp:HandlePieStart` — для `num_clients > 1` net mode изменён с `PIE_Standalone` на `PIE_ListenServer`. Эмпирически подтверждено (diag `_verify_multiworld` / `_diag_multiclient`): `PIE_Standalone` + `NumberOfClients>1` создаёт ОДИН world со split-screen локальными игроками (оба `controller_index` резолвятся в один `WBP_Login_C_0`), а не N отдельных миров. Отдельные клиентские миры в UE PIE рождаются только в сетевом режиме. `PIE_ListenServer` + `RunUnderOneProcess(true)` + `NumberOfClients=N` создаёт N инстансов (0 = listen-server-игрок, 1..N-1 = клиенты), каждый со своим world/`GameInstance`/`WBP_Login`, всё в одном процессе. listen-server world имеет `NM_ListenServer` (не `NM_DedicatedServer`), поэтому фильтр `PIEUtils::CollectPIEContexts` его не отбрасывает.
- `EpicUnrealMCPBridge.cpp:269` — `ping.plugin_version` рассинхронизирован (оставался `2.16.1` при .uplugin 2.17.0); синхронизирован с `VersionName`.

### Why

В 2.17.0 был выбран `PIE_Standalone` по неверной трактовке `PlayLevel.cpp`; реальный прогон показал один общий мир — войти мог лишь один клиент. `PIE_ListenServer` — стандартный «test multiplayer in PIE» сетап, реально дающий N миров.

---

## [2.17.0] — 2026-05-28

Multi-client PIE теперь поднимает N независимых standalone-клиентов в ОДНОМ процессе редактора (FIX-UI-008).

### Changed

- `PIECommands.cpp:HandlePieStart` — для `num_clients > 1` конфигурирует `ULevelEditorPlaySettings` комбинацией `SetRunUnderOneProcess(true)` + `SetPlayNetMode(PIE_Standalone)` + `SetPlayNumberOfClients(N)`. Раньше выставлялся только `SetPlayNumberOfClients(N)` без net mode и run-under-one-process, что давало ОДИН общий `UWorld` с N локальными игроками (split-screen) — единственный набор UMG-виджетов, войти мог лишь один клиент. Теперь UE создаёт N полноценных standalone PIE-инстансов (каждый со своим `GameInstance`/`PlayerController`/viewport'ом → своим `WBP_Login`), всё в этом же процессе, поэтому единственный MCP TCP-listener (55557) рулит обоими через `controller_index`. `num_clients == 1` не регрессирует (классический single-PIE).
- `dedicated_server=true` теперь явно включает `PIE_Client` + `bLaunchSeparateServer` + `RunUnderOneProcess(true)` (UE-server-flow для отладки; для WarCard не нужен — клиенты ходят на внешний Spring/STOMP :8081).

### Fixed

- `PIEUtils.cpp:CollectPIEContexts` теперь исключает dedicated-server `WorldContext` (`World->GetNetMode() == NM_DedicatedServer`), чтобы `controller_index` 0/1 всегда указывал на реальные КЛИЕНТСКИЕ миры, а не на server-world без viewport'а/UMG.
- `PIEUtils.cpp:GetPlayerControllerByIndex` / `GetPIEWorldForClient` — при наличии >1 клиентского контекста индекс мапится СТРОГО на N-й клиентский world и больше НЕ откатывается на split-screen fallback (`GEditor->PlayWorld`), из-за которого оба индекса резолвились в один и тот же `WBP_Login_C_0` / `BP_StrategyPlayerController_C_0`.

### Why

Диагностика против живого редактора (v2.16.1): `pie_status` с `num_clients=2`
показывал оба клиента с идентичными `world_name=MainMenu_Map` и
`controller_name=BP_StrategyPlayerController_C_0`; `set_text_on_widget` /
`invoke_button_click` для index=0 и index=1 возвращали ОДИН и тот же
`WBP_Login_C_0`. Тесты matchmaking/draft/battle с двумя клиентами были
невозможны — второй клиент не имел собственного UI и не мог залогиниться.

---

## [2.16.1] — 2026-05-28

Critical patch: UE краш при вызове void-функций без параметров через `InvokeFunction`.

### Fixed

- `WarCardGameCommands.cpp:InvokeFunction` теперь корректно обрабатывает функции с `ParmsSize == 0` (например, `SendCompositionToServer()`, `ConfirmDeployment()`). Раньше `Buffer.SetNumZeroed(0)` возвращал `nullptr`, и `InitializeStruct(nullptr)` валился на assertion `Dest` (`Class.cpp:1189`), роняя весь UE Editor. Теперь буфер создаётся только когда `ParmsSize > 0`; `ProcessEvent` корректно принимает `nullptr` для void-функций.

### Why

E2e smoke: `wc_confirm_selection` крашил UE сразу после AddUnitToComposition × 5
("Состав готов!"). Stage 6 (UnitSelection → Deployment) был полностью
заблокирован — без этого фикса дальше пройти невозможно.

---

## [2.16.0] — 2026-05-27

Adds `text` field to widget tree dump — тест может видеть содержимое
TextBlock/EditableText/RichTextBlock без отдельной команды.

### Added

- `BuildWidgetJson` (используется `get_widget_tree`) теперь добавляет поле `text` для виджетов с текстом: `UTextBlock`, `URichTextBlock`, `UEditableTextBox`, `UEditableText`, `UMultiLineEditableTextBox`, `UMultiLineEditableText`. Для контейнеров поле отсутствует (избегаем раздувания JSON).

### Why

E2e-тест UnitSelection не мог проверить, что карточки `WBP_UnitCatalogEntry`
содержат `NameText/TypeText/StatsText/AbilityText` с правильным содержимым
после server response. Раньше snapshot показывал только структуру, без
текста — баг "карточка отрисована, но пустая" был невидим автоматизации.

---

## [2.15.0] — 2026-05-27

Adds WarCard UnitSelection bridge — reflection-based MCP commands для
`UUnitSelectionSubsystem` (выбор юнитов до Deployment).

### Added

- **`wc_select_unit(unit_id, controller_index)`** — `UUnitSelectionSubsystem::AddUnitToComposition(UnitId)` → `bool`.
- **`wc_deselect_unit(unit_id, controller_index)`** — `RemoveUnitFromComposition(UnitId)`.
- **`wc_confirm_selection(controller_index)`** — `SendCompositionToServer()` (переход в Deployment phase).
- **`wc_get_selection_state(controller_index)`** — `{ selected_count, ready }`.
- Class path: `/Script/Client.UnitSelectionSubsystem`.

### Why

После MATCH_FOUND игрок попадает не в Deployment, а в **UnitSelection** —
выбрать 5 юнитов из roster (9 в каталоге). Без этих команд e2e flow не
доходит до Deployment.

---

## [2.14.0] — 2026-05-27

Adds WarCard Deployment bridge — первая project-specific категория команд
через UE reflection. Плагин не include'ит клиентские заголовки —
subsystem resolution через class path + `FindFunction` + `ProcessEvent`
с FProperty iteration. См. MCP-PLUGIN-006.

### Added

- **`WarCardGameCommands.h/.cpp`** — новая категория команд.
- **`wc_deploy_unit(unit_id, grid_x, grid_y, controller_index)`** — `UDeploymentSubsystem::DeployUnit(UnitId, GridX, GridY)` → `bool`. Минует UI-клики по GridCell.
- **`wc_confirm_deployment(controller_index)`** — `ConfirmDeployment()` (переход в Battle).
- **`wc_get_deployment_state(controller_index)`** — `{ deployed_count, ready }`.
- **`InvokeFunction`** helper в плагине — generic UFunction reflection-вызов: упаковка параметров по `FProperty.GetName()` (устойчиво к перестановке), типы FString/int32/bool/float, return value через `GetReturnProperty()`.
- Python tools: `unreal-mcp/Python/tools/warcard_tools.py` + регистрация в `unreal_mcp_server.py`.

---

## [2.13.2] — 2026-05-27

Patch: `HandleGetWidgetTree` фильтр был слишком агрессивным.

### Fixed

- `HandleGetWidgetTree` теперь применяет PC-фильтр **только** при явно
  заданном `controller_index` (раньше включался автоматически на
  `NumPIEClients > 1` — это приводило к пустому tree, потому что виджеты
  на ранних тиках могут иметь nullptr owner).

---

## [2.13.1] — 2026-05-27

Patch: PC-фильтр сломал single-client backward compat.

### Fixed

- `FindWidgetByName` теперь пропускает widget **только** если `OwningPlayer != nullptr && != OwningPC`. Виджеты без явного owner (CreateWidget без указания PC — нормальный паттерн `WBP_MainMenu` после login) считаются "глобальными" и проходят фильтр для любого `controller_index`. Закрывает регрессию `smoke_pie_login` (single-client) после 2.13.0.

---

## [2.13.0] — 2026-05-27

Adds OwningPlayer-фильтр для FindWidgetByName — изолирует UI клиентов в
split-screen multi-PIE. См. MCP-PLUGIN-005.

### Added

- `FindWidgetByName` в `UMGRuntimeCommands` и `UMGTestCommands` теперь принимает опциональный `APlayerController* OwningPC`. Если задан, виджеты с другим owner отрезаются. Резолв PC через `FUnrealMCPPIEUtils::GetPlayerControllerByIndex(controller_index)` автоматически.

### Why

В split-screen multi-PIE (`num_clients=2` без dedicated server) оба клиента
живут в одном UWorld. Фильтр только по World не разделяет — set_text для
client 0 мог попасть в виджет client 1. С PC-фильтром каждый клиент видит
только свои UI.

---

## [2.12.0] — 2026-05-27

Adds `invoke_button_click` — прямой broadcast OnClicked, обход Slate event
injection. Закрывает критическую дыру в e2e UI-тестировании.

### Added

- **`invoke_button_click(widget_name, controller_index)`** — вызывает `UButton::OnClicked.Broadcast()` напрямую через UMGRuntimeCommands. Минует `FSlateApplication`.

### Why

`click_widget_by_name` симулирует mouse event через FSlateApplication, но
когда PIE viewport не focused (типичный случай headless e2e через MCP),
Slate route'ит событие в editor, а не в game. Blueprint `OnClicked` не
triggered → submit-flow ломается (LoginButton.click не запускал AuthSubsystem).
Прямой broadcast гарантирует попадание в делегат.

---

## [2.11.0] — 2026-05-27

Patch для T002: `SetText()` не broadcast'ит OnTextChanged.

### Fixed

- `HandleSetTextOnWidget` теперь после `SetText()` явно вызывает `OnTextChanged.Broadcast(Text)` + `OnTextCommitted.Broadcast(Text, CommitMethod)` на всех 4 поддерживаемых классах (EditableTextBox / EditableText / MultiLine варианты). Поддержан опциональный параметр `commit_method` (`"OnEnter"` по умолчанию). Без broadcast — Blueprint, слушающий OnTextChanged для сохранения значения в state, оставался в неведении, и submit отправлял пустые/устаревшие креды.

---

## [2.10.0] — 2026-05-27

Adds UE5.7-native Enhanced Input через MCP. См. MCP-PLUGIN-004.

### Added

- **`create_input_action(name, value_type, path)`** — `UInputAction` (Bool/Axis1D/Axis2D/Axis3D).
- **`create_input_mapping_context(name, path)`** — `UInputMappingContext`.
- **`add_input_action_mapping(context_path, action_path, key, modifiers[], triggers[])`** — биндит key + модификаторы (Negate, Swizzle.YXZ).
- **`add_enhanced_input_action_event_node(blueprint_path, action_path, trigger_event)`** — добавляет `K2Node_EnhancedInputAction` в Blueprint с 9 выходными пинами (5 exec: Triggered/Started/Ongoing/Canceled/Completed + ActionValue/ElapsedSeconds/TriggeredSeconds/InputAction).
- Recipe `wc.bind_input_context_on_begin_play(blueprint_path, imc_path)` — собирает граф BeginPlay → GetPlayerController → GetEnhancedInputLocalPlayerSubsystem → AddMappingContext.
- `UnrealMCP.Build.cs` — добавлен `EnhancedInput` модуль; `.uplugin` — plugin dependency.

---

## [2.9.0] — 2026-05-27

Adds multi-client PIE — `num_clients` параметр, `controller_index` во всех PIE-командах. См. MCP-PLUGIN-003.

### Added

- **`pie_start(num_clients=N, dedicated_server=bool)`** — N клиентов через `ULevelEditorPlaySettings::SetPlayNumberOfClients`.
- **`pie_status`** — теперь возвращает `num_clients` + `clients[]` массив (`controller_class`, `controller_name`, `world_name`, `current_level`, `current_widget`).
- **`controller_index` параметр** в `simulate_key`, `click_widget_by_name`, `pie_screenshot`, `wait_for_widget`, `find_widget`, `get_widget_tree` (опционально, default 0).
- **`pie_screenshot(controller_index=N)`** — суффикс `_client<N>.png` в filename.
- **`PIEUtils.h/.cpp`** — `FUnrealMCPPIEUtils::GetPlayerControllerByIndex`, `GetNumPIEClients`, `GetPIEWorldForClient`, `DescribeClient`. Поддерживает multi-PIE (отдельный WorldContext per client) и split-screen (несколько PC в одном PIE world).
- **`pie_status` early-return** в idle state теперь тоже возвращает `num_clients:0, clients:[]` — consistent shape.

---

## [2.8.0] — 2026-05-27

Adds `set_text_on_widget` — Unicode-ввод в UMG поля без `simulate_key`. См. MCP-PLUGIN-002.

### Added

- **`UMGRuntimeCommands.h/.cpp`** — новая категория команд для runtime-state виджетов.
- **`set_text_on_widget(widget_name, text, controller_index)`** — записывает любой Unicode в `UEditableTextBox` / `UEditableText` / `UMultiLineEditableTextBox` / `UMultiLineEditableText` через `SetText(FText::FromString)`. Закрывает TODO в `client/Documentation/TESTING_GUIDE.md:97`.
- При неподдерживаемом классе error содержит `details.actualClass`.

### Why

`simulate_key` работает посимвольно и только для ASCII (нужен раскладочный
маппинг). Логин/пароль с кириллицей или спецсимволами через `simulate_key`
невозможны.

---

## [2.7.0] — 2026-05-27

Hardening pin resolver для Blueprint-операций. См. MCP-PLUGIN-001.

### Added

- **`PinResolver.h/.cpp`** — `FUnrealMCPPinResolver` единый helper для `set_pin_default_value`, `get_pin_info`, `connect_blueprint_nodes`, `set_node_property`. Логика: exact `PinName` → fallback на `PinFriendlyName` → sub-pin lookup (через `.` или `_`) → `ReconstructNode` fallback для K2Node (материализует dynamic пины у CallFunction / CreateWidget / CastTo).
- При неудачном резолве error содержит `details.availablePins[]` (массив `{name, friendlyName, direction, pinCategory, isSplit, isSubPin}`) — полная диагностика всех реальных пинов узла.
- **`_pin_diagnostics.py`** — Levenshtein-based `enrich_pin_error` в Python: добавляет `did_you_mean` подсказку при typo.

### Fixed

- **`EpicUnrealMCPBridge.cpp`** — на error пути bridge ранее **отбрасывал** поле `details`, из-за чего `availablePins[]` от `PinResolver::MakeErrorResponse` не доходил до клиента. Теперь bridge пробрасывает `details` если handler его положил.

### Why

`set_node_property` / `get_pin_info` периодически валились с `Pin not found`
на K2Node с optional/dynamic пинами (`Class` у `CreateWidget`), sub-pins
после `split_struct_pin`, и узлах с `PinFriendlyName != PinName`. Это был
самый частый источник ручных fallback'ов.

---

## [2.6.0] — 2026-05-26

Adds console-command proxy для произвольного `GEngine->Exec()`.

### Added

- **`execute_console_command(cmd)`** — выполняет UE console команду через `GEngine->Exec()`. Возвращает `{ success, command, log_lines[], log_truncated, lines_captured }`. Подписывается на `GLog` через `FConsoleCaptureOutputDevice` (max 500 строк защита от log-flood).
- World resolution: PIE world (если активен) → editor world fallback.

---

## [2.4.0] — 2026-05-25

Adds PIE automation + UMG runtime тестирование (Playwright-style).

### Added

- **`PIECommands.h/.cpp`** — `pie_start`, `pie_stop`, `pie_status`, `pie_screenshot`, `simulate_key`, `tick_world`.
- **`UMGTestCommands.h/.cpp`** — runtime операции над живыми `UUserWidget` в PIE-мире: `find_widget`, `wait_for_widget`, `click_widget_by_name`, `get_widget_tree`.
- Python tools в `Python/tools/pie_tools.py` (стdio + http server обновлены).
- `find_widget` находит виджет через все `UUserWidget` PIE-мира (root + всё в `WidgetTree`).
- `click_widget_by_name` симулирует left-mouse event через `FSlateApplication` (центр cached geometry).
- `get_widget_tree` — DOM-like snapshot (имя, класс, visible, size, children).

---

## [2.1.0] — 2026-05-24

Adds declarative graph builder; Python-only (no plugin rebuild needed for this release).

### Added

- **`build_blueprint_graph` MCP tool** (`Python/tools/graph_builder.py`) — atomically build an EventGraph or function graph from a declarative JSON spec instead of orchestrating 15-30 separate `add_blueprint_*_node` + `connect_blueprint_nodes` calls. Supports 9 node types covering ~80% of UI-task patterns:
  `Event`, `FunctionCall`, `VariableGet`, `VariableSet`, `Branch`, `ForEachLoop`, `DynamicCast`, `BindEvent`, `CustomEvent`.
  Includes pre-validation (no UE5 state-change until spec is structurally valid), atomic rollback on failure (`rollback_on_failure: true` by default), and a structured result envelope (`{success, nodes_created, connections_made, compile_result, node_id_map}` or `{success: false, phase, fail_at, cause, rolled_back, rollback_result}`).
  See `Python/tools/graph_builder.py` docstring for the full spec schema.

### Why

Sonnet-driven `impl_bp` regularly produced UMG widgets with empty `K2Node_ComponentBoundEvent` connections (clicks → nothing). Each loopback added 25 min and another LLM-call-orchestration risk surface. A high-level builder removes the per-node orchestration step, lets `plan_bp` emit the wiring as data instead of prose, and lets `review_bp` compare actual graph against the same spec deterministically. Source: FEAT-MULLIGAN-001 + 002 retrospective.

---

## [2.0.0] — 2026-05-23

Initial tracked release. Brings tooling parity, an error-envelope contract, and recipe-level lifecycle hooks.

### Added

- **`ping` response now carries `plugin_version`** (`EpicUnrealMCPBridge.cpp`) — pipeline guards (`check_unreal`) use presence of this field to confirm the editor loaded the right plugin binary. Earlier plugins answered `pong` without it; pipeline now hard-fails when the field is missing instead of false-greening on stale binary. Source: FEAT-MULLIGAN-001 post-mortem (false-green widget creation without `WidgetSwitcher`).
- **`list_recipes` MCP server tool** — returns full metadata (qualified name, description, args[], produces[]) for every registered recipe. Lets clients introspect without re-running discovery.
- **HTTP server registers all 13 tool modules** (`unreal_mcp_server_http.py`). Pre-2.0 only registered 5; in-container pipelines now have the same tool surface as the stdio server.
- **Recipe-level `rollback_on_failure` parameter** (`recipe_framework.py`). When set, the framework journals every primitive call that creates/overwrites an asset and reverses the journal on recipe failure (`delete_asset` for `created`, surfaced-as-skipped for `overwritten` since no backup is taken). Result lands under `error.details.rollback`.
- **`meta.produces_check`** in recipe responses. After a successful recipe call, each `produces[]` template is expanded with the recipe's actual arguments and verified via `asset_exists`. Missing assets appear in `meta.produces_check.missing[]` — the recipe response stays successful, only signaling.

### Changed

- **Dual-key result envelope** — every response now carries both the unified `ok`/`status`/`assetPath`/`meta` shape AND the legacy `success`/`message` shape (`ok == success` invariant). Old and new consumers coexist; nothing breaks for clients that read either side.
- **Naming convention guidance** — `subject_first` style (`blueprint_*`, `variable_*`, `node_*`, `component_*`, `pin_*`, `anim_*`) is enforced for NEW commands. Legacy `verb_first` names (`create_blueprint`, `compile_blueprint`, etc.) kept indefinitely for backward compat. New `subject_first` is added to bridge allow-list only.

### Fixed

- **`BPConnector.cpp:147`** — `Schema->CanLinkedTo(...)` was a typo (method doesn't exist on `UEdGraphSchema_K2`); changed to `Schema->CanCreateConnection(...)`. Plugin failed to compile against UE 5.7 until this fix.

---

## Migration notes

### 1.x → 2.0.0

- No breaking changes for client code that reads `success`/`message`. New code SHOULD prefer the unified `ok`/`status`/`assetPath`/`meta`/`error` envelope.
- Existing recipes work unchanged. Add `rollback_on_failure=True` to the `@recipe` decorator if you want automatic cleanup on failure.
- HTTP-server clients gain access to tool modules that were previously stdio-only (Niagara, Animation BP, etc.). No client-side change needed; new tools just appear in the registered list after the server restart.

### 2.0.0 → 2.1.0

- No breaking changes. `build_blueprint_graph` is additive — existing `add_blueprint_*_node` + `connect_blueprint_nodes` primitives keep working.
- To benefit from the builder, update `plan_bp` prompts (in the calling pipeline, not this plugin) to emit graph specs in the planner output. Operators using ad-hoc MCP calls can keep doing them; the builder is opt-in per call.
