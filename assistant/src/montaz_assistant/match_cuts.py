# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded object-match proposals. Kdenlive/MLT owns decoding, tracking and effects."""

import hashlib
import json
import math
import os
import tempfile
import time
from pathlib import Path
from typing import Callable

from .contracts import FrameRate, Snapshot
from .gemini import AssistantError, Gemini
from .planning import MediaTools
from .protocol import decode, fields, frame

VERSION = "object-match-3"
MAX_CANDIDATES = 3
MAX_TRACKED = 2
# WorkerClient kills a match run 90 s after sending it. Keep 10 s for interpreter
# start-up and the final message. A further candidate starts only when both of
# its helper children could reach MediaTools' 15 s child timeout plus a re-hash.
BUDGET_SECONDS = 80.0
CANDIDATE_SECONDS = 2 * 15 + 5
# Seed pairs whose outgoing and incoming boxes both overlap this much are one pair.
DUPLICATE_OVERLAP = 0.5
# One percent of canvas per frame of added reframing motion costs as much as
# discarding a further ten percent of the source picture.
MOTION_WEIGHT = 10.0
UNSUITABLE = "The alignment needs excessive cropping or the object track is unstable."
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


def _text(value: object, limit: int) -> bool:
    # Native review limits count UTF-16 code units, as QString::size() does.
    return (
        type(value) is str
        and bool(value.strip())
        and len(value.encode("utf-16-le", "surrogatepass")) <= 2 * limit
    )


def parse_candidates(raw: object) -> tuple[str, list[dict]]:
    """Strict provider answer: an overall reason and at most three seed pairs."""
    if (
        type(raw) is not dict
        or set(raw) != {"reason", "candidates"}
        or not _text(raw["reason"], 600)
        or type(raw["candidates"]) is not list
        or len(raw["candidates"]) > MAX_CANDIDATES
    ):
        raise AssistantError("invalid_match_evidence")
    found = []
    for c in raw["candidates"]:
        if (
            type(c) is not dict
            or set(c) != {"reason", "boxes"}
            or not _text(c["reason"], 200)
            or type(c["boxes"]) is not list
            or len(c["boxes"]) != 2
        ):
            raise AssistantError("invalid_match_evidence")
        found.append(dict(reason=c["reason"], boxes=[list(box(b)) for b in c["boxes"]]))
    return raw["reason"], found


def _overlap(a: list[float], b: list[float]) -> float:
    w = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    h = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    if w <= 0 or h <= 0:
        return 0.0
    return w * h / (a[2] * a[3] + b[2] * b[3] - w * h)


def metrics(transforms: list[list[dict]]) -> dict:
    """Selection cost of an accepted alignment; lower is better.

    zoom is the largest uniform scale. crop is the mean source area discarded by
    that scale, 1 - 1/scale^2, over every keyframe of both clips. motion is the
    mean, over consecutive keyframes within each clip, of the largest canvas
    displacement of any source corner added by the reframing. These are
    geometric costs, not identity confidence.
    """
    keys = [k for values in transforms for k in values]
    crop = sum(1 - 1 / k["scale"] ** 2 for k in keys) / len(keys)
    steps = [
        max(
            math.hypot(
                b["x"] - a["x"] + (b["scale"] - a["scale"]) * u,
                b["y"] - a["y"] + (b["scale"] - a["scale"]) * v,
            )
            for u in (0, 1)
            for v in (0, 1)
        )
        for values in transforms
        for a, b in zip(values, values[1:])
    ]
    motion = sum(steps) / len(steps) if steps else 0.0
    return dict(
        zoom=max(k["scale"] for k in keys),
        crop=crop,
        motion=motion,
        cost=crop + MOTION_WEIGHT * motion,
    )


