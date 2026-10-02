<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Quick match montage: native foundation

This is N03's native contract subgate. Compatible-shot discovery, the worker
solver, dock review and real-footage acceptance remain pending. The existing
selected-pair Match cut action still uses its existing protocol. No montage
action is exposed in the editor yet.

## Operation and ownership

`EditorAdapter::montageRequest(shotFrames)` captures 2–6 selected video-only
instances on one unlocked track. Sources must be normal-speed, ungrouped and
without effects or mixes. Track/bin/master effects, compositions, interlacing
and nonsquare project pixels are ineligible. Sources need known dimensions.
Native project frames determine the requested 8–12-frame shot duration.

The native-only review reserves protocol v5. It binds the immutable project,
sequence, revision, rational FPS, original instance/bin identities and source
trims. A matched review orders at least two distinct selected instances, with
exactly one numeric uniform Transform keyframe for every resulting shot frame.
All selected sources are accounted for as included or explicitly excluded.
An empty `no_match` settles without any editable geometry or application.

Every shot's anchor center and height must reach the same common target within
two project pixels. Coverage, resolution-based zoom, per-frame scale/motion and
source bounds are validated before mutation. This checks claimed geometry;
it does not establish object identity, landmark precision or perspective fit.
The forthcoming discovery/tracking stages must establish that evidence.

Apply regenerates selection scope, rejects changed/replayed reviews and verifies
all selected source checksums. It appends short copies after the entire existing
timeline through Kdenlive's insertion/effect APIs, preserving source edits and
existing neighbors/audio. It does not replace the session-owned story draft.
Insertion and Transform effects form one rollback-capable Undo/Redo action.
Hash verification currently has the existing 128 MiB per-source limit.

## Reproduce the checks

Run Qt contracts with `assistant/tools/run_native_tests.py`. Build the native
`kdenlive` and `assistanttest` targets, then run `ctest -R '^assistanttest$'
inside the configured build directory with Craft's runtime environment.

The independent moving-subject render check needs no API key:

```sh
python3 assistant/tools/check_montage_quality.py \
  --craft-root "$HOME/CraftRoot" \
  --build-root /path/to/configured/build \
  --output-dir "$HOME/Movies/montaz-montage-check"
```

It generates three 1080p moving rectangles with known source coordinates, then
checks every montage frame in native playback, an MLT scene reload, Undo/Redo
and an H.264 export. Shot lengths are 8, 10 and 12 frames at 30000/1001 FPS;
decoded frame counts must be 24, 30 and 36. Reports distinguish mechanical
geometry checks from AI detection and perceptual quality.

Full N03 stays pending until C01/N02 discovery, bounded joint tracking/selection,
review/Stop/stale-result behavior and a real-footage comparison are integrated.

## Verification — October 2, 2026

- Craft compiled `kdenlive` and `assistanttest`. Qt checks passed: 44 transport/
  contract cases and seven chat cases.
- Native insertion checks passed 948 assertions over 8/10/12-frame shots at
  25 and 30000/1001 FPS. They cover ordering, source trims, neighboring video/
  audio preservation, shared keyframe positions, replay, changed selections/
  checksums, Undo/Redo and rollback after partial insertion/effect application.
- The opt-in moving-rectangle check passed 2,238 assertions. Every frame was
  measured before/after MLT reload and Undo/Redo, then after native H.264 export.
  FFprobe independently counted 24/30/36 frames at 30000/1001 FPS and 1920×1080.
- The fixture caught an eligibility bug: raw `meta.media` dimensions can be
  absent despite valid native dimensions. The adapter now uses Kdenlive's
  `frameSize()` API, which also handles alternate fields and proxy metadata.
- CTest's `assistanttest` passed. The existing match-quality tool also passed
  candidate selection, actual CSRT tracking, cached reuse and all eight constant/
  zoom stability exports after the shared Transform helper was extracted.
  Its full native pass completed 2,157 assertions in 12 cases; the montage render
  gate is opt-in and was run separately, rather than inferred from that count.

Private evidence is under `~/Movies/montaz-montage-foundation-20261002/`:
`report.json`, `native-check.log`, `montage-{8,10,12}.mlt` and the matching MP4s.
These synthetic files are deliberately excluded from Git. No live Gemini call,
real-footage claim or desktop button/review verification is included in this gate.
