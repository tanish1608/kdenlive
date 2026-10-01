# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded object-match proposals. Kdenlive/MLT owns decoding, tracking and effects."""

import hashlib
import json
import math
import os
import tempfile
from pathlib import Path
from typing import Callable

from .contracts import FrameRate, Snapshot
from .gemini import AssistantError, Gemini
from .planning import MediaTools
from .protocol import decode, fields, frame

VERSION = "object-match-2"
CLIP_FIELDS = {
    "instance_id",
    "bin_id",
    "track_id",
    "start_frame",
    "in_frame",
    "duration_frames",
}


def match_spec(snapshot: Snapshot, raw: dict) -> dict:
    spec = fields(raw, {"clips", "width", "height"})
    width, height = spec["width"], spec["height"]
    if (
        type(width) is not int
        or type(height) is not int
        or not 32 <= width <= 8192
        or not 32 <= height <= 8192
        or not 0.25 <= width / height <= 4
        or not 1
        <= snapshot.frame_rate.numerator / snapshot.frame_rate.denominator
        <= 60
        or type(spec["clips"]) is not list
        or len(spec["clips"]) != 2
    ):
        raise AssistantError("invalid_match_request")
    occupied = {o.instance_id: o for o in snapshot.occupied}
    assets = {a.bin_id: a for a in snapshot.assets}
    tracks = {t.track_id: t for t in snapshot.tracks}
    previous = None
    for c in spec["clips"]:
        fields(c, CLIP_FIELDS)
        if any(type(c[k]) is not str for k in ("instance_id", "bin_id", "track_id")):
            raise AssistantError("invalid_match_request")
        o, a, t = (
            occupied.get(c["instance_id"]),
            assets.get(c["bin_id"]),
            tracks.get(c["track_id"]),
        )
        start, length, source = (
            frame(c["start_frame"]),
            frame(c["duration_frames"], 2),
            frame(c["in_frame"]),
        )
        if (
            not o
            or not a
            or not a.has_video
            or not t
            or t.kind != "video"
            or t.locked
            or (o.track_id, o.start_frame, o.duration_frames)
            != (c["track_id"], start, length)
            or source + length > a.duration_frames
            or (
                previous
                and (
                    previous["track_id"] != c["track_id"]
                    or previous["start_frame"] + previous["duration_frames"] != start
                )
            )
        ):
            raise AssistantError("invalid_match_request")
        previous = c
    return spec


def window(clip: dict, index: int, rate: FrameRate) -> tuple[int, int]:
    length = min(clip["duration_frames"], max(2, rate.seconds_to_frame("1")))
    offset = clip["duration_frames"] - length if index == 0 else 0
    return offset, length


def box(raw: object) -> list[float]:
    if (
        type(raw) is not list
        or len(raw) != 4
        or any(type(x) not in (int, float) or not math.isfinite(x) for x in raw)
    ):
        raise AssistantError("invalid_match_evidence")
    x, y, w, h = raw
    if x < 0 or y < 0 or w < 0.04 or h < 0.04 or x + w > 1.000001 or y + h > 1.000001:
        raise AssistantError("invalid_match_evidence")
    return raw


