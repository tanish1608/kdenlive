# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded movement and occlusion evidence from low-resolution grayscale frames.

Pure standard-library analysis of caller-supplied buffers at integer project
frame positions. It decodes nothing, calls no model and applies nothing: results
are measurements with explicit limits and an explicit unavailable outcome.
Image motion is reported as the direction the picture content moves; whether
that comes from a camera pan or a moving foreground is not established.
Occlusion results are candidates only: object identity and complete coverage
by a real object cannot be proved from these inputs, so none is "verified".
"""

import math
from dataclasses import dataclass
from typing import Callable, Sequence

VERSION = "motion-evidence-1"
MIN_SIDE, MAX_SIDE = 32, 96  # The minimum keeps a search interior at half size.
MIN_FRAMES, MAX_FRAMES = 2, 24
SEARCH_RADIUS = 8  # Full-resolution pixels per sampled step, searched exhaustively.
COARSE_RADIUS = SEARCH_RADIUS // 2  # Half-resolution search, then a +-1 refinement.
MIN_TEXTURE = 2.0  # Mean absolute gradient (grey levels) needed to measure motion.
AMBIGUITY = 0.85  # Best/second-best cost above this: no distinct translation.
MAX_RESIDUAL = 0.75  # Aligned cost / texture above this: content changed.
STATIC_SPEED = 0.5  # Pixels per project frame treated as no movement.
CONSISTENT_TURN = math.radians(45)
GRID = 4  # Occlusion blocks per side.
FLAT_STD = 4.0  # Block standard deviation below this has no usable texture.
SAME_CORRELATION = 0.6
BLOCK_SEARCH = 2  # Pixels of per-block registration slack against the reference.
PARTIAL_COVERAGE, COMPLETE_COVERAGE = 0.2, 0.9
SAME_DIRECTION_TURN = math.radians(30)
MAX_SPEED_RATIO = 2.0
LIMITS = dict(
    max_side=MAX_SIDE,
    max_frames=MAX_FRAMES,
    search_radius_px=SEARCH_RADIUS,
    grid=GRID,
    min_texture=MIN_TEXTURE,
)


class MotionInputError(ValueError):
    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class Frame:
    """Grayscale 0-255 pixels, row-major, at an integer project frame position."""

    position: int
    pixels: bytes | bytearray | Sequence[float]


@dataclass(frozen=True)
class Window:
    width: int
    height: int
    frames: tuple[Frame, ...]


@dataclass(frozen=True)
class Step:
    """Content translation between two sampled frames, in pixels per project frame."""

    start: int
    end: int
    status: str  # measured | textureless | ambiguous | unmatched
    dx: float | None
    dy: float | None
    texture: float
    ambiguity: float | None
    residual: float | None


@dataclass(frozen=True)
class Occlusion:
    """A region of the picture losing its reference content. Never verified."""

    start: int
    end: int
    peak_position: int
    peak_coverage: float
    kind: str  # partial | complete
    textured_cover: bool
    note: str
    verified: bool = False


@dataclass(frozen=True)
class MotionEvidence:
    version: str
    width: int
    height: int
    first: int
    last: int
    steps: tuple[Step, ...]
    direction: str  # static | left | up-right | ... | inconsistent | unavailable
    velocity: tuple[float, float] | None  # Median content motion, px per frame.
    consistency: float
    fade: tuple[int, int] | None
    abrupt_change: tuple[int, int] | None
    local_motion: bool
    occlusions: tuple[Occlusion, ...]
    notes: tuple[str, ...]
    limits: dict


@dataclass(frozen=True)
class PairAssessment:
    outcome: str  # same_direction | different_direction | unavailable
    boundary: tuple[int, int]
    turn_degrees: float | None
    speed_ratio: float | None
    note: str


def _rows(window: object) -> list[list[list[float]]]:
    if type(window) is not Window:
        raise MotionInputError("invalid_window")
    w, h, frames = window.width, window.height, window.frames
    if (
        type(w) is not int
        or type(h) is not int
        or not MIN_SIDE <= w <= MAX_SIDE
        or not MIN_SIDE <= h <= MAX_SIDE
    ):
        raise MotionInputError("invalid_size")
    if type(frames) not in (list, tuple) or not MIN_FRAMES <= len(frames) <= MAX_FRAMES:
        raise MotionInputError("invalid_frame_count")
    images, previous = [], -1
    for f in frames:
        if (
            type(f) is not Frame
            or type(f.position) is not int
            or f.position <= previous
        ):
            raise MotionInputError("invalid_positions")
        previous = f.position
        p = f.pixels
        if type(p) in (bytes, bytearray):
            if len(p) != w * h:
                raise MotionInputError("invalid_pixels")
            values = list(p)
        elif type(p) in (list, tuple):
            if len(p) != w * h or any(
                type(v) not in (int, float) or not 0 <= v <= 255 for v in p
            ):
                raise MotionInputError("invalid_pixels")  # Also rejects NaN/inf.
            values = [float(v) for v in p]
        else:
            raise MotionInputError("invalid_pixels")
        images.append([values[y * w : (y + 1) * w] for y in range(h)])
    return images


def _centered(img: list[list[float]]) -> list[list[float]]:
    mean = sum(map(sum, img)) / (len(img) * len(img[0]))
    return [[v - mean for v in row] for row in img]


def _half(img: list[list[float]]) -> list[list[float]]:
    return [
        [
            (
                img[2 * y][2 * x]
                + img[2 * y][2 * x + 1]
                + img[2 * y + 1][2 * x]
                + img[2 * y + 1][2 * x + 1]
            )
            / 4
            for x in range(len(img[0]) // 2)
        ]
        for y in range(len(img) // 2)
    ]


def _cost(a, b, dx: int, dy: int, margin: int) -> float:
    """Mean |a(x, y) - b(x + dx, y + dy)| over a fixed interior."""
    h, w = len(a), len(a[0])
    total = 0.0
    for y in range(margin, h - margin):
        total += sum(
            abs(p - q)
            for p, q in zip(
                a[y][margin : w - margin], b[y + dy][margin + dx : w - margin + dx]
            )
        )
    return total / ((w - 2 * margin) * (h - 2 * margin))


def _texture(img: list[list[float]], margin: int) -> float:
    h, w = len(img), len(img[0])
    total = 0.0
    for y in range(margin, h - margin):
        row, below = img[y], img[y + 1]
        total += sum(
            abs(row[x + 1] - row[x]) + abs(below[x] - row[x])
            for x in range(margin, w - margin)
        )
    return total / ((w - 2 * margin) * (h - 2 * margin))


def _step(a, b, start, end, check) -> Step:
    span = end - start
    a, b = _centered(a), _centered(b)
    texture = _texture(a, SEARCH_RADIUS + 1)
    if texture < MIN_TEXTURE:
        return Step(start, end, "textureless", None, None, texture, None, None)
    ca, cb = _half(a), _half(b)
    costs = {}
    for dy in range(-COARSE_RADIUS, COARSE_RADIUS + 1):
        check()
        for dx in range(-COARSE_RADIUS, COARSE_RADIUS + 1):
            costs[dx, dy] = _cost(ca, cb, dx, dy, COARSE_RADIUS)
    best = min(costs, key=lambda k: (costs[k], abs(k[0]) + abs(k[1]), k))
    rival = min(
        (
            c
            for k, c in costs.items()
            if max(abs(k[0] - best[0]), abs(k[1] - best[1])) >= 2
        ),
        default=None,
    )
    ambiguity = costs[best] / rival if rival else 0.0
    if ambiguity > AMBIGUITY:
        return Step(start, end, "ambiguous", None, None, texture, ambiguity, None)
    fine = {}
    for dy in range(2 * best[1] - 1, 2 * best[1] + 2):
        for dx in range(2 * best[0] - 1, 2 * best[0] + 2):
            if max(abs(dx), abs(dy)) <= SEARCH_RADIUS:
                fine[dx, dy] = _cost(a, b, dx, dy, SEARCH_RADIUS + 1)
    shift = min(fine, key=lambda k: (fine[k], abs(k[0]) + abs(k[1]), k))
    residual = fine[shift] / texture
    if residual > MAX_RESIDUAL:
        return Step(start, end, "unmatched", None, None, texture, ambiguity, residual)
    # Sub-pixel refinement: a parabola through the costs either side of the best
    # integer shift, per axis. Integer steps would accumulate up to half a pixel
    # of drift per step when compensating motion over a window.
    offset = []
    for axis in (0, 1):
        lower = list(shift)
        upper = list(shift)
        lower[axis] -= 1
        upper[axis] += 1
        lower, upper = tuple(lower), tuple(upper)
        if max(map(abs, lower + upper)) > SEARCH_RADIUS:
            offset.append(0.0)
            continue
        for k in (lower, upper):
            if k not in fine:
                fine[k] = _cost(a, b, k[0], k[1], SEARCH_RADIUS + 1)
        curve = fine[lower] - 2 * fine[shift] + fine[upper]
        offset.append(
            max(-0.5, min(0.5, (fine[lower] - fine[upper]) / (2 * curve)))
            if curve > 0
            else 0.0
        )
    return Step(
        start,
        end,
        "measured",
        round((shift[0] + offset[0]) / span, 3),
        round((shift[1] + offset[1]) / span, 3),
        texture,
        ambiguity,
        residual,
    )


def _label(vx: float, vy: float) -> str:
    if math.hypot(vx, vy) < STATIC_SPEED:
        return "static"
    sector = round(math.atan2(-vy, vx) / (math.pi / 4)) % 8  # Screen y grows down.
    return (
        "right",
        "up-right",
        "up",
        "up-left",
        "left",
        "down-left",
        "down",
        "down-right",
    )[sector]


def _median(values: list[float]) -> float:
    values = sorted(values)
    mid = len(values) // 2
    return values[mid] if len(values) % 2 else (values[mid - 1] + values[mid]) / 2


def _blocks(img, ox: int, oy: int):
    """Per-block statistics of `img` sampled at the reference grid moved by (ox, oy)."""
    h, w = len(img), len(img[0])
    bw, bh = w // GRID, h // GRID
    result = {}
    for gy in range(GRID):
        for gx in range(GRID):
            x0, y0 = gx * bw + ox, gy * bh + oy
            if x0 < 0 or y0 < 0 or x0 + bw > w or y0 + bh > h:
                continue
            result[gx, gy] = [v for row in img[y0 : y0 + bh] for v in row[x0 : x0 + bw]]
    return result


def _block(img, gx: int, gy: int, ox: int, oy: int) -> list[float] | None:
    h, w = len(img), len(img[0])
    bw, bh = w // GRID, h // GRID
    x0, y0 = gx * bw + ox, gy * bh + oy
    if x0 < 0 or y0 < 0 or x0 + bw > w or y0 + bh > h:
        return None
    return [v for row in img[y0 : y0 + bh] for v in row[x0 : x0 + bw]]


def _best_correlation(reference: list[float], img, key, ox: int, oy: int) -> float:
    """Highest correlation within +-BLOCK_SEARCH pixels: integer motion
    compensation drifts by a pixel or two, which fine texture cannot tolerate."""
    best = -1.0
    for ey in range(-BLOCK_SEARCH, BLOCK_SEARCH + 1):
        for ex in range(-BLOCK_SEARCH, BLOCK_SEARCH + 1):
            values = _block(img, key[0], key[1], ox + ex, oy + ey)
            if values is not None:
                best = max(best, _correlation(reference, values))
    return best


def _stats(values: list[float]) -> tuple[float, float]:
    mean = sum(values) / len(values)
    return mean, math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))


def _correlation(a: list[float], b: list[float]) -> float:
    ma, sa = _stats(a)
    mb, sb = _stats(b)
    if sa == 0 or sb == 0:
        return 0.0
    return sum((p - ma) * (q - mb) for p, q in zip(a, b)) / (len(a) * sa * sb)


def _one_side(cells: set) -> bool:
    """A side band (from any edge or corner) stays within one half of the frame
    along at least one axis; zoom or dolly decorrelation reaches every quadrant."""
    half = GRID / 2
    both_columns = {x < half for x, _ in cells} == {True, False}
    both_rows = {y < half for _, y in cells} == {True, False}
    return not (both_columns and both_rows)


def _from_edge(cells: set) -> bool:
    """Most covered blocks lie on the outer ring of the grid. A connectivity test
    is too brittle: one block just above the correlation threshold splits it."""
    if not cells:
        return False
    ring = [c for c in cells if c[0] in (0, GRID - 1) or c[1] in (0, GRID - 1)]
    return len(ring) >= 0.8 * len(cells)


def analyse(
    window: Window, *, check: Callable[[], None] = lambda: None
) -> MotionEvidence:
    """Measure content movement and occlusion candidates for one window."""
    images = _rows(window)
    positions = [f.position for f in window.frames]
    steps = []
    for i in range(len(images) - 1):
        check()
        steps.append(
            _step(images[i], images[i + 1], positions[i], positions[i + 1], check)
        )
    notes = []
    measured = [s for s in steps if s.status == "measured"]
    velocity, direction, consistency = None, "unavailable", 0.0
    if measured and len(measured) * 2 >= len(steps):
        vx, vy = _median([s.dx for s in measured]), _median([s.dy for s in measured])
        velocity, direction = (vx, vy), _label(vx, vy)
        if direction == "static":
            agree = [s for s in measured if math.hypot(s.dx, s.dy) < STATIC_SPEED]
        else:
            heading = math.atan2(vy, vx)
            agree = [
                s
                for s in measured
                if math.hypot(s.dx, s.dy) >= STATIC_SPEED
                and abs(
                    (math.atan2(s.dy, s.dx) - heading + math.pi) % (2 * math.pi)
                    - math.pi
                )
                <= CONSISTENT_TURN
            ]
        consistency = len(agree) / len(measured)
        if consistency < 0.75:
            direction = "inconsistent"
    else:
        notes.append("Too few textured, unambiguous steps to measure movement.")
    # Noise has no distinct best shift (ambiguous). A wipe or cut has one but
    # changes the picture (unmatched), which the coverage analysis examines.
    ambiguous = [s for s in steps if s.status == "ambiguous"]
    incoherent = len(ambiguous) * 4 >= len(steps) * 3
    if incoherent:
        notes.append(
            "Picture content is not coherent between samples (noise or flicker): no occlusion, fade or cut evidence."
        )

    # Occlusion and fade evidence against the first frame, compensated for the
    # measured (or, where unmeasured, the median) content translation.
    offsets, ox, oy = [(0, 0)], 0.0, 0.0
    for s in steps:
        if s.status == "measured":
            vx, vy = s.dx, s.dy
        else:
            vx, vy = (
                velocity if velocity and direction != "inconsistent" else (0.0, 0.0)
            )
        ox, oy = ox + vx * (s.end - s.start), oy + vy * (s.end - s.start)
        offsets.append((round(ox), round(oy)))
    reference = _blocks(images[0], 0, 0)
    textured = (
        set()
        if incoherent
        else {k for k, v in reference.items() if _stats(v)[1] >= FLAT_STD}
    )
    ref_mean = sum(map(sum, images[0])) / (window.width * window.height)
    coverage, covered_sets, flat_sets, uniform_dark, structure_kept = [], [], [], [], []
    for img, (dx, dy) in zip(images, offsets):
        check()
        current = _blocks(img, dx, dy)
        eligible = [k for k in textured if k in current]
        covered, flat, ratios, kept = set(), set(), [], True
        for k in eligible:
            mean_r, _ = _stats(reference[k])
            mean_c, std_c = _stats(current[k])
            ratios.append(mean_c / mean_r if mean_r else 1.0)
            if std_c < FLAT_STD:
                covered.add(k)
                flat.add(k)
            elif _best_correlation(reference[k], img, k, dx, dy) < SAME_CORRELATION:
                covered.add(k)
                kept = False
        coverage.append(len(covered) / len(eligible) if eligible else None)
        covered_sets.append(covered)
        flat_sets.append(flat)
        spread = _stats(ratios)[1] if ratios else 0.0
        uniform_dark.append(bool(ratios) and spread <= 0.12)
        structure_kept.append(kept)

    if not textured and not incoherent:
        notes.append(
            "Textureless reference: no occlusion or wipe evidence is possible."
        )
    # Fade: the whole picture darkens uniformly while remaining texture keeps its
    # structure. Blocks that go flat during a fade are not an occluding object.
    means = [sum(map(sum, img)) / (window.width * window.height) for img in images]
    fade = None
    if textured and ref_mean > 0 and min(means) / ref_mean <= 0.35:
        dark = [
            i
            for i, m in enumerate(means)
            if m / ref_mean < 0.9 and uniform_dark[i] and structure_kept[i]
        ]
        darkening = [i for i, m in enumerate(means) if m / ref_mean < 0.9]
        if (
            dark
            and dark == darkening
            and all(
                means[i] >= means[i + 1] - 1.0
                for i in range(dark[0], len(means) - 1)
                if i + 1 in dark
            )
        ):
            fade = (positions[dark[0]], positions[dark[-1]])
            notes.append(
                "Uniform darkening with preserved structure: a fade, not an occlusion."
            )
    in_fade = {i for i, p in enumerate(positions) if fade and fade[0] <= p <= fade[1]}

    occlusions, abrupt, local_motion, scale_suspected = [], None, False, False
    i = 1
    while i < len(images):
        c = coverage[i]
        if i in in_fade or c is None or c < PARTIAL_COVERAGE:
            i += 1
            continue
        before = coverage[i - 1] or 0.0
        changed = steps[i - 1].status in ("unmatched", "ambiguous")
        if before < PARTIAL_COVERAGE and c >= COMPLETE_COVERAGE and changed:
            abrupt = (positions[i - 1], positions[i])
            notes.append(
                "Whole picture changed in one sampled step: a cut or flash, not a wipe."
            )
            while i < len(images) and (coverage[i] or 0.0) >= PARTIAL_COVERAGE:
                i += 1
            continue
        j = i
        while (
            j + 1 < len(images)
            and j + 1 not in in_fade
            and (coverage[j + 1] or 0.0) >= PARTIAL_COVERAGE
        ):
            j += 1
        run = range(i, j + 1)
        # An occluder enters from outside the picture: whatever is covered first
        # must touch the frame edge. Something appearing mid-frame is foreground.
        first = next(k for k in range(1, i + 1) if covered_sets[k])
        anchored = _from_edge(covered_sets[first])
        peak = max(run, key=lambda k: (coverage[k], -k))
        # While it spreads, an occluder keeps covering what it covered first; a
        # passing object uncovers blocks it covered earlier, even below 20%.
        earlier = set().union(*covered_sets[1:i])

        def holds(frames) -> bool:
            return all(
                len(covered_sets[i] & covered_sets[k]) >= 0.8 * len(covered_sets[i])
                and not (earlier - covered_sets[k])
                for k in frames
            )

        complete = coverage[peak] >= COMPLETE_COVERAGE and holds(range(i, peak + 1))
        # A partial cover must last to the end of the window; one that moves on
        # is foreground motion, not an occlusion.
        rest = [k for k in range(i, len(images)) if k not in in_fade]
        persistent = complete or holds(rest)
        # A partial occluder covers one side. Edge content changing in every
        # quadrant together is what a zoom or dolly does, which is not modelled.
        two_sided = not complete and not _one_side(covered_sets[peak])
        if two_sided and persistent and anchored:
            scale_suspected = True
        elif persistent and anchored:
            flat_cover = len(flat_sets[peak]) * 2 > len(covered_sets[peak])
            note = "Coverage on a 4x4 block grid; object identity and true occlusion are not established."
            if complete and flat_cover:
                note += " The covering surface is textureless: it cannot be told from a dark object, lens cover or exposure loss."
            occlusions.append(
                Occlusion(
                    positions[i],
                    positions[j],
                    positions[peak],
                    round(coverage[peak], 3),
                    "complete" if complete else "partial",
                    not flat_cover,
                    note,
                )
            )
        else:
            local_motion = True
        i = j + 1
    if scale_suspected:
        notes.append(
            "Edge content changes in every quadrant of the frame: consistent with a zoom or "
            "dolly, which is not modelled. No occlusion is reported."
        )
    if local_motion:
        notes.append(
            "A changing region moves or recurs without spreading: independent foreground motion."
        )
    return MotionEvidence(
        VERSION,
        window.width,
        window.height,
        positions[0],
        positions[-1],
        tuple(steps),
        direction,
        velocity,
        round(consistency, 3),
        fade,
        abrupt,
        local_motion,
        tuple(occlusions),
        tuple(notes),
        dict(LIMITS),
    )


def assess_pair(outgoing: MotionEvidence, incoming: MotionEvidence) -> PairAssessment:
    """Whether measured content motion continues in the same direction across a cut."""
    boundary = (outgoing.last, incoming.first)
    caveat = "Measured image motion only; a camera pan and a moving foreground are not distinguished."
    moving = {"static", "inconsistent", "unavailable"}
    if outgoing.direction in moving or incoming.direction in moving:
        return PairAssessment(
            "unavailable",
            boundary,
            None,
            None,
            f"Needs consistent movement on both sides (outgoing {outgoing.direction}, incoming {incoming.direction}). {caveat}",
        )
    (ax, ay), (bx, by) = outgoing.velocity, incoming.velocity
    turn = abs(
        (math.atan2(by, bx) - math.atan2(ay, ax) + math.pi) % (2 * math.pi) - math.pi
    )
    speeds = sorted((math.hypot(ax, ay), math.hypot(bx, by)))
    ratio = speeds[1] / speeds[0]
    same = turn <= SAME_DIRECTION_TURN and ratio <= MAX_SPEED_RATIO
    return PairAssessment(
        "same_direction" if same else "different_direction",
        boundary,
        round(math.degrees(turn), 1),
        round(ratio, 2),
        f"Outgoing {outgoing.direction}, incoming {incoming.direction}. {caveat}",
    )
