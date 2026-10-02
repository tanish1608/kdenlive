<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Landmark integration — October 2, 2026

Claude's `e186bc1` is integrated with the native scale-registration fix `1e2e226`
and draft-revision work `2d47bee`. Kdenlive still owns selection, timing, effects,
Undo and export. This milestone preserves the selected shots' order and duration.

## Integration corrections

- Removed the old frame-one compensation: the newer helper preserves the exact
  seed region. Landmark offsets now reference that region, retaining actual
  translation and zoom from the first frame onward.
- Both branches previously used `object-match-6` for different evidence/geometry.
  Version 7 prevents reuse across implementations; other analysis is preserved.
- Limited landmark target size to the native fraction contract. An impossible
  spread finishes as no match before Apply instead of producing an invalid review.
- Retained strict provider parsing, whole-subject landmarks, central-region
  tracking and resolution-based zoom caps. Existing Gemini callers are unchanged.

## Combined verification

- 225 Python tests pass, including ten opt-in checks using the shared native
  tracker. Focused landmark/provider tests and Ruff checks pass.
- 40 Qt transport and seven chat tests pass.
- Controlled native quality validation passes 1,208 assertions in ten test cases,
  including selection, Apply, Undo/Redo, serialization and export. Nine controlled
  exports include the ten genuine-zoom windows. These use known subject seeds;
  they do not establish live landmark accuracy.
- One live Gemini call in a separate desktop project matched the gold/red stock
  wheel pair using the hub and tyre top, at up to 1.42× zoom. Compared boundary
  previews, applied two native Transform effects, removed both with one Undo,
  restored both with one Redo, saved and reopened in a fresh editor process.
- Both original and aligned native 1080p exports decode to exactly 120 frames
  at 30000/1001 FPS. `wheel-compare.mp4` places original left and alignment right.
  Decoded frames 59/60 show improved hub positioning and tyre-top alignment;
  wheel perspective remains visibly different and the crop is substantial.

Private fixtures and exports live outside Git under
`~/Movies/montaz-landmark-integration-20261002/`. Historical four-wheel results in
Claude's report have not been rerun as a chain with this combined implementation.

## Quality limits and next work

Landmarks follow one rigid tracked region; the helper does not refine rim edges
or track landmarks individually. The checks do not measure whole-object identity
or compensate for different perspectives. Gemini answers still vary, and weak
landmarks fall back to subject alignment. No zoom cap was raised to force a match.

Next: bounded compatible-shot suggestions, then an explicit quick montage using
one shared framing per shot and native timing operations. Whips, occlusion wipes
and speed ramps remain separate, unfinished work. Passing these checks does not
establish finished travel-video quality.
