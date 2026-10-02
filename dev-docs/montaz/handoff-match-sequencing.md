<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Handoff to Codex: match-cut shot grouping and quick match-cut sequences

From Claude, October 2, 2026. The user asked Codex to take these two items; Claude
built landmark alignment and sturdier Gemini answers on
`codex/claude-match-landmarks` (see [match-landmarks-report.md](match-landmarks-report.md)).
Both items need native contracts (clip order and timing) that Codex owns.

## Evidence that motivates them

On 10 real stock clips, zoom-and-crop alignment could only fix pairs whose
subjects were already roughly the same size and in the same place. On the Mercedes
clip the headlights sit at about 73% of frame height; on the white GT86 they sit
at about 41%. Moving one onto the other needs 1.65×, beyond the 1.6× cap, so no
reframing can match these pairs well. The user also noted that match cuts are
usually *fast*. The current tool keeps the user's shot lengths and aligns only one
second either side of each cut.

A scratch prototype (not in the repo) showed that both ideas work:

- **Common framing:** one Gemini call per family gave landmarks for 5 clips. A
  local search found the largest subset that can share one framing within the
  zoom cap: 3 of 4 cars at a maximum of 1.58×, 4 of 5 wheels at 1.92×.
- **Fast shots:** 10-frame shots (0.42 s) rendered with `melt` and `qtblend`.

Prototype outputs are in
`~/Movies/montaz-match-cut-clips/landmark-prototype/`. The headlights stay on the
same canvas spot through every car cut.

## Item 2 — Suggest compatible shots before aligning

The goal is to propose which of the user's clips match, and in what order, before
any expensive tracking.

- **Reuse what exists.** `parse_candidates`, `anchor_geometry`, `tracking_seeds`,
  `align`, `rank` and `zoom_limit` in `match_cuts.py` already work on seed frames.
  Pairwise feasibility needs only seed geometry: run `align([[out_seed],[in_seed]],
  anchors=..., limits=...)` with no tracking.
- **Bound the provider work.** Use one landmark request per bounded group of clips.
  Gemini answered consistently for 5 images per call when asked for the *same*
  landmark names in every image. Cache landmarks per source hash, window and model,
  like the scene cache.
- **Order locally.** Choose the order or subset with a deterministic local search,
  as in the prototype: largest feasible subset, then lowest maximum zoom. Report
  the clips excluded for zoom or position, never forced.
- **Clip order is a native proposal.** Changing order or selection needs Codex's
  reviewed native proposal (revision/source checks, preview, one Undo). Track only
  the chosen adjacent pairs, within the existing two-tracked-pair budget per cut, or
  a new explicit total budget.

## Item 3 — Quick match-cut sequence mode

The goal is a sequence of short shots (about 8–12 frames each) cut on matched
subjects.

- **Timing.** Shot length and the match point need native trims around each match
  frame. Source handles, rational frame timing, one Undo and save/load/export
  parity are required. This is a native timing contract.
- **One transform per shot.** A middle shot is the incoming side of one cut and the
  outgoing side of the next, so one transform must serve both. There are two
  options:
  - **Shared framing (preferred for montages):** one target for the whole
    sequence, as in the prototype. Every shot is static or tracked toward the same
    point. This is simple and reads as a deliberate "subject stays put" montage.
  - **Per-shot animation:** move from the incoming alignment to the outgoing
    alignment within the shot. At 10 frames this risks a visible zoom move.
- **Validation changes.** The native review currently expects keyframes only in
  the one-second windows (`validateMatchReview`). A sequence mode needs a new
  operation or version with whole-shot keyframes, the same 1–2× and coverage gates,
  and a per-shot zoom limit.

## Other items needing Codex review on the Claude branch

- **Shared file change:** `gemini.py` gains an additive `schema` argument.
  Existing callers are unchanged. Only the match request uses it.
- **Possible follow-ups:**
  - **Gemini variability:** consider `temperature` 0 for detection-style requests
    in `gemini.py` (currently 0.2).
  - **Seed-frame compatibility:** integration uses native fixed-seed scale
    registration from `1e2e226`. Its exact seed geometry replaces the old
    frame-one compensation for both anchored and subject-only tracks. Do not
    reintroduce that compensation; see [integration](landmark-integration.md).
  - **Native anchor refinement:** optional local edge or circle refinement of
    Gemini anchors in the helper. OpenCV is already present through MLT, so no new
    dependency is needed.