def align(tracks: list[list[list[float]]]) -> tuple[dict, list[list[dict]]]:
    """Keep subject height/center constant with aspect-preserving crops, capped at 2x."""
    for boxes in tracks:
        for b in boxes:
            box(b)
    # CSRT's integer boxes fluctuate by a few pixels even on stable subjects.
    # Weighted size over five frames avoids breathing zoom; preserve measured centers.
    smoothed = []
    for boxes in tracks:
        values = []
        for i, b in enumerate(boxes):
            if i and (
                abs(b[0] - boxes[i - 1][0]) > 0.12
                or abs(b[1] - boxes[i - 1][1]) > 0.12
                or max(b[3], boxes[i - 1][3]) / min(b[3], boxes[i - 1][3]) > 1.25
            ):
                raise AssistantError("tracking_unavailable")
            neighbors = [
                (boxes[j], 3 - abs(j - i))
                for j in range(max(0, i - 2), min(len(boxes), i + 3))
            ]
            weight = sum(w for _, w in neighbors)
            width = sum(v[2] * w for v, w in neighbors) / weight
            height = sum(v[3] * w for v, w in neighbors) / weight
            values.append(
                [b[0] + (b[2] - width) / 2, b[1] + (b[3] - height) / 2, width, height]
            )
        smoothed.append(values)
    tracks = smoothed
    ratios = [b[2] / b[3] for boxes in tracks for b in boxes]
    if max(ratios) / min(ratios) > 1.3:
        raise AssistantError("no_match")
    all_boxes = [b for boxes in tracks for b in boxes]
    minimum = max(b[3] for b in all_boxes) * 1.03
    maximum = min(1.0, min(b[3] for b in all_boxes) * 2)
    if minimum > maximum:
        raise AssistantError("no_match")
    for attempt in range(25):
        target_height = minimum + (maximum - minimum) * attempt / 24
        scales = [[target_height / b[3] for b in boxes] for boxes in tracks]
        low, high = [0, 0], [1, 1]
        centers = []
        for boxes, values in zip(tracks, scales):
            for i, (b, scale) in enumerate(zip(boxes, values)):
                if i and (
                    abs(scale - values[i - 1]) > 0.08
                    or abs(b[0] - boxes[i - 1][0]) > 0.12
                    or abs(b[1] - boxes[i - 1][1]) > 0.12
                ):
                    raise AssistantError("tracking_unavailable")
                center = [b[0] + b[2] / 2, b[1] + b[3] / 2]
                centers.append(center)
                for axis in (0, 1):
                    low[axis] = max(low[axis], scale * center[axis] + 1 - scale)
                    high[axis] = min(high[axis], scale * center[axis])
        if any(low[a] > high[a] for a in (0, 1)):
            continue
        target = [
            max(low[a], min(high[a], sum(c[a] for c in centers) / len(centers)))
            for a in (0, 1)
        ]
        transforms = [
            [
                {
                    "x": target[0] - scale * (b[0] + b[2] / 2),
                    "y": target[1] - scale * (b[1] + b[3] / 2),
                    "scale": scale,
                }
                for b, scale in zip(boxes, values)
            ]
            for boxes, values in zip(tracks, scales)
        ]
        return {"x": target[0], "y": target[1], "height": target_height}, transforms
    raise AssistantError("no_match")


def _digest(path: Path, media: MediaTools) -> str:
    if (
        not path.is_absolute()
        or not path.is_file()
        or path.stat().st_size > 128 * 1024 * 1024
    ):
        raise AssistantError("invalid_sources")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            media.check()
            digest.update(chunk)
    return digest.hexdigest()


def native_frames(
    path: Path,
    start: int,
    length: int,
    spec: dict,
    rate: FrameRate,
    media: MediaTools,
    seed: list[float] | None = None,
) -> dict:
    size = max(spec["width"], spec["height"])
    width = round(spec["width"] * 640 / size)
    height = round(spec["height"] * 640 / size)
    request = dict(
        path=str(path),
        start=start,
        length=length,
        width=width,
        height=height,
        numerator=rate.numerator,
        denominator=rate.denominator,
    )
    if seed is not None:
        request["box"] = box(seed)
    with tempfile.TemporaryDirectory(prefix="montaz-match-") as directory:
        input_path = Path(directory) / "input.json"
        input_path.write_text(json.dumps(request))
        helper = os.environ.get("MONTAZ_TRACKER")
        if not helper or not Path(helper).is_file():
            raise AssistantError("tracking_unavailable")
        try:
            result = decode(
                media._output([helper, str(input_path)], maximum_bytes=512 * 1024)
            )
        except AssistantError as exc:
            if exc.code == "media_unreadable":
                raise AssistantError("tracking_unavailable") from exc
            raise
    fields(result, {"boxes", "first", "last"})
    if (
        type(result["boxes"]) is not list
        or len(result["boxes"]) != (length if seed else 0)
        or any(
            type(result[k]) is not str or len(result[k]) > 250000
            for k in ("first", "last")
        )
    ):
        raise AssistantError("invalid_match_evidence")
    for b in result["boxes"]:
        box(b)
    return result


