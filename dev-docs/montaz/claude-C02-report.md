<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# C02 — Local movement and occlusion evidence (delivered, pending review)

Claude, October 2, 2026. Base `cbf834b`, branch `codex/claude-motion-evidence`.
The checklist item stays open until Codex reviews and integrates the branch.

## Files

New files only:

- `assistant/src/montaz_assistant/motion_evidence.py` (standard library only)
- `assistant/tests/test_motion_evidence.py`
- `assistant/tools/check_motion_evidence.py` (private QA)
- this report

No decoder, worker, native or existing-module changes.

## Interface

```python
from montaz_assistant.motion_evidence import Frame, Window, analyse, assess_pair

window = Window(width=96, height=54, frames=(Frame(position=120, pixels=gray_bytes), ...))
evidence = analyse(window, check=media.check)   # MotionEvidence
pair = assess_pair(outgoing_evidence, incoming_evidence)   # PairAssessment
```

**Input.**

- **Size:** 32–96 pixels wide and high. The 32-pixel floor keeps a search interior
  at half resolution.
- **Frames:** 2–24 `Frame`s at strictly increasing integer project positions.
  Sampled frames may be sparse.
- **Pixels:** grayscale, either `bytes` or a list of numbers in the range 0–255.
- **Rejections:** bad shapes, `bool`s, NaN or infinity, out-of-range values, wrong
  lengths and unordered positions raise `MotionInputError(code)`.

**`MotionEvidence` fields.**

| Field | Meaning |
| --- | --- |
| `steps` | per sampled interval: `measured`, `textureless`, `ambiguous` or `unmatched`, with content velocity in px per project frame, texture, ambiguity ratio and residual |
| `direction` | `static`, an 8-way label such as `left` or `up-right`, `inconsistent` or `unavailable` |
| `velocity` | median content motion |
| `consistency` | fraction of measured steps that agree |
| `fade` | `(start, end)` of a uniform darkening with structure kept, or `None` |
| `abrupt_change` | `(before, after)` of a one-step whole-picture change, or `None` |
| `local_motion` | an independently moving foreground was seen |
| `occlusions` | `Occlusion` candidates: `kind` `partial` or `complete`, start, end, peak position and coverage, `textured_cover`, a note, and `verified=False` always |
| `notes` | plain explanations of what was or wasn't concluded |
| `limits` | the fixed limits used, as listed below |

Direction is the way the **content** moves. A camera panning right makes the
content move left, but a pan and a moving foreground are not distinguished.

**`assess_pair()`** returns `same_direction`, `different_direction` or `unavailable`.
It includes the boundary frames, the turn in degrees and the speed ratio, and its
note always carries the pan-versus-foreground caveat. The thresholds are a turn of
at most 30° and a speed ratio of at most 2. The result is evidence, never an
instruction to apply a transition.

## Method and fixed limits

**Translation.**

- **Search:** an exhaustive search over a fixed 8-pixel radius. It runs at half
  resolution (±4, which covers 8 full-resolution pixels), then refines at full
  resolution (±1) with sub-pixel parabolic interpolation on each axis.
- **Cost:** mean absolute difference on mean-removed frames over a fixed interior.
- **Textureless:** mean absolute gradient below 2 grey levels.
- **Ambiguous:** best cost over second-best (at least 2 coarse pixels away) above
  0.85.
- **Unmatched:** residual over texture above 0.75.
- **Static:** below 0.5 px per frame.
- **Direction:** the median of measured steps. It needs at least half the steps
  measured, and is `inconsistent` if fewer than 75% agree within 45°.

**Coverage.** A 4×4 block grid is compared against the first frame, which is
motion-compensated by the cumulative measured translation. Each block allows a ±2
px registration search, because integer compensation drifts by a pixel or two.

- **Covered block:** it loses texture (standard deviation below 4) or its
  correlation with the reference falls below 0.6.
- **Fade:** at least 65% overall darkening, uniform across blocks (ratio spread at
  most 0.12), with the remaining texture still correlated. Fades are never
  occlusions.
- **Abrupt change:** coverage jumps from under 20% to 90% or more in one step,
  *and* that step's content actually changed (unmatched or ambiguous). A cleanly
  measured step is never called a cut.
- **Complete candidate:** reaches 90% coverage, appeared first at the frame edge,
  and keeps what it first covered while spreading. It may move on afterwards, as a
  passing wipe does.
- **Partial candidate:** at least 20% coverage, appeared first at the edge, keeps
  what it first covered to the end of the window, and stays within one half of the
  frame on some axis.
