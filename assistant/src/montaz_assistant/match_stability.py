# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Deterministic size/position models for tracked match-cut subjects.

CSRT reports a constant subject at a few quantized box sizes that persist for
several frames, so a short moving average passes them through as breathing
zoom. These pure functions fit smooth trends instead and return None when the
trend does not explain the measurements, leaving the caller's baseline in place.
"""

import math
import statistics

# Fewer frames cannot separate a trend from tracker quantization.
MIN_FIT_FRAMES = 8
# Log-height residuals. On the controlled constant disc, CSRT's quantized sizes
# give RMS 0.036, peak 0.055 and nine-frame mean ("drift") 0.035. A 20% step or a
# 12% approach-and-retreat exceeds the drift limit and keeps the baseline sizes.
SIZE_RMS, SIZE_MAX, SIZE_DRIFT = 0.05, 0.10, 0.045
# Center residuals as fractions of the subject height.
CENTER_RMS, CENTER_MAX = 0.05, 0.12
CENTER_RADIUS = 4


def triangle(values: list[float], radius: int) -> list[float]:
    """Weights radius+1-|k| over +-radius frames, renormalized at the edges."""
    result = []
    for i in range(len(values)):
        near = range(max(0, i - radius), min(len(values), i + radius + 1))
        weights = [radius + 1 - abs(j - i) for j in near]
        result.append(sum(values[j] * w for j, w in zip(near, weights)) / sum(weights))
    return result


def theil_sen(values: list[float]) -> float:
    """Median pairwise slope; robust to isolated outliers."""
    return statistics.median(
        (values[j] - values[i]) / (j - i)
        for i in range(len(values))
        for j in range(i + 1, len(values))
    )


def _residuals_fit(residuals: list[float], rms: float, peak: float) -> bool:
    return (
        math.sqrt(sum(r * r for r in residuals) / len(residuals)) <= rms
        and max(abs(r) for r in residuals) <= peak
    )


def fit_heights(heights: list[float]) -> list[float] | None:
    """Exponential size trend: constant subjects stay constant, gradual zooms keep
    their direction. Structured residuals (steps, approach then retreat) return None.

    The slope is robust (Theil-Sen); the level is the mean log size given that
    slope, because a median level is biased by which quantized size dominates.
    A trend whose total change is below the residual RMS cannot be told apart
    from tracker jitter, so the size is held constant instead of inventing a zoom.
    """
    if len(heights) < MIN_FIT_FRAMES:
        return None
    logs = [math.log(h) for h in heights]

    def fit(slope: float) -> tuple[list[float], list[float]]:
        intercept = sum(v - slope * i for i, v in enumerate(logs)) / len(logs)
        fitted = [intercept + slope * i for i in range(len(logs))]
        return fitted, [v - f for v, f in zip(logs, fitted)]

    slope = theil_sen(logs)
    fitted, residuals = fit(slope)
    if abs(slope) * (len(logs) - 1) <= _rms(residuals):
        fitted, residuals = fit(0.0)
    if (
        not _residuals_fit(residuals, SIZE_RMS, SIZE_MAX)
        or max(abs(r) for r in triangle(residuals, 4)) > SIZE_DRIFT
    ):
        return None
    return [math.exp(f) for f in fitted]


def local_linear(values: list[float], radius: int) -> list[float]:
    """Least-squares line over +-radius frames evaluated at each frame. Constant
    velocity passes exactly; the window shortens at the ends, never extrapolates
    beyond measured frames."""
    result = []
    for i in range(len(values)):
        near = list(range(max(0, i - radius), min(len(values), i + radius + 1)))
        mean_t = sum(near) / len(near)
        mean_v = sum(values[j] for j in near) / len(near)
        spread = sum((j - mean_t) ** 2 for j in near)
        slope = sum((j - mean_t) * (values[j] - mean_v) for j in near) / spread
        result.append(mean_v + slope * (i - mean_t))
    return result


def fit_centers(
    centers: list[list[float]], heights: list[float]
) -> list[list[float]] | None:
    """Local-linear centers, or None if they move the subject by more than a small
    fraction of its height (a real stop, turn or jump stays measured)."""
    if len(centers) < MIN_FIT_FRAMES:
        return None
    axes = [local_linear([c[a] for c in centers], CENTER_RADIUS) for a in (0, 1)]
    fitted = [[x, y] for x, y in zip(*axes)]
    residuals = [
        math.hypot(c[0] - f[0], c[1] - f[1]) / h
        for c, f, h in zip(centers, fitted, heights)
    ]
    if not _residuals_fit(residuals, CENTER_RMS, CENTER_MAX):
        return None
    return fitted


def _rms(values: list[float]) -> float:
    return math.sqrt(sum(v * v for v in values) / len(values)) if values else 0.0


def stability(transforms: list[list[dict]]) -> dict:
    """Per-frame reframing components within each clip, never across the cut.

    translation: RMS movement of the source frame's center on the canvas.
    scale_change: RMS frame-to-frame change of the uniform scale.
    scale_acceleration: RMS second difference of the scale.
    """
    moves, changes, accelerations = [], [], []
    for values in transforms:
        for a, b in zip(values, values[1:]):
            ds = b["scale"] - a["scale"]
            moves.append(math.hypot(b["x"] - a["x"] + ds / 2, b["y"] - a["y"] + ds / 2))
            changes.append(ds)
        for a, b, c in zip(values, values[1:], values[2:]):
            accelerations.append(c["scale"] - 2 * b["scale"] + a["scale"])
    return dict(
        translation=_rms(moves),
        scale_change=_rms(changes),
        scale_acceleration=_rms(accelerations),
    )
