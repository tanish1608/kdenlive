<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Native output checkpoint — October 1, 2026

## Evidence and limits

Use the supplied Goa footage outside Git. It contains 15 sources and about 30.81
seconds of raw material, with mixed orientations, frame rates and micro-clips.
A 60-second brief cannot be fulfilled without repetition or generated footage.

`video-windows-4` analyzes silent, bounded video windows at four sampled FPS,
records usable trim offsets and rejects unsuitable sources. Native preview adds
shot reasons and duration feasibility. Trims and scores remain model estimates;
local/native checks enforce bounds, not cinematographic quality.

A provider response failure interrupted the initial analysis; successful partial
entries survived. A diagnostic retry proposed 9 shots/574 frames (19.15 seconds).
The native chat with a different brief proposed 7 shots/474 frames (15.82 seconds),
with 700 frames estimated available and 5 unsuitable sources excluded. All 12
eligible analyses were reused; 3 micro-clips required no provider analysis. The
native run took 18.65 seconds. These are observed runs, not an A/B quality test.

## Actual editor and export checks

- Apply inserts one undoable edit; one Undo removes it and Redo restores it.
- Save/reopen retains source ranges, including native constant-frame-rate
  conversions. The earlier Goa review project and original footage are preserved.
- Native Rendering completed in 17 seconds. H.264 output is 1920×1080 at
  30000/1001 FPS, exactly 474 decoded frames, 15.8158 seconds.
- Inspect both frames around cuts at 75, 188, 239, 276, 329 and 393, plus the first
  and last frames. The exported bridge/coast/pool/karts/sunset/night sequence has
  no extra blank frames at those boundaries. Native seeking at the first cut
  agrees with the exported coast shot.
- AAC output measures -91 dB peak/mean: the current assistant inserts video only.
- Preset switching keeps the AI sidebar on the right and source monitor hidden.

Native conversion was blocked by unknown phone auxiliary audio. It now preserves
recognized streams and rejects nonzero encoder exits. Real phone conversion and
synthetic unsupported/multiple/silent/failure cases pass. The development launcher
also supplies missing workspace/conversion/render resources and the render helper.

63 Python checks, 22 Qt transport and 7 chat checks, native `assistanttest`, and
native `transcodetest` pass. Accessibility inspection immediately after closing
Rendering reproduced the macOS SDK AX hierarchy crash; reopening the saved
project succeeded. This is an unresolved UI-testing limitation.

## Remaining output work

Portrait shots are heavily pillarboxed in the landscape project; the sequence
changes aspect ratio abruptly. Camera movement and the final obstructed archway
shot still need editorial judgment. The draft is silent and shorter than even
its feasible maximum. More duration alone would not make it better.

Next gates are explicit orientation/native framing, music and measured cut timing,
then narration/caption modes and one bounded review revision. Do not describe this
checkpoint as a finished film or as proof of uniformly improved editorial quality.