- **Independent foreground:** a cover that appears mid-frame or uncovers earlier
  blocks.
- **Zoom or dolly:** edge decorrelation in every quadrant gets a note saying it is
  consistent with a zoom or dolly, and no occlusion is reported.

**Noise.** If 75% or more of steps are ambiguous, the window is incoherent and no
fade, cut or occlusion is claimed.

**Cancellation.** `check()` runs once per step, once per coarse search row (9 per
step) and once per frame for coverage. A 96×96, 24-frame window makes exactly 254
calls (tested).

## Validation

| Check | Result |
| --- | --- |
| `test_motion_evidence` | 17 tests pass in about 8 s |
| Full suite | 242 run, 232 pass, 10 skipped |
| Ruff on all three files | clean |

The 10 skips are the opt-in native-tracker checks that need `MONTAZ_TRACKER`.
There is no native code here, so no native validation is claimed.

The unit fixtures are pure Python, built from a blurred random texture:

- static texture;
- translation left, right, up, down and up-right, measured to within 0.1 px per
  frame;
- sparse 2-frame sampling, giving −1.5 px per frame;
- an independent foreground appearing mid-frame;
- an object entering from the edge and passing through;
- noise;
- a fade to black;
- a partial flat cover from the left;
- complete textured and flat covers;
- a wipe that covers everything and then moves on;
- a zoom (no occlusion, with the note);
- an abrupt cut;
- a textureless window (no claims);
- pair assessment: same direction, opposite direction, static, and a speed ratio
  of 3.5;
- invalid inputs and cancellation;
- the largest window, 96×96 by 24 frames at 8 px per frame, measured exactly at
  the search radius.

**Decoded QA** (private, outside Git; `check_motion_evidence.py`): FFmpeg x264
fixtures at 24 fps, decoded through `MediaTools` at 96×54. Each took 0.3–1.5 s to
decode and 0.7–2.3 s to analyse.

| Fixture | Result |
| --- | --- |
| static | static, no claims ✓ |
| pan-left | left, −1.501 px per frame (truth −1.50) ✓ |
| pan-up (camera tilts up) | content down, 0.85 px per frame (truth 0.75: **+13% sub-pixel bias**) ✓ direction |
| fade-out | fade, no occlusion ✓ |
| dark wipe | complete candidate, textureless cover, unverified ✓ |
| partial cover (47%) | partial candidate at 0.5 coverage ✓ (`textured_cover` true: edge blocks are mixed) |
| passing object | local foreground motion, no occlusion ✓ |
| cut | abrupt change ✓ |

Real stock clips (Pexels car and wheel footage), 8 windows of 24 frames each,
every other frame:

- **Seven tripod shots:** static, with no claims.
- **The tyre clip:** −0.36 px per frame of slow drift (below the static floor) plus
  local foreground motion. That matches the slight camera move seen earlier.
- **The orange car:** this dollies back and up. An earlier version reported a false
  partial occlusion here; that led to the zoom rule, and the window now carries the
  zoom/dolly note with no occlusion.

None of these clips contains a real pan or occluder, so **real positive cases are
not yet validated**.

Artifacts are in `~/Movies/montaz-claude-motion-evidence/{synthetic,real}/`:
`motion-evidence-report.json` plus the generated fixtures.

## Limitations

- **Translation only.** Zoom, rotation, perspective and parallax are not modelled.
  A zoom is only detected indirectly, through the all-quadrant note, and a zooming
  shot reports a near-zero velocity.
- **Measurement limits.**
  - **Speed:** at most 8 analysis px per sampled step, so faster pans become
    ambiguous or unmatched.
  - **Accuracy:** sub-pixel estimates showed up to 13% bias on compressed video.
  - **Periodic texture:** this may be marked ambiguous, which is the correct,
    conservative result.
- **Coarse coverage.** Coverage uses a 4×4 grid, so small occluders (under about one
  block) and thin edges are missed. A partial cover's `textured_cover` reflects
  mixed edge blocks.
- **Nothing is verified.** No occlusion is ever established: a dark object, a lens
  cover and exposure loss all look the same at a textureless peak.
- **No real positives yet.** T01 and T02 still need real panning, whip and occlusion
  footage, positive and negative, and the native transition contract (Codex).
- **QA decoding is simple.** The QA tool decodes by frame index (`trim`), so long
  clips with late windows decode slowly. Runtime decoding belongs to Codex's
  integration.
