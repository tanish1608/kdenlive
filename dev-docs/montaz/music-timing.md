<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Reviewed music timing

## Implemented

**Align cuts** analyzes the existing assistant instrumental locally. FFmpeg
decodes three frequency bands at 8 kHz; positive log-energy rises form a 10 ms
onset envelope. Recurrence, phase coverage and interval stability support a
conservative 60–180 BPM estimate. Anchors retain observed attacks rather than
inventing beats through gaps. Ambiguous material uses the existing, clearly
labelled energy-accent method. Silence terminates without a proposal.

This follows the distinction between onset evidence and rhythmic consistency
described in [Ellis's beat-tracking research](https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf).
It is a compact heuristic, not an implementation of that paper's dynamic-programming
beat tracker. The cut-selection stage separately uses dynamic programming to
maximize feasible anchor matches, then minimize movement.

The native review shows estimated BPM or accent fallback, before/after cut
times, and each shift in project frames. Apply remains explicit and uses one
native Undo command. Every shift is bounded to one quarter second, source
length and neighboring shot minimums. Order, source starts, music placement,
clip identities and total span stay unchanged. Permanent groups, muted/hidden
tracks and bin/track/clip effects are ineligible; temporary multiselection works.

## Contract and diagnostics

The worker emits protocol-v4 timing proposals with `anchors` and strict
`analysis`: `version`, `mode`, `bpm`, `periodicity`, `support`, `reason`, `cached`.
Beat anchors are limited to 2,048; legacy protocol-v2 accent proposals remain
accepted. Periodicity and support are heuristic measurements, not probabilities.
Native validation rejects unknown fields, stale context, invalid measurements,
changed music, unsupported shifts and altered source starts or total span.

`timing_review` logs include those measurements, anchor count and shifted-cut
count without audio paths or content. Complete beat estimates are cached under
`MONTAZ_ANALYSIS_CACHE/beats`, keyed by SHA-256, exact FPS, analyzed duration and
algorithm version. Source checks before/after analysis prevent publishing
evidence under a changed file's old key. Fallback accent measurement still
requires its separate short local decode. Stop and the existing 30-second worker
deadline cover the operation; no network call or new dependency is introduced.

## Reproduce the native export check

```sh
python3 assistant/tools/check_beat_quality.py \
  --craft-root /path/to/CraftRoot \
  --build-root /path/to/kdenlive/build \
  --output-dir /private/review/beat-check
```

This creates three colored source videos and a known 120 BPM instrumental
fixture outside Git. Real local analysis proposes frames **48/102 → 50/100**.
The native adapter applies those cuts, checks one Undo/Redo, serializes the
timeline and renders its reviewed range. Decoding every exported video frame
confirms boundaries; before/after PCM audio must be byte-identical, retain
exactly six seconds, and place attacks within one frame of the proposed cuts.
Kdenlive's background seek padding is excluded through its normal export range.

## Evidence and limits

The 2026-10-01 controlled export passed with **150 video frames** and unchanged
audio. Fractional-FPS, missing-onset, irregular/changed-tempo, silence, cache,
source-change and cancellation regressions are offline. A ten-minute synthetic
180 BPM track produced 1,800 observed anchors in approximately **1.65 seconds**
on the development Mac; cache reuse took under one millisecond. This measures
local analysis, not overall editing or export speed.

Validation: the full Python suite passed 148 tests with two optional native-tracker
tests skipped; the Qt transport/chat suites passed 39/7 checks. The editor and
native `assistanttest` built successfully; the full native suite and the separate
controlled render check passed. These checks do not replace listening to real music.

Read-only analysis of the existing eight-second Goa review instrumental returned
`weak_periodicity`, correctly retaining accent fallback. No user project was
modified and no provider was called. These tests establish timing and lifecycle
behavior, not musical taste or better travel storytelling.

Downbeats, phrases, variable-tempo tracking, manual beat corrections and broad
real-music evaluation remain open. Dense subdivisions can yield a half/double
tempo interpretation. Listen in the native monitor before Apply. Claude's object
match candidate work remains isolated and retains its existing protocol-v3
contract; music timing does not change that interface.