def propose_match(
    snapshot: Snapshot,
    request_id: str,
    raw: dict,
    sources: dict[str, str],
    cache: Path,
    provider: Gemini,
    media: MediaTools,
    progress: Callable[[str, bool], None] = lambda *_: None,
) -> dict:
    spec = match_spec(snapshot, raw)
    clips, rate = spec["clips"], snapshot.frame_rate
    ids = {c["bin_id"] for c in clips}
    if (
        type(sources) is not dict
        or set(sources) != ids
        or any(type(p) is not str for p in sources.values())
    ):
        raise AssistantError("invalid_sources")
    paths = {bid: Path(p) for bid, p in sources.items()}
    hashes = {bid: _digest(p, media) for bid, p in paths.items()}
    windows = [window(c, i, rate) for i, c in enumerate(clips)]
    identity = [
        VERSION,
        provider.model,
        rate.numerator,
        rate.denominator,
        spec["width"],
        spec["height"],
        [
            [hashes[c["bin_id"]], c["in_frame"] + o, n]
            for c, (o, n) in zip(clips, windows)
        ],
    ]
    cache.mkdir(parents=True, exist_ok=True)
    entry = cache / (
        hashlib.sha256(json.dumps(identity).encode()).hexdigest() + ".json"
    )
    cached = False
    try:
        evidence = decode(entry.read_bytes())
        fields(evidence, {"reason", "tracks", "preview"})
        cached = True
    except (OSError, ValueError):
        evidence = None
    progress("finding", cached)
    if evidence is None:
        assets = {a.bin_id: a for a in snapshot.assets}
        usable = {
            bid: media.visual_frames(paths[bid], rate, assets[bid].duration_frames)
            for bid in ids
        }
        if any(
            c["in_frame"] + o + n > usable[c["bin_id"]]
            for c, (o, n) in zip(clips, windows)
        ):
            # Audio/container duration can exceed the actual video stream. Do not
            # track frozen tail frames or silently move the user's selected cut.
            raise AssistantError("invalid_match_request")
        frames = [
            native_frames(paths[c["bin_id"]], c["in_frame"] + o, n, spec, rate, media)
            for c, (o, n) in zip(clips, windows)
        ]
        media.check()
        seeds = provider.generate(
            [
                {
                    "text": "Find ONE convincing graphic-match object pair in these two images (outgoing then incoming). "
                    'Return only {"reason":"short explanation","boxes":[[x,y,width,height],[x,y,width,height]]}. '
                    "Coordinates are fractions of the whole supplied image, INCLUDING any letterboxing. "
                    "Choose distinctive textured regions of similar shape/orientation, not sky/water/background, "
                    "and boxes at least 0.04 wide/high. The objects may differ in identity. "
                    'If no convincing pair exists return {"reason":"No compatible objects","boxes":[]}. '
                    "Do not invent objects or force a match."
                },
                *[
                    {"inline_data": {"mime_type": "image/jpeg", "data": f["first"]}}
                    for f in frames
                ],
            ]
        )
        fields(seeds, {"reason", "boxes"})
        if (
            type(seeds["reason"]) is not str
            or not 1 <= len(seeds["reason"]) <= 600
            or type(seeds["boxes"]) is not list
            or len(seeds["boxes"]) not in (0, 2)
        ):
            raise AssistantError("invalid_match_evidence")
        progress("tracking", False)
        tracks = []
        if seeds["boxes"]:
            tracks = [
                native_frames(
                    paths[c["bin_id"]], c["in_frame"] + o, n, spec, rate, media, seed
                )["boxes"]
                for c, (o, n), seed in zip(clips, windows, seeds["boxes"])
            ]
        evidence = dict(
            reason=seeds["reason"],
            tracks=tracks,
            preview={"outgoing": frames[0]["last"], "incoming": frames[1]["first"]},
        )
    if cached:
        progress("tracking", True)
    media.check()
    if hashes != {bid: _digest(p, media) for bid, p in paths.items()}:
        raise AssistantError("media_changed")
    if (
        type(evidence["reason"]) is not str
        or not 1 <= len(evidence["reason"]) <= 600
        or type(evidence["tracks"]) is not list
        or len(evidence["tracks"]) not in (0, 2)
    ):
        raise AssistantError("invalid_match_evidence")
    operations, target, status = [], {"x": 0.5, "y": 0.5, "height": 0}, "no_match"
    if evidence["tracks"]:
        if any(
            type(t) is not list or len(t) != n
            for t, (_, n) in zip(evidence["tracks"], windows)
        ):
            raise AssistantError("invalid_match_evidence")
        try:
            target, transforms = align(evidence["tracks"])
            operations = [
                dict(
                    instance_id=c["instance_id"],
                    keyframes=[dict(frame=o + i, **v) for i, v in enumerate(values)],
                )
                for c, (o, _), values in zip(clips, windows, transforms)
            ]
            status = "matched"
        except AssistantError as exc:
            if exc.code not in {"no_match", "tracking_unavailable"}:
                raise
            evidence["reason"] = (
                "The alignment needs excessive cropping or the object track is unstable."
            )
    if not cached:
        with tempfile.NamedTemporaryFile(mode="w", dir=cache, delete=False) as temp:
            json.dump(evidence, temp)
        Path(temp.name).replace(entry)
    return dict(
        request_id=request_id,
        project_id=snapshot.project_id,
        sequence_id=snapshot.sequence_id,
        base_revision=snapshot.revision,
        frame_rate=dict(numerator=rate.numerator, denominator=rate.denominator),
        protocol_version=3,
        status=status,
        reason=evidence["reason"],
        source_hashes=hashes,
        operations=operations,
        target=target,
        preview=evidence["preview"],
        cached=cached,
    )
