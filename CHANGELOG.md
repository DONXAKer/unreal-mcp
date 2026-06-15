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

## [3.7.0] — 2026-06-15

### Fixed
- `set_widget_property` для `Border` теперь применяет `BrushColor`, `Padding` и
  `ContentColorAndOpacity` вместо ошибки `Failed to import value`. Раньше у
  `UBorder` не было собственной ветки — эти свойства уходили в reflection-fallback,
  где `ImportText` не понимает bare-CSV (`"0.5,0.5,0.5,1.0"`, `"2,2,2,2"`).
  Это была причина частичных ошибок `build_umg_widget` (errors[] с
  `BrushColor`/`Padding`), хотя дерево виджетов фактически строилось.

### Changed
- Reflection-fallback в `set_widget_property` теперь сам разбирает строковые
  значения для struct-свойств `FLinearColor`, `FSlateColor`, `FMargin`,
  `FVector2D` из формата CSV (`"R,G,B"` / `"R,G,B,A"`, `float` / `"H,V"` /
  `"L,T,R,B"`, `"X,Y"`) ДО вызова `ImportText_Direct`. Так типовые цвета/отступы/
  векторы, передаваемые MCP-клиентами строкой, применяются, а не падают на
  парсинге parenthesised-литерала, который ожидает UE (`"(R=0.5,...)"`).
- Сообщение об ошибке импорта теперь включает CPP-тип свойства
  (`... for property 'X' (type 'Y')`) для облегчения диагностики.

### Why
- `build_umg_widget` (Python-оркестратор в `tools/umg_tools.py`) уже корректно
  репортит partial build через `errors[]`/`log[]` и `success=False` — он НЕ
  бросает необработанный exception. «Unknown failure / TOOL_FAILURE» в клиенте —
  это Python-envelope, который маппит `success=False` в error-конверт. Истинная
  первопричина непустого `errors[]` была в C++: невозможность распарсить
  строковые `BrushColor`/`Padding`. Этот фикс устраняет именно её, так что
  типовое дерево (`Border` с цветом и отступом, `ProgressBar.Percent`) строится с
  `errors=[]` и `success=True`.

---

## [3.6.1] — 2026-06-15

### Fixed
- Резолв ассетов в команлах больше не зависит от того, загружен ли уже объект.
  `asset_exists`, `delete_asset`, `create_material_instance` (резолв
  `parentMaterial`), `set_material_instance_params` (резолв `assetPath`),
  текстурный параметр материала и material-override меша теперь подгружают
  ассет с диска через find-then-load, а не возвращают miss.
- `asset_exists("/Game/Art/Textures/T_CardArt_1")` теперь возвращает
  `exists:true` для реально существующего ассета (раньше — `false`).
- `create_material_instance(parentMaterial="/Game/Maps/Materials/M_Cell")` и
  `parentMaterial="/Engine/BasicShapes/BasicShapeMaterial"` больше не падают с
  `PARENT_MATERIAL_NOT_FOUND` для существующих материалов.
