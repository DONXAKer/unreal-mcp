"""Numeric metrics of a rendered frame (TOOL-SHOT-001).

Why this exists: pixel-diff against a previous baseline catches REGRESSION but
never answers "is the picture good?". These metrics turn the look of a frame
into numbers that can be compared against a target corridor derived from the
art direction reference (Broken Arrow / modern military RTS).

Implemented on PIL histograms only - no numpy in this environment.

All output is ASCII (Windows console is cp1251 and dies on box-drawing/arrows).
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

from PIL import Image

# Pixels at or above this luma count as blown highlights.
OVEREXPOSED_THRESHOLD = 250
# Pixels at or below this luma count as crushed blacks.
CRUSHED_THRESHOLD = 5


def _histogram_stats(hist: list[int]) -> tuple[float, float]:
    """Mean and standard deviation of a 256-bin histogram, in 0..255 units."""
    total = sum(hist)
    if total == 0:
        return 0.0, 0.0
    mean = sum(i * n for i, n in enumerate(hist)) / total
    variance = sum(((i - mean) ** 2) * n for i, n in enumerate(hist)) / total
    return mean, math.sqrt(variance)


def compute_metrics(image_path: str | Path) -> dict[str, Any]:
    """Compute look metrics for a single frame.

    Returns a dict with values normalized to 0..1 where that is meaningful:

      brightness        mean luma (0..1)
      contrast          RMS contrast, i.e. luma stddev (0..1)
      saturation        mean HSV saturation (0..1)
      overexposed       share of pixels with luma >= 250 (0..1)
      crushed_blacks    share of pixels with luma <= 5 (0..1)
      warm_ratio        mean red / mean blue; >1 is a warm grade, <1 is cool
      width, height     pixel dimensions

    A frame that is mostly one flat color (a black screen, a failed capture)
    shows up as contrast near zero - worth checking before trusting anything
    else in the report.
    """
    path = Path(image_path)
    with Image.open(path) as img:
        rgb = img.convert("RGB")
        width, height = rgb.size

        luma = rgb.convert("L")
        luma_hist = luma.histogram()
        total = width * height

        mean_luma, std_luma = _histogram_stats(luma_hist)

        overexposed = sum(luma_hist[OVEREXPOSED_THRESHOLD:]) / total if total else 0.0
        crushed = sum(luma_hist[: CRUSHED_THRESHOLD + 1]) / total if total else 0.0

        # HSV saturation channel: histogram slice 256..511 of the HSV image.
        hsv_hist = rgb.convert("HSV").histogram()
        sat_hist = hsv_hist[256:512]
        mean_sat, _ = _histogram_stats(sat_hist)

        # Per-channel means for a crude color-temperature read.
        rgb_hist = rgb.histogram()
        mean_r, _ = _histogram_stats(rgb_hist[0:256])
        mean_b, _ = _histogram_stats(rgb_hist[512:768])

    return {
        "brightness": round(mean_luma / 255.0, 4),
        "contrast": round(std_luma / 255.0, 4),
        "saturation": round(mean_sat / 255.0, 4),
        "overexposed": round(overexposed, 5),
        "crushed_blacks": round(crushed, 5),
        "warm_ratio": round(mean_r / mean_b, 4) if mean_b > 0.001 else 0.0,
        "width": width,
        "height": height,
    }


def check_targets(metrics: dict[str, Any], targets: dict[str, list[float]]) -> dict[str, Any]:
    """Compare metrics against {name: [min, max]} corridors.

    Returns {"pass": bool, "violations": [{metric, value, min, max}]}.
    Metrics without a declared corridor are ignored - the corridor set is
    deliberately small, since only a few numbers actually describe a look.
    """
    violations = []
    for name, bounds in (targets or {}).items():
        if name not in metrics:
            continue
        if not isinstance(bounds, (list, tuple)) or len(bounds) != 2:
            continue
        value = metrics[name]
        low, high = float(bounds[0]), float(bounds[1])
        if value < low or value > high:
            violations.append(
                {"metric": name, "value": value, "min": low, "max": high}
            )
    return {"pass": len(violations) == 0, "violations": violations}


def format_report(name: str, metrics: dict[str, Any], check: dict[str, Any]) -> str:
    """One-line-per-frame ASCII summary for console output."""
    status = "PASS" if check.get("pass") else "FAIL"
    parts = [
        f"bright={metrics['brightness']:.3f}",
        f"contrast={metrics['contrast']:.3f}",
        f"sat={metrics['saturation']:.3f}",
        f"blown={metrics['overexposed']:.4f}",
        f"black={metrics['crushed_blacks']:.4f}",
        f"warm={metrics['warm_ratio']:.2f}",
    ]
    line = f"[{status}] {name}: " + " ".join(parts)
    for v in check.get("violations", []):
        line += f"\n    out of range: {v['metric']}={v['value']} target [{v['min']}, {v['max']}]"
    return line