def rank(found: list[dict]) -> list[int]:
    """Indices of unique seed pairs that pass the alignment gates, best first.

    Order: least seed crop, then closest box aspect ratios, then the larger
    smaller subject, then provider order. A pair overlapping an earlier pair on
    both sides is a duplicate and is dropped.
    """
    scored = []
    for i, c in enumerate(found):
        outgoing, incoming = c["boxes"]
        try:
            _, transforms = align([[outgoing], [incoming]])
        except AssistantError as exc:
            if exc.code not in {"no_match", "tracking_unavailable"}:
                raise
            continue
        aspect = abs(math.log(outgoing[2] / outgoing[3] * incoming[3] / incoming[2]))
        scored.append(
            (
                round(metrics(transforms)["crop"], 9),
                round(aspect, 9),
                -round(min(outgoing[3], incoming[3]), 9),
                i,
            )
        )
    order = []
    for *_, i in sorted(scored):
        if not any(
            min(_overlap(a, b) for a, b in zip(found[i]["boxes"], found[j]["boxes"]))
            >= DUPLICATE_OVERLAP
            for j in order
        ):
            order.append(i)
    return order


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


def _cached(entry: Path, windows: list[tuple[int, int]]) -> dict | None:
    """Complete evidence only. Unreadable or inconsistent entries are a miss."""
    try:
        evidence = fields(
            decode(entry.read_bytes()), {"reason", "candidates", "tracked", "preview"}
        )
        _, found = parse_candidates(
            dict(reason=evidence["reason"], candidates=evidence["candidates"])
        )
        tracked, preview = evidence["tracked"], evidence["preview"]
        if (
            type(tracked) is not list
            or len(tracked) != min(MAX_TRACKED, len(rank(found)))
            or (tracked and all(t is None for t in tracked))
            or any(
                t is not None
                and (
                    type(t) is not list
                    or len(t) != 2
                    or any(
                        type(b) is not list or len(b) != n
                        for b, (_, n) in zip(t, windows)
                    )
                )
                for t in tracked
            )
            or type(preview) is not dict
            or set(preview) != {"outgoing", "incoming"}
            or any(type(v) is not str or len(v) > 250000 for v in preview.values())
        ):
            return None
        for t in tracked:
            for b in [b for boxes in t or [] for b in boxes]:
                box(b)
        return evidence
    except (OSError, ValueError, RecursionError):
        return None


