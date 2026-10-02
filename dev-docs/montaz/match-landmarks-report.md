<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Landmark match alignment and sturdier Gemini answers

Claude, October 2, 2026. Branch `codex/claude-match-landmarks`, from integration
commit `a5bfe52`. **Historical branch report.** Codex subsequently reviewed and
integrated it with the newer native tracker; see [combined integration and desktop
evidence](landmark-integration.md). The measurements below describe Claude's
original branch, not the combined implementation. Items 2 and 3 (shot grouping and a quick
match-cut mode) are handed to Codex in [handoff-match-sequencing.md](handoff-match-sequencing.md).

## Why

Review of real stock footage (10 Pexels car and wheel clips) showed that the
earlier "matched" cuts lined up a single small box chosen by Gemini, such as a side
mirror or a wheel hub, and scaled by that box's height. The mirrors lined up while
the cars jumped about 8% sideways. The hubs lined up while the wheels jumped about
1.6× in size. The user judged them not good enough.

## What changed (`match_cuts.py`, plus an additive option in `gemini.py`)

**1 — Landmark (anchor) alignment.** Each Gemini candidate is now a *subject*
(the whole car front, or the whole wheel) plus up to three *anchors* visible on it
in both images, such as both headlights and the badge, or the hub and the top and
bottom of the rim.

- **Geometry.** `anchor_geometry()` turns the anchors into a position and size
  relative to the tracked box. The aligned position is the anchor centroid. The
  aligned size is the anchors' RMS spread, measured in canvas heights. With a single
  anchor, the subject box supplies the size.
- **Rigid model.** The anchors move and scale with the native CSRT track. No new
  tracker calls are made.
- **Fallback.** Anchors that are mirrored (turned more than 30°), outside the
  subject, or degenerate (spread under 0.02) fall back to the old subject-box
  alignment. Pairs without anchors behave exactly as before.
- **Calibration to the tracker.** The helper reports Gemini's seed box on frame 0
  and switches to its own box size from frame 1. On real cars the box shrank 13–19%
  in one frame. `calibrated()` therefore measures the anchors against the tracker's
  frame-1 box and uses that box for the seed frame. One frame of motion is the
  accepted error.
- **Tracking the core.** Anchored pairs track the central 60% of the subject
  (`tracking_seeds()`), inset 2% from the frame edges. Whole-subject boxes that
  touched the image edge were rejected by the helper's bounds check, observed as
  exit 5 on a seed reaching y = 0 and x + w = 1.
- **Shape gate.** The 1.3× subject-shape gate applies only to subject-box
  alignment. A car box may include the windscreen in one shot and not the other;
  anchored pairs are checked by landmark order and spread instead.

**4 — Sturdier provider answers.**

- **Constrained output.** `Gemini.generate(parts, schema=None)` adds `responseSchema`
  when a caller asks for it. The match request passes `CANDIDATE_SCHEMA`; local
  validation stays authoritative. Live gemini-3.8-flash accepted it on all 45 live
  requests in this work.
- **Native box format.** Boxes use Gemini's own `box_2d` format, [ymin, xmin, ymax,
  xmax] on a 0–1000 scale, converted locally and exactly (the subtraction happens
  before the division). Earlier, the model sometimes ignored "fractions" and
  answered on the 0–1000 scale anyway.
- **Malformed versus weak.** Structural, type and range violations remain terminal
  `invalid_match_evidence`. Weak answers are dropped instead: subjects under the
  tracker's 0.04 minimum, and anchors with null boxes. Range checks also reject
  NaN, infinity and huge integers without raising `OverflowError`.
- **Resolution-aware zoom.** `zoom_limit()` probes the source size through the
  managed child runner, so Stop applies. Zoom may upscale source pixels at most
  1.6×: 1.6× for 1080p on a 1080p canvas, the native 2× for 4K, and a 1.1× floor for
  low-resolution sources.
- **Clearer no-match reason.** When no suggested pair fits before any tracking, the
  reason now reads *"No suggested pair fits within the zoom and full-frame limits"*
  rather than blaming tracking.
