<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Goa beat-cut demo evidence

October 2, 2026. Existing features only. Private artifacts:
`~/Movies/montaz-demo-goa-beats-20261002/`.

## Presentation files

- `goa-beat-synced.mp4`: native 1920×1080 H.264/AAC export, 408 frames at
  30000/1001 fps, 13.6136 seconds, stereo soundtrack.
- `goa-beat-before-after.mp4`: labelled comparison, before left / after right,
  same timing origin and one unchanged soundtrack.
- `goa-beat-before.kdenlive`, `goa-beat-synced.kdenlive`: editable native projects.
- `verification.json`, `final-music-analysis.json`, `cut-review.png`: measurements
  and frames on both sides of every cut. Preparation scripts/media stay outside Git.

## Preparation and actual application

Used seven sources from `~/Downloads/goa-clips-test/`, preserving the earlier
music-review edit's source order and zero source starts. This is a prepared demo
sequence with shorter trims and spare source frames, not the untouched original
AI draft. The original draft has little room to shift several cuts.

The old music produced an honest weak-periodicity accent fallback. Two native
Add music attempts returned `invalid_music_audio`. A separate existing ElevenLabs
client call produced a usable 20-second instrumental, normalized and trimmed to
408 frames with fades. The requested 120 BPM is not the measured result: the
actual detector reports a steady 60 BPM pulse, support 1.0, periodicity 0.7243.
This measures beat-supporting onsets, not downbeats or musical phrases.

Imported an OTIO preparation through Kdenlive's native importer, saved the baseline,
requested **Align cuts to music**, reviewed and applied the actual assistant
proposal. Six cuts shifted: `[88,176,207,238,268,328]` →
`[91,180,210,240,270,330]`. Shot order, source starts, soundtrack and total duration
were preserved. One native Undo restored all original boundaries; Redo restored
all aligned boundaries, confirmed in saved project serialization.

## Verification and blocker fix

The worker returns `periodic-onsets-2`, but the native validator still required
version 1. Updated the validator and fixtures; the Qt contract test now reads the
actual Python version constant and explicitly rejects the obsolete version.
Commit: `b965267`.

- Python timing tests: 10 pass.
- Qt client/command: 49 pass; chat: 7 pass.
- Native `assistanttest`: pass.
- Controlled real beat worker → native apply/Undo/Redo → serialized export:
  both cuts land on beat frames; audio is identical; cache reused.
- Real Goa exports: 408 decoded frames each; every frame has picture; all six
  aligned cuts have zero frame distance to observed beat anchors. Decoded audio
  is byte-identical, peak −5.4 dBFS, RMS −19.45 dBFS.
- Visually inspected every cut's outgoing/incoming frames in the contact sheet.

## Demo limits

Portrait framing remains pillarboxed, as previously deferred. Playback listening
and Zoom audio-sharing rehearsal still need confirmation. Add music generation
was not verified successful in the UI. The macOS Qt accessibility/render-queue
crash recurred: exports use **Generate Script** in Kdenlive and its native
`kdenlive_render` executable on the generated scene; no live project XML was edited.
Do not present either issue as fixed or promise a live generation/render rehearsal
based on these saved artifacts.
