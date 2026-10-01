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


## Described sound checkpoint

The separate `goa-sound-review.kdenlive` keeps the timing edit and instrumental
and adds one explicitly described coastal-wave effect at frame **79**, lasting
**60 frames / 2.002 seconds** on the previously free audio track. This is generated
foley, not recovered location audio or automatically recognized sound evidence.
The dedicated ElevenLabs effect request completed; local preparation produced
96,096 stereo samples at 48 kHz, measured **-26.0 LUFS / -13.2 dBFS** true peak.

Qt preview completed, native Apply added one audio asset/clip, and one Undo removed
only that sound. Redo and save/reopen retained both the effect and existing music.
The saved effect resource is relative to `goa-sound-review.assets/effects/`. Keep
that folder and the referenced `goa-music-review.assets/music/` with this project.
No source video, earlier project or export was overwritten.

Native export `~/Movies/goa-sound-review.mp4` retains all **474 decoded video
frames exactly** from the timing review. The H.264/AAC mix measures **-18.0 LUFS /
-4.7 dBFS** true peak, with no clipping. It remains a pillarboxed editorial
prototype; signal measurements do not establish perceptual sound-design quality.

92 Python checks, 27 Qt transport checks and 7 chat checks (including fixtures),
plus native `assistanttest`, pass. Automatic cue selection, source-audio pairing,
ducking and narration/captions remain pending; portrait framing remains deferred.

## Cached cue checkpoint

`goa-cues-review.kdenlive` adds a separately selected pool-water effect to the
previous timing/music/coastal-sound review. One native suggestion run checked only
seven selected unique sources: three current analyses were reusable and four
unavailable entries were skipped, with no fallback visual analysis. Gemini
selected one supported cue; the run completed in 11.49 seconds including worker
startup. It is an observed run, not a controlled speed or editorial benchmark.

The cue starts at **frame 188 (6.273 s)** and lasts **51 frames (1.7017 s)**, wholly
inside the pool shot. Native seeking at `00:00:06,08` displayed the pool. Generation
was explicitly selected after reviewing the cached observation; ElevenLabs then
completed in 2.51 seconds, independently of suggestion selection. The prepared
48 kHz stereo WAV has **81,682 samples**, measures **-26.0 LUFS / -8.8 dBFS true
peak**, and uses quiet level/soft edges. These are proposed water sounds, not
recovered location audio.

Listen toggled on and returned to idle. Native Apply imported/injected one cue;
one Undo restored all previous media, trims and positions, and Redo restored the
same applied timeline. Save/reopen retained the new relative effect resource.
Stop during separate suggestion runs, both before and after reopening, reached
terminal tasks and left the edit unchanged. Run logs contain counts/model and
outcomes, not observations, prompts or source paths.

Native export `~/Movies/goa-cues-review.mp4` contains **474 frames** at 30000/1001
FPS, 1920×1080. Every decoded video frame exactly matches `goa-sound-review.mp4`.
The combined audio measures **-17.9 LUFS / -4.7 dBFS true peak**, without clipping.
The source footage, previous projects and exports are preserved outside Git.
Portrait framing is still deferred. Speech/ducking, captions, perceptual listening
review, broader categories and cue selection on complex timelines remain open.