- **Cache identity.** The cache version is now `object-match-6`. The prompt is
  shorter and names the subject/anchor contract.

The wire review (protocol v3), progress messages, one Gemini call, at most two
tracked pairs, deadlines, Stop and the terminal error classes are unchanged.

## Evidence

**Live Gemini on real footage.** The last of four runs used the final code. Each
cut made 1 Gemini call and at most 4 helper calls, and took 7–18 s.

| Cut | Before (`a5bfe52`) | After |
| --- | --- | --- |
| rain tyre → gold wheel | matched on whole tyres; rims jump in size | **matched, 1.52×** |
| gold wheel → red wheel | no match | **matched, 1.56×** |
| red wheel → tyre | tracking error | **matched, 1.33×** |
| tyre → chrome wheel | matched on hubs only; wheel 1.6× bigger after the cut | **matched, 1.11×** |
| chrome wheel → rain tyre | no match | no fit (needs more than 1.6×) |
| Mercedes → Toyota | matched on one mirror; cars about 8% apart | no fit: needs 1.65× (headlights at 73% vs 41% of frame height) |
| other three car cuts | 2 "matched" on small parts, 1 none | no fit or explained no match |
| red wheel → Mercedes (control) | no match | no match |

A 50/50 blend of the frames either side of each cut confirms the four wheel
matches: in the aligned render the two wheels overlap as one (hub and rim), while
the original cut shows two separate wheels. Outputs are in
`~/Movies/montaz-match-cut-clips/results-landmarks/`:

- `quick-wheel-cuts.mp4`: 0.4 s either side of each cut, original on the left and
  aligned on the right;
- `frames/blend-check.png`: the blend test;
- `results.json`: diagnostics, including which anchors were used.

**Tests.**

| Check | Result |
| --- | --- |
| Python unit tests | 212 discovered: 206 pass, 6 optional native checks skip by default |
| Real tracker tests (this branch's `montaz-tracker`) | 6/6 pass |
| Qt checks | 39 transport and 7 chat pass |
| Native `assistanttest` (separate build) | pass |
| Controlled rendered QA (`check_match_quality.py`) | 807 assertions in 9 test cases pass |

The unit tests cover:

- **Answer validation:** the strict native-box schema and the drop rules for weak
  answers;
- **Anchor geometry:** two-, one- and zero-anchor cases, plus mirrored, outside and
  degenerate anchors;
- **Alignment:** headlights coinciding across the cut when subject boxes differ in
  shape or size, and anchors following a moving and zooming track;
- **Tracker handling:** calibration to the tracker's own box (this test fails if
  calibration is disabled), and tracking the core with an edge inset;
- **Zoom limits:** resolution limits for 1080p, 4K, 720p, portrait, unreadable
  media and Stop, and enforcement of those limits;
- **Pipeline:** the schema being passed, anchors driving the review, cache
  round-trip, and the mirrored fallback.

## Limitations

- **Gemini varies between calls.** Across four runs of essentially the same
  pipeline, the number of wheel matches varied (2–4), because the model's subjects
  and anchors differ between calls. The schema fixes the shape of an answer, not
  its content.
- **Zoom-and-crop cannot move a subject far.** Within the 1.6× cap, a subject can
  shift only by the zoom margin. The car clips frame their subjects too
  differently, so they are honest no-fits. Choosing compatible shots is item 2.
- **The cap is a policy, not a measurement.** 1.6× upscaling is a judgement:
  Mercedes → Toyota would match at 1.65×. The value is one constant.
- **Rim landmarks come from Gemini alone.** They are still model-placed and were
  accurate in the final evidence. There is no local edge or circle refinement; that
  would be native OpenCV work.
- **Subject-only alignment keeps the seed-frame jump.** Pairs without anchors still
  start from Gemini's seed on frame 0. Calibration applies only to anchored pairs,
  to keep the earlier behaviour unchanged; Codex may extend it.
- **Not yet checked in the editor.** Desktop Apply, Undo and Redo, and review of the
  exported result, still await Codex.
