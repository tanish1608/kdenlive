# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded object-match proposals. Kdenlive/MLT owns decoding, tracking and effects."""

import base64
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
from .planning import MediaChildError, MediaTools
from .match_stability import fit_centers, fit_heights, stability, triangle
from .protocol import decode, fields, frame

VERSION = "object-match-7"
MODELS = ("baseline", "triangle9", "size-fit", "stable")
MAX_CANDIDATES = 3
MAX_ANCHORS = 3
# Uniform zoom may upscale source pixels at most this much (1080p on a 1080p
# canvas: 1.6x; 4K: the native 2x ceiling). Low-resolution sources keep 1.1x.
MAX_UPSCALE, MIN_ZOOM_LIMIT = 1.6, 1.1
# Anchors may lie this fraction of the subject size outside its box; a smaller
# spread (canvas heights) cannot fix scale; a larger turn means mirrored labels.
ANCHOR_MARGIN, MIN_ANCHOR_SPREAD, MAX_ANCHOR_TURN = 0.15, 0.02, math.radians(30)
EDGE_INSET = 0.02
# Anchored pairs track this central fraction of the subject box.
TRACK_CORE = 0.6
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
NO_FIT = "No suggested pair fits within the zoom and full-frame limits."
# Gemini's native detection format: [ymin, xmin, ymax, xmax] integers on 0-1000.
_BOX_2D = {"type": "ARRAY", "items": {"type": "INTEGER"}, "minItems": 4, "maxItems": 4}
_PAIR = {"type": "ARRAY", "items": _BOX_2D, "minItems": 2, "maxItems": 2}
CANDIDATE_SCHEMA = {
    "type": "OBJECT",
    "properties": {
        "reason": {"type": "STRING"},
        "candidates": {
            "type": "ARRAY",
            "maxItems": MAX_CANDIDATES,
            "items": {
                "type": "OBJECT",
                "properties": {
                    "reason": {"type": "STRING"},
                    "subject": _PAIR,
                    "anchors": {
                        "type": "ARRAY",
                        "maxItems": MAX_ANCHORS,
                        "items": {
                            "type": "OBJECT",
                            "properties": {"name": {"type": "STRING"}, "boxes": _PAIR},
                            "required": ["name", "boxes"],
                        },
                    },
                },
                "required": ["reason", "subject", "anchors"],
            },
        },
    },
    "required": ["reason", "candidates"],
}
PROMPT = (
    "Find up to THREE graphic-match pairs between these two images (first = outgoing "
    "shot, second = incoming shot). A pair is the same kind of subject in both images "
    "that an editor could line up across a cut, for example the fronts of two cars, two "
    "wheels or two faces. The subjects may differ in identity. For each pair give: "
    "subject = the box of the WHOLE subject in each image, the thing whose size and "
    "position should match across the cut; anchors = up to three small distinct "
    "landmarks visible on that subject in BOTH images (for example left headlight, "
    "right headlight and badge, or the wheel hub), in the same order for both images, "
    "where left and right mean image-left and image-right. Prefer two or three anchors "
    "far apart on the subject. Every box is [ymin, xmin, ymax, xmax] in integers from 0 "
    "to 1000 relative to the whole image, including any black bars. Subject boxes must "
    "be at least 40 wide and 40 high. Choose distinctive textured subjects, not sky, "
    "water or plain background. Each pair must use a different subject. If no "
    "convincing pair exists, return an empty candidates list with a short reason. "
    "Fewer strong pairs are better than weak ones. Do not invent objects or force a match."
)
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
        or any(type(x) not in (int, float) or not 0 <= x <= 1.000001 for x in raw)
    ):
        raise AssistantError("invalid_match_evidence")
    x, y, w, h = raw
    if x < 0 or y < 0 or w < 0.04 or h < 0.04 or x + w > 1.000001 or y + h > 1.000001:
        raise AssistantError("invalid_match_evidence")
    return raw


