# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded, deterministic shortlists of possibly matching subjects.

Input is caller-issued, already-validated visual evidence (one record per
sampled window). The module reads no caches, decodes no media and calls no
provider or tracker. Text and role evidence can only suggest that two shots may
show the same kind of subject: it never establishes shared landmarks, geometry,
perspective or a verified match. Every pair is returned as unverified for a
separately budgeted geometry check.
"""

import re
from dataclasses import dataclass
from typing import Callable

from .contracts import FrameRate

VERSION = "shot-discovery-1"
ROLES = frozenset({"establishing", "detail", "action", "closing", "other"})
MIN_QUALITY = 50  # Same usable-window floor as planning.MIN_QUALITY.
MAX_DESCRIPTION = 600
MAX_ID = 128

# Object-like subjects that can anchor a graphic match. Backgrounds such as sky,
# water, sand, grass, roads or "street" are deliberately absent: a shared
# background is not a matchable subject. Earlier entries are more specific.
SUBJECTS: tuple[tuple[str, frozenset[str]], ...] = (
    (
        "wheel",
        frozenset(
            {
                "wheel",
                "wheels",
                "tyre",
                "tyres",
                "tire",
                "tires",
                "rim",
                "rims",
                "hubcap",
                "hubcaps",
            }
        ),
    ),
    (
        "car front",
        frozenset({"headlight", "headlights", "grille", "grill", "bumper", "bonnet"}),
    ),
    (
        "car",
        frozenset(
            {
                "car",
                "cars",
                "vehicle",
                "vehicles",
                "suv",
                "sedan",
                "jeep",
                "truck",
                "trucks",
                "hatchback",
                "coupe",
                "convertible",
            }
        ),
    ),
    ("face", frozenset({"face", "faces", "portrait"})),
    ("hand", frozenset({"hand", "hands"})),
    ("cup", frozenset({"cup", "cups", "mug", "mugs"})),
    ("bicycle", frozenset({"bicycle", "bicycles", "bike", "bikes"})),
    (
        "boat",
        frozenset(
            {
                "boat",
                "boats",
                "ship",
                "ships",
                "ferry",
                "kayak",
                "kayaks",
                "canoe",
                "canoes",
                "yacht",
                "yachts",
                "sailboat",
            }
        ),
    ),
    ("door", frozenset({"door", "doors", "doorway", "gate", "gates"})),
    ("window", frozenset({"window", "windows"})),
    ("arch", frozenset({"arch", "arches", "archway"})),
    ("sign", frozenset({"sign", "signs", "signpost", "billboard"})),
    ("lamp", frozenset({"lamp", "lamps", "lantern", "lanterns", "streetlight"})),
    ("statue", frozenset({"statue", "statues", "sculpture"})),
    ("ball", frozenset({"ball", "balls"})),
    ("sun", frozenset({"sun", "sunset", "sunrise"})),
    (
        "person",
        frozenset(
            {
                "person",
                "people",
                "man",
                "men",
                "woman",
                "women",
                "couple",
                "friends",
                "child",
                "children",
                "boy",
                "girl",
                "traveler",
                "travelers",
                "traveller",
                "travellers",
                "tourist",
                "tourists",
                "hiker",
                "hikers",
            }
        ),
    ),
)
NEGATIONS = frozenset({"no", "not", "without", "none", "absent", "lacking", "nobody"})
NEGATION_REACH = 3  # Tokens before a term that can negate it ("no visible people").
# Coarse shot scale from the planner's roles. A detail shot and a wide
# establishing/closing view rarely show a subject at a compatible size.
SCALE = {"detail": "close", "establishing": "wide", "closing": "wide"}


class DiscoveryError(ValueError):
    """Malformed or excessive caller input; nothing is partially returned."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class Limits:
    """Work ceilings. Callers may lower them; values above the defaults are refused."""

    windows: int = 24
    groups: int = 6
    candidates_per_group: int = 6
    pairs: int = 24


DEFAULT_LIMITS = Limits()


@dataclass(frozen=True)
class EvidenceWindow:
    """One validated usable window, in absolute source frames at the project rate.

    `source_in`/`duration_frames` are the usable range after trims. `content_id`
    is the caller's content identity (for example the source SHA-256); None means
    unknown, never "same as another unknown source".
    """

    source_id: str
    window_id: int
    source_in: int
    duration_frames: int
    description: str
    role: str
    quality: int
    content_id: str | None = None


@dataclass(frozen=True)
class WindowRef:
    source_id: str
    window_id: int
    source_in: int
    duration_frames: int


