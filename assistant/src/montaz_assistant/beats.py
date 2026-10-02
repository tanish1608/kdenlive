# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Conservative periodic-onset estimates; no claim of downbeats or phrase detection."""

import hashlib
import json
import math
import os
import sys
import tempfile
from array import array
from pathlib import Path

from .contracts import FrameRate
from .gemini import AssistantError
from .planning import MediaTools
from .protocol import decode, fields

VERSION = "periodic-onsets-2"
SAMPLE_RATE = 8000
HOP = 80
TICKS_PER_SECOND = SAMPLE_RATE // HOP
MAX_ANCHORS = 2048


def music_hash(path: Path, media: MediaTools) -> str:
    if not path.is_file() or path.stat().st_size > 128 * 1024 * 1024:
        raise AssistantError("invalid_timing_request")
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            media.check()
            digest.update(chunk)
    return digest.hexdigest()


def unavailable(reason: str) -> dict:
    return dict(
        mode="accents", bpm=0, periodicity=0, support=0, reason=reason, beats=[]
    )


def onset_envelope(raw: bytes, media: MediaTools) -> list[float]:
    """Positive log-energy rises in low/mid/high bands at 10 ms resolution."""
    if not raw or len(raw) % 6:
        raise AssistantError("invalid_music_audio")
    samples = array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    if max(abs(v) for v in samples) < 100:
        raise AssistantError("invalid_music_audio")
    previous = [0.0] * 3
    onset = []
    for offset in range(0, len(samples), 3 * HOP):
        if len(onset) % 100 == 0:
            media.check()
        block = samples[offset : offset + 3 * HOP]
        strength = 0.0
        for band, weight in enumerate((1.5, 1.0, 0.75)):
            values = block[band::3]
            energy = math.sqrt(sum(v * v for v in values) / len(values))
            level = math.log1p(max(0, energy - 25))
            strength += weight * max(0, level - previous[band] - 0.04)
            previous[band] = level
        # The first block has no preceding audio to compare against. Treating
        # its whole bed level as an attack can hide every subsequent beat.
        onset.append(strength if offset else 0.0)
    return onset


def estimate(onset: list[float], media: MediaTools) -> dict:
    """Fit a steady 60–180 BPM pulse, then retain only observed supported onsets.

    Periodicity/support are explicit heuristic measurements, not probabilities.
    No beat is synthesized across a missing onset or ambiguous passage.
    """
    media.check()
    if len(onset) < 300 or max(onset, default=0) <= 0:
        return unavailable("insufficient_onsets")
    peak = max(onset)
    values = [v / peak if v >= peak * 0.08 else 0.0 for v in onset]
    # Pick local peaks, retaining strongest evidence within a 120 ms cluster.
    peaks = []
    for i, v in enumerate(values):
        if (
            v < 0.15
            or (i and values[i - 1] > v)
            or (i + 1 < len(values) and values[i + 1] >= v)
        ):
            continue
        if peaks and i - peaks[-1] < 12:
            if v > values[peaks[-1]]:
                peaks[-1] = i
        else:
            peaks.append(i)
    if len(peaks) < 6:
        return unavailable("insufficient_onsets")
    sparse = [0.0] * len(values)
    for p in peaks:
        sparse[p] = values[p]
    correlations = {}
    for lag in range(33, 101):
        media.check()
        aa = bb = ab = 0.0
        for i in range(lag + 2, len(values)):
            a = sparse[i]
            b = sparse[i - lag]
            aa += a * a
            bb += b * b
            if a:
                ab += a * max(sparse[i - lag - 2 : i - lag + 3])
        correlations[lag] = min(1, ab / math.sqrt(aa * bb)) if aa and bb else 0.0
    best_correlation = max(correlations.values())
    # Similar half-tempo aliases must not discard every other equally strong attack.
    lag = min(
        n for n, value in correlations.items() if value >= 0.98 * best_correlation
    )
    periodicity = correlations[lag]
    if periodicity < 0.55:
        return unavailable("weak_periodicity")
    # Recover fractional tempo from observed intervals near the integer lag.
    intervals = []
    for i, p in enumerate(peaks):
        for q in peaks[i + 1 : i + 10]:
            if q - p > lag + 3:
                break
            if abs(q - p - lag) <= 3:
                intervals.append(q - p)
    if len(intervals) < 4:
        return unavailable("irregular_onsets")
    period = sum(intervals) / len(intervals)
    bpm = 6000 / period
    if not 59 <= bpm <= 181:
        return unavailable("irregular_onsets")
    bpm = min(180, max(60, bpm))
    tolerance = max(2, period * 0.1)
    best = None
    # Bounded phase search; no dependence on source filenames or provider output.
    seeds = sorted(peaks, key=lambda p: (-values[p], p))[:32]
    for origin in seeds:
        media.check()
        cells = {}
        for p in peaks:
            cell = round((p - origin) / period)
            distance = abs(p - (origin + cell * period))
            if distance > tolerance:
                continue
            score = values[p] * (1 - 0.5 * distance / tolerance)
            if cell not in cells or score > cells[cell][0]:
                cells[cell] = (score, p)
        selected = sorted(v[1] for v in cells.values())
        if len(selected) < 6:
            continue
        expected = max(1, round((peaks[-1] - peaks[0]) / period) + 1)
        support = min(1, len(selected) / expected)
        score = (sum(v[0] for v in cells.values()), support, -origin)
        if best is None or score > best[0]:
            best = (score, selected, support)
    if (
        best is None
        or best[2] < 0.7
        or sum(values[p] for p in best[1]) < 0.6 * sum(values[p] for p in peaks)
    ):
        return unavailable("unstable_tempo")
    beats = best[1]
    intervals = [
        (b - a) / max(1, round((b - a) / period)) for a, b in zip(beats, beats[1:])
    ]
    stable = sum(abs(v - period) <= 0.08 * period for v in intervals) / len(intervals)
    if stable < 0.85:
        return unavailable("unstable_tempo")
    return dict(
        mode="beats",
        bpm=round(bpm, 2),
        periodicity=round(periodicity, 4),
        support=round(best[2], 4),
        reason="none",
        beats=beats,
    )