def _model(boxes: list[list[float]], name: str, used: list[str]) -> list[list[float]]:
    """Subject boxes for one track. Fits that do not explain it fall back to baseline."""
    centers = [[b[0] + b[2] / 2, b[1] + b[3] / 2] for b in boxes]
    widths = triangle([b[2] for b in boxes], 2)
    heights = triangle([b[3] for b in boxes], 2)
    if name == "triangle9":
        widths = triangle([b[2] for b in boxes], 4)
        heights = triangle([b[3] for b in boxes], 4)
    elif name in {"size-fit", "stable"}:
        fitted = fit_heights([b[3] for b in boxes])
        used.append("size-trend" if fitted else "size-baseline")
        if fitted:
            # Keep each frame's smoothed aspect; only the size trend changes.
            widths = [w / h * f for w, h, f in zip(widths, heights, fitted)]
            heights = fitted
    if name == "stable":
        moved = fit_centers(centers, heights)
        used.append("center-trend" if moved else "center-measured")
        centers = moved or centers
    return [
        [c[0] - w / 2, c[1] - h / 2, w, h] for c, w, h in zip(centers, widths, heights)
    ]


def _center(b: list[float]) -> tuple[float, float]:
    return b[0] + b[2] / 2, b[1] + b[3] / 2


def _inside(point: tuple[float, float], s: list[float]) -> bool:
    margin_x, margin_y = s[2] * ANCHOR_MARGIN, s[3] * ANCHOR_MARGIN
    return (
        s[0] - margin_x <= point[0] <= s[0] + s[2] + margin_x
        and s[1] - margin_y <= point[1] <= s[1] + s[3] + margin_y
    )


def kept_anchors(candidate: dict) -> list[dict]:
    """Anchors whose centres lie in (or just outside) the subject on both sides."""
    return [
        a
        for a in candidate["anchors"]
        if all(_inside(_center(b), s) for b, s in zip(a["boxes"], candidate["boxes"]))
    ]


def tracking_seeds(candidate: dict, aspect: float) -> list[list[float]]:
    """What the helper tracks. With usable anchors the tracked box only carries
    motion, so track the subject's textured core, away from the frame edges that
    whole-subject boxes often touch. Without anchors, track the subject itself."""
    if anchor_geometry(candidate, aspect)[0] is None:
        return candidate["boxes"]
    seeds = []
    for b in candidate["boxes"]:
        w, h = max(0.04, b[2] * TRACK_CORE), max(0.04, b[3] * TRACK_CORE)
        cx, cy = _center(b)
        x = min(max(cx - w / 2, EDGE_INSET), 1 - EDGE_INSET - w)
        y = min(max(cy - h / 2, EDGE_INSET), 1 - EDGE_INSET - h)
        seeds.append([x, y, w, h])
    return seeds


def anchor_geometry(
    candidate: dict, aspect: float, reference: list[list[float]] | None = None
) -> tuple[list[tuple[float, float, float]] | None, str]:
    """Where the anchors sit relative to each side's subject box.

    Returns per side (dx, dy, ratio): the anchor centroid minus the subject center
    in subject heights, and the anchor size in subject heights (the RMS spread of
    two or more anchors, or the subject itself for one anchor). Anchors move and
    scale with the tracked box: `reference` (the tracker's own box per side)
    when given, otherwise the subject. None keeps subject-box alignment: no
    anchors, anchors outside the subject, a degenerate spread, or a left/right
    order that turns by more than 30 degrees between the images.
    """
    subjects = reference or candidate["boxes"]
    kept = kept_anchors(candidate)
    if not kept:
        return None, "anchors-outside" if candidate["anchors"] else "subject"
    sides, turns = [], []
    for side, s in enumerate(subjects):
        points = [_center(a["boxes"][side]) for a in kept]
        cx = sum(p[0] for p in points) / len(points)
        cy = sum(p[1] for p in points) / len(points)
        ratio = 1.0
        if len(points) > 1:
            # Isotropic canvas units: x distances count in canvas heights too.
            spread = math.sqrt(
                sum(((p[0] - cx) * aspect) ** 2 + (p[1] - cy) ** 2 for p in points)
                / len(points)
            )
            if spread < MIN_ANCHOR_SPREAD:
                return None, "anchors-degenerate"
            ratio = spread / s[3]
            turns.append(
                math.atan2(
                    points[1][1] - points[0][1], (points[1][0] - points[0][0]) * aspect
                )
            )
        sx, sy = _center(s)
        sides.append(((cx - sx) / s[3], (cy - sy) / s[3], ratio))
    if turns:
        turn = (turns[1] - turns[0] + math.pi) % (2 * math.pi) - math.pi
        if abs(turn) > MAX_ANCHOR_TURN:
            return None, "anchors-mirrored"
    return sides, f"anchors-{len(kept)}"


