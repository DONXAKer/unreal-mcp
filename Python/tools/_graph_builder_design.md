# build_blueprint_graph — Design

## Зачем

`impl_bp` (LLM) при сборке EventGraph делает 15-30 последовательных MCP вызовов
(`add_blueprint_*_node`, `connect_blueprint_nodes`, `set_pin_default_value`).
Sonnet через MCP цепочку нод собирает с ошибками: теряет `node_id`, забывает
соединения, путается в порядке.

`FEAT-MULLIGAN-002` дал реальные цифры: 2 итерации impl_bp (9 + 24 мин),
2 review_bp loopback'а, 4/7 checklist passed — wiring так и не закрылся.

`build_blueprint_graph` — высокоуровневый orchestrator: один JSON-спек
→ один MCP-вызов → весь граф построен **атомарно** (с rollback при сбое).

## Контракт

### Спек

```json
{
  "blueprint_path": "/Game/UI/Mulligan/WBP_Mulligan",
  "graph": "EventGraph",
  "nodes": [
    {"id": "<local_id>", "type": "<NodeType>", ...type-specific params}
  ],
  "connections": [
    {"from": "<local_id>.<pin_name>", "to": "<local_id>.<pin_name>"}
  ],
  "defaults": [
    {"pin": "<local_id>.<pin_name>", "value": <literal>}
  ],
  "existing_nodes": {"<local_id>": "<unreal_guid>"},
  "compile": true,
  "rollback_on_failure": true,
  "clear_graph_first": false
}
```

- `local_id` — произвольный строковый идентификатор узла внутри спека.
  Orchestrator маппит его на реальный `node_id` от UE.
- `existing_nodes` — словарь уже существующих в графе нод, к которым можно
  присоединять новые. Нужно для инкрементального построения.
- `compile` default `true`. После defaults вызывается `compile_blueprint`.
- `rollback_on_failure` default `true`. При любом сбое на phase A/B/C —
  обратный обход (disconnect → delete) для всего созданного.

### Поддерживаемые типы нод

| `type`         | Параметры                                          | Под капотом              |
|----------------|----------------------------------------------------|--------------------------|
| `Event`        | `event_name` (`ReceiveBeginPlay`, custom-имя)      | `add_event_node`         |
| `FunctionCall` | `target` (класс или `"self"`), `function_name`, `params?` | `add_blueprint_node` (`CallFunction`) |
| `VariableGet`  | `var_name`                                         | `add_blueprint_node` (`VariableGet`)  |
| `VariableSet`  | `var_name`                                         | `add_blueprint_node` (`VariableSet`)  |
| `Branch`       | —                                                  | `add_blueprint_node` (`Branch`)       |
| `ForEachLoop`  | —                                                  | `add_blueprint_node` (`ForEachLoop`)  |
| `DynamicCast`  | `target_class`                                     | `add_blueprint_node` (`DynamicCast`)  |
| `BindEvent`    | `component`, `delegate`, `delegate_class?`         | `add_component_bound_event`           |
| `CustomEvent`  | `event_name`, `inputs?: [{name,type}]`             | `create_custom_event` + `add_custom_event_input` |

Любой `type` вне таблицы — `validation_error.code = unsupported_type`. Caller
переходит на fallback по примитивам.

### Конвенция pin-ссылок

`<local_id>.<pin_name>` — `<pin_name>` это имя пина в UE (обычно совпадает с
`PinName` в C++ метаданных).

Частые имена:
- `then` / `exec` — стандартные exec пины
- `loopbody` / `completed` — выходы `ForEachLoop`
- `success` / `failed` — выходы `DynamicCast`
- `element` / `index` — данные `ForEachLoop`
- `target` — self/target input у `FunctionCall`
- `return` — возврат у `FunctionCall`
- `condition` — `bool` вход у `Branch`

Pin-совместимость **не проверяется** в orchestrator — это делает UE через
`connect_nodes` (он возвращает ошибку, если pin типы не совпадают). Это
осознанный выбор: дублировать reflection на стороне Python дорого и хрупко.

## Алгоритм

```
build_blueprint_graph_impl(unreal, spec):
    # Phase 0: pure-Python валидация
    errors = _validate_spec(spec)
    if errors: return {"phase": "validation", "errors": errors}

    spec_id_to_node_id = dict(spec.existing_nodes)
    created_order = []
    created_connections = []

    # Phase A: create nodes
    for node in spec.nodes:
        r = _create_node(unreal, ..., node)
        if not _is_success(r): rollback + return create-fail
        node_id = _extract_node_id(r)
        spec_id_to_node_id[node.id] = node_id
        created_order.append((node.id, node_id))

    # Phase B: connect pins
    for conn in spec.connections:
        r = unreal.send_command("connect_nodes", resolved-pins)
        if not _is_success(r): rollback + return connect-fail
        created_connections.append(...)

    # Phase C: defaults
    for default in spec.defaults:
        r = unreal.send_command("set_pin_default_value", ...)
        if not _is_success(r): rollback + return defaults-fail

    # Phase D: compile (опционально)
    if spec.compile:
        r = unreal.send_command("compile_blueprint", ...)
        compile_result = "ok" | "failed"

    return success-shape
```

