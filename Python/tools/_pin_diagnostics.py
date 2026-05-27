"""
Python-side pin diagnostics (MCP-PLUGIN-001).

Когда плагин возвращает ошибку резолва пина с details.availablePins[],
эта утилита подбирает ближайший вариант по Levenshtein-расстоянию и
обогащает ошибку полем details.did_you_mean.

Используется обёртками set_pin_default_value, get_pin_info, set_node_property,
connect_blueprint_nodes.
"""

from __future__ import annotations

from typing import Any


def _levenshtein(a: str, b: str) -> int:
    """Чистая реализация — без зависимостей. Достаточно для коротких pin name'ов."""
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)

    prev_row = list(range(len(b) + 1))
    for i, ca in enumerate(a):
        cur_row = [i + 1]
        for j, cb in enumerate(b):
            ins_cost = cur_row[j] + 1
            del_cost = prev_row[j + 1] + 1
            sub_cost = prev_row[j] + (0 if ca == cb else 1)
            cur_row.append(min(ins_cost, del_cost, sub_cost))
        prev_row = cur_row
    return prev_row[-1]


def enrich_pin_error(response: dict[str, Any], query: str, max_distance: int = 2) -> dict[str, Any]:
    """
    Если в ответе плагина есть details.availablePins[] и near-match (Levenshtein <= max_distance)
    к query — добавляет details.did_you_mean.

    Безопасно вызывать на любом ответе: при отсутствии details ничего не делает.

    Args:
        response: ответ от unreal.send_command(...).
        query:    исходный запрос пина (то что пользователь передал).
        max_distance: порог расстояния (по умолчанию 2 — typical typo).

    Returns:
        Тот же response (мутируется in-place + возвращается).
    """
    if not isinstance(response, dict):
        return response

    # Структура details может лежать либо в response.details (raw plugin reply),
    # либо в response.result.details (через envelope), либо в response.error_details.
    details = None
    for key in ("details", "error_details"):
        if isinstance(response.get(key), dict):
            details = response[key]
            break
    if details is None and isinstance(response.get("result"), dict):
        result_details = response["result"].get("details")
        if isinstance(result_details, dict):
            details = result_details

    if not isinstance(details, dict):
        return response

    pins = details.get("availablePins")
    if not isinstance(pins, list) or not pins or not query:
        return response

    q = query.lower()
    best_name: str | None = None
    best_distance = max_distance + 1
    for pin in pins:
        if not isinstance(pin, dict):
            continue
        candidates: list[str] = []
        for field in ("name", "friendlyName"):
            v = pin.get(field)
            if isinstance(v, str) and v:
                candidates.append(v)
        for cand in candidates:
            d = _levenshtein(q, cand.lower())
            if d < best_distance:
                best_distance = d
                best_name = cand

    if best_name is not None and best_distance <= max_distance:
        details["did_you_mean"] = best_name
        details["did_you_mean_distance"] = best_distance

    return response