def _anchored(
    boxes: list[list[float]], geometry: tuple[float, float, float]
) -> list[list[float]]:
    """Square stand-in boxes centered on the anchors, sized by their spread."""
    dx, dy, ratio = geometry
    result = []
    for b in boxes:
        cx, cy = _center(b)
        cx, cy, size = cx + dx * b[3], cy + dy * b[3], ratio * b[3]
        result.append([cx - size / 2, cy - size / 2, size, size])
    return result


def align(
    tracks: list[list[list[float]]],
    model: str = "stable",
    used: list[str] | None = None,
    anchors: list[tuple[float, float, float]] | None = None,
    limits: tuple[float, float] = (2.0, 2.0),
) -> tuple[dict, list[list[dict]]]:
    """Keep subject height/center constant with aspect-preserving crops, capped at 2x.

    `model` chooses the per-track subject estimate: "baseline" (five-frame size
    average, measured centers), "triangle9" (nine-frame size average), "size-fit"
    (robust exponential size trend) or "stable" (size trend plus local-linear
    centers). Trend fits fall back to baseline when their residuals are large.
    The baseline is always solved first, so every model accepts exactly the
    evidence the baseline accepts; if a model's own boxes fail a zoom, step or
    coverage gate, the baseline result is returned instead of a forced fit.

    `anchors` (from anchor_geometry) aligns the anchor centroid and spread instead
    of the subject box center and height; tracking and every gate still use the
    subject box. `limits` caps each clip's zoom (at most the native 2x).
    """
    if model not in MODELS:
        raise AssistantError("invalid_match_evidence")
    used = [] if used is None else used
    for boxes in tracks:
        for b in boxes:
            box(b)
    # CSRT's integer boxes fluctuate by a few pixels even on stable subjects.
    smoothed = []
    for boxes in tracks:
        for i, b in enumerate(boxes):
            if i and (
                abs(b[0] - boxes[i - 1][0]) > 0.12
                or abs(b[1] - boxes[i - 1][1]) > 0.12
                or max(b[3], boxes[i - 1][3]) / min(b[3], boxes[i - 1][3]) > 1.25
            ):
                raise AssistantError("tracking_unavailable")
        smoothed.append(_model(boxes, "baseline", []))
    # Shape compatibility is judged on the baseline estimate for every model.
    # Anchored pairs are matched by landmark order and spread instead: a car box
    # may include the windscreen in one shot and not the other.
    ratios = [b[2] / b[3] for boxes in smoothed for b in boxes]
    if anchors is None and max(ratios) / min(ratios) > 1.3:
        raise AssistantError("no_match")

    def frame(subjects):
        if anchors is None:
            return subjects
        return [_anchored(b, g) for b, g in zip(subjects, anchors)]

    result = _solve(frame(smoothed), limits)
    if model == "baseline":
        return result
    try:
        return _solve(frame([_model(boxes, model, used) for boxes in tracks]), limits)
    except AssistantError as exc:
        if exc.code not in {"no_match", "tracking_unavailable"}:
            raise
        used.append("geometry-baseline")
        return result


