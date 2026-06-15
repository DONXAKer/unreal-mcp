"""Screenshot regression: baseline + diff утилита для PIE-тестов.

Использование:
    from tests._screenshot_diff import assert_screenshot_matches, save_baseline

    pie_send("pie_screenshot", {"filename": "login_screen.png"})
    assert_screenshot_matches("login_screen.png", threshold=0.95)

Baseline-файлы хранятся в `tests/fixtures/baseline/`. Чтобы (пере)записать
baseline для конкретного скриншота — вызвать `save_baseline(filename)` один
раз (или установить env WC_UPDATE_BASELINES=1 при прогоне теста).

Diff алгоритм:
    1. Pillow есть    → pixel-by-pixel сравнение, similarity = ratio matching
       пикселей. Параметр `threshold` — минимальная допустимая доля совпавших.
    2. Pillow нет     → fallback на побайтовое сравнение (точная match).
       Менее устойчиво к anti-aliasing/timing-jitter, но работает без
       зависимостей.
"""

from __future__ import annotations

import hashlib
import os
import shutil
from pathlib import Path

# Skip Pillow если не установлен.
try:
    from PIL import Image  # type: ignore
    _HAS_PIL = True
except ImportError:
    _HAS_PIL = False

# Куда UE сохраняет PIE-скриншоты.
SCREENSHOTS_DIR = Path("D:/WarCard/client/Saved/Screenshots")

# Где лежат baseline'ы.
BASELINE_DIR = Path(__file__).resolve().parent / "fixtures" / "baseline"

# Куда сложить diff-картинки при провале (только если Pillow есть).
DIFF_DIR = Path(__file__).resolve().parent / "fixtures" / "diff"


class ScreenshotMismatch(AssertionError):
    """Реальный скриншот не совпал с baseline в пределах threshold."""


class BaselineMissing(FileNotFoundError):
    """Нет baseline для этого filename. Создай через save_baseline() или WC_UPDATE_BASELINES=1."""


def _resolve_actual(filename: str) -> Path:
    """Найти actual screenshot. UE кладёт в Saved/Screenshots/<flat> или
    Saved/Screenshots/<platform>/<name>.png — проверяем оба варианта."""
    p1 = SCREENSHOTS_DIR / filename
    if p1.is_file():
        return p1
    # platform subdir (HighRes / WindowsEditor / etc)
    for sub in SCREENSHOTS_DIR.glob(f"*/{filename}"):
        return sub
    raise FileNotFoundError(f"Скриншот не найден: {p1} (и нет в subdir'ах)")


def save_baseline(filename: str) -> Path:
    """Скопировать текущий actual screenshot в baseline под тем же именем."""
    actual = _resolve_actual(filename)
    BASELINE_DIR.mkdir(parents=True, exist_ok=True)
    target = BASELINE_DIR / filename
    shutil.copyfile(actual, target)
    print(f"baseline saved: {target}")
    return target


def _file_hash(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def _compare_pillow(actual_path: Path, baseline_path: Path) -> tuple[float, str]:
    """Pixel-by-pixel similarity, return (ratio_matched, diff_image_path)."""
    a = Image.open(actual_path).convert("RGB")
    b = Image.open(baseline_path).convert("RGB")

    if a.size != b.size:
        # Resize actual to baseline для честного сравнения.
        a = a.resize(b.size)

    width, height = a.size
    pixels_a = a.load()
    pixels_b = b.load()

    matched = 0
    total = width * height
    diff_image = Image.new("RGB", b.size, (0, 0, 0))
    diff_pixels = diff_image.load()

    for y in range(height):
        for x in range(width):
            pa = pixels_a[x, y]
            pb = pixels_b[x, y]
            # Допуск ±5 на канал — компенсация antialiasing'а.
            if all(abs(pa[c] - pb[c]) <= 5 for c in range(3)):
                matched += 1
                diff_pixels[x, y] = (0, 0, 0)  # совпало — чёрный
            else:
                # Не совпало — красный с интенсивностью по дельте.
                d = sum(abs(pa[c] - pb[c]) for c in range(3)) // 3
                diff_pixels[x, y] = (min(255, d * 2), 0, 0)

    DIFF_DIR.mkdir(parents=True, exist_ok=True)
    diff_path = DIFF_DIR / actual_path.name
    diff_image.save(diff_path)
    return matched / total, str(diff_path)


def assert_screenshot_matches(filename: str, threshold: float = 0.95) -> None:
    """Сверить actual screenshot с baseline. Raise если расхождение > (1-threshold).

    Env override:
        WC_UPDATE_BASELINES=1 — вместо проверки переписать baseline и пройти тест.

    Args:
        filename: имя файла как в pie_screenshot(filename=).
        threshold: минимальная доля совпавших пикселей (0..1). 0.95 = допускаем
                   до 5% расхождения. Для стабильных сцен можно 0.99.
    """
    actual = _resolve_actual(filename)
    baseline = BASELINE_DIR / filename

    if os.environ.get("WC_UPDATE_BASELINES") == "1":
        save_baseline(filename)
        print("WC_UPDATE_BASELINES=1 — baseline переписан, проверка пропущена")
        return

    if not baseline.is_file():
        raise BaselineMissing(
            f"Нет baseline для {filename}. "
            f"Один раз запусти с WC_UPDATE_BASELINES=1 либо вызови save_baseline('{filename}')."
        )

    if not _HAS_PIL:
        # Fallback: точное побайтовое сравнение.
        if _file_hash(actual) == _file_hash(baseline):
            return
        raise ScreenshotMismatch(
            f"{filename}: hash mismatch (Pillow не установлен, точное сравнение)\n"
            f"  actual:   {actual}\n  baseline: {baseline}\n"
            f"  Поставь Pillow для tolerant pixel-diff: pip install Pillow"
        )

    similarity, diff_path = _compare_pillow(actual, baseline)
    if similarity < threshold:
        raise ScreenshotMismatch(
            f"{filename}: similarity {similarity:.4f} < threshold {threshold:.4f}\n"
            f"  actual:   {actual}\n  baseline: {baseline}\n  diff:     {diff_path}"
        )
    print(f"    screenshot OK: similarity={similarity:.4f} >= {threshold}")


if __name__ == "__main__":
    import sys
    print(f"Pillow: {'installed' if _HAS_PIL else 'NOT installed (fallback to hash)'}")
    print(f"Baselines: {BASELINE_DIR}")
    print(f"Screenshots: {SCREENSHOTS_DIR}")
    if len(sys.argv) > 1:
        for fn in sys.argv[1:]:
            try:
                assert_screenshot_matches(fn)
                print(f"{fn}: PASS")
            except (BaselineMissing, ScreenshotMismatch, FileNotFoundError) as e:
                print(f"{fn}: FAIL - {e}")
