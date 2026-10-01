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

Next gates are music and measured cut timing, narration/caption modes and one
bounded review revision. Portrait framing is deferred by the user. Do not describe this
checkpoint as a finished film or as proof of uniformly improved editorial quality.

## Instrumental checkpoint

The separate `goa-music-review.kdenlive` keeps the seven-shot visual edit unchanged
and adds ElevenLabs instrumental music through native import/insertion. Preview
starts/stops; one Undo removes both the clip and imported asset, and Redo restores
them. Reopening restores the imported music and its waveform; native timeline
playback advances with active audio meters. Saving uses a relative audio reference to `goa-music-review.assets/music/`
beside the project. Retain that folder with the project.

The first generation failed a strict duration check. A diagnostic generation
revealed 15.792 seconds of MP3 against a 15.8158-second video span. Raw responses
now persist before local preparation. Local reuse padded only the final codec
shortfall (less than one project frame), produced 759,158 samples at 48 kHz, and
detected 27 energy accents. These are not verified beats; no cuts moved.

Native H.264/AAC export `~/Movies/goa-music-review.mp4` has exactly **474 video
frames**, 1920×1080 at 30000/1001 FPS. Audio container duration is 15.829 seconds
because of AAC framing. All 474 decoded video frames match the prior silent
export exactly. Exported audio measures **-18.1 LUFS** integrated and **-5.0 dBFS**
true peak. This is a soundtrack integration check; portrait framing,
automatic musical cut timing, environmental audio/ducking and effects remain open.


## Local timing checkpoint

A separate `goa-timing-review.kdenlive` was opened from the music review. Local
analysis proposed one internal cut shift: frame **75 → 79** (four frames, about
0.13 seconds at 30000/1001 FPS). All later boundaries, source starts, shot order,
474-frame total and music placement stay unchanged. Other cuts either already
match an accent, lack a nearby accent or cannot extend into available source.
The candidate is an energy accent, not a verified beat or phrase boundary.

Native Apply, one Undo and Redo restored the corresponding cut ranges. The refined
project was saved. Native H.264/AAC export `~/Movies/goa-timing-review.mp4` contains
474 frames at 30000/1001 FPS and retains all 742 decoded audio-frame checksums from
the music review. All decoded video frames from frame 188 onward are unchanged;
encoder lookahead also changes frames 65–74 before the moved boundary. Exported
frames 78/79 show bridge/coast, with no blank frame, and native seeking at frame 79
agrees with the export. No new provider call was made during timing analysis.
Reopening the refined project retained the ranges and music. Repeating Align cuts
finished as No change with no running tasks or enabled Apply. This verifies bounded
timing and export consistency, not improved musical taste.

83 Python checks, 26 Qt transport checks and 7 chat checks (including fixtures),
plus native `assistanttest`, pass. The native synthetic test verifies actual
resize/move, one Undo/Redo, stable identities/source starts, replay rejection,
stale locks and rollback after an injected batch failure. Those checks establish
mutation safety; they do not assess musical or visual taste.