- Добавлены helper'ы `FAssetCommonUtils::NormalizeToObjectPath` (package-path
  `/Game/X/Foo` → object-path `/Game/X/Foo.Foo`) и
  `FAssetCommonUtils::LoadAssetObject` (StaticFindObject → StaticLoadObject,
  работает для `/Game/`, `/Engine/` и plugin-mount'ов). `FindAssetByPath`,
  `AssetExistsInRegistry`, `GetAssetClassName` переведены на них.

### Why
- Прежний код резолва ассетов использовал `UEditorAssetLibrary::LoadAsset` /
  `DoesAssetExist` / `FSoftObjectPath` напрямую с **package-path** на входе
  (`/Game/X/Foo` без суффикса `.Foo`). В UE 5.7 эти API ожидают **object-path**:
  bare package path даёт registry-miss и null-load. Из-за этого валились все
  операции над незагруженными ассетами в `/Game/` и `/Engine/`, хотя в
  Content Browser / AssetRegistry они присутствуют. Нормализация
  package→object path + явный StaticLoadObject устраняют расхождение.
  Коды ошибок (`PARENT_MATERIAL_NOT_FOUND` и т.п.) сохранены — срабатывают
  только если ассет действительно не грузится.

### Note
- Сигнатуры команд и Python-обёртки не изменены — bugfix-only, поэтому patch.

---

## [3.6.0] — 2026-06-02

### Added
- `simulate_key` получил параметр `action`: `"press"` (Pressed+Released в одном
  тике, дефолт), `"down"` (только нажатие), `"up"` (только отпускание). Python-tool
  `simulate_key(..., action=...)`.

### Why
- Для тестирования игрового ввода через Enhanced Input: событие
  `ETriggerEvent::Triggered` у экшена без явного триггера требует, чтобы клавиша
  была актуирована ≥1 тик. `action="press"` (нажатие+отпускание в одном тике) не
  даёт длительности → IA не срабатывает. Теперь:
  `simulate_key(action="down")` → реальные тики (sleep) → `simulate_key(action="up")`.
  `simulate_key` доходит до PlayerInput (в отличие от screen_click для мира),
  поэтому удержание через тики реально драйвит Enhanced Input.

---

## [3.5.0] — 2026-06-02

### Added
- `screen_click` получил параметр `action`: `"click"` (down+up, дефолт — старое
  поведение), `"down"` (только нажатие), `"up"` (только отпускание). Python-tool
  `screen_click(..., action=...)`.

### Why
- Для тестирования игровых кликов по миру через Enhanced Input: событие
  `ETriggerEvent::Triggered` порождается только когда кнопка «зажата» хотя бы один
  тик. `action="click"` (down+up в одном кадре) этого не даёт — IA не срабатывает.
  Теперь авто-клик по клетке/юниту делается так:
  `screen_click(action="down")` → `tick_world(2)` → `screen_click(action="up")`,
  и Enhanced Input видит зажатие → `Triggered` доходит до контроллера. Для UMG-кнопок
  по-прежнему достаточно `action="click"`.

---

## [3.4.0] — 2026-06-02

Новая команда `screen_click` — настоящий клик мышью по экранным координатам PIE
через Slate hit-testing.

### Added

- **`screen_click`** (`FPIECommands::HandleScreenClick`, `PIECommands.cpp`;
  Python tool `screen_click` в `tools/pie_tools.py`). Инъектирует реальное
  mouse-событие (`FSlateApplication::ProcessMouseButtonDownEvent` +
  `...UpEvent`) по координатам игрового вьюпорта PIE. Параметры:
  `x`, `y` (пиксели вьюпорта или 0..1 при `normalized=true`),
  `button` ("Left"/"Right"/"Middle", default Left), `controller_index`
  (мульти-PIE окно), `normalized` (bool). Резолв окна/геометрии переиспользует
  путь `pie_screenshot` (`GameViewport->GetWindow()` /
  `FUnrealMCPPIEUtils::GetPIEWorldForClient`); перевод viewport-пикселей в
  абсолютные десктопные координаты — через геометрию widget'а вьюпорта
  (`GetAbsolutePosition` + `GetAbsoluteSize`, учитывает позицию окна и DPI).
  Ответ: `{ ok, button, screen_x, screen_y, abs_x, abs_y, controller_index }`.

### Why

- Для авто-тестирования UI/мировых кликов нужен клик, проходящий Slate
  hit-testing: проверить, что виджет с `SelfHitTestInvisible` пропускает клик
  в мир (в клетку поля), а кнопки — перехватывают. Существующая
  `click_widget_by_name` для динамически созданных виджетов (NewObject, не
  BindWidget) даёт геометрию 0,0 и кликает в (0,0) — мимо. А `simulate_key`
  инъектит на уровне `APlayerController::InputKey`, минуя Slate, поэтому не
  тестирует UI hit-test/перехват кликов. `screen_click` кликает по конкретным
  пикселям ЧЕРЕЗ Slate, как реальная мышь.

---

## [3.3.1] — 2026-06-01

Фикс readback в `pie_screenshot`: кадр рендерился на экране, но захват писал
полностью чёрный PNG.

### Fixed

- **`pie_screenshot` чёрный PNG** (`FPIECommands::HandlePieScreenshot`,
  `PIECommands.cpp`). Подтверждено вживую: при `pie_start mode="new_window"`
  floating-окно корректно рендерит игру НА ЭКРАНЕ, но прежние пути захвата
  (`Viewport->TakeHighResScreenShot()` и `FScreenshotRequest::RequestScreenshot`)
  читали scene-буфер/не ту поверхность → чёрный PNG (1280×720, ~17758 Б,
  идентичный между снимками). Рендер работал — баг был чисто в readback.

### Changed

- **Основной путь захвата — Slate window readback.** Когда найден `SWindow`
  игрового PIE-вьюпорта (`UGameViewportClient::GetWindow()`, иначе
  `FSlateApplication::FindWidgetWindow(GetGameViewportWidget())`), кадр читается
  с реального backbuffer'а окна через
  `FSlateApplication::Get().TakeScreenshot(Window, OutColor, OutSize)` (UE 5.7
  сигнатура `bool TakeScreenshot(const TSharedRef<SWidget>&, TArray<FColor>&,
  FIntVector&)`) — это то, что видно на экране (3D-сцена + UMG поверх).
  Последовательность: forced redraw → `TakeScreenshot` (ставит запрос) → ещё
  один `Draw(true)` + `FlushRenderingCommands()` (запрос исполняется на
  следующей отрисовке окна). Alpha принудительно ставится в 255 (TakeScreenshot
  может вернуть A=0 → прозрачный/чёрный PNG). Кодирование —
  `FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>, TArray64<uint8>)`
  + запись `FFileHelper::SaveArrayToFile`. Работает и для `show_ui=true`, и для
  `show_ui=false` (захват окна включает финальный кадр со сценой и UI).
- Ответ при Slate-захвате: `source="slate_window"`, добавлены `width`/`height`/
  `bytes`, `note="...via Slate window readback..."`. Старый scene-путь
  (`source="game_viewport"`, forced redraw 3.2.1) сохранён как fallback, если
  окно не найдено или readback пуст; ещё ниже — editor-fallback
  (`source="fallback_editor_viewport"`). Контракт ответа (`status`/`assetPath`/
  `filename`/`show_ui`/`source`/`note`) и `controller_index`-суффикс сохранены.

### Why

`new_window` (3.3.0) и forced redraw (3.2.1) гарантировали корректный present
на экране, но scene-readback всё равно отдавал чёрное. Чтение именно
презентованного Slate-backbuffer'а окна — единственный путь, видящий то же, что
и пользователь. Новых build-модулей не требуется: `FSlateApplication` —
Slate/SlateCore, `FImageUtils` — Engine, `FFileHelper` — Core (все уже
подключены).

---

## [3.3.0] — 2026-06-01

Режим запуска PIE в отдельном floating game-окне для надёжного (не-чёрного)
скриншота в авто-прогонах.

### Added

- **`pie_start` mode `"new_window"`** (`FPIECommands::HandlePieStart`,
  `PIECommands.cpp`). Новое значение параметра `mode` (рядом с дефолтным
  `"selected_viewport"`). При `mode="new_window"` PIE презентится в отдельном
  floating-окне (самостоятельный `SWindow` + `SPIEViewport` с реальным
  present'ом), а не вкладывается в level-вьюпорт редактора. Механика (UE 5.7
  `PlayLevel.cpp`): `SessionDestination` остаётся `InProcess` (тот же процесс →
  MCP сохраняет TCP-listener), но `FRequestPlaySessionParams::DestinationSlateViewport`
  явно очищается → движок идёт в ветку `GeneratePIEViewportWindow`. Окно
  размером по умолчанию 1280×720, центрируется; размер настраивается опциональными
  параметрами `window_width`/`window_height` (clamp ≥64). Размер задаётся через
  public-поля `ULevelEditorPlaySettings` (`NewWindowWidth/Height`,
  `CenterNewWindow`, `LastExecutedPlayModeType = PlayMode_InEditorFloating`).
  Ответ дополнен полями `new_window` (bool) и, при new_window, `window_width`/
  `window_height`.

### Why

- В авто-прогоне (`playtest_visual.py`) редактор в фоне/не realtime → его
  PIE level-вьюпорт не презентит реальный кадр, поэтому `pie_screenshot`
  читал чёрный backbuffer даже с форс-redraw из 3.2.1 (для UMG-экранов hi-res
  screenshot снимает scene-буфер без UI). Отдельное presented-окно имеет живой
  backbuffer со сценой+UMG → существующий путь захвата даёт не-чёрный кадр.

### Compatibility

- Дефолт `mode="selected_viewport"` и режимы net mode
  (`PIE_Standalone`/`PIE_ListenServer`/`PIE_Client`, ось `dedicated_server`/
  `num_clients`) не изменены — `new_window` ортогонален net mode.
  `controller_index`/multi-world резолв (`GetPIEWorldForClient`,
  `pie_screenshot`/`get_widget_tree`/`pie_status`) работает без правок: для n=1
  `PlayWorld->GetGameViewport()` указывает на viewport нового окна.

---

## [3.2.1] — 2026-06-01

Фикс чёрного кадра в `pie_screenshot` при авто-прогонах с фоновым редактором.

### Fixed

- **`pie_screenshot` отдавал полностью чёрный PNG** в headless/фоновом
  авто-режиме (`FPIECommands::HandlePieScreenshot`, `PIECommands.cpp`). Раньше
  команда просто ставила запрос (`TakeHighResScreenShot` / `RequestScreenshot`)
  в очередь, а кадр обрабатывался на следующем тике рендера. Когда редактор в
  фоне, его PIE-вьюпорт не перерисовывается сам → читался пустой backbuffer.
  Теперь вокруг постановки запроса команда форсирует
  `Viewport->InvalidateDisplay()` + `Viewport->Draw(true)` +
  `FlushRenderingCommands()`: первый Draw презентует свежую сцену+UI, второй
  Draw обрабатывает screenshot-запрос на актуальном буфере, после чего флаш
  синхронно дожидается записи PNG. Команда уже исполняется на GameThread
  (`UEpicUnrealMCPBridge::ExecuteCommand` → `AsyncTask(GameThread)`), поэтому
  синхронный redraw/flush безопасен. Контракт ответа не изменён
  (`status/assetPath/filename/show_ui/source/note`), `controller_index`-суффикс
  и fallback-ветка сохранены; обновлён только текст `note`.

### Why

В авто-харнессе `tests/playtest_visual.py` поднимаются два UnrealEditor (PIE
n=1 в каждом), управляемые по TCP, окна НЕ в фокусе. Все кадры на всех фазах
выходили побайтно идентичными чёрными PNG (35601 байт, 1914×994 = размер
level-вьюпорта редактора), хотя PIE реально бежал (`is_running=true`, живые
юниты). `Slate.bAllowThrottling 0`, `t.IdleWhenNotForeground 0` и
`SetForegroundWindow` не помогали, потому что проблема не в throttling, а в
отсутствии свежего present для фонового PIE-вьюпорта — нужен явный redraw
перед чтением буфера.

---

## [3.2.0] — 2026-06-01

Конфигурируемый порт MCP-моста — несколько инстансов Editor'а могут работать
одновременно, каждый со своим TCP-портом.

### Added

- **Per-process MCP port override** в `EpicUnrealMCPBridge::Initialize()`
  (`EpicUnrealMCPBridge.cpp`). Порт берётся из (по приоритету): аргумент
  командной строки `-MCPPort=<n>`, затем env-переменная `UNREALMCP_PORT`,
  иначе дефолт `55557`. Валидация диапазона `1..65535`. Без флага поведение
  не меняется (обратная совместимость).

### Why

Для 2-player визуального playtest-харнесса нужно поднять два инстанса
UnrealEditor одного проекта одновременно — каждый рендерит своего игрока. Два
процесса не могут слушать один порт `55557`, поэтому второй запускается с
`-MCPPort=55558`. Мост — `UEditorSubsystem`, создаётся в каждом процессе
отдельно и читает свою командную строку, поэтому override работает по-процессно.

---

## [3.1.0] — 2026-05-30

Read-only инспекция Enhanced Input — верификация конфигурации без чтения
бинарных `.uasset`.

### Added

- **`input_action_get_info`** — загружает `UInputAction` по `action_path` и
  возвращает `value_type` (`Boolean`/`Axis1D`/`Axis2D`/`Axis3D`), массив имён
  классов триггеров (`triggers`) и модификаторов (`modifiers`). Реализовано в
  `FInputCommands` (`InputCommands.cpp`), зарегистрировано в bridge allow-list.
- **`input_mapping_context_get_info`** — загружает `UInputMappingContext` по
  `context_path` и возвращает массив `mappings`, по одному объекту на
  `FEnhancedActionKeyMapping`: `key`, путь привязанного `action`, классы
  `triggers` и `modifiers` маппинга.
- Python-обёртки `input_action_get_info` / `input_mapping_context_get_info` в
  `Python/tools/enhanced_input_tools.py` (subject-first, read-only).

### Why

Чтобы автоматически верифицировать корректность настройки Enhanced Input
(`IA_*` / `IMC_*`) после генерации через MCP, не открывая ассеты вручную в
Editor и не парся бинарный формат `.uasset`.

---

## [3.0.0] — 2026-05-29

Точка расширения: общий плагин стал generic — project-specific команды вынесены.

### Added

- **Реестр внешних обработчиков команд.** Новый интерфейс
  `IUnrealMCPCommandHandler` (`CanHandleCommand` + `HandleCommand`) и API модуля
  `FEpicUnrealMCPModule::RegisterCommandHandler` / `UnregisterCommandHandler`.
  Bridge для нераспознанной команды опрашивает зарегистрированные внешние
  обработчики. Это позволяет project-specific плагинам добавлять свои команды,
  не трогая код ядра.

### Removed

- **Все `wc_*` команды** (unit selection / deployment / battle) и
  `WarCardGameCommands.*` удалены из плагина. Они переехали в отдельный плагин
  **WarCardMCP** (репо клиента WarCard), который регистрируется через новый
  реестр. Python-обёртки `warcard_tools.py` больше не регистрируются в
  `unreal_mcp_server.py`.
- Команды: `wc_select_unit`, `wc_deselect_unit`, `wc_confirm_selection`,
  `wc_get_selection_state`, `wc_deploy_unit`, `wc_confirm_deployment`,
  `wc_get_deployment_state`, `wc_surrender`, `wc_end_turn`,
  `wc_get_battle_state`, `wc_free_move`, `wc_attack`, `wc_get_battle_units`
  (history: вводились в 2.14.0 / 2.18.0 / 2.19.0 / 2.20.0 — все перенесены).

### Why

- Общий плагин UnrealMCP (DONXAKer/unreal-mcp) должен оставаться переносимым:
  в ядре не должно быть ни одной строки про конкретный проект. WarCard-команды
  ни от чего игрового не зависят на уровне компиляции (reflection-вызовы), поэтому
  выносятся без потерь. **MAJOR** — удалены команды из поверхности плагина (хотя
  при включённом WarCardMCP они по-прежнему доступны в рантайме).

---

## [2.18.0] — 2026-05-28

Фикс `get_actors_in_level` / `find_actors_by_name` envelope (FIX-UI-008).

> Примечание: battle-команды `wc_*`, вводившиеся в этой версии, перенесены в
> плагин WarCardMCP (см. 3.0.0). Здесь оставлен только generic-фикс.

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

- `get_actors_in_level`/`find_actors_by_name` возвращали голый список и ломали
  envelope-обёртку FastMCP — любой успешный ответ превращался в ошибку. Фикс
  generic, к WarCard отношения не имеет. Рестарт MCP-сервера нужен для подхвата.

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
- `dedicated_server=true` теперь явно включает `PIE_Client` + `bLaunchSeparateServer` + `RunUnderOneProcess(true)` (UE-server-flow для отладки; для проектов с внешним игровым сервером не нужен — клиенты ходят на него напрямую).

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

> Историческое: фикс UE-краша в reflection-хелпере `InvokeFunction` при
> void-функциях без параметров (`ParmsSize == 0` → `InitializeStruct(nullptr)`
> assertion). Этот код был project-specific и перенесён в плагин WarCardMCP
> (см. 3.0.0).

---

## [2.16.0] — 2026-05-27

Adds `text` field to widget tree dump — тест может видеть содержимое
TextBlock/EditableText/RichTextBlock без отдельной команды.

### Added

- `BuildWidgetJson` (используется `get_widget_tree`) теперь добавляет поле `text` для виджетов с текстом: `UTextBlock`, `URichTextBlock`, `UEditableTextBox`, `UEditableText`, `UMultiLineEditableTextBox`, `UMultiLineEditableText`. Для контейнеров поле отсутствует (избегаем раздувания JSON).

### Why

E2e-тесты не могли проверить, что текстовые виджеты содержат правильное
содержимое после обновления данных. Раньше snapshot показывал только
структуру, без текста — баг "виджет отрисован, но пустой" был невидим
автоматизации.

---

## [2.15.0] — 2026-05-27

> Историческое: вводило project-specific команды unit-selection (`wc_select_unit`
> и соседние). Перенесены в плагин WarCardMCP (см. 3.0.0).

---

## [2.14.0] — 2026-05-27

> Историческое: первая project-specific категория команд (deployment-bridge через
> UE reflection: `WarCardGameCommands.*`, `wc_deploy_unit` и соседние, helper
> `InvokeFunction`, `warcard_tools.py`). Весь этот код перенесён в плагин
> WarCardMCP (см. 3.0.0); reflection-паттерн (class-path + `FindFunction` +
> `ProcessEvent` с FProperty iteration) остаётся образцом для внешних обработчиков.

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
