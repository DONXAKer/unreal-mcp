"""Test fixtures для WarCard e2e тестов.

Helpers для создания/получения тестовых пользователей через WarCard REST API.

Использует существующие endpoints:
    POST /api/auth/register  — создать юзера (идемпотентно: при дубле игнорим 400)
    POST /api/auth/login     — получить JWT для существующего юзера

Не делает отдельного "test-only" endpoint, потому что register/login и так
открыты. Если в будущем понадобится изолированное создание (с фикс. ID,
сбросом state) — добавить POST /api/test/users в server, активный только
при profile=test, и заменить здесь.

Env переменные:
    WC_SERVER_URL       (default http://127.0.0.1:8080)
    WC_TEST_LOGIN       (default warcard_test)
    WC_TEST_PASSWORD    (default Test1234)
    WC_TEST_EMAIL       (default warcard_test@example.com)
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Any

DEFAULT_SERVER_URL = "http://127.0.0.1:8081"
DEFAULT_LOGIN = "warcard_test"
DEFAULT_PASSWORD = "Test1234"
DEFAULT_EMAIL = "warcard_test@example.com"

REQUEST_TIMEOUT_S = 5.0


class FixtureError(RuntimeError):
    """Сервер недоступен / API вернуло неожиданный ответ."""


def server_url() -> str:
    return os.environ.get("WC_SERVER_URL", DEFAULT_SERVER_URL).rstrip("/")


def test_credentials() -> tuple[str, str, str]:
    """Return (login, password, email) — из ENV или дефолтов."""
    return (
        os.environ.get("WC_TEST_LOGIN", DEFAULT_LOGIN),
        os.environ.get("WC_TEST_PASSWORD", DEFAULT_PASSWORD),
        os.environ.get("WC_TEST_EMAIL", DEFAULT_EMAIL),
    )


def _post_json(
    path: str,
    body: dict[str, Any],
    token: str | None = None,
) -> tuple[int, dict[str, Any]]:
    """POST JSON, return (status_code, parsed_body). Не бросает на 4xx.

    token -- bearer-токен, если маршрут требует аутентификации.
    """
    url = f"{server_url()}{path}"
    data = json.dumps(body).encode("utf-8")
    headers = {"Content-Type": "application/json", "Accept": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, data=data, method="POST", headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT_S) as resp:
            raw = resp.read().decode("utf-8")
            return resp.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = {"message": raw}
        return exc.code, parsed
    except urllib.error.URLError as exc:
        raise FixtureError(f"Сервер недоступен на {url}: {exc.reason}") from exc


def ensure_test_user(
    login: str | None = None,
    password: str | None = None,
    email: str | None = None,
) -> dict[str, Any]:
    """Гарантировать наличие test user. Идемпотентно — register или uses existing.

    Returns:
        { "created": bool, "login": str }
    """
    login = login or os.environ.get("WC_TEST_LOGIN", DEFAULT_LOGIN)
    password = password or os.environ.get("WC_TEST_PASSWORD", DEFAULT_PASSWORD)
    email = email or os.environ.get("WC_TEST_EMAIL", DEFAULT_EMAIL)

    status, body = _post_json("/api/auth/register", {
        "username": login,
        "email": email,
        "password": password,
    })
    if status == 200:
        _dev_set_level(login, 6)
        return {"created": True, "login": login}
    if status == 400:
        # Скорее всего юзер уже есть (registration failed: username taken).
        # Принимаем как success — login дальше подтвердит работоспособность.
        _dev_set_level(login, 6)
        return {"created": False, "login": login, "register_response": body}
    raise FixtureError(f"Неожиданный статус регистрации {status}: {body}")


def _login_token(login: str, password: str | None = None) -> str:
    """Плоский логин без ensure_test_user — иначе _dev_set_level зациклится.

    ensure_test_user сам зовёт _dev_set_level, поэтому здесь используется только
    прямой /api/auth/login.
    """
    password = password or os.environ.get("WC_TEST_PASSWORD", DEFAULT_PASSWORD)
    status, body = _post_json("/api/auth/login", {
        "usernameOrEmail": login,
        "password": password,
    })
    if status != 200:
        raise FixtureError(f"Login вернул {status}: {body}")
    token = body.get("token") or body.get("accessToken")
    if not token:
        raise FixtureError(f"Login OK, но JWT не найден в ответе: {body}")
    return token


def _dev_set_level(login: str, level: int, password: str | None = None) -> None:
    """PROG-XP-001: тестовые аккаунты получают уровень 6 — полный ростер юнитов
    (гейт прогрессии в драфте не должен ломать smoke/e2e-тесты).

    SEC-DEV-001: /api/dev/** больше НЕ анонимный — маршрут ушёл из permitAll, потому
    что dev это профиль по умолчанию, и на любом стенде без SPRING_PROFILES_ACTIVE
    кто угодно мог выставить произвольный уровень любому игроку. Поэтому здесь нужен
    токен. Отказ больше не глушится молча: без уровня 6 гейт прогрессии урежет ростер
    в драфте, и тест упадёт далеко от настоящей причины.

    Best-effort остаётся только по НАЛИЧИЮ эндпоинта: при warcard.dev-tools.enabled=false
    бина нет вовсе (404) — это законная конфигурация, а не сбой.
    """
    try:
        token = _login_token(login, password)
    except Exception as exc:  # noqa: BLE001 — до логина юзера может не быть
        print(f"[fixtures] WARN: не удалось получить токен для set-level ({login}): {exc}")
        return

    status, body = _post_json(
        "/api/dev/player/set-level",
        {"username": login, "level": level},
        token=token,
    )
    if status == 200:
        return
    if status == 404:
        print("[fixtures] INFO: /api/dev/** недоступен (dev-tools выключены) — уровень не выставлен")
        return
    print(f"[fixtures] WARN: set-level вернул {status}: {body}. "
          f"Аккаунт {login} останется с текущим уровнем, драфт может урезать ростер.")


def get_test_jwt(
    login: str | None = None,
    password: str | None = None,
) -> str:
    """Залогинить test user, вернуть JWT.

    Если юзера ещё нет — создаст через ensure_test_user.
    """
    login = login or os.environ.get("WC_TEST_LOGIN", DEFAULT_LOGIN)
    password = password or os.environ.get("WC_TEST_PASSWORD", DEFAULT_PASSWORD)

    status, body = _post_json("/api/auth/login", {
        "usernameOrEmail": login,
        "password": password,
    })
    if status == 200:
        token = body.get("token") or body.get("accessToken")
        if not token:
            raise FixtureError(f"Login OK но JWT не найден в ответе: {body}")
        return token
    if status == 400:
        # Возможно юзер не существует — создаём и пробуем снова.
        ensure_test_user(login, password)
        status2, body2 = _post_json("/api/auth/login", {
            "usernameOrEmail": login,
            "password": password,
        })
        if status2 == 200:
            return body2.get("token") or body2.get("accessToken") or ""
        raise FixtureError(f"Login после регистрации не прошёл: {status2}: {body2}")
    raise FixtureError(f"Login вернул {status}: {body}")


def is_server_alive() -> bool:
    """Health-check сервера. Возвращает True если /actuator/health отвечает 200."""
    try:
        url = f"{server_url()}/actuator/health"
        with urllib.request.urlopen(url, timeout=REQUEST_TIMEOUT_S) as resp:
            return resp.status == 200
    except (urllib.error.URLError, TimeoutError):
        return False


if __name__ == "__main__":
    import sys
    if not is_server_alive():
        print(f"FAIL: сервер не отвечает на {server_url()}/actuator/health", file=sys.stderr)
        sys.exit(1)
    login, _, _ = test_credentials()
    result = ensure_test_user()
    print(f"ensure_test_user: {result}")
    jwt = get_test_jwt()
    print(f"JWT (first 40 chars): {jwt[:40]}...")