@dataclass(frozen=True)
class SubjectGroup:
    subject: str
    members: tuple[WindowRef, ...]
    terms: tuple[str, ...]


@dataclass(frozen=True)
class PairSuggestion:
    """Two windows that MAY share a subject. Never a verified match."""

    subject: str
    first: WindowRef
    second: WindowRef
    score: int
    reason: str
    verified: bool = False


@dataclass(frozen=True)
class Exclusion:
    source_id: str
    window_id: int | None
    reason: str


@dataclass(frozen=True)
class Discovery:
    version: str
    groups: tuple[SubjectGroup, ...]
    pairs: tuple[PairSuggestion, ...]
    exclusions: tuple[Exclusion, ...]
    empty_reason: str | None
    truncated: bool


def _native_key(value: str) -> tuple:
    # Native IDs are usually decimal; order them numerically, then textually.
    return (0, int(value), value) if value.isdigit() else (1, 0, value)


def _window_key(w: EvidenceWindow) -> tuple:
    return (_native_key(w.source_id), w.window_id, w.source_in)


def _ref(w: EvidenceWindow) -> WindowRef:
    return WindowRef(w.source_id, w.window_id, w.source_in, w.duration_frames)


def _text(value: object, limit: int) -> bool:
    return type(value) is str and bool(value.strip()) and len(value) <= limit


def _validate(windows: object, limits: object) -> tuple[list[EvidenceWindow], Limits]:
    if type(limits) is not Limits or any(
        type(getattr(limits, f)) is not int
        or not 1 <= getattr(limits, f) <= getattr(DEFAULT_LIMITS, f)
        for f in ("windows", "groups", "candidates_per_group", "pairs")
    ):
        raise DiscoveryError("invalid_limits")
    if type(windows) not in (list, tuple):
        raise DiscoveryError("invalid_evidence")
    if len(windows) > limits.windows:
        raise DiscoveryError("too_many_windows")
    seen = set()
    for w in windows:
        if (
            type(w) is not EvidenceWindow
            or not _text(w.source_id, MAX_ID)
            or type(w.window_id) is not int
            or w.window_id < 0
            or type(w.source_in) is not int
            or w.source_in < 0
            or type(w.duration_frames) is not int
            or w.duration_frames < 1
            or not _text(w.description, MAX_DESCRIPTION)
            or type(w.role) is not str
            or w.role not in ROLES
            or type(w.quality) is not int
            or not 0 <= w.quality <= 100
            or not (w.content_id is None or _text(w.content_id, MAX_ID))
        ):
            raise DiscoveryError("invalid_evidence")
        if (w.source_id, w.window_id) in seen:
            raise DiscoveryError("duplicate_window")
        seen.add((w.source_id, w.window_id))
    return list(windows), limits


def subject_terms(description: str) -> dict[str, tuple[str, ...]]:
    """Subjects mentioned without a nearby negation, with the words that named them."""
    tokens = re.findall(r"[a-z]+", description.lower())
    found: dict[str, list[str]] = {}
    for i, token in enumerate(tokens):
        if any(t in NEGATIONS for t in tokens[max(0, i - NEGATION_REACH) : i]):
            continue
        for subject, words in SUBJECTS:
            if token in words and token not in found.get(subject, []):
                found.setdefault(subject, []).append(token)
    return {s: tuple(t) for s, t in found.items()}


def _identity(w: EvidenceWindow) -> tuple[str, str]:
    # Unknown identity stays tied to its own bin; it never merges with another.
    return ("content", w.content_id) if w.content_id else ("source", w.source_id)


def _overlaps(a: EvidenceWindow, b: EvidenceWindow) -> bool:
    return (
        a.source_in < b.source_in + b.duration_frames
        and b.source_in < a.source_in + a.duration_frames
    )