### Rollback

При сбое на phase A/B/C (если `rollback_on_failure=True`):

1. **Phase 1** — для каждой созданной connection (в обратном порядке) вызвать
   `disconnect_pin`. Ошибки идут в `rollback_result.errors`, не прерывают
   процесс.
2. **Phase 2** — для каждой созданной ноды (в обратном порядке) вызвать
   `delete_node`. Ошибки также копятся в `rollback_result.errors`.

Цель — best-effort cleanup, не транзакция. Если UE не смог удалить ноду
(например, она была переименована), запись попадает в `errors`, но процесс
продолжается. Caller получает `rollback_result: {deleted: [...], errors: [...]}`
и видит, что осталось.

## Контракт ответа

### Успех

```json
{
  "success": true,
  "nodes_created": 5,
  "connections_made": 4,
  "defaults_set": 0,
  "compile_result": "ok",
  "compile_errors": [],
  "node_id_map": {"<local_id>": "<unreal_guid>"}
}
```

### Validation error (UE не трогали)

```json
{
  "success": false,
  "phase": "validation",
  "errors": [{"code": "unsupported_type", "node": "n1", "message": "..."}]
}
```

### Build error (atomic rollback)

```json
{
  "success": false,
  "phase": "create" | "connect" | "defaults",
  "fail_at": "<local_id или 'from→to'>",
  "cause": {"success": false, "message": "..."},
  "rolled_back": true,
  "rollback_result": {"deleted": ["n1", "n2"], "errors": []}
}
```

### Compile failure (граф собран, BP не компилируется)

```json
{
  "success": true,
  "nodes_created": 5,
  "connections_made": 4,
  "compile_result": "failed",
  "compile_errors": ["orphan node K2Node_Branch_42 detected"]
}
```

`success: true` потому что **граф построен** (rollback не запускается на
compile-фазе — каунтер `nodes_created/connections_made` правдив). Caller
должен смотреть `compile_result` отдельно. Это явное design-решение:
compile-error не означает «спек кривой», часто это последствие missing
variable / переименованной переменной в C++.

## Что вне scope

- Function/Macro graphs — поддержано через параметр `graph: "<FunctionName>"`,
  но не все типы нод обрабатывают этот контекст одинаково. Тестировано на
  EventGraph.
- ~200 UE5 node-типов — покрыты только 9 базовых. Расширение — отдельные
  задачи (`FEAT-PLUGIN-XXX-add-<node-type>`).
- Pin-type compatibility — делегирована UE через `connect_nodes`.
- Сложные паттерны (custom delegate, hot-reload subscription) — fallback на
  ручной primitive flow в impl_bp.

## Интеграция с pipeline

- **plan_bp** генерирует `event_graph_spec` в plan output для каждой UMG-задачи.
- **impl_bp** при наличии `event_graph_spec` в плане вызывает
  `build_blueprint_graph` **одним** MCP-вызовом вместо 15-30 примитивов.
- **review_bp** — после wiring-проверки сравнивает реальный граф (через
  `analyze_blueprint_graph`) с ожидаемым спеком из плана.

Промпты `plan_bp` / `impl_bp` в `D:/WarCard/.ai-workflow/pipelines/feature.yaml`
обновляются под этот контракт отдельной задачей.

## Тесты

`tests/unit/test_graph_builder.py` — 16 unit тестов на mock'ах:

- `_validate_spec` — empty/missing path/unsupported type/duplicate id/unknown
  ref/bad pin format/valid/existing_nodes.
- Каждый из 9 типов нод диспатчится в правильный примитив (parametrized).
- `CustomEvent` с inputs делает доп. вызовы `add_custom_event_input`.
- Connections строятся после нод; defaults — после connections.
- Rollback при сбое create/connect; `rollback_on_failure=False` запрещает rollback.
- `compile` default true / `compile: false` пропускает; compile failure →
  `success: true, compile_result: "failed"`.
- Integration scenario: MULLIGAN visual highlight chain (5 нод, 4 connection).

Запуск: `uv run pytest tests/unit/test_graph_builder.py -v`.

## Тех. долг

Если в процессе работы выяснится, что какой-то примитив (например, для
конкретного node-type) не существует или ведёт себя иначе — escalate в
новую задачу `FEAT-PLUGIN-XXX-extend-<X>-primitive`. Текущий design
полагается на то, что под капотом у `add_blueprint_node`, `connect_nodes`,
`set_pin_default_value`, `compile_blueprint` стабильные контракты — это уже
существующие примитивы.

См. также: `_envelope_design.md` (envelope контракт), `recipe_framework.py`
(`@recipe` rollback паттерн, на котором вдохновлено rollback графа).
