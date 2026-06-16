"""TEST-VISUAL-001: мини-тест детектора пустого/чёрного кадра в _screenshot_diff.

Запуск:
    uv run pytest tests/test_blank_frame.py -q
    либо standalone:  uv run python tests/test_blank_frame.py

Требует Pillow (детектор без него выключен) — при отсутствии тест пропускается.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(__file__.rsplit("\\tests", 1)[0]))

from tests._screenshot_diff import is_blank_frame

try:
    from PIL import Image  # type: ignore
    _HAS_PIL = True
except ImportError:
    _HAS_PIL = False


def _make_png(tmp: Path, name: str, filler) -> Path:
    """filler(img) красит изображение; вернуть путь к сохранённому PNG."""
    img = Image.new("RGB", (96, 64), (0, 0, 0))
    filler(img)
    p = tmp / name
    img.save(p)
    return p


def _gradient(img: Image.Image) -> None:
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            px[x, y] = (int(255 * x / w), int(255 * y / h), 128)


def test_black_frame_is_blank(tmp_path: Path) -> None:
    if not _HAS_PIL:
        return  # детектор выключен без Pillow — нечего проверять
    black = _make_png(tmp_path, "black.png", lambda im: None)  # всё (0,0,0)
    assert is_blank_frame(black) is True


def test_flat_color_frame_is_blank(tmp_path: Path) -> None:
    if not _HAS_PIL:
        return
    flat = _make_png(tmp_path, "flat.png", lambda im: im.paste((40, 40, 40), (0, 0, *im.size)))
    assert is_blank_frame(flat) is True


def test_gradient_frame_is_not_blank(tmp_path: Path) -> None:
    if not _HAS_PIL:
        return
    grad = _make_png(tmp_path, "grad.png", _gradient)
    assert is_blank_frame(grad) is False


if __name__ == "__main__":
    if not _HAS_PIL:
        print("Pillow не установлен — детектор выключен, тест пропущен")
        sys.exit(0)
    import tempfile
    tmp = Path(tempfile.mkdtemp())
    black = _make_png(tmp, "black.png", lambda im: None)
    flat = _make_png(tmp, "flat.png", lambda im: im.paste((40, 40, 40), (0, 0, *im.size)))
    grad = _make_png(tmp, "grad.png", _gradient)
    r_black = is_blank_frame(black)
    r_flat = is_blank_frame(flat)
    r_grad = is_blank_frame(grad)
    print(f"black -> blank={r_black} (ожидание True)")
    print(f"flat  -> blank={r_flat} (ожидание True)")
    print(f"grad  -> blank={r_grad} (ожидание False)")
    ok = (r_black is True) and (r_flat is True) and (r_grad is False)
    print("РЕЗУЛЬТАТ:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
