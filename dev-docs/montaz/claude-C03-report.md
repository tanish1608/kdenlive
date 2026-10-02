<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# C03 — Versioned travel recipe data (delivered, pending review)

Claude, October 2, 2026. Base `cbf834b`, branch `codex/claude-travel-recipes`.
The checklist item stays open until Codex reviews and integrates the branch.

## Files

New files only:

- `assistant/src/montaz_assistant/travel_recipes.py`
- `assistant/tests/test_travel_recipes.py`
- this report

The module imports `contracts.FrameRate`. Planner, prompts, UI and native code are
untouched.

## Interface

```python
from montaz_assistant.travel_recipes import recipe, resolve_transitions, plan_slots

r = recipe("postcard")                     # Recipe, r.id == "postcard@1"
cuts = resolve_transitions(r, {"hard_cut", "beat_cut", "match_cut"})
plan = plan_slots(r, FrameRate(30000, 1001), 20,
                  {"establishing": 3, "other": 2, "detail": 2, "closing": 1})
```

**`Recipe`.**

| Field | Meaning |
| --- | --- |
| `name`, `version` | the recipe's identity; `id` is `name@version` |
| `story` | the planner's existing story line |
| `shot_seconds` | the planner's existing inclusive bounds |
| `distinct_sources` | always `True` |
| `slots` | `opening`, `development`, `detail_or_action` and `ending`, as described below |
| `music` | one `MusicSection(slot, energy 1–5, description)` per slot |
| `beat_cuts` | `prefer`, `allow` or `avoid` |

Each slot carries:

- an exact `Fraction` share range of the edit's duration;
- preferred planner roles;
- preferred cuts into the slot, best first and always ending in `hard_cut`;
- an honest explanation to use when no footage fits.

**`resolve_transitions(recipe, implemented)`** returns one
`ResolvedTransition(slot, preferred, chosen, note)` per slot. `implemented` is the
caller's allowlist of transitions the editor can actually produce and review. Any
unsupported preference falls through to the next, and finally to a hard cut, with
a note such as *"dissolve is not implemented; using a hard cut instead."* Unknown
transition names raise `RecipeError("invalid_allowlist")`.

**`plan_slots(recipe, rate, requested_seconds, sources_by_role)`** returns a
`RecipePlan`. The last argument counts distinct usable sources by their best
window's role.

| Field | Meaning |
| --- | --- |
| `requested_frames` | the request at the project rate (half-up rounding, as `FrameRate` does) |
| `achievable_frames` | the most that can be reached *without repeating footage* |
| `shot_frames` | the recipe's shot bounds in frames |
| `max_shots` | how many shots fit |
| `feasible` | whether anything can be planned |
| `slots` | per slot: exact frame range, roles, distinct sources allocated, missing-footage note |
| `explanation` | joined plain-language notes |

The opening and ending each take at most one fitting source and are allocated
first; the middle slots share what remains. Errors use `RecipeError(code)`, with
codes `unknown_recipe`, `invalid_rate`, `invalid_duration`, `invalid_inventory`,
`invalid_recipe` and `invalid_allowlist`.

## Recipe data (version 1)

| | postcard | adventure | diary |
| --- | --- | --- | --- |
| Shot seconds | 2–4 (planner) | 1–3 (planner) | 3–6 (planner) |
| Slot shares, % of the edit | 15–25 / 30–50 / 15–35 / 10–25 | 10–20 / 25–40 / 30–50 / 10–20 | 15–30 / 25–45 / 15–35 / 15–30 |
| Music energy by slot | 2 / 3 / 3 / 2 | 2 / 4 / **5** / 3 | 1 / 2 / 2 / 1 |
| Beat cuts | allow | prefer | avoid |
| Preferred cuts | match cut and dissolve in the middle and at the end | beat cut, whip pan and speed ramp into action | dissolves and matches |

All three keep distinct sources. The quick match montage is **not** a recipe:
`recipe("quick_match")` is refused, and no recipe allows shots under one second.

## Examples (30000/1001 FPS)

- **Postcard, 20 s, full inventory:** feasible. 599 frames; shot range 60–120
  frames; up to 8 shots from the 8 sources. "All story slots have fitting footage."
- **Postcard, 3 s:** one shot only. "Room for at most 1 shot of 2–4 s: the story
  slots collapse rather than using shorter shots." At **1 s** it is infeasible:
  "shorter than this recipe's 2 s minimum shot". Adventure at 1 s is feasible.
- **Adventure, 60 s, 5 sources (2 action, 3 other):** 450 achievable frames. "5
  distinct sources allow at most 15.0 s without repeating footage; the edit will be
  shorter than requested. No settled finishing view was observed; the edit ends on
  its last action shot."
- **Missing roles (postcard, only "other" footage):** opening, detail and ending
  each give their honest note, for example "…ends on its last shot rather than
  inventing an ending."
- **No footage:** infeasible, "No usable footage was observed; nothing can be
  planned."
- **Transitions with today's allowlist** (`hard_cut`, `beat_cut`, `match_cut`):
  - Diary's ending dissolve becomes a hard cut, with a note.
  - Adventure keeps beat cuts.
  - With nothing implemented, adventure's action slot explains "beat cut, speed
    ramp, whip pan are not implemented; using a hard cut instead."

## Validation

| Check | Result |
| --- | --- |
| `test_travel_recipes` | 10 tests pass |
| Full suite | 235 run, 225 pass, 10 skipped |
| Ruff on both files | clean |

The 10 skips are the opt-in native-tracker checks (`MONTAZ_TRACKER`). No native
code is involved.

The tests cover:

- **Planner agreement:** the catalog matches `planning.RECIPES` shot bounds and story
  exactly.
- **Recipe shape:** shares are consistent, and music arcs and beat preferences
  differ by recipe.
- **No hidden montage recipe.**
- **Transitions:** resolution against today's allowlist, an empty list, the full
  list, and invalid lists.
- **Exact frames:** at 30000/1001, including half-up rounding.
- **Short, impossible and non-padded requests.**
- **Missing-role and no-footage explanations.**
- **Determinism:** independent of inventory order.
- **Invalid input:** 9 bad-argument cases and 8 malformed recipe records.

## Limitations and integration notes for Codex (N06)

- **Shares are preferences.** Slot shares are not validated against the shots a
  planner actually picks. Integration should report when a draft departs from them.
- **Coarse inventory.** `sources_by_role` counts each source once, by role. A source
  with several good windows of different roles is counted under one role only;
  `planning.source_limits` is the authority for durations.
- **Notes are English.** Explanations are English data, and need localisation or
  wrapping before user display.
- **Not wired into the planner.** Recipes don't change prompts yet. Music energy
  values are preferences for prompt wording, not measured loudness.
- **Some transitions aren't implemented.** `dissolve`, `whip_pan`, `object_wipe` and
  `speed_ramp` are listed so preferences can be written down; they resolve to hard
  cuts until Codex implements and allowlists them (T01–T03).