def analyze(
    path: Path,
    rate: FrameRate,
    duration: int,
    sha256: str,
    cache: Path,
    media: MediaTools,
) -> dict:
    """Decode once, reuse complete content/FPS/duration/version-bound rhythm evidence."""
    identity = [VERSION, sha256, rate.numerator, rate.denominator, duration]
    cache.mkdir(parents=True, exist_ok=True)
    entry = cache / (
        hashlib.sha256(json.dumps(identity).encode()).hexdigest() + ".json"
    )
    try:
        result = decode(entry.read_bytes())
        fields(result, {"mode", "bpm", "periodicity", "support", "reason", "beats"})
        validate(result, duration)
        media.check()
        return dict(result, cached=True, version=VERSION)
    except (OSError, ValueError):
        pass
    seconds = duration * rate.denominator / rate.numerator
    raw = media._output(
        [
            os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
            "-v",
            "error",
            "-nostdin",
            "-threads",
            "1",
            "-i",
            str(path),
            "-t",
            f"{seconds:.9f}",
            "-filter_complex",
            "[0:a:0]aformat=channel_layouts=mono,aresample=8000,asplit=3[a][b][c];"
            "[a]lowpass=f=200[l];[b]highpass=f=200,lowpass=f=1600[m];"
            "[c]highpass=f=1600[h];[l][m][h]join=inputs=3:channel_layout=3.0:map=0.0-FL|1.0-FR|2.0-FC[out]",
            "-map",
            "[out]",
            "-f",
            "s16le",
            "pipe:1",
        ],
        maximum_bytes=30 * 1024 * 1024,
    )
    tail = math.ceil(SAMPLE_RATE * rate.denominator / rate.numerator) + HOP
    if len(raw) < (int(seconds * SAMPLE_RATE) - tail) * 6:
        raise AssistantError("invalid_music_audio")
    result = estimate(onset_envelope(raw, media), media)
    result["beats"] = sorted(
        {
            rate.seconds_to_frame(f"{tick // 100}.{tick % 100:02d}")
            for tick in result["beats"]
            if 0 < rate.seconds_to_frame(f"{tick // 100}.{tick % 100:02d}") < duration
        }
    )
    validate(result, duration)
    # Never publish evidence under an old content key after the source changes.
    if music_hash(path, media) != sha256:
        raise AssistantError("media_changed")
    media.check()
    with tempfile.NamedTemporaryFile(mode="w", dir=cache, delete=False) as temp:
        json.dump(result, temp)
    Path(temp.name).replace(entry)
    return dict(result, cached=False, version=VERSION)


def validate(result: dict, duration: int) -> None:
    if (
        type(result["mode"]) is not str
        or result["mode"] not in {"beats", "accents"}
        or type(result["reason"]) is not str
        or any(
            type(result[k]) not in (int, float) or not math.isfinite(result[k])
            for k in ("bpm", "periodicity", "support")
        )
        or not 0 <= result["periodicity"] <= 1
        or not 0 <= result["support"] <= 1
        or type(result["beats"]) is not list
        or len(result["beats"]) > MAX_ANCHORS
        or any(type(x) is not int or not 0 < x < duration for x in result["beats"])
        or result["beats"] != sorted(set(result["beats"]))
    ):
        raise AssistantError("invalid_timing_request")
    if result["mode"] == "beats":
        if (
            not 60 <= result["bpm"] <= 180
            or result["periodicity"] < 0.55
            or result["support"] < 0.7
            or len(result["beats"]) < 5
            or result["reason"] != "none"
        ):
            raise AssistantError("invalid_timing_request")
    elif (
        result["beats"]
        or result["bpm"]
        or result["periodicity"]
        or result["support"]
        or result["reason"]
        not in {
            "insufficient_onsets",
            "weak_periodicity",
            "irregular_onsets",
            "unstable_tempo",
        }
    ):
        raise AssistantError("invalid_timing_request")
