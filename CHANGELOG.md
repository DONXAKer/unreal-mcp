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

## [3.24.0] - 2026-08-11

### Added

- **`material_get_graph`** — чтение графа мастер-материала. Первый *читающий*
  material-инструмент: до сих пор были только писатели (`material_create`,
  `material_add_node`, `material_connect`, `material_set_node_param`), а
  `get_available_materials` / `get_actor_material_info` граф не показывают.
  Параметры: `assetPath` (UMaterial **или** UMaterialInstance — для инстанса
  берётся `GetBaseMaterial()`), `includeUnconnectedInputs` (bool, default true),
  `maxNodes` (int, default 500).
  Ответ в `meta`: `materialPath`, `resolvedFrom` (`material` | `instance`),
  `nodeCount`, `returnedNodeCount`, `truncated`,
  `nodes[]` — `{name, class, nodeId, posX, posY, props{}, inputs[]}` и
  `rootConnections{}` — `BaseColor` / `Normal` / `Roughness` / `Metallic` /
  `WorldPositionOffset` / `CustomizedUV0..7` / … → `{node, nodeClass, nodeId,
  outputIndex}` (только реально подключённые пины).
  `props` зависят от типа узла: `ParameterName` + `DefaultValue`
  (ScalarParameter / VectorParameter), `SpeedX` / `SpeedY` / `bFractionalPart`
  (Panner), `Texture` + `TexturePath` (любой наследник
  `UMaterialExpressionTextureBase`, включая `TextureSampleParameter2D`),
  `CoordinateIndex` / `UTiling` / `VTiling` (TextureCoordinate),
  `ConstA` / `ConstB` (Add, Multiply), `ConstAlpha` (Lerp), `Code`
  (Custom HLSL, обрезается до 2000 символов с флагом `CodeTruncated`).
  `inputs[]` — `{index, name, connected, fromNode, fromClass, fromNodeId,
  fromOutputIndex}`: связи и есть главное, ради чего команда написана.
- Python-обёртка `material_get_graph` в `Python/tools/material_tools.py`.

### Why

Гусеницы Т-72 не крутятся, и все остальные гипотезы уже отработаны фактами:
материал `MI_t72b_Track` на мастере `M_WarCard_UnitTrack` сидит в слоте 1 у
`SM_t72_red_Hull` / `SM_t72_red_Combined`, мастер экспонирует скаляр
`TrackUVOffset`, `used_with_instanced_static_meshes` = true, `AVehicleUnit`
создаёт `TrackMID` и пишет в него значение, порядок `PostInitUnit` →
`InitMultiInstance` / `ApplyInstanceMIDs` верный. Оставался ровно один
непроверяемый вопрос: доходит ли `TrackUVOffset` до UV-координат сэмплера
внутри графа, или параметр объявлен и висит в воздухе. Проверить это было
нечем — Python-API UE 5.8 выражения не отдаёт (`Material.expression_collection`
нет, `expressions` protected, `get_expressions()` отсутствует), поэтому
единственный путь — C++.

Реализация опирается на editor-only API UE 5.8: `UMaterial::GetExpressions()`
для узлов, `CountInputs()` / `GetInput(i)` / `GetInputName(i)` для связей и
`UMaterial::GetEditorOnlyData()` (`UMaterialEditorOnlyData`) для корневых
пинов — в 5.8 `BaseColor` / `Normal` / `CustomizedUVs[]` живут именно там, а не
на самом `UMaterial`. Всё тело обёрнуто в `#if WITH_EDITOR` / `WITH_EDITORONLY_DATA`,
в cooked-сборке команда отвечает `EDITOR_ONLY`.

## [3.22.0] - 2026-08-04

### Added

- **`niagara_get_info(assetPath, includeModuleInputs=False)`** — read-only
  интроспекция `UNiagaraSystem`. Возвращает `meta.emitterCount`,
  `meta.enabledEmitterCount`, `meta.totalModuleCount`, `meta.totalRendererCount`,
  `meta.isEmpty`, `meta.warmupTime`, `meta.warnings[]`, `meta.exposedParameters[]`
  и `meta.emitters[]` — по каждому эмиттеру: `name`, `enabled`, `mode`,
  `uniqueName`, `sourceAsset`, `localSpace`, `simTarget` (CPU/GPU),
  `determinism`, `randomSeed`, `boundsMode`, `renderers[]` (класс + enabled),
  `modules{emitterSpawn,emitterUpdate,particleSpawn,particleUpdate}` (имена
  модулей в порядке исполнения), `loopBehavior`, `loopDuration`.
  С `includeModuleInputs=True` дополнительно отдаёт `moduleInputs[]` — все
  rapid-iteration параметры (`name`/`type`/`value`) и `staticSwitchInputs[]`.

- **`niagara_add_emitter(systemPath, emitterAssetPath, emitterName="", compile=True)`**
  — добавляет ассет `UNiagaraEmitter` в систему через
  `FNiagaraEditorUtilities::AddEmitterToSystem` (UE 5.8 сигнатура с
  `FGuid EmitterVersion`), опционально переименовывает handle,
  `RequestCompile` + `WaitForCompilationComplete`, сохраняет.

- **`niagara_set_emitter_properties(systemPath, emitterName, ...)`** — `enabled`,
  `localSpace`, `simTarget` ("CPU"/"GPU"), `determinism`, `randomSeed`,
  `boundsMode`, `fixedBounds`, а также `loopBehavior` ("Once"/"Multiple"/
  "Infinite"), `loopDuration`, `loopCount` — последние три маршрутизируются
  в модуль Emitter State (в Niagara они не поля эмиттера, а входы модуля).

- **`niagara_set_module_input(systemPath, emitterName, inputName, value, moduleName="", compile=True)`**
  — правка одного входа модуля. Порядок резолва: (1) rapid-iteration параметры
  скриптов эмиттера (`Constants.<Emitter>.<Module>.<Input>`), (2) static-switch
  пины на `UNiagaraNodeFunctionCall`. Типы: float, int, bool, enum (по имени
  элемента), vec2/vec3/vec4/color/quat/position. Имена модулей и входов
  сравниваются без учёта регистра и пробелов ("Loop Behavior" == "loopbehavior").

- Python-обёртки в `Python/tools/niagara_tools.py` (4 новых `@mcp.tool`,
  typed-сигнатуры без `Any`/`Optional`) и примитивы в `Python/tools/primitives.py`.

### Changed

- `NiagaraCommands.cpp` собирает список модулей стадии собственным обходом
  цепочки Parameter Map от `UNiagaraNodeOutput` — `FNiagaraStackGraphUtilities::
  GetOrderedModuleNodes` не экспортируется из `NiagaraEditor` (нет
  `NIAGARAEDITOR_API`), линковаться на неё нельзя.

### Why

`/Game/Effects/Cards/NS_SmokeCloud` был ПУСТОЙ Niagara-системой: 0 эмиттеров.
`UNiagaraFunctionLibrary::SpawnSystemAtLocation` честно возвращал компонент,
5 облаков ложились в `ActiveSmokeClouds`, частиц не было ни одной, в логе —
тишина. Карта «Дымовая завеса» не работала визуально, и проверить это было
нечем: граф системы недоступен ни из C++-команд плагина, ни из editor-Python
(`get_editor_property('emitter_handles')` не находит свойство — оно не
экспонировано в Python API). Приходилось сравнивать размеры `.uasset` и
grep'ать бинарник. `niagara_get_info` закрывает этот класс «молчаливых
успехов» одним вызовом (`meta.isEmpty` + `meta.warnings`), а
`niagara_add_emitter` / `niagara_set_emitter_properties` /
`niagara_set_module_input` позволяют собрать зацикленную дымовую завесу по
спеке `client/Documentation/VFX_Combat_Design.md:82-83, 311-318` вместо
копирования burst-системы `NS_DeathSmoke` поверх.

Контракт «никаких молчаливых успехов» соблюдён явно:
- `niagara_add_emitter` сверяет количество handle'ов до/после и валидность
  возвращённого `FGuid` → иначе `NIAGARA_ADD_EMITTER_FAILED`;
- `niagara_set_module_input` перечитывает записанное значение из
  `FNiagaraParameterStore` и сравнивает байты → иначе
  `NIAGARA_MODULE_INPUT_WRITE_FAILED`; при отсутствии входа возвращает
  `NIAGARA_MODULE_INPUT_NOT_FOUND` с `details.availableModules` и
  `details.availableInputs`, чтобы следующий вызов не был угадыванием;
- `niagara_set_emitter_properties` при частичном применении отдаёт `ok:false` /
  `NIAGARA_PROPERTY_NOT_APPLIED` с `details.applied` + `details.unapplied`
  (и `details.savedAppliedChanges`), а не «успех» с потерянными полями;
