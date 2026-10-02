# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Versioned travel recipe data: story slots, pacing, music arc and transitions.

Recipes are preferences, not permission. They never authorize an unimplemented
transition, shorter shots than the planner allows, repeated sources or invented
footage. `resolve_transitions` maps preferences onto a caller-supplied allowlist
of implemented features; `plan_slots` turns a recipe, a requested duration and
the caller's footage inventory into frame ranges with honest explanations. No
planner, prompt, UI or media analysis is wired here.
"""

import math
from dataclasses import dataclass
from fractions import Fraction
from typing import Mapping

from .contracts import FrameRate

VERSION = "travel-recipes-1"
SLOTS = ("opening", "development", "detail_or_action", "ending")
ROLES = frozenset({"establishing", "detail", "action", "closing", "other"})
# Every transition a recipe may name. Only "hard_cut" is always available.
TRANSITIONS = frozenset(
    {
        "hard_cut",
        "beat_cut",
        "match_cut",
        "dissolve",
        "whip_pan",
        "object_wipe",
        "speed_ramp",
    }
)
ALWAYS_AVAILABLE = "hard_cut"
MAX_REQUEST_SECONDS = 600  # The brief's existing upper bound.


class RecipeError(ValueError):
    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class Slot:
    name: str
    share: tuple[Fraction, Fraction]  # Fraction of the edit's duration, inclusive.
    roles: tuple[str, ...]  # Planner roles in preference order.
    transitions_in: tuple[str, ...]  # Preferred cut into this slot, best first.
    missing: str  # Honest explanation when no footage fits the slot.


@dataclass(frozen=True)
class MusicSection:
    slot: str
    energy: int  # 1 (sparse) to 5 (driving); a preference for music prompts.
    description: str


@dataclass(frozen=True)
class Recipe:
    name: str
    version: int
    story: str
    shot_seconds: tuple[int, int]  # Same inclusive bounds the planner validates.
    distinct_sources: bool  # Each source used at most once; never relaxed here.
    slots: tuple[Slot, ...]
    music: tuple[MusicSection, ...]
    beat_cuts: str  # prefer | allow | avoid

    @property
    def id(self) -> str:
        return f"{self.name}@{self.version}"


def _share(low: int, high: int) -> tuple[Fraction, Fraction]:
    return Fraction(low, 100), Fraction(high, 100)


RECIPES: dict[str, Recipe] = {
    "postcard": Recipe(
        "postcard",
        1,
        "Establish the place, explore details, end on a memorable view.",
        (2, 4),
        True,
        (
            Slot(
                "opening",
                _share(15, 25),
                ("establishing",),
                ("hard_cut",),
                "No establishing view was observed; the edit opens on its strongest development shot.",
            ),
            Slot(
                "development",
                _share(30, 50),
                ("establishing", "other", "action"),
                ("match_cut", "dissolve", "hard_cut"),
                "Too few distinct views to develop the place; the edit stays short instead of repeating shots.",
            ),
            Slot(
                "detail_or_action",
                _share(15, 35),
                ("detail", "action"),
                ("match_cut", "hard_cut"),
                "No detail or activity was observed; wider views carry the middle instead.",
            ),
            Slot(
                "ending",
                _share(10, 25),
                ("closing", "establishing"),
                ("dissolve", "hard_cut"),
                "No settled closing view was observed; the edit ends on its last shot rather than inventing an ending.",
            ),
        ),
        (
            MusicSection("opening", 2, "Warm, open introduction."),
            MusicSection("development", 3, "Steady pulse while exploring."),
            MusicSection("detail_or_action", 3, "Light, playful movement."),
            MusicSection("ending", 2, "Gentle lift that resolves."),
        ),
        "allow",
    ),
    "adventure": Recipe(
        "adventure",
        1,
        "Build energy from setup through action to a satisfying finish.",
        (1, 3),
        True,
        (
            Slot(
                "opening",
                _share(10, 20),
                ("establishing", "other"),
                ("hard_cut",),
                "No setup view was observed; the edit starts directly on movement.",
            ),
            Slot(
                "development",
                _share(25, 40),
                ("other", "establishing", "action"),
                ("beat_cut", "whip_pan", "hard_cut"),
                "Too few distinct shots to build energy; the edit stays short instead of repeating shots.",
            ),
            Slot(
                "detail_or_action",
                _share(30, 50),
                ("action", "detail"),
                ("beat_cut", "speed_ramp", "whip_pan", "hard_cut"),
                "No action was observed; the peak uses the most dynamic available shots and stays honest about it.",
            ),
            Slot(
                "ending",
                _share(10, 20),
                ("closing", "establishing"),
                ("match_cut", "hard_cut"),
                "No settled finishing view was observed; the edit ends on its last action shot.",
            ),
        ),
        (
            MusicSection("opening", 2, "Anticipation with a clear pulse."),
            MusicSection("development", 4, "Rising drive."),
            MusicSection("detail_or_action", 5, "Peak energy."),
            MusicSection("ending", 3, "Release to a confident finish."),
        ),
        "prefer",
    ),
    "diary": Recipe(
        "diary",
        1,
        "Use calmer establishing shots and details with room to breathe.",
        (3, 6),
        True,
        (
            Slot(
                "opening",
                _share(15, 30),
                ("establishing",),
                ("dissolve", "hard_cut"),
                "No calm establishing view was observed; the edit opens on its quietest available shot.",
            ),
            Slot(
                "development",
                _share(25, 45),
                ("establishing", "other", "detail"),
                ("dissolve", "hard_cut"),
                "Too few distinct moments to develop the day; the edit stays short instead of repeating shots.",
            ),
            Slot(
                "detail_or_action",
                _share(15, 35),
                ("detail", "other"),
                ("match_cut", "dissolve", "hard_cut"),
                "No close details were observed; the middle stays on wider moments.",
            ),
            Slot(
                "ending",
                _share(15, 30),
                ("closing", "establishing"),
                ("dissolve", "hard_cut"),
                "No settled closing view was observed; the edit ends on its calmest remaining shot.",
            ),
        ),
        (
            MusicSection("opening", 1, "Sparse and reflective."),
            MusicSection("development", 2, "Soft, unhurried texture."),
            MusicSection("detail_or_action", 2, "Intimate and light."),
            MusicSection("ending", 1, "Quiet resolution."),
        ),
        "avoid",
    ),
}


def recipe(name: str) -> Recipe:
    """A travel recipe by name. The quick match montage is a separate mode, not a recipe."""
    if type(name) is not str or name not in RECIPES:
        raise RecipeError("unknown_recipe")
    return RECIPES[name]


def validate(r: Recipe) -> None:
    """Deterministic structural checks for a recipe record."""
    low, high = r.shot_seconds
    if (
        type(r.version) is not int
        or r.version < 1
        or not 1 <= low <= high
        or r.distinct_sources is not True
        or tuple(s.name for s in r.slots) != SLOTS
        or tuple(m.slot for m in r.music) != SLOTS
        or r.beat_cuts not in ("prefer", "allow", "avoid")
    ):
        raise RecipeError("invalid_recipe")
    if not sum(s.share[0] for s in r.slots) <= 1 <= sum(s.share[1] for s in r.slots):
        raise RecipeError("invalid_recipe")
    for s in r.slots:
        if (
            not 0 < s.share[0] <= s.share[1] < 1
            or not s.roles
            or not set(s.roles) <= ROLES
            or not s.transitions_in
            or not set(s.transitions_in) <= TRANSITIONS
            or s.transitions_in[-1] != ALWAYS_AVAILABLE
            or not s.missing
        ):
            raise RecipeError("invalid_recipe")
    if any(not 1 <= m.energy <= 5 or not m.description for m in r.music):
        raise RecipeError("invalid_recipe")


@dataclass(frozen=True)
class ResolvedTransition:
    slot: str
    preferred: str
    chosen: str
    note: str | None  # Why a preference was not used.


def resolve_transitions(
    r: Recipe, implemented: frozenset[str] | set[str]
) -> tuple[ResolvedTransition, ...]:
    """Choose the first implemented preference for the cut into each slot.

    `implemented` is the caller's allowlist of transitions that the editor can
    actually produce and review. Anything else falls back, ending at a hard cut,
    with an explanation. The opening has no incoming cut but is resolved for
    symmetry (always a hard cut in the current recipes).
    """
    if type(implemented) not in (set, frozenset) or not set(implemented) <= TRANSITIONS:
        raise RecipeError("invalid_allowlist")
    available = set(implemented) | {ALWAYS_AVAILABLE}
    result = []
    for s in r.slots:
        chosen = next(t for t in s.transitions_in if t in available)
        skipped = s.transitions_in[: s.transitions_in.index(chosen)]
        note = None
        if skipped:
            note = (
                f"{', '.join(t.replace('_', ' ') for t in skipped)} "
                f"{'is' if len(skipped) == 1 else 'are'} not implemented; using a "
                f"{chosen.replace('_', ' ')} instead."
            )
        result.append(ResolvedTransition(s.name, s.transitions_in[0], chosen, note))
    return tuple(result)


@dataclass(frozen=True)
class SlotPlan:
    slot: str
    frames: tuple[int, int]  # Inclusive target range in project frames.
    roles: tuple[str, ...]
    available_sources: int  # Distinct usable sources with a preferred role.
    note: str | None  # Honest fallback explanation, if any.


@dataclass(frozen=True)
class RecipePlan:
    recipe: str  # name@version
    requested_frames: int
    achievable_frames: int  # Upper bound without repeating footage.
    shot_frames: tuple[int, int]
    max_shots: int
    feasible: bool
    slots: tuple[SlotPlan, ...]
    explanation: str


def plan_slots(
    r: Recipe,
    rate: FrameRate,
    requested_seconds: int,
    sources_by_role: Mapping[str, int],
) -> RecipePlan:
    """Slot frame ranges for a request, given distinct usable sources per role.

    `sources_by_role` counts the caller's distinct usable sources whose best
    window has each planner role (a source counts once). Requests that cannot be
    met with distinct sources and the recipe's shot bounds are reported, never
    padded: the plan says how long an honest edit can be and which story slots
    have no fitting footage.
    """
    if type(rate) is not FrameRate:
        raise RecipeError("invalid_rate")
    if (
        type(requested_seconds) is not int
        or not 1 <= requested_seconds <= MAX_REQUEST_SECONDS
    ):
        raise RecipeError("invalid_duration")
    if (
        not isinstance(sources_by_role, Mapping)
        or not set(sources_by_role) <= ROLES
        or any(type(v) is not int or v < 0 for v in sources_by_role.values())
    ):
        raise RecipeError("invalid_inventory")
    validate(r)
    requested = rate.seconds_to_frame(str(requested_seconds))
    low, high = (rate.seconds_to_frame(str(s)) for s in r.shot_seconds)
    sources = sum(sources_by_role.values())
    max_shots = min(sources, requested // low)
    achievable = min(requested, sources * high)
    if not sources:
        return RecipePlan(
            r.id,
            requested,
            0,
            (low, high),
            0,
            False,
            (),
            "No usable footage was observed; nothing can be planned.",
        )
    if requested < low:
        return RecipePlan(
            r.id,
            requested,
            0,
            (low, high),
            0,
            False,
            (),
            f"{requested_seconds} s is shorter than this recipe's {r.shot_seconds[0]} s minimum shot.",
        )
    # Opening and ending each need one fitting source, so they are allocated
    # first; the middle slots then share what remains. Each source counts once.
    remaining, counts = dict(sources_by_role), {}
    for name in ("opening", "ending", "detail_or_action", "development"):
        s = next(x for x in r.slots if x.name == name)
        count = 0
        for role in s.roles:
            take = remaining.get(role, 0)
            if name in ("opening", "ending"):
                take = min(take, 1 - count)
            count += take
            remaining[role] = remaining.get(role, 0) - take
        counts[name] = count
    slots, notes = [], []
    for s in r.slots:
        frames = (
            math.floor(s.share[0] * achievable),
            math.ceil(s.share[1] * achievable),
        )
        note = None if counts[s.name] else s.missing
        if note:
            notes.append(note)
        slots.append(SlotPlan(s.name, frames, s.roles, counts[s.name], note))
    if max_shots < len(SLOTS):
        notes.insert(
            0,
            f"Room for at most {max_shots} shot{'s' * (max_shots != 1)} of "
            f"{r.shot_seconds[0]}-{r.shot_seconds[1]} s: the story slots collapse rather "
            "than using shorter shots.",
        )
    if achievable < requested:
        notes.insert(
            0,
            f"{sources} distinct source{'s' * (sources != 1)} allow at most "
            f"{achievable / Fraction(rate.numerator, rate.denominator):.1f} s without "
            "repeating footage; the edit will be shorter than requested.",
        )
    return RecipePlan(
        r.id,
        requested,
        achievable,
        (low, high),
        max_shots,
        True,
        tuple(slots),
        " ".join(notes) if notes else "All story slots have fitting footage.",
    )