def _solve(
    tracks: list[list[list[float]]],
    limits: tuple[float, float] = (2.0, 2.0),
) -> tuple[dict, list[list[dict]]]:
    """Uniform scale/offset per frame for already modelled subject boxes.

    The native v3 target size remains a fraction in [0, 1], even when an
    anchor spread measured in canvas heights exceeds the canvas height.
    """
    all_boxes = [b for boxes in tracks for b in boxes]
    minimum = max(b[3] for b in all_boxes) * 1.03
    maximum = min(
        min(b[3] for b in boxes) * min(limit, 2.0)
        for boxes, limit in zip(tracks, limits)
    )
    maximum = min(1.0, maximum)
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


def _jpeg(value: object) -> bool:
    """Match the native review's canonical base64/JPEG boundary validation."""
    if type(value) is not str or len(value) > 250000:
        return False
    try:
        raw = base64.b64decode(value, validate=True)
    except (ValueError, UnicodeError):
        return False
    return (
        raw.startswith(b"\xff\xd8")
        and raw.endswith(b"\xff\xd9")
        and base64.b64encode(raw).decode("ascii") == value
    )


def _region(raw: object) -> list[float]:
    """A fractional [x, y, w, h] region inside the image with a non-zero size."""
    if (
        type(raw) is not list
        or len(raw) != 4
        or any(type(v) not in (int, float) or not 0 <= v <= 1 for v in raw)
        or raw[2] <= 0
        or raw[3] <= 0
        or raw[0] + raw[2] > 1
        or raw[1] + raw[3] > 1
    ):
        raise AssistantError("invalid_match_evidence")
    return list(raw)


def _box_2d(raw: object) -> list[float]:
    """Gemini [ymin, xmin, ymax, xmax] on 0-1000 to a fractional [x, y, w, h]."""
    # Range checks also reject NaN, infinities and huge integers without overflow.
    if (
        type(raw) is not list
        or len(raw) != 4
        or any(type(v) not in (int, float) or not 0 <= v <= 1000 for v in raw)
        or raw[0] >= raw[2]
        or raw[1] >= raw[3]
    ):
        raise AssistantError("invalid_match_evidence")
    ymin, xmin, ymax, xmax = raw
    # Subtract before dividing so integer coordinates convert exactly.
    return [xmin / 1000, ymin / 1000, (xmax - xmin) / 1000, (ymax - ymin) / 1000]


def _inset(b: list[float]) -> list[float]:
    """Keep a tracking seed EDGE_INSET inside the frame.

    Whole-subject boxes often touch the image edge; the helper rejects any
    tracked box that steps outside the frame, which then fails the whole pair.
    """
    result = list(b)
    for axis in (0, 1):
        start, end = b[axis], b[axis] + b[axis + 2]
        if start < EDGE_INSET or end > 1 - EDGE_INSET:
            # Only clipped axes are recomputed; others keep their exact values.
            start, end = max(start, EDGE_INSET), min(end, 1 - EDGE_INSET)
            result[axis], result[axis + 2] = start, max(0.0, end - start)
    return result


def _subject_fits(boxes: list[list[float]]) -> bool:
    # The native helper tracks subjects of at least 0.04 of the frame each way.
    return all(b[2] >= 0.04 and b[3] >= 0.04 for b in boxes)


def parse_candidates(raw: object) -> tuple[str, list[dict]]:
    """Strict provider answer in Gemini's native box format.

    Structure, types, ranges and lengths are validated; violations are terminal.
    Semantically weak parts are dropped instead: subjects under the tracker's
    minimum size, and anchors whose boxes are null.
    """
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
            or set(c) != {"reason", "subject", "anchors"}
            or not _text(c["reason"], 200)
            or type(c["subject"]) is not list
            or len(c["subject"]) != 2
            or type(c["anchors"]) is not list
            or len(c["anchors"]) > MAX_ANCHORS
        ):
            raise AssistantError("invalid_match_evidence")
        subject = [_inset(_box_2d(b)) for b in c["subject"]]
        anchors = []
        for a in c["anchors"]:
            if (
                type(a) is not dict
                or set(a) != {"name", "boxes"}
                or not _text(a["name"], 60)
                or type(a["boxes"]) is not list
                or len(a["boxes"]) != 2
            ):
                raise AssistantError("invalid_match_evidence")
            if any(b is None for b in a["boxes"]):
                continue
            anchors.append(dict(name=a["name"], boxes=[_box_2d(b) for b in a["boxes"]]))
        if _subject_fits(subject):
            found.append(dict(reason=c["reason"], boxes=subject, anchors=anchors))
    return raw["reason"], found