- отказ `SaveAsset` — отдельный `NIAGARA_SAVE_FAILED`, а не игнорируется;
- `AddEmitterToSystem` внутри делает `CastChecked` на `UNiagaraSystemEditorData` —
  handler проверяет её наличие заранее и возвращает
  `NIAGARA_SYSTEM_EDITOR_DATA_MISSING` вместо падения редактора.

`NiagaraEditor` уже был в `PrivateDependencyModuleNames` — новых зависимостей
в `UnrealMCP.Build.cs` не потребовалось.

## [3.21.2] - 2026-08-04

### Fixed

- **Любая команда, тянущая за собой синхронную загрузку ассета или компиляцию
  Blueprint'а, роняла редактор** ассертом
  `Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1`
  (`Engine/Source/Runtime/Core/Private/Async/TaskGraph.cpp:705`), а MCP отвечал
  `No response from Unreal Engine`. Стопроцентный репро (UE 5.8):

  ```
  set_component_property(blueprint_name="/Game/Effects/BP_CombatVFXManager",
                         component_name="CombatVFXManager",
                         property_name="NS_SmokeCloud",
                         property_value="/Game/Effects/Cards/NS_SmokeScreen.NS_SmokeScreen")
  ```

  Причина — не в самом handler'е, а в конвейере: `UEpicUnrealMCPBridge::ExecuteCommand`
  ставил тело команды в `AsyncTask(ENamedThreads::GameThread, ...)`, то есть весь
  обработчик исполнялся **внутри TaskGraph-задачи** (`ProcessThreadUntilIdle`).
  Любая синхронная блокировка оттуда — `StaticLoadObject` → `FlushAsyncLoading`,
  `FKismetEditorUtilities::CompileBlueprint`, `FlushRenderingCommands`, ожидание
  компиляции Niagara-скриптов/шейдеров — повторно входит в цикл разбора задач
  именованного потока и срывает recursion guard.
  Фикс — `EpicUnrealMCPBridge.cpp:305`: команда планируется через
  `FTSTicker::GetCoreTicker().AddTicker(..., 0.0f)` (одноразовый тикер), который
  тикается из `FEngineLoop::Tick` — верхний уровень стека game thread'а, вне
  контекста задачи. Контракт не изменился: сокет-поток по-прежнему блокируется на
  `Future.Get()` и отдаёт синхронный ответ, просто на такт позже.
  Фикс общий для всех ~145 команд: `set_blueprint_property`,
  `set_variable_default_value`, `apply_material_to_blueprint`,
  `delete_component_from_blueprint`, `compile_blueprint` и прочие, зовущие
  `CompileBlueprint` прямо в handler'е, были подвержены тому же крашу.
- **`set_component_property` для object-свойств** (`UNiagaraSystem*`,
  `UStaticMesh*`, `TObjectPtr<T>`) теперь резолвит ассет явно
  (`EpicUnrealMCPBlueprintCommands.cpp:2899`), а не отдаёт строку в
  `ImportText_Direct`: принимается короткий путь пакета без суффикса объекта
  (`/Game/FX/NS_Smoke` == `/Game/FX/NS_Smoke.NS_Smoke`), `""`/`"None"` очищает
  ссылку, а нерезолвящийся путь даёт внятное
  `Could not load asset '<path>' (expected class <Class>)` вместо голого
  `Failed to import value`. `value` в ответе — полный path ассета.
  `FSoftObjectProperty` в эту ветку не попадает и, как и раньше, идёт через
  `ImportText_Direct` без загрузки.

### Why

Крашилось не одно свойство одного Blueprint'а, а класс операций: «MCP-команда,
которая впервые подтягивает ассет в память». Точечные обходы уже накапливались
(отложенная перестройка в `landscape_set_properties` v3.21.0, ожидание
компиляции текстур в v3.21.1) — каждый чинил свой симптом. Здесь убрана общая
причина: контекст исполнения команды. `landscape_set_properties` оставлен как
есть (его ответ документирует `rebuild: "deferred_next_tick"`), но новым
handler'ам городить FTSTicker внутри себя больше не нужно — тяжёлую работу можно
делать прямо в обработчике.

### Known issues

- Если редактор завис в операции, не прокачивающей core ticker, команда теперь
  дождётся тика (или таймаута Python-клиента) вместо реентрантного исполнения.
  Это намеренно: реентрантное исполнение и было причиной краша.

## [3.21.1] - 2026-08-04

### Fixed

- **`texture_set_properties` ронял редактор fatal-ассертом.** `PostEditChange()`
  запускает асинхронную перекомпрессию, а следующий за ним
  `UEditorAssetLibrary::SaveAsset` внутри себя зовёт
  `FTextureCompilingManager::FinishCompilation`. Если к этому моменту компилятор
  уже вошёл в `PostCompilation` той же текстуры, движок падает по
  `Engine/Private/TextureCompiler.cpp:484` — «Calling FinishCompilation is not
  allowed during PostCompilation». Гонка, поэтому падало через раз: на пакетном
  импорте 42 ground-текстур редактор умер трижды.
  Фикс — `WaitForTextureCompilation()` в `TextureCommands.cpp`: ждём окончания
  компрессии через `FTextureCompilingManager::ProcessAsyncTasks` (на него запрет
  не распространяется) с таймаутом 120 с, дважды — перед правкой свойств и перед
  сейвом.

### Known issues

- **`import_texture` игнорирует `ifExists: "overwrite"`.** Импорт идёт во
  временное имя с последующим `RenameAsset`, а переименование на занятый путь
  падает: ответ `{"status":"success","result":{"ok":false,"error":{"code":
  "TEXTURE_RENAME_FAILED"}}}`, ассет остаётся старым. Обход на стороне
  вызывающего — снести существующий ассет `delete_asset` перед импортом (так
  делает `client/Scripts/import_ground_textures.py`).
- Напоминание про конверт ответа: внешний `status: "success"` означает лишь
  «команда выполнилась». Настоящий итог — в `result.ok`; проверять надо его.
  Хелперы, смотрящие только на внешний уровень, считают успехом любую ошибку.

## [3.21.0] - 2026-08-04

Подготовка инструментария под переход боевой карты на настоящий `ALandscape`
(WarCard, Ф0 плана «детализированная карта»).

### Added
- **`landscape_set_properties`** (C++ `FLandscapeCommands::HandleLandscapeSetProperties`
  + Python-обёртка) — правка СУЩЕСТВУЮЩЕГО ландшафта на месте: `material_path`,
  `enable_nanite` (с перестройкой Nanite-представления через
  `UpdateNaniteRepresentation`), `nanite_lod`, `location`, `save`.
  Без `actor_label` берётся единственный ландшафт уровня; при нескольких —
  отказ `AMBIGUOUS_LANDSCAPE` со списком меток. Масштаб намеренно не
  редактируется — он привязан к разрешению heightmap'а.
  **Перестройка материалов и Nanite выполняется отложенно, на следующем тике**
  (`FTSTicker`): вызванные прямо из обработчика команды, они качают TaskGraph
  изнутри уже исполняющейся задачи и роняют редактор ассертом
  `++Queue(QueueIndex).RecursionGuard == 1` (`Async/TaskGraph.cpp:705`) — тот же
  класс краха, что у `import_asset_tasks()` через Interchange. Ответ содержит
  `meta.rebuild = "deferred_next_tick"`.
- **Python-обёртки для команд, которые были только в C++** и потому оставались
  недоступны как MCP-tool: `actor_get_component_properties`,
  `actor_set_component_property` (3.15.0), `mesh_set_material` (3.16.0),
  `mesh_set_build_scale` (3.19.1), `texture_set_properties` (3.18.0).
- **`material_set_node_param` понимает `TextureCoordinate`**: `UTiling`,
  `VTiling`, `CoordinateIndex`.

### Fixed
- **`create_landscape` портил рельеф двойным swap'ом байт.** PNG хранит 16-битные
  сэмплы big-endian, но `IImageWrapper::GetRaw` отдаёт их уже в порядке
  платформы; ручной `(Hi << 8) | Lo` делал второй swap, младший байт становился
  старшим — и любой нефлэтовый heightmap превращался в частокол игл во весь
  диапазон высот. На плоских тестовых картах (ENV-LANDSCAPE-001) баг был не виден,
  поэтому дожил до первой карты с рельефом. Теперь данные копируются как есть.
- **`create_landscape` смещал ландшафт вдвое.** Центрирующий офсет применялся и к
  позиции актора, и к диапазону квадов в `Import()` — ландшафт уезжал на половину
  своего размера (карта 6300 UU занимала `[-6300..0]` вместо `[-3150..+3150]` и не
  совпадала с игровой сеткой). Актор теперь стоит в начале координат, центрирует
  только `Import()`. Баг не был виден, пока движок перетирал масштаб: при квадах
  по 800 UU положение относительно сетки никто не сверял.