def propose_match(
    snapshot: Snapshot,
    request_id: str,
    raw: dict,
    sources: dict[str, str],
    cache: Path,
    provider: Gemini,
    media: MediaTools,
    progress: Callable[[str, bool], None] = lambda *_: None,
    *,
    clock: Callable[[], float] = time.monotonic,
    report: dict | None = None,
) -> dict:
    """One Gemini request for up to three seed pairs; at most two are tracked.

    The selected pair is the tracked pair with the lowest metrics() cost that
    passes align(); ties keep the earlier rank() position. `report`, when given,
    receives local selection diagnostics that never enter the wire review.
    """
    started = clock()
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
    evidence = _cached(entry, windows)
    cached, complete = evidence is not None, True
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
        reason, found = parse_candidates(
            provider.generate(
                [
                    {
                        "text": "Find up to THREE convincing graphic-match object pairs in these two images "
                        "(outgoing then incoming). Return only "
                        '{"reason":"short overall explanation","candidates":[{"reason":"short explanation",'
                        '"boxes":[[x,y,width,height],[x,y,width,height]]}]}. '
                        "In each pair the first box is in the outgoing image and the second in the incoming image. "
                        "Coordinates are fractions of the whole supplied image, INCLUDING any letterboxing. "
                        "Choose distinctive textured regions of similar shape, orientation and size, "
                        "not sky/water/background, and boxes at least 0.04 wide/high. The objects may differ "
                        "in identity. Each pair must use different objects; never repeat a pair. "
                        'If no convincing pair exists return {"reason":"No compatible objects","candidates":[]}. '
                        "Fewer strong pairs are better than weak ones. Do not invent objects or force a match."
                    },
                    *[
                        {"inline_data": {"mime_type": "image/jpeg", "data": f["first"]}}
                        for f in frames
                    ],
                ]
            )
        )
        progress("tracking", False)
        tracked = []
        for position, index in enumerate(rank(found)[:MAX_TRACKED]):
            media.check()
            if position and clock() - started > BUDGET_SECONDS - CANDIDATE_SECONDS:
                complete = False
                break
            try:
                # Seeds refer to the first frame of each window, the images Gemini saw.
                tracked.append(
                    [
                        native_frames(
                            paths[c["bin_id"]],
                            c["in_frame"] + o,
                            n,
                            spec,
                            rate,
                            media,
                            seed,
                        )["boxes"]
                        for c, (o, n), seed in zip(
                            clips, windows, found[index]["boxes"]
                        )
                    ]
                )
            except AssistantError as exc:
                # The helper rejected this subject (appearance/bounds). Others may track.
                if exc.code != "tracking_unavailable":
                    raise
                tracked.append(None)
        if tracked and all(t is None for t in tracked):
            # No candidate produced evidence. This is not a semantic empty answer.
            raise AssistantError("tracking_unavailable")
        evidence = dict(
            reason=reason,
            candidates=found,
            tracked=tracked,
            preview={"outgoing": frames[0]["last"], "incoming": frames[1]["first"]},
        )
    if cached:
        progress("tracking", True)
    media.check()
    if hashes != {bid: _digest(p, media) for bid, p in paths.items()}:
        raise AssistantError("media_changed")
    found, order = evidence["candidates"], rank(evidence["candidates"])
    outcomes, best = [], None
    for position, (index, tracks) in enumerate(zip(order, evidence["tracked"])):
        outcome = dict(candidate=index, rank=position, outcome="unstable")
        outcomes.append(outcome)
        if tracks is None:
            continue
        try:
            target, transforms = align(tracks)
        except AssistantError as exc:
            if exc.code not in {"no_match", "tracking_unavailable"}:
                raise
            outcome["outcome"] = "rejected"
            continue
        measured = metrics(transforms)
        outcome.update(outcome="valid", **measured)
        if best is None or round(measured["cost"], 9) < round(best[3]["cost"], 9):
            best = (position, target, transforms, measured)
    operations, target, status = [], {"x": 0.5, "y": 0.5, "height": 0}, "no_match"
    reason = evidence["reason"] if not found else UNSUITABLE
    if best:
        position, target, transforms, measured = best
        index = order[position]
        operations = [
            dict(
                instance_id=c["instance_id"],
                keyframes=[dict(frame=o + i, **v) for i, v in enumerate(values)],
            )
            for c, (o, _), values in zip(clips, windows, transforms)
        ]
        status = "matched"
        reason = found[index]["reason"].strip()
        reason = (
            f"{reason}{'' if reason[-1] in '.!?' else '.'} Chose ranked pair {position + 1} of "
            f"{len(order)} by lowest crop/motion cost: {measured['zoom']:.2f}x zoom, "
            f"{measured['crop']:.0%} cropped, {measured['motion']:.2%} canvas motion per frame."
        )
    elif not complete:
        reason += " Remaining pairs were not tracked within the time limit."
    if not cached and complete:
        with tempfile.NamedTemporaryFile(mode="w", dir=cache, delete=False) as temp:
            json.dump(evidence, temp)
        Path(temp.name).replace(entry)
    if report is not None:
        report.update(
            candidates=len(found),
            ranked=order,
            tracked=outcomes,
            selected=None if best is None else order[best[0]],
            complete=complete,
            cached=cached,
            elapsed=clock() - started,
        )
    return dict(
        request_id=request_id,
        project_id=snapshot.project_id,
        sequence_id=snapshot.sequence_id,
        base_revision=snapshot.revision,
        frame_rate=dict(numerator=rate.numerator, denominator=rate.denominator),
        protocol_version=3,
        status=status,
        reason=reason,
        source_hashes=hashes,
        operations=operations,
        target=target,
        preview=evidence["preview"],
        cached=cached,
    )