def stored_candidates(raw: object) -> list[dict]:
    """Revalidate parse_candidates() output read back from the match cache."""
    if type(raw) is not list or len(raw) > MAX_CANDIDATES:
        raise AssistantError("invalid_match_evidence")
    found = []
    for c in raw:
        if (
            type(c) is not dict
            or set(c) != {"reason", "boxes", "anchors"}
            or not _text(c["reason"], 200)
            or type(c["boxes"]) is not list
            or len(c["boxes"]) != 2
            or type(c["anchors"]) is not list
            or len(c["anchors"]) > MAX_ANCHORS
        ):
            raise AssistantError("invalid_match_evidence")
        boxes = [_region(b) for b in c["boxes"]]
        anchors = []
        for a in c["anchors"]:
            if (
                type(a) is not dict
                or set(a) != {"name", "boxes"}
                or not _text(a["name"], 60)
                or type(a["boxes"]) is not list
                or len(a["boxes"]) != 2
            ):
                raise AssistantError("invalid_match_evidence")
            anchors.append(dict(name=a["name"], boxes=[_region(b) for b in a["boxes"]]))
        if not _subject_fits(boxes):
            raise AssistantError("invalid_match_evidence")
        found.append(dict(reason=c["reason"], boxes=boxes, anchors=anchors))
    return found


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


def rank(
    found: list[dict],
    aspect: float = 16 / 9,
    limits: tuple[float, float] = (2.0, 2.0),
) -> list[int]:
    """Indices of unique seed pairs that pass the alignment gates, best first.

    Order: least seed crop, then closest box aspect ratios, then the larger
    smaller subject, then provider order. A pair overlapping an earlier pair on
    both sides is a duplicate and is dropped. Seeds align on their anchors.
    """
    scored = []
    for i, c in enumerate(found):
        outgoing, incoming = c["boxes"]
        try:
            _, transforms = align(
                [[outgoing], [incoming]],
                anchors=anchor_geometry(c, aspect)[0],
                limits=limits,
            )
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
        except MediaChildError as exc:
            # matchtracker.cpp reserves status 5 for subject bounds/appearance rejection.
            # Decoder errors, invalid requests, missing filters and crashes stay terminal.
            if seed is not None and exc.returncode == 5:
                raise AssistantError("tracking_unavailable") from exc
            raise
    fields(result, {"boxes", "first", "last"})
    if (
        type(result["boxes"]) is not list
        or len(result["boxes"]) != (length if seed else 0)
        or any(not _jpeg(result[k]) for k in ("first", "last"))
    ):
        raise AssistantError("invalid_match_evidence")
    for b in result["boxes"]:
        box(b)
    return result


def zoom_limit(path: Path, spec: dict, media: MediaTools) -> float:
    """Largest uniform zoom that upscales this source's pixels at most MAX_UPSCALE.

    Kdenlive fits the source inside the canvas, so source pixels per canvas pixel
    are 1 / min(canvas_w / source_w, canvas_h / source_h).
    """
    try:
        stream = decode(
            media._output(
                [
                    os.environ.get("MONTAZ_FFPROBE", "ffprobe"),
                    "-v",
                    "error",
                    "-select_streams",
                    "v:0",
                    "-show_entries",
                    "stream=width,height",
                    "-of",
                    "json",
                    str(path),
                ]
            )
        )["streams"][0]
        width, height = stream["width"], stream["height"]
        if type(width) is not int or type(height) is not int or width < 1 or height < 1:
            raise AssistantError("media_unreadable")
    except AssistantError:
        raise
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        raise AssistantError("media_unreadable") from exc
    density = 1 / min(spec["width"] / width, spec["height"] / height)
    return max(MIN_ZOOM_LIMIT, min(2.0, MAX_UPSCALE * density))