- **`create_landscape` молча игнорировал `scale_*`.** `Import()` /
  `CreateLandscapeInfo()` перетирают масштаб из spawn-трансформа: запрос
  6.25 см на квад возвращался как 800, а `scale_z` 0.75 — как 192 (×128 по XY,
  ×256 по Z). Ландшафт выходил на два порядка больше запрошенного, при том что
  location считался по правильному масштабу — то есть террейн ещё и уезжал
  относительно игровой сетки. Масштаб теперь переустанавливается после
  `CreateLandscapeInfo()` с записью в лог.
- Нода `TextureCoordinate` из 3.20.0 создавалась, но оставалась при дефолтном
  тайлинге 1×1 — задать его было нечем, то есть нода была бесполезна.
  Это ровно тот класс дефекта, из-за которого `material_connect` считали
  сломанным: «1 пиксель из 1.5 млн» при `UTiling=1` против `50`.
- **`import_texture` / `texture_set_properties` молча подменяли неизвестное
  сжатие на BC7.** `Masks` в таблице не было вовсе, хотя Python-докстринг его
  рекламировал: splatmap импортировался как BC7, команда отвечала `ok`, и
  расхождение всплывало только при разборе готового ландшафта. Добавлены
  `Masks`, `Alpha`, `DisplacementMap`, `VectorDisplacementMap`; неизвестное имя
  теперь отдаёт `UNKNOWN_COMPRESSION` со списком допустимых.
- **Смена `LandscapeMaterial` не меняла кадр.** У каждого `LandscapeComponent`
  свой производный MaterialInstance; без
  `UpdateAllComponentMaterialInstances` ландшафт продолжал рендериться прежним
  материалом, притом что запись свойства отвечала успехом.
- Документация `material_add_node` / `material_set_node_param` не упоминала
  `TextureCoordinate` и `Panner`-параметры.