def discover(
    windows: list[EvidenceWindow],
    rate: FrameRate,
    *,
    limits: Limits = DEFAULT_LIMITS,
    check: Callable[[], None] = lambda: None,
) -> Discovery:
    """Suggest possibly matching subject pairs from cached window evidence.

    Deterministic for a given input set regardless of input order. `check` is
    the caller's cancellation hook (for example MediaTools.check); it is called
    inside every bounded loop and may raise to stop the work.
    """
    if type(rate) is not FrameRate:
        raise DiscoveryError("invalid_rate")
    windows, limits = _validate(windows, limits)
    exclusions: list[Exclusion] = []
    one_second = rate.seconds_to_frame("1")
    eligible = []
    for w in sorted(windows, key=_window_key):
        check()
        if w.quality < MIN_QUALITY:
            exclusions.append(Exclusion(w.source_id, w.window_id, "low_quality"))
        elif w.duration_frames < one_second:
            exclusions.append(Exclusion(w.source_id, w.window_id, "too_short"))
        else:
            eligible.append(w)

    # Identical content imported twice (same identity, overlapping range) is one
    # shot. Keep the best-quality copy, then the first by native order.
    kept: list[EvidenceWindow] = []
    for w in sorted(eligible, key=lambda w: (-w.quality, *_window_key(w))):
        check()
        twin = next(
            (
                k
                for k in kept
                if w.content_id and k.content_id == w.content_id and _overlaps(k, w)
            ),
            None,
        )
        if twin:
            exclusions.append(Exclusion(w.source_id, w.window_id, "duplicate_content"))
        else:
            kept.append(w)

    mentions: dict[str, list[tuple[EvidenceWindow, tuple[str, ...]]]] = {}
    for w in sorted(kept, key=_window_key):
        check()
        terms = subject_terms(w.description)
        if not terms:
            exclusions.append(
                Exclusion(w.source_id, w.window_id, "no_matchable_subject")
            )
        for subject, words in terms.items():
            mentions.setdefault(subject, []).append((w, words))

    order = {name: i for i, (name, _) in enumerate(SUBJECTS)}
    groups, truncated = [], False
    ranked = sorted(
        mentions.items(),
        key=lambda item: (-len({_identity(w) for w, _ in item[1]}), order[item[0]]),
    )
    for subject, members in ranked:
        check()
        if len({_identity(w) for w, _ in members}) < 2:
            continue  # One source cannot suggest a cross-shot match.
        if len(groups) == limits.groups:
            truncated = True
            exclusions.append(Exclusion("*", None, f"group_limit:{subject}"))
            continue
        members = sorted(members, key=lambda m: (-m[0].quality, *_window_key(m[0])))
        for w, _ in members[limits.candidates_per_group :]:
            truncated = True
            exclusions.append(
                Exclusion(w.source_id, w.window_id, f"candidate_limit:{subject}")
            )
        groups.append((subject, members[: limits.candidates_per_group]))

    pairs: list[tuple[tuple, PairSuggestion]] = []
    claimed: set[tuple] = set()
    # Groups are ranked by breadth then specificity; a pair is suggested once,
    # under the first (most specific among equally broad) subject naming it.
    for rank, (subject, members) in enumerate(groups):
        for i, (a, words_a) in enumerate(members):
            for b, words_b in members[i + 1 :]:
                check()
                if _identity(a) == _identity(b) or (
                    a.source_id == b.source_id and _overlaps(a, b)
                ):
                    continue  # Same source or overlapping range: not a cut between shots.
                scale_a, scale_b = SCALE.get(a.role), SCALE.get(b.role)
                if scale_a and scale_b and scale_a != scale_b:
                    continue  # Close detail against a wide view: sizes rarely compatible.
                first, second = sorted((a, b), key=_window_key)
                key = (_window_key(first), _window_key(second))
                if key in claimed:
                    continue
                claimed.add(key)
                score = a.quality + b.quality + (20 if a.role == b.role else 0)
                words = sorted(set(words_a) | set(words_b))
                reason = (
                    f"Both descriptions mention {subject} ({', '.join(words)}); roles "
                    f"{first.role}/{second.role}. Text evidence only: landmarks, "
                    "geometry and perspective are unverified."
                )
                pairs.append(
                    (
                        (rank, -score, _window_key(first), _window_key(second)),
                        PairSuggestion(
                            subject, _ref(first), _ref(second), score, reason
                        ),
                    )
                )
    pairs.sort(key=lambda p: p[0])
    if len(pairs) > limits.pairs:
        truncated = True
    chosen = tuple(p for _, p in pairs[: limits.pairs])
    empty = None
    if not chosen:
        empty = (
            "no_eligible_windows"
            if not kept
            else "no_shared_subject"
            if not groups
            else "no_compatible_pair"
        )
    return Discovery(
        VERSION,
        tuple(
            SubjectGroup(
                s,
                tuple(_ref(w) for w, _ in m),
                tuple(sorted({t for _, words in m for t in words})),
            )
            for s, m in groups
        ),
        chosen,
        tuple(
            sorted(
                exclusions,
                key=lambda e: (
                    _native_key(e.source_id),
                    -1 if e.window_id is None else e.window_id,
                    e.reason,
                ),
            )
        ),
        empty,
        truncated,
    )