def _cached(
    entry: Path,
    windows: list[tuple[int, int]],
    aspect: float,
    limits: tuple[float, float],
) -> dict | None:
    """Complete evidence only. Unreadable or inconsistent entries are a miss."""
    try:
        evidence = fields(
            decode(entry.read_bytes()), {"reason", "candidates", "tracked", "preview"}
        )
        if not _text(evidence["reason"], 600):
            return None
        found = stored_candidates(evidence["candidates"])
        tracked, preview = evidence["tracked"], evidence["preview"]
        if (
            type(tracked) is not list
            or len(tracked) != min(MAX_TRACKED, len(rank(found, aspect, limits)))
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
            or any(not _jpeg(v) for v in preview.values())
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

    Each pair is a tracked subject plus up to three anchors that set its aligned
    position and size. The selected pair is the tracked pair with the lowest
    metrics() cost that passes align(); ties keep the earlier rank() position.
    `report`, when given, receives local selection diagnostics that never enter
    the wire review.
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
    aspect = spec["width"] / spec["height"]
    limits = tuple(zoom_limit(paths[c["bin_id"]], spec, media) for c in clips)
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
    evidence = _cached(entry, windows, aspect, limits)
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
                    {"text": PROMPT},
                    *[
                        {"inline_data": {"mime_type": "image/jpeg", "data": f["first"]}}
                        for f in frames
                    ],
                ],
                schema=CANDIDATE_SCHEMA,
            )
        )
        progress("tracking", False)
        tracked = []
        for position, index in enumerate(rank(found, aspect, limits)[:MAX_TRACKED]):
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
                            clips, windows, tracking_seeds(found[index], aspect)
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
    found = evidence["candidates"]
    order = rank(found, aspect, limits)
    outcomes, best = [], None
    for position, (index, tracks) in enumerate(zip(order, evidence["tracked"])):
        geometry, anchored = anchor_geometry(found[index], aspect)
        outcome = dict(
            candidate=index, rank=position, outcome="unstable", anchors=anchored
        )
        outcomes.append(outcome)
        if tracks is None:
            continue
        if geometry is not None:
            # Native fixed-seed registration preserves the exact seed geometry
            # on frame zero. Express landmarks relative to the tracked core,
            # retaining frame one's genuine translation and scale change.
            reference = [t[0] for t in tracks]
            geometry, anchored = anchor_geometry(found[index], aspect, reference)
            outcome["anchors"] = anchored
        try:
            used = []
            target, transforms = align(
                tracks, used=used, anchors=geometry, limits=limits
            )
        except AssistantError as exc:
            if exc.code not in {"no_match", "tracking_unavailable"}:
                raise
            outcome["outcome"] = "rejected"
            continue
        measured = metrics(transforms)
        outcome.update(
            outcome="valid", models=used, **measured, **stability(transforms)
        )
        if best is None or round(measured["cost"], 9) < round(best[3]["cost"], 9):
            names = [a["name"].strip() for a in kept_anchors(found[index])]
            best = (position, target, transforms, measured, geometry and names)
    operations, target, status = [], {"x": 0.5, "y": 0.5, "height": 0}, "no_match"
    reason = evidence["reason"] if not found else UNSUITABLE if order else NO_FIT
    if best:
        position, target, transforms, measured, names = best
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
        if names:
            reason = f"{reason}{'' if reason[-1] in '.!?' else '.'} Lined up on {', '.join(names)[:120]}"
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
            zoom_limits=limits,
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
