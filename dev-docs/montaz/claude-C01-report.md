<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# C01 — Cached-evidence shot discovery (delivered, pending review)

Claude, October 2, 2026. Base `cbf834b`, branch `codex/claude-shot-discovery`.
The checklist item stays open until Codex reviews and integrates the branch.

## Files

New files only:

- `assistant/src/montaz_assistant/shot_discovery.py`
- `assistant/tests/test_shot_discovery.py`
- this report

The module imports `contracts.FrameRate`; no existing module changes.

## Interface

```python
from montaz_assistant.shot_discovery import EvidenceWindow, Limits, discover

windows = [
    EvidenceWindow(source_id="3", window_id=1, source_in=180, duration_frames=150,
                   description="Red sports car wheel with a five-spoke rim",
                   role="detail", quality=82, content_id="<sha256>"),
    ...  # at most 24
]
result = discover(windows, FrameRate(30000, 1001), limits=Limits(), check=media.check)
for pair in result.pairs:  # PairSuggestion(subject, first, second, score, reason, verified=False)
    ...
```

**Input: `EvidenceWindow`**

| Field | Type and meaning |
| --- | --- |
| `source_id` | bin ID |
| `window_id` | integer, at least 0 |
| `source_in` | absolute source frame of the *usable* range |
| `duration_frames` | usable length, at least 1 |
| `description` | 1–600 characters |
| `role` | one of planning's five roles |
| `quality` | 0–100 |
| `content_id` | optional content identity; `None` means unknown |

From planning's cached evidence, `source_in = in_frame + trim_in` and
`duration_frames = trim_out - trim_in`. `rate` must be the project `FrameRate`.

**Output: `Discovery`**

| Field | Meaning |
| --- | --- |
| `version` | `"shot-discovery-1"` |
| `groups` | at most 6 `SubjectGroup(subject, members, terms)`, each with at most 6 `WindowRef`s |
| `pairs` | at most 24 `PairSuggestion`s, each with `verified=False` and a reason that states the evidence is text only |
| `exclusions` | `Exclusion(source_id, window_id, reason)` |
| `empty_reason` | `no_eligible_windows`, `no_shared_subject` or `no_compatible_pair`, or `None` |
| `truncated` | whether any ceiling cut the result |

The exclusion reasons are `low_quality`, `too_short`, `duplicate_content`,
`no_matchable_subject`, `candidate_limit:<subject>` and `group_limit:<subject>`.

Errors raise `DiscoveryError(code)` and nothing is returned partially. The codes
are `invalid_evidence`, `duplicate_window`, `too_many_windows`, `invalid_limits`
and `invalid_rate`. The `check()` callback is called inside every loop and may
raise to cancel.

## Rules (conservative by design)

**Eligibility.**

- **Floors:** windows need quality of at least 50 (planning's floor) and at least
  one second at the project rate.
- **Duplicates:** the same `content_id` with overlapping ranges counts as one shot.
  The best-quality copy is kept, ties going to the earliest native ID.
- **Unknown identity:** a missing `content_id` keeps the window tied to its own bin
  and never merges it with another.

**Subjects.** A fixed lexicon of object-like terms is matched on word boundaries:

- wheel, car front, car, face, hand, cup, bicycle, boat, door, window, arch, sign,
  lamp, statue, ball, sun and person.
- Backgrounds such as sky, sea, sand, grass, road and street are deliberately
  absent: a shared background is not a matchable subject.
- A term is ignored when "no", "not", "without", "none", "absent", "lacking" or
  "nobody" appears up to three words before it.

**Groups.** A group needs at least two distinct sources (by content identity, or
by bin when the identity is unknown). Groups are ranked by source count, then by
lexicon specificity.

**Pairs.**

- **Never paired:** the same source, or overlapping ranges.
- **Scale mismatch:** a `detail` window is never paired with an `establishing` or
  `closing` one, since the subject sizes rarely fit the zoom cap.
- **One suggestion per pair:** each pair appears once, under its most specific
  group.
- **Score:** quality A + quality B, plus 20 when the roles are equal. Ties break by
  numeric native ID (`"11" < "100"`), then window and source frame.

The output is identical for any input order (tested with shuffles).

## Validation

| Check | Result |
| --- | --- |
| `test_shot_discovery` | 11 tests pass |
| Full suite (`discover -s assistant/tests`) | 236 run, 226 pass, 10 skipped |
| Ruff on both files | clean |

The 10 skips are the opt-in native-tracker checks that need `MONTAZ_TRACKER`. This
package has no native code, so no native validation is claimed.

The tests cover:

- **Wheel/car example:** four wheel details form one group with all six pairs. Car
  fronts form their own group. No wheel detail is paired with a wide car front.
- **Unrelated travel scenes:** beach, mountain, market stall and temple roof give no
  pairs, `no_shared_subject`, and an exclusion per window.
- **Text rules:** negation, and background-only descriptions.
- **Same source:** two windows of the same source never pair.
- **Duplicates:** the lower-quality duplicate is excluded; the same identity at
  separate ranges still doesn't pair; unknown identities do pair.
- **Role scales:** detail↔action and wide↔action are allowed; detail↔wide is not.
- **Floors:** quality and one-second floors at 30000/1001 and 15 FPS.
- **Ceilings:** 25 windows are rejected. Six candidates give 15 pairs with 18
  `candidate_limit` exclusions. Seven subjects give 6 groups plus a `group_limit`.
  A lowered pair limit works, and limits above the defaults are refused.
- **Malformed input:** 11 invalid-field cases, duplicate windows, and wrong
  container, rate and record types.
- **Order:** stable output for shuffled input, with numeric ID ties.
- **Cancellation:** a `check()` that raises stops the work, and calls stay bounded.

**Measured budgets.** For 24 windows, `check()` runs at most once per window per
pre-pass, once per group, and once per candidate pair (at most 15 per group, 90
overall). Six groups of six candidates yield at most 90 raw pairs before the
24-pair cap. No I/O, provider or tracker calls are made.

## Limitations and integration notes for Codex

- **Text matching, nothing more.** Suggestions are lexical. Synonyms outside the
  lexicon, other languages, or descriptions naming a subject only implicitly are
  missed. Common words like "sun" or "person" can group visually incompatible shots.
- **No geometry.** Nothing here establishes landmarks, geometry, perspective, or
  whether two subjects fit the zoom cap. Every pair must go through the landmark
  matcher under a total request budget (N02) before it is shown as a match.
- **Pairs are unordered.** `first`/`second` follow native ID order, not timeline
  order. Proposing an order or reordering clips stays with Codex's native proposal.
- **Planning inputs.** Planning's evidence dicts map directly to `EvidenceWindow`
  as described above; `content_id` is already present there.