### Why
Переход на `ALandscape` требует итеративной доводки: подмена материала и
включение Nanite не должны стоить переимпорта heightmap'а — пересоздание
ландшафта роняет уже расставленный по нему декор (Z берётся line trace'ом) и
заново пишет уровень. Пресеты погоды (солнце/туман/атмосфера) настраиваются на
КОМПОНЕНТАХ level-акторов, поэтому без Python-обёртки `actor_set_component_property`
они недостижимы из MCP.

## [3.20.0] - 2026-08-03

Запись восстановлена задним числом (в 3.20.0 бампнули `VersionName`, но
CHANGELOG не тронули — история была нелинейной).

### Added
- `material_add_node` принимает `nodeType="TextureCoordinate"`
  (`UMaterialExpressionTextureCoordinate`). Без явного входа UV сэмплер берёт
  `TexCoord0` как есть, и уникальная развёртка меша растягивает текстуру —
  из-за этого подложка карты читалась «гигантским гравием».

## [3.19.1] - 2026-08-03

### Changed
- **`mesh_set_build_scale` ПЕРЕПИСАН и теперь работает.** Три изменения, каждое
  закрывает конкретную причину прежних отказов:
  1. **Абсолютный масштаб вместо множителя.** Целевой размер считается от габаритов
     ИСХОДНОЙ геометрии (`MeshDescription`), а не от границ собранного меша. Команда
     идемпотентна — повторные вызовы больше не накапливаются (прежде накопилось
     `BuildScale3D = 124 117 817`).
  2. **Масштаб запекается в вершины, а не в `BuildSettings`.** Проверено на
     `SM_t72_red`: `BuildScale3D = 105.55` корректно сохранялся и переживал
     перезапуск редактора, но габариты меша оставались 1.9 UU — для этих ассетов
     (все с Nanite) движок `BuildScale3D` к результату не применяет. Правка позиций
     вершин через `ModifyMeshDescription`/`CommitMeshDescription` работает
     независимо от пути сборки. `BuildScale3D` при этом сбрасывается в 1.
  3. **Санитарный диапазон** 1e-4..1e4 — отказ вместо молчаливой порчи ассета.
- Параметры: `targetLongestAxisUU` (целевой габарит) или `buildScale` (абсолютный).
  Относительный множитель убран by design.

### Result
Парк приведён к согласованным габаритам: `SM_t72_red` 1.9 -> **200.0 UU** (2.00 тайла,
вровень с `abrams_blue` 201.7 и `btr80_red` 200.0), вся пехота -> **180.0 UU** (рост
1.8 м), RED и BLUE совпадают. До этого танк при 1.9 UU был в игре практически невидим.

## [3.18.0] - 2026-08-03

### Added
- **`texture_set_properties`** - правка свойств УЖЕ импортированной текстуры:
  `compression`, `sRGB`, `flipGreenChannel`. Существует потому, что передача
  `compression="Normalmap"` В МОМЕНТ импорта вешает импортёр UE 5.8 намертво
  (см. [3.17.1] Known issue). Рабочая схема: импортировать с дефолтным BC7, затем
  этой командой поставить правильный тип. При выборе `Normalmap`/`Grayscale`
  автоматически снимается `sRGB` - это линейные данные, и sRGB на них ломает
  освещение. В ответе отдаёт `sizeX/sizeY` из `Source`, то есть авторский размер,
  а не текущий загруженный мип.

### Why
Нормаль-карты юнитов лежали как BC7 c sRGB и трактовались движком как цвет.
Теперь у всех трёх наборов: BaseColor = BC7 + sRGB, Roughness = Grayscale линейный,
Normal = Normalmap линейный, все 2048x2048.

### Note
`mesh_get_stats` читает `UTexture2D::GetSizeX()`, который у недостримленной текстуры
отдаёт младший мип (наблюдалось "128" сразу после импорта). Для аудита авторского
разрешения надо переключить его на `Source.GetSizeX()` - как сделано в
`texture_set_properties`.

## [3.17.1] - 2026-08-03

### Added
- **`mesh_set_build_scale`** - правка масштаба В САМОМ АССЕТЕ через
  `BuildSettings.BuildScale3D` + `Build()`. Принимает либо `scale` (множитель),
  либо `targetLongestAxisUU` (целевой габарит - множитель считается сам).
  Нужна, потому что патчить масштаб при спавне неправильно: ошибка импорта
  должна чиниться в источнике.

### Known issue
- **`import_texture` с `compression="Normalmap"` ВЕШАЕТ импортёр UE 5.8.**
  Воспроизводимо: вызов с этим значением возвращает пустой ответ, после чего
  `AssetTools::ImportAssetsAutomated` перестаёт отдавать ассеты для ВСЕХ
  последующих импортов до перезапуска редактора. Обходной путь — не передавать
  `compression` вовсе (дефолт BC7): в этом режиме подряд проходят все девять
  текстур без единого сбоя. Нормаль-карты при этом заводятся как BC7, тип сжатия
  нужно менять отдельно. Причина, судя по всему, в маппинге короткого кода на
  `TextureCompressionSettings` — стоит проверить перед следующим релизом.
- **`mesh_set_build_scale` НЕПРИГОДЕН К ПРИМЕНЕНИЮ — не использовать.** Помимо
  ненадёжности, множители НАКАПЛИВАЮТСЯ в ассете (запись проходит, пересборка нет)
  и срабатывают отложенно: наблюдался меш, пересобравшийся в 17 474 256 UU, и
  накопленный `BuildScale3D = 124 117 817`. Требуется переписать на абсолютный
  сеттер с синхронной пересборкой перед `SaveAsset`. Прежняя формулировка ниже
  описывает только часть проблемы. Наблюдение: при нескольких
  вызовах подряд срабатывает только ПОСЛЕДНИЙ - `UEditorAssetLibrary::SaveAsset`
  успевает записать старые рендер-данные раньше, чем завершится асинхронная
  компиляция меша. Добавленный `FStaticMeshCompilingManager::FinishCompilation`
  проблему не снял и вдобавок стал приводить к таймаутам моста (пустой ответ).
  Также `GetBoundingBox()` сразу после `Build()` отдаёт устаревшие границы,
  поэтому `longestAxisAfterUU` в ответе доверять нельзя - проверять отдельным
  `mesh_get_stats`. Требуется другой подход: переимпорт с import-масштабом либо
  правка в исходном FBX/GLB.

### Why
Аудит парка показал ассеты, импортированные не в тех единицах: `SM_t72_red`
имеет габарит 1.9 UU (танк размером 2 см), пехотные меши ~18 UU.

## [3.16.0] - 2026-08-03

### Added
- **`mesh_set_material`** - назначение материала на слот(ы) UStaticMesh-АССЕТА.
  `apply_material_to_actor` правит актора на уровне, но юниты WarCard спавнятся в
  рантайме, поэтому правка обязана попасть в ассет. Параметры: `assetPath`,
  `materialPath`, `slot` (<0 = все слоты; у AI-мешей имя слота непредсказуемо
  вроде `tripo_mat_2304d4ef`, поэтому адресация по индексу надёжнее).
- **`actor_get_component_properties` / `actor_set_component_property`** (3.15.0) -
  свойства КОМПОНЕНТОВ level-акторов. `set_actor_property` не спускается в
  компоненты, а `set_component_property` адресует компоненты Blueprint-класса;
  свет, туман и атмосфера настраиваются именно на компонентах актора уровня.
  У get есть фильтр по подстроке: у `UDirectionalLightComponent` сотни полей.
- **`viewport_capture`** (3.14.0) - синхронный захват level-вьюпорта.
  `take_screenshot` идёт через `FScreenshotRequest`, обслуживаемый GAME
  viewport'ом: в редакторе БЕЗ PIE он не срабатывает вообще и файл не появляется.

### Fixed
- **Материалы не перекомпилировались после правки графа** (3.15.1). `material_create`,
  `material_add_node`, `material_connect` и `material_set_node_param` сохраняли ассет,
  но не вызывали `PostEditChange()`. Из-за этого список параметров материала не
  пересобирался, и дочерние Material Instance отвечали `not_in_parent` на любой
  `TextureSampleParameter2D`/`ScalarParameter` - собрать PBR-мастер через MCP было
  невозможно. Добавлен `PostEditChange()` в 4 местах.

### Why
Задача «убрать серые нетекстурированные юниты» упёрлась в три отсутствующие
возможности подряд: нельзя прочитать габариты/текстуры меша, нельзя собрать
рабочий PBR-мастер (параметры не регистрировались), нельзя назначить материал на
ассет. Все три закрыты.

## [3.14.0] - 2026-08-02

### Added
- **`viewport_set_camera` / `viewport_get_camera`** - точная постановка и чтение камеры
  перспективного level-вьюпорта: `location` [x,y,z], `rotation` [pitch,yaw,roll], `fov`.
  Существующий `focus_viewport` ставит камеру ОТНОСИТЕЛЬНО актора и потому не
  воспроизводим между прогонами; для сравнения кадров нужна абсолютная постановка.
- **`viewport_set_view_mode`** - режим отображения вьюпорта: `lit`, `unlit`, `wireframe`,
  `detaillighting`, `lightingonly`, `shadercomplexity`, `pathtracing`. Path Tracer заявлен
  обязательным для промо-кадров в ArtDirection_StyleGuide, но канала к нему из MCP не было.
  Захват делает существующий `take_screenshot` (он уже умеет hi-res).
- **`postprocess_get_params` / `postprocess_set_params`** - чтение и запись
  `FPostProcessSettings` на PostProcessVolume по имени актора. Пишет парный флаг
  `bOverride_<Name>` - без него движок молча игнорирует записанное значение, из-за чего
  generic `set_actor_property` "срабатывал", а кадр не менялся. Get возвращает ТОЛЬКО
  переопределённые параметры (полный дамп - сотни полей).
- **`mesh_get_stats`** - read-only аудит UStaticMesh: габариты в UU и в тайлах поля,
  треугольники/вершины LOD0, число LOD, Nanite, максимальное разрешение текстур по
  слотам. Отвечает числами на вопрос "фигура правильного размера и качества?" -
  раньше tri count считался только внутри сеттера `set_static_mesh_properties`.
- **`viewport_capture`** - синхронный захват level-вьюпорта в PNG. Причина существования:
  `take_screenshot` работает через `FScreenshotRequest`, который обслуживается GAME
  viewport'ом - в редакторе БЕЗ PIE он не срабатывает вообще, файл не появляется
  (проверено 2026-08-03). Здесь вьюпорт инвалидируется, рисуется и читается через
  `ReadPixels` немедленно: к моменту ответа файл на диске, polling не нужен. Альфа
  принудительно 255 (readback часто отдаёт 0, и PNG выглядит пустым). Размер кадра
  следует размеру вьюпорта.
- Python-обёртки перечисленного в `Python/tools/editor_tools.py` и `Python/tools/mesh_tools.py`.

### Why
Плагин умел МЕНЯТЬ Editor, но не умел ДОВОДИТЬ картинку: не было воспроизводимой
постановки кадра, доступа к Path Tracer, рабочей записи PostProcess и числовых габаритов
мешей. Эти пять команд - минимальный набор, на котором строится шот-харнесс
(`TOOL-SHOT-001`): снять кадр детерминированно, померить, поправить, снять снова.

## [3.13.0] — 2026-07-05

### Added
- **`reorder_widget`** — новая UMG-команда: перемещает существующий виджет на заданный индекс внутри его текущего родителя-панели. Параметры: `blueprint_path`, `widget_name`, `new_index`. Реализована через `UPanelWidget::ShiftChild`, который двигает сам объект `UPanelSlot` внутри массива Slots — **все свойства слота (alignment, padding, size rule) сохраняются**, в отличие от пары RemoveChild+InsertChildAt, уничтожающей слот. Индекс кламптся в `[0, child_count - 1]`. Ответ: `{success, widget_name, parent_name, old_index, new_index}`.
- **`reparent_widget`** — новая UMG-команда: переносит существующий виджет под другого родителя-панель. Параметры: `blueprint_path`, `widget_name`, `new_parent_name`, `index` (default `-1` = append). Реализация: `RemoveChild` у старого родителя → `InsertChildAt(index)`/`AddChild` у нового. Защита от циклов (новый родитель не может быть потомком переносимого виджета), запрет на reparent корневого виджета; если новая панель отвергла child (single-child панель уже заполнена) — виджет best-effort возвращается старому родителю. Свойства старого слота теряются (новая панель создаёт слот своего типа — тип возвращается в `slot_type`, докрутить alignment/padding можно через `set_widget_property("Slot.*")`). Ответ: `{success, widget_name, old_parent, new_parent, index, slot_type}`.
- Python-обёртки `reorder_widget` / `reparent_widget` в `Python/tools/umg_tools.py` (рядом с `add_widget_to_umg`/`delete_widget_from_umg`).

### Why
При сборке UMG-интерфейсов через MCP до сих пор не было способа изменить порядок детей в VerticalBox/HorizontalBox/Overlay или перенести готовый виджет в другую панель — любая ошибка компоновки требовала delete + пересоздание всего поддерева с потерей настроенных свойств. `reorder_widget`/`reparent_widget` закрывают этот пробел атомарными операциями над WidgetTree с сохранением слота при reorder.

## [3.12.11] — 2026-06-29

### Fixed
- **`set_blueprint_property` падал с `unsupported type 'SoftObjectProperty'`** — добавлена ветка `FSoftObjectProperty` в `HandleSetBlueprintProperty`. Принимает строку `/Game/...` asset path, создаёт `FSoftObjectPtr(FSoftObjectPath(str))` и сохраняет в CDO. Нужно для назначения `TSoftObjectPtr<UDataTable>` и аналогичных soft-ref свойств из C++ через MCP без Blueprint-переменной. Добавлены includes `UObject/SoftObjectPath.h` + `UObject/SoftObjectPtr.h`.

### Why
`UnitInstanceLayoutTable` в `BasicUnit` определён как `TSoftObjectPtr<UDataTable>` (C++ UPROPERTY). `set_blueprint_property` не имел ветки для `FSoftObjectProperty` → `unsupported type`. Soft references — стандартный UE5-паттерн; отсутствие поддержки блокировало все назначения DataTable/Texture/Sound asset-ref через CDO.

## [3.12.10] — 2026-06-29

### Fixed
- **`import_datatable_from_csv` зависал навсегда и роняет bridge (port 55557)** — `UCSVImportFactory` + `AssetTools.ImportAssets()` открывали UI-диалог выбора rowStruct в headless MCP-контексте (путь с F-префиксом `/Script/Client.FUnitInstanceLayoutRow` не матчился — в рефлексии UE5 struct называется `UnitInstanceLayoutRow` без `F`). `UDataTableFactory` + `IAssetTools::CreateAsset` тоже падал/блокировался. Итоговое решение: **полный обход factory pipeline** — `NewObject<UDataTable>(Pkg, *AssetName, RF_Public|RF_Standalone|RF_Transactional)` + `DataTable->RowStruct = RowStruct` + `UDataTable::CreateTableFromCSVString(CSVContent)` + `FAssetRegistryModule::AssetCreated` + `UEditorAssetLibrary::SaveAsset`. Добавлены includes `Factories/DataTableFactory.h` и `Misc/FileHelper.h`. `rowStruct` теперь обязательный параметр — без него `ROWSTRUCT_NOT_FOUND` вместо зависания.

### Why
Четыре падения bridge (port 55557) при попытках создать `DT_UnitInstanceLayout` через MCP. Корень: все factory-based API (`UCSVImportFactory`, `UDataTableFactory`, `IAssetTools::CreateAsset`) в MCP-контексте ведут к UI-блокировке или краш/дедлок. `NewObject<UDataTable>` + `CreateTableFromCSVString` — единственный путь без UI для headless DataTable import в UE5.8.

## [3.12.8] — 2026-06-28

### Fixed
- **`set_component_property` падал с ошибкой при установке `TSubclassOf<UCameraShakeBase>` через Blueprint-класс** — проверка `ClassProp->MetaClass && !FoundClass->IsChildOf(MetaClass)` возвращала `false` для Blueprint-классов шейков камеры (например `CS_TankShot`), у которых иерархия полностью не разрешена до финальной компиляции. Хардовый `CreateErrorResponse` заменён на `UE_LOG(Warning)` + продолжение выполнения. Blueprint-компилятор проверит совместимость при сохранении/компиляции Blueprint'а. Правка затронула оба места в файле (HandleSetBlueprintProperty и HandleSetComponentProperty).
- **`plugin_version` в `EpicUnrealMCPBridge.cpp` не обновлялся с 3.12.3** — строка `"3.12.3"` была забыта в коммите 3.12.7. Исправлено на `"3.12.8"`.

### Why
`set_component_property(blueprint_name="BP_Tank", component_name="CameraShakeComponent", property_name="TankShotShakeClass", property_value="CS_TankShot")` падал с `"Class 'CS_TankShot' is not a child of required meta class 'CameraShakeBase'"`. Blueprint-класс шейка технически является потомком `UCameraShakeBase`, но на момент вызова MCP `IsChildOf` возвращал `false` — это известное поведение UE5 для лениво-загруженных Blueprint-классов до полной инициализации class hierarchy. Решение: доверяем Blueprint-компилятору, только логируем предупреждение.

## [3.12.7] — 2026-06-28

### Fixed
- **`set_component_property` не мог устанавливать `TSubclassOf<X>` свойства Blueprint-классами** — `ImportText_Direct` не способен загружать Blueprint-generated классы, которых нет в памяти (возвращает `nullptr`). Добавлен специальный путь для `FClassProperty`: перед вызовом `ImportText_Direct` проверяем `CastField<FClassProperty>(Property)` и если совпадает — используем тот же алгоритм разрешения что и `set_blueprint_property` (несколько вариантов `LoadClass` по `/Script/*` путям + `LoadBlueprintByName` через AssetRegistry). Класс записывается через `ClassProp->SetObjectPropertyValue` напрямую, минуя `ImportText_Direct`. Добавлена проверка совместимости с `MetaClass`.

### Why
Вызов `set_component_property(blueprint_name, component_name, property_name="TankShotShakeClass", property_value="CS_TankShot")` падал с ошибкой `Failed to import value 'CS_TankShot' for property 'TankShotShakeClass'` потому что `ImportText_Direct` ищет класс только в уже загруженных пакетах. Blueprint-классы загружаются лениво и недоступны без явного `LoadClass`/`LoadBlueprintByName`.

## [3.12.6] — 2026-06-25

### Added
- **save_blueprint** — новая команда в EpicUnrealMCPBlueprintCommands. Вызывает UEditorAssetLibrary::SaveAsset для сохранения Blueprint на диск. Параметры: blueprint_name. Возвращает {name, saved: bool}. Добавлена в allow-list EpicUnrealMCPBridge.cpp.
- **compile_blueprint теперь сохраняет на диск** — HandleCompileBlueprint вызывает SaveAsset после CompileBlueprint (bOnlyIfIsDirty=false). Ответ дополнен полем saved: true. Предотвращает потерю изменений при краше Editor.

### Fixed
- **Изменения Blueprint не сохранялись между сессиями** — add_component_to_blueprint, add_blueprint_node, connect_nodes — не вызывали SaveAsset. После краша Editor все изменения терялись. Решение: compile_blueprint теперь всегда сохраняет.

### Why
При построении роторной анимации BP_mi24 Editor упал до вызова compile_blueprint, что привело к потере всех узлов EventGraph и SCS-компонентов. Добавление SaveAsset в compile и отдельная save_blueprint команда делают работу атомарной.

## [3.12.5] — 2026-06-25

### Fixed
- ** (SCS component variables): пины пустые — вторая попытка** — 3.12.4 пытался использовать  вместо , но UE log показывал  при . Корневая причина:  не может разрешить SCS UPROPERTY потому что  резолвит переменную через skel-class, который не содержит SCS component properties напрямую. Фикс: используем  —  уже содержит SCS properties как UPROPERTY и доступен на момент создания ноды. Фолбэк на  если GeneratedClass недоступен или property не найдено.

### Why
 вызывает . Для  это ищет UPROPERTY в skel-class, который для SCS-компонентов содержит только forward-declarations без объектов. Для  резолюция идёт напрямую через  — что находит UPROPERTY реального компонента.

## [3.12.4] — 2026-06-25

### Fixed
- **`add_blueprint_function_node` (non-static instance methods): `AddRelativeRotation` и другие component methods получают 0 пинов** — фикс 3.12.3 (PostPlacedNewNode + AllocateDefaultPins без ReconstructNode) работает для статических/чистых функций (KismetMathLibrary), но не для нестатических instance-методов на компонентах (USceneComponent::AddRelativeRotation). Для instance-методов `AllocateDefaultPins` без предшествующего `PostPlacedNewNode` не создаёт Target и exec пины. Фикс: два пути инициализации в `CreateCallFunctionNode` — (A) статические функции: PostPlacedNewNode + AllocateDefaultPins (как было в 3.12.3); (B) нестатические instance-методы (флаг `FUNC_Static` отсутствует или это компонент из SCS): `FNodeCreatorUtils::InitializeK2Node` (AllocateDefaultPins + ReconstructNode). Safety-fallback в обоих путях: если Pins.Num() == 0 после первичной инициализации — применяет обратную стратегию.
- **`add_blueprint_variable_get_node` (SCS component variables): пины пустые** — `CreateVariableGetNode` использовал `InitializeK2Node` (AllocateDefaultPins + ReconstructNode). Для SCS компонентных переменных (MainRotorMesh, TailRotorMesh) `ReconstructNode` не мог разрешить VariableReference и очищал Pins[]. Фикс: заменён на PostPlacedNewNode + AllocateDefaultPins с fallback на ReconstructNode при 0 пинах.

### Why
Два разных сбоя инициализации: статические library-функции требуют PostPlacedNewNode+AllocateDefaultPins (без ReconstructNode), а нестатические component instance-методы требуют полного InitializeK2Node цикла (AllocateDefaultPins+ReconstructNode) для создания Target и exec пинов. Предыдущие фиксы (3.12.2-3.12.3) решали только один из двух случаев.

## [3.12.3] — 2026-06-25

### Fixed
- **`add_blueprint_function_node`: пины по-прежнему пустые после 3.12.2** — `FNodeCreatorUtils::InitializeK2Node` вызывал `ReconstructNode()` после `AllocateDefaultPins()`. Для свежесозданного `K2Node_CallFunction` (особенно pure-функций) `ReconstructNode()` очищал `Pins[]` до завершения привязки `FunctionReference`, оставляя ноду без пинов. Фикс: заменил `InitializeK2Node` на явный вызов `PostPlacedNewNode() + AllocateDefaultPins()` — тот же паттерн что использует `FBlueprintNodeManager::CreateCallFunctionNode` (строки 617-624 NodeManager.cpp).

### Why
`FBlueprintNodeManager::CreateCallFunctionNode` никогда не вызывает `ReconstructNode()` на новой ноде — только `PostPlacedNewNode` + `AllocateDefaultPins`. Копирование этого паттерна в `FUtilityNodeCreator::CreateCallFunctionNode` устраняет гонку инициализации.

## [3.12.2] — 2026-06-25

### Fixed
- **`add_blueprint_function_node`: пины по-прежнему пустые после 3.12.1** — `SetFromFunction` устанавливала флаги (`bIsPureFunc`, `bIsConstFunc`) корректно, но для actor-based Blueprint могла резолвить `FunctionReference` в `SetSelfMember` (function found on blueprint's own class, not the library class), что приводило к `GetTargetFunction() → nullptr` во время `AllocateDefaultPins()`. Фикс: вызываем `SetFromFunction(TargetFunc)` для копирования флагов, затем **явно** переписываем `FunctionReference.SetExternalMember(TargetFunc->GetFName(), TargetFunc->GetOwnerClass())`. `GetOwnerClass()` всегда возвращает правильный класс — без зависимости от scope `TargetClass`.

### Why
`Multiply_FloatFloat` (KismetMathLibrary), `AddRelativeRotation` (SceneComponent) — после 3.12.1 класс резолвился корректно и `UFunction*` находился, но `FunctionReference` всё равно указывал на self-member → `AllocateDefaultPins()` не создавал ни одного пина. `SetExternalMember` исправляет разрешение `GetTargetFunction()` через `ResolveMember<UFunction>(GetBlueprint())`.

## [3.12.1] — 2026-06-25

### Fixed
- **`add_blueprint_function_node`: пустые пины (`pins: [], count: 0`)** — `CreateCallFunctionNode` в `UtilityNodes.cpp` не мог разрешить `UClass` для целевых объектов из-за устаревшего `StaticFindObject(nullptr, path)` в UE 5.7/5.8. Фикс: многоуровневое разрешение класса:
  - **Шаг 0** — прямое сравнение с известными библиотечными классами по короткому имени (`KismetMathLibrary`, `KismetSystemLibrary`) через `StaticClass()` (100% надёжно).
  - **Шаг 1** — полный `/Script/...` путь через `LoadObject<UClass>` вместо устаревшего `StaticFindObject`.
  - **Шаг 2** — перебор Package-префиксов (`/Script/Engine.`, `/Script/UMG.`, …) через `LoadObject<UClass>`.
  - **Шаг 3** — `FindFirstObject<UClass>` как UE5 fallback.
  - **Шаг 4** — **новый**: поиск компонента по имени через `Blueprint->SimpleConstructionScript->GetAllNodes()` с взятием `ComponentTemplate->GetClass()`. Позволяет передавать имя компонента (например `MainRotorMesh`) как `target` вместо имени класса.
- `FindFunctionByName` теперь вызывается с `EIncludeSuperFlag::IncludeSuper` — ищет по цепочке наследования (нужно для методов родительских классов, например `AddRelativeRotation` из `USceneComponent`).
- Устаревший `FindObject<UClass>(nullptr, ClassPath)` в else-ветке заменён на `LoadObject<UClass>` для UMG-классов.
- Добавлены include: `Engine/Blueprint.h`, `Engine/SimpleConstructionScript.h`, `Engine/SCS_Node.h`.

### Why
`Multiply_FloatFloat`, `MakeRotator` (KismetMathLibrary) и `AddRelativeRotation` (StaticMeshComponent) не создавали пины после `AllocateDefaultPins()` / `ReconstructNode()` — потому что `UFunction` не резолвился и нода создавалась с `SetSelfMember` без реального указателя на функцию. После фикса `SetFromFunction(UFunction*)` вызывается корректно.

## [3.12.0] — 2026-06-24

### Added
- **`set_nanite_enabled`** — включает или отключает Nanite на существующем `UStaticMesh` ассете.
  Параметры: `assetPath` (required), `enabled` (bool, default `true`).
  Ответ содержит `meta.naniteEnabled` и `meta.trianglesCount` (LOD 0).
  Использует `UStaticMesh::NaniteSettings.bEnabled` + `PostEditChange` + `SaveAsset`.
- **`import_skeletal_mesh`** — импортирует FBX-файл с арматурой/скелетом как `USkeletalMesh`.
  Параметры: `assetPath` (required), `sourcePath` (required, .fbx only), `ifExists` (skip|overwrite|fail|update, default skip).
  Применяет тот же Interchange-bypass CVar (`Interchange.FeatureFlags.Import.FBX=0`) что и `import_static_mesh` — предотвращает GameThread TaskGraph re-entry в UE 5.8.
  `UFbxFactory` авто-определяет тип меша по содержимому FBX (скелет → SkeletalMesh).
  Ответ: `meta.verticesCount`, `meta.boneCount`, `meta.materialSlots[]`, `meta.sourcePath`.
- **Python wrappers** в `Python/tools/mesh_tools.py`: `set_nanite_enabled` и `import_skeletal_mesh` зарегистрированы через `@mcp.tool()`.

### Why
Необходимы для AutoGen3D Pipeline (`gen3d_pipeline.py`): Nanite-контроль для LOD-оптимизации terrain tiles; skeletal mesh import для персонажей/юнитов с риггом без обходных путей через ручной Editor.

## [3.11.1] — 2026-06-24

### Fixed
- **`import_static_mesh`: UE 5.8 RecursionGuard crash при FBX-импорте** — в UE 5.8 `ImportAssetsAutomated` направляет FBX через Interchange, что вызывает `Assertion failed: ++Queue(QueueIndex).RecursionGuard == 1` в `TaskGraph.cpp:705` (GameThread re-entrance из bridge-диспатча). Фикс: перед импортом устанавливаем `Interchange.FeatureFlags.Import.FBX=0` через `IConsoleManager`, после импорта восстанавливаем. `UFbxFactory` пиним в `ImportData->Factory` (belt-and-suspenders). Оба флага гарантируют синхронный legacy-путь без TaskGraph re-entry.

### Why
Terrain tile FBX-импорт через MCP роняет Editor в 5.8 из-за Interchange. Временное отключение CVar + pin factory — минимальный хирургический фикс без переписывания пайплайна.

## [3.11.0] — 2026-06-22

### Added
- **`set_widget_property`: свойства `FontObject` и `Font` для TextBlock** — два новых значения `property_name` в команде `set_widget_property`:
  - `FontObject` — принимает строку с `/Game/...` путём до шрифтового ассета (например `/Game/UI/Fonts/Font_Impact`), загружает его через `LoadObject<UObject>` и назначает `FSlateFontInfo.FontObject`. Размер шрифта остаётся прежним.
  - `Font` — combined-setter формата `"<asset_path>,<size>"` (например `/Game/UI/Fonts/Font_Impact,52`); в одном вызове устанавливает и `FontObject`, и `Size`.
  - Обе ветки возвращают понятный `CreateErrorResponse` если ассет не найден или входная строка некорректна.

### Why
Позволяет менять гарнитуру шрифта (не только размер) у TextBlock виджетов через MCP без ручного открытия Designer.

## [3.10.5] — 2026-06-19

### Fixed
- **`generate_box_static_mesh` → `MESHDESC_CREATE_FAILED` на UE 5.8** — `UStaticMesh::CreateMeshDescription(0)` возвращал `null` потому что вызывался до `AddSourceModel()`. В UE 5.8 `CreateMeshDescription(idx)` требует чтобы соответствующий LOD (SourceModel) уже существовал. Исправление: переместить `AddSourceModel()` перед `CreateMeshDescription(0)` и убрать дублирующий вызов после построения геометрии.

### Why
Команда `generate_box_static_mesh` не работала в UE 5.8 (ART-TERRAIN-002: создание SM_Cell_Hill/River/Trench/Bridge для рельефа боевых ячеек).

## [3.10.4] — 2026-06-19

### Fixed
- **`create_landscape` crash: `Pair != nullptr` в `LandscapeEdit.cpp:3230`** — `ALandscapeProxy::Import()` в UE 5.8 ищет данные по `FGuid()` (пустой GUID, "final layer"), а не по GUID ландшафта. Строка 3229 исходника:
  ```cpp
  const FGuid FinalLayerGuid = FGuid();
  HeightData = InImportHeightData.FindChecked(FinalLayerGuid);
  ```
  Предыдущие версии добавляли данные в карту с ключом `LandscapeGuid` (GUID актора), который не совпадал с `FGuid()`, что вызывало `Assertion failed: Pair != nullptr`.
  - Исправление: ключи `HeightDataPerLayer.Add(FGuid(), ...)` и `MaterialLayerInfo.Add(FGuid(), ...)` заменены на пустой GUID.
  - Первый аргумент `Import(LandscapeGuid, ...)` остаётся GUID'ом актора (для `check(InGuid.IsValid())`).

### Why
- UE5 различает "landscape actor GUID" (первый аргумент Import) и "layer data key" (ключ в картах). Base layer (финальный) всегда ключируется пустым `FGuid()`.

---

## [3.10.3] — 2026-06-19

### Fixed
- **`create_landscape` crash: `InGuid.IsValid()` assertion in `LandscapeEdit.cpp:3128`** — предыдущий фикс (3.10.2) использовал `Landscape->GetLandscapeGuid()`, но `ALandscape` не назначает GUID сразу после `SpawnActor`. `GetLandscapeGuid()` возвращает `FGuid()` (невалидный), что приводило к `Assertion failed: InGuid.IsValid()` при вызове `CreateLandscapeInfo()`.
  - Исправление: генерируем `FGuid::NewGuid()` и ПРИСВАИВАЕМ его актору через `Landscape->LandscapeGuid = LandscapeGuid` ДО вызова `Import()`. Это гарантирует: (а) GUID валиден (`IsValid() == true`), (б) все три места (карта `HeightDataPerLayer`, первый аргумент `Import()`, поле актора) используют один и тот же GUID.

### Why
- `ALandscapeProxy::LandscapeGuid` инициализируется в `FGuid()` при конструировании и назначается только через редактор или явно. После `SpawnActor` в editor-контексте без вызова `RegisterAllComponents()` GUID остаётся невалидным.

---

## [3.10.2] — 2026-06-18

### Fixed
- **`create_landscape` crash: Map.h assertion in `ALandscape::Import`** — `LandscapeCommands.cpp` использовал `FGuid::NewGuid()` для ключа в `HeightDataPerLayer` и `MaterialLayerInfo`, но `ALandscape::Import` внутри делает `FindChecked(Landscape->GetLandscapeGuid())` — т.е. ищет данные по GUID'у, уже присвоенному spawned-актору, а не по переданному первым аргументом. Несовпадение GUID'ов приводило к `Assertion failed: Pair != nullptr [Map.h.inl:638]` и крашу редактора.
  - Заменено: `FGuid::NewGuid()` → `Landscape->GetLandscapeGuid()` (вызывается после `SpawnActor`, до заполнения TMap).
  - Оба ключа `HeightDataPerLayer.Add` и `MaterialLayerInfo.Add` теперь используют правильный GUID актора.

### Why
- `ALandscape::Import` в UE 5.8 делает `FindChecked` по собственному GUID'у актора (не по параметру `InGuid`): актор уже имеет GUID от SpawnActor, и Import ищет именно его в переданных TMap'ах. Передача случайного GUID гарантированно вызывает assert.

---

## [3.10.1] — 2026-06-18

### Fixed
- **Python wrapper for `create_landscape` was missing** — v3.10.0 добавила C++
  реализацию `FLandscapeCommands::HandleCreateLandscape` и зарегистрировала её
  в outer bridge, но забыла создать Python MCP tool. Добавлены:
  - `Python/tools/landscape_tools.py` — `register_landscape_tools(mcp)` с
    `@mcp.tool() create_landscape(...)`, документация + логирование.
  - Регистрация в `unreal_mcp_server.py` (import + вызов `register_landscape_tools`).
  Команда `create_landscape` теперь доступна как MCP tool через stdio и HTTP серверы.

## [3.10.0] — 2026-06-18

### Added
- **`create_landscape`** — новая команда в `FLandscapeCommands` (новые файлы
  `LandscapeCommands.h` / `LandscapeCommands.cpp`). Создаёт полноценный
  `ALandscape` actor из 16-bit grayscale PNG heightmap через `ALandscape::Import`.
  Python/generic spawn_actor создавал только LandscapePlaceholder без компонентов;
  этот путь использует нативный C++ Import API.
  - Параметры: `heightmap_path` (обязательный, абс. путь к PNG), `section_size`
    (def 63), `sections_per_component` (def 1), `components_x`/`components_y`
    (def 32), `scale_x`/`scale_y`/`scale_z` (def 100), `material_path` (опц.),
    `actor_label` (def "Landscape").
  - Размер PNG проверяется: ожидается `(components_x * sections_per_component *
    section_size + 1) x (components_y * sections_per_component * section_size + 1)`.
  - Landscape центрируется на (0,0) через offset `-(width-1)/2`.
  - После Import сохраняет текущий уровень через `UEditorLoadingAndSavingUtils::SaveMap`.
  - Ответ содержит: `actor_label`, `width`, `height`, `components` (NxM),
    `section_size`, `scale`, `location`.

### Build deps added
- `Landscape` (runtime) — `ALandscape`, `FLandscapeImportLayerInfo`
- `LandscapeEditor` (editor-only) — `ALandscape::Import` живёт в editor модуле
- `Foliage` (runtime) — `AInstancedFoliageActor`, транзитивно требуется `LandscapeEdit.h`
- `ImageWrapper` (runtime) — 16-bit PNG decode через `IImageWrapperModule`

### Why
- Python UE 5.8 нет нативного API создания Landscape с heightmap; единственный
  путь — C++ `ALandscape::Import`. Нужно для автоматического создания terrain из
  процедурно-генерированных heightmap'ов через MCP-пайплайн.

## [3.9.0] — 2026-06-18

### Changed
- **UE 5.8: UMG-команды поддерживают `UWidgetBlueprint::WidgetVariableNameToGuidMap`.** UE 5.8 добавила эту карту (`TMap<FName,FGuid>`) и компилятор теперь ensure'ит её консистентность (`WidgetBlueprintCompiler.cpp:781` — у каждого добавленного виджета есть GUID; стр. 815 — нет осиротевших GUID'ов от удалённых переменных). MCP-команды писались под 5.7 (карты не было) → каждый MCP-созданный виджет ловил non-fatal ensure (под отладчиком — breakpoint 0x80000003).
  - **При добавлении** (`add_widget_to_umg`, `add_text_block_to_widget`, `add_button_to_widget` + его вложенный Label, `add_panel_widget_to_widget`): после `AddChild` и до compile регистрируем GUID через новый helper `UMGCommandsUtils::RegisterWidgetGuid` — ровно как движок на стр. 783 (`Add(W->GetFName(), FGuid::NewGuid())`).
  - **При удалении** (`delete_widget_from_umg`): до detach обходим удаляемое поддерево через `WidgetTree->ForWidgetAndChildren` и снимаем GUID-записи (`UnregisterWidgetGuid`), чтобы не плодить осиротевшие GUID'ы (ensure 815).
  - Обёрнуто в `#if WITH_EDITOR`; `WidgetVariableNameToGuidMap` — публичное поле `UWidgetBlueprint` (модуль UMGEditor уже в зависимостях).

### Why
- После перехода проекта на UE 5.8 каждая компиляция WidgetBlueprint'а, созданного/изменённого через MCP, сыпала ensure'ы; под отладчиком это выглядело как краш. Теперь MCP сам поддерживает карту GUID'ов в актуальном состоянии, и компилятору нечего чинить.

## [3.8.4] — 2026-06-17

### Changed
- `FPIECommands` помечен `UNREALMCP_API` (экспорт из модуля). Нужно для миграции MCP на офиц. сервер UE 5.8 (Фаза 2): toolset-классы в плагине WarCardMCP делегируют PIE-команды (`pie_*`/`simulate_key`/`screen_click`/`tick_world`) в существующую боевую логику `FPIECommands` через cross-module вызов вместо переписывания.

### Why
- Стратегия миграции — делегирование: офиц. MCP-сервер вызывает тонкие `UToolsetDefinition`-обёртки, которые переиспользуют выверенные handler'ы (особенно Slate-readback в `pie_screenshot`). Для линковки из другого модуля класс должен быть экспортирован.

## [3.8.3] — 2026-06-17

### Fixed
- **UE 5.8 build break** (hard errors): мигрированы 6 точек с устаревшего `FJsonObject` API под новый тип ключа `UE::FSharedString`. В UE 5.8 `FJsonObject::Values` теперь `TMap<UE::FSharedString, ...>` (а не `TMap<FString, ...>`):
  - `BPVariables.cpp` (×2): `Params->Values.FindRef("default_value")` → `Params->TryGetField(TEXT("default_value"))` (char*-ключ больше не принимается — C2665).
  - `DataAssetCommands.cpp`, `LevelCommands.cpp`, `MeshCommands.cpp`, `NiagaraCommands.cpp`: `const FString& Key = Pair.Key;` → `const FString Key = *Pair.Key;` (ключ `FSharedString` не биндится в `const FString&` — C2440; берём копию через `operator*` → `const TCHAR*`).
- **UE 5.8 deprecations** (C4996, future-proof): обёрнуты в `TEXT(...)` ключи 59 вызовов `Set{String,Number,Bool,Array,Object}Field("...", ...)` с ANSI-литералом в `BPConnector.cpp` и `BPVariables.cpp` → биндятся на `FStringView`-перегрузку вместо deprecated `const ANSICHAR*`. Остальной плагин уже использовал `TEXT()`-ключи. Сейчас это были предупреждения, но в следующем релизе UE стали бы ошибками.

### Why
- Бинарная установка UE 5.8 скомпилирована с новым `FSharedString`-layout ключей JSON; legacy-workaround `UE_JSONOBJECT_LEGACY_STRING_KEYS=1` неприменим (ABI-рассинхрон с готовым движком + блокируется shared build environment), поэтому код плагина мигрирован на новый API полностью — и hard errors, и deprecated-вызовы.

- **TODO (миграция UE 5.8 JSON API):** заменить deprecated `FJsonObject::SetStringField/SetBoolField/SetNumberField/SetObjectField(const char*/ANSICHAR*, ...)` (C4996) на `FStringView`/`TEXT(...)`-перегрузки по всему плагину. Сейчас компилируется как warning, но в следующем релизе UE станет ошибкой.

## [3.8.3] — 2026-06-17

### Fixed
- **UE 5.8 build break**: мигрированы 6 точек с устаревшего `FJsonObject` API под новый тип ключа `UE::FSharedString`. В UE 5.8 `FJsonObject::Values` теперь `TMap<UE::FSharedString, ...>` (а не `TMap<FString, ...>`):
  - `BPVariables.cpp` (×2): `Params->Values.FindRef("default_value")` → `Params->TryGetField(TEXT("default_value"))` (char*-ключ больше не принимается — C2665).
  - `DataAssetCommands.cpp`, `LevelCommands.cpp`, `MeshCommands.cpp`, `NiagaraCommands.cpp`: `const FString& Key = Pair.Key;` → `const FString Key = *Pair.Key;` (ключ `FSharedString` не биндится в `const FString&` — C2440; берём копию через `operator*` → `const TCHAR*`).

### Why
- Бинарная установка UE 5.8 скомпилирована с новым `FSharedString`-layout ключей JSON; legacy-workaround `UE_JSONOBJECT_LEGACY_STRING_KEYS=1` неприменим (ABI-рассинхрон с готовым движком + блокируется shared build environment), поэтому код плагина мигрирован на новый API. Полная зачистка deprecated SetField-вызовов (C4996) — в Unreleased TODO.

## [3.8.2] — 2026-06-17

### Fixed
- `material_set_node_param`: добавлена поддержка `UMaterialExpressionPanner` — поля `SpeedX` (float) и `SpeedY` (float). Ранее Panner пропускал все параметры в `Skipped[]`, оставляя UV статичными (SpeedX=0 → анимации не было).

### Why
- Обработчик `HandleMaterialSetNodeParam` содержал кейсы для Constant/Constant3Vector/ScalarParam/VectorParam/Texture/Noise, но Panner был пропущен. Дополнено вместе с задачей ENV-WATER-001 (animated UV river shader).

---

## [3.8.1] — 2026-06-17

### Fixed
- `material_add_node`: заменён `LoadObject<UClass>(nullptr, classPath)` на прямые `UMaterialExpressionXxx::StaticClass()` для всех поддерживаемых типов нод. Устраняет зависание (timeout >120 s) при добавлении `Panner` и других нод, чей `LoadObject` инициировал длинный lookup в asset registry.

### Why
- `UMaterialExpressionPanner::StaticClass()` мгновенен (класс уже включён через `.h`); `LoadObject<UClass>(nullptr, ...)` создавал задержку при первом обращении к Panner из-за lazy-load механизма (особенно для классов, не находящихся в таблице eager-load движка).

---

## [3.8.0] — 2026-06-17

### Added
- `material_create` — создаёт новый UMaterial (мастер-материал) с выбором domain (`Surface` / `UserInterface`); поддерживает `ifExists`: skip | overwrite | fail.
- `material_add_node` — добавляет expression-ноду (Constant, Constant3Vector, ScalarParameter, VectorParameter, TextureSample, TextureSampleParameter2D, Lerp, Multiply, Add, Panner, Noise) в граф мастер-материала; ID ноды хранится в поле `Desc` для последующей адресации.
- `material_connect` — соединяет выход expression → вход другой expression (`ConnectMaterialExpressions`) или корневое свойство материала (`ConnectMaterialProperty`): BaseColor, EmissiveColor, Opacity, Roughness, Metallic, Normal. Возвращает `PIN_NOT_FOUND` если пин не найден вместо краша.
- `material_set_node_param` — устанавливает типозависимые параметры expression-ноды: `R` у Constant, `Constant` (RGBA-массив) у Constant3Vector, `ParameterName`/`DefaultValue` у ScalarParameter/VectorParameter/TextureSampleParameter2D, `Texture` у TextureSample*, `Scale`/`Levels` у Noise. Неизвестные ключи попадают в `meta.skipped`.
- Python `@mcp.tool()` обёртки: `material_create`, `material_add_node`, `material_connect`, `material_set_node_param` в `Python/tools/material_tools.py`.

### Why
- FEAT-MAT-001: построение master-материалов (M_Panel, M_BG и т.д.) через MCP без ручного открытия Material Editor; устраняет ограничение «только Material Instance» предыдущих команд (`create_material_instance`, `set_material_instance_params`).

---

## [3.7.5] — 2026-06-17

### Added
- **`parent_name` параметр в `add_text_block_to_widget`, `add_button_to_widget`, `add_panel_widget_to_widget`** — необязательный параметр `parent_name` позволяет добавлять виджеты в именованный `UPanelWidget` (VerticalBox, HorizontalBox, и т.д.) вместо root `CanvasPanel`. Если `parent_name` не задан или виджет с таким именем не найден — поведение прежнее (добавление в root CanvasPanel). Добавлен helper `FindParentPanelWidget()` в `UMGCommandsUtils`.

### Why
- Построить вложенную UMG-иерархию (VerticalBox → Button → TextBlock) было невозможно через MCP: все три команды жёстко добавляли в root `CanvasPanel`. Команда `add_panel_widget_to_widget` с `widget_name=/Game/.../WBP_X` + `parent_name=RootBox` теперь добавляет панель внутрь `RootBox`.

---

## [3.7.4] — 2026-06-17

### Added
- `generate_box_static_mesh`: programmatically creates a box `UStaticMesh` via `FMeshDescription` + `FStaticMeshAttributes` API — no file import required.
  Params: `assetPath` (required), `halfExtentX/Y/Z` (float, cm, default 50), `ifExists` (skip|overwrite|update|fail).
  Response meta: `trianglesCount`, `verticesCount`, `halfExtentX/Y/Z`, `collision` ("Simple"), `materialSlots`.
  Added `MeshDescription` + `StaticMeshDescription` to `UnrealMCP.Build.cs` private deps.

### Why
- `import_static_mesh` с OBJ-файлами вызывает EXCEPTION_ACCESS_VIOLATION / deadlock в UE 5.7: Interchange framework
  запускает пайплайн на GameThread'е, который уже занят MCP-dispatch'ем (`AsyncTask(GameThread)`), что приводит к
  рекурсивной блокировке TaskGraph. Новая команда создаёт геометрию полностью программно через `FMeshDescription` API,
  минуя импорт-пайплайн полностью.

---

## [3.7.3] — 2026-06-17

### Fixed
- `import_sound_wave`: вместо RenameAssets (вызывало UE crash) теперь копируем исходный WAV
  во временный файл `Intermediate/MCPImport/<AssetName>.wav` перед импортом.
  Так factory видит правильное имя файла и создаёт ассет с нужным именем сразу.
  Temp-файл удаляется после import'а.

### Why
- `AssetTools.RenameAssets()` вызывал EXCEPTION_ACCESS_VIOLATION в Engine при вызове
  из MCP-контекста (неверный state object'а сразу после ImportAssets). Подход с temp-копией
  обходит rename полностью.

---

## [3.7.2] — 2026-06-17

### Fixed
- `import_sound_wave`: после импорта asset переименовывается в запрошённый `AssetName` из пути.
  `AssetTools.ImportAssets` называет ассет по имени исходного WAV-файла (например, `_placeholder`),
  игнорируя запрошённое имя. Добавлен шаг `AssetTools.RenameAssets` если имена не совпадают.

### Why
- Импорт нескольких SFX из одного WAV-плейсхолдера создавал только `_placeholder.uasset` вместо
  `SFX_UI_Click.uasset`, `SFX_UI_Hover.uasset` и т.д. — все перезаписывали друг друга.

---

## [3.7.1] — 2026-06-17

### Fixed
- `set_widget_property` теперь сохраняет Widget Blueprint на диск (`UEditorAssetLibrary::SaveAsset`)
  сразу после применения изменения. До этого изменения оставались только в памяти Editor'а и
  не записывались в `.uasset` до ручного Ctrl+S или закрытия Editor'а. Это приводило к тому,
  что изменения слотов (Anchors, Offsets, AutoSize) не отражались в git после компиляции.

### Why
- Вызов `MarkBlueprintAsModified` + `MarkPackageDirty` без `SaveAsset` — неполная операция
  для MCP-сценариев (CI, batch-модификации); явный save завершает транзакцию.

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
