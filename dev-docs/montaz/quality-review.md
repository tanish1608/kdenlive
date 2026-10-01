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

## Reviewed caption checkpoint

The separate `goa-captions-review.kdenlive` tests captions with a controlled
5.205-second synthetic spoken sentence placed as original audio. Its black prefix
comes from manual fixture insertion; this is caption integration evidence, not
a revised travel film. The original Goa footage and earlier projects are intact.

On the video-only/assistant-sound edit, Generate captions finished with zero
eligible audio and no Gemini call or working tasks. On the speech fixture, live
recognition completed in 3.94 seconds and returned the scripted words with
approximate segment timing. A second run reused one cached recognition. Native
Apply created one caption at frame 0 for 153 frames; one Undo removed it and Redo
restored it. Saving/reopening retained the event and readable two-line rendering.

Clean, Bold, Undo to Clean, Redo to Bold and Original were exercised in the
desktop. Clean/Bold use scaled native force styles; Original clears the override.
The ASS file remained byte-for-byte identical throughout style changes: one
event, unchanged text and timing. The test ends with Original appearance.

Desktop `~/Movies/goa-captions-review.mp4` and command-line
`~/Movies/goa-captions-headless-review.mp4` both contain **474 decoded frames**,
1920×1080 at 30000/1001 FPS. Both exports display the caption clearly. Desktop
frame 152 contains its lettering and frame 153 does not, matching the reviewed
exclusive end. The initial command-line attempt exposed a null-main-window crash
while loading native subtitle properties; direct headless model initialization
fixes it, with a native regression and successful export retry.

114 Python, 32 Qt transport and 7 chat checks, plus native `assistanttest` and
upstream `subtitlestest`, pass. Recognition is not verified word alignment; the
energy gate is not voice detection. Narration, processed/overlapping dialogue,
ducking and perceptual editorial review remain pending. Qt/macOS accessibility
inspection can still crash on dynamic UI; that separate SDK issue is unresolved.
All fixture media, logs, projects, caches and exports remain outside Git.

## Bounded draft review checkpoint

One additional text request now reviews each valid draft against cached sampled
observations. It can keep the draft or return one revised shot sequence with
exactly the same total frames. Original bounds, recipe durations and distinct
content still apply. Failure retains an explicitly unreviewed draft; cancellation
never takes that fallback. The phase has a 45-second native cap within the
three-minute run deadline, with no automatic retry or recursive critic.

A controlled request using five cache-matching sources from the saved Goa bin
reused all five entries, selected four shots totaling **193 frames / 6.440 seconds**
at 30000/1001 FPS, and completed planning/review in **7.70 seconds**. The model kept
the draft. A second real native Qt/Python transport check completed in **14.32
seconds**, reused those five entries, and returned a kept four-shot/193-frame
draft after strict native progress/review validation and clean exit. Its log
records status/code and numeric metrics, without the model's explanation.

These are selected-source integration checks, not a controlled comparison with
the older travel export. Sampled observations cannot establish exact action
endings or finished-film quality; the model keeping a draft is not approval.

Desktop testing used a copied `goa-editorial-review.kdenlive` with a separate empty
sequence. The full-bin request failed in analysis with `invalid_model_response`
before reaching review. AX inspection then reproduced the known accessibility
crash. At that checkpoint the saved fixture retained the earlier sequence and
empty test sequence; successful desktop review/Apply/export remained an open gate.
No new finished travel video or perceptual improvement is claimed.

127 Python, 34 Qt transport and 7 chat checks, plus native `assistanttest`, pass.
Tests cover invalid revisions/approval claims, exact duration, cached planning,
failed review, clean exits, progress order, redaction and cancellation during an
unresponsive review. Replacing a finished edit and mixed-media perceptual review
remain pending; private fixtures and diagnostic outputs stay outside Git.

## Full-bin desktop review checkpoint

The original full-bin cache discrepancy was caused by seven converted resources
with different content hashes, sometimes lengths. The originals retain valid
entries. Reanalysis completed in **20.65 seconds** with five hits, seven new
entries and three micro-clips skipped. No content identity checks were bypassed.
The earlier generic provider failure cannot be reconstructed from its old code;
new terminal diagnoses distinguish output limits, blocked/empty answers and
rejected requests without logging private response bodies.

A separate Qt tree/list application reproduced the accessibility crash. The
upstream QTBUG-149612 ownership patch, built as a scoped development Cocoa plugin,
retains selected rows and survives list replacement. Real Kdenlive inspection now
passes after planning, Apply/Undo/Redo and closing Rendering. The reproducible
builder checks source digests and the exact Qt SDK/plugin identity; the launcher
only opts in with `MONTAZ_COCOA_PLUGIN_ROOT`. The global Craft plugin is unchanged.

The empty `Coastal editorial review` sequence received a three-shot draft in
**20.21 seconds**, reusing all twelve eligible entries. One review kept it; Stop
was disabled after completion and no task remained Working. Native Apply inserted
the draft as one Undo/Redo action. Saving and launching a fresh editor preserved
the three source ranges and the earlier 474-frame sequence. The fresh session
correctly reset conversation/proposal state. Native seeking at frame **75** shows
the pool shot, matching the exported first cut.

Native Rendering completed in **11 seconds**. `~/Movies/goa-editorial-review.mp4`
contains exactly **240 H.264 frames / 8.008 seconds**, 1920×1080 at 30000/1001 FPS.
First/last frames and both sides of cuts at **75 and 126** contain source imagery;
black detection finds no added blank frames. This draft is video-only, with no
speech/narration. Portrait pillarboxing remains deferred.

**Quality limitation:** the brief requested 20 seconds, the feasible estimate was
708 frames (~23.62 seconds), yet the draft selected only 240. The model's kept
assessment does not establish that this is a good finished film. Purposeful
duration, richer travel coverage and a resolved ending still need measured
comparison. Existing-sequence refinement and structured recipe improvements are
the next editorial gates; do not present this export as a quality benchmark.

132 Python, 35 Qt transport, 7 chat checks and native `assistanttest` pass. The
editor and scoped plugin compile. Plugin identity regressions reject changed SDK
binaries even at the same version, changed architecture/location, patch or plugin.


## Object-match checkpoint

The first transition prototype works on two selected adjacent video-only shots.
A live Gemini run selected matching textured discs on controlled footage; native
CSRT tracked the boundary windows and the editor displayed original/aligned frames.
Apply, one Undo and Redo were exercised in the desktop; saving retained both
30-frame Transform animations. A fresh-process reopen verifies native loading.

`assistant/tools/check_match_quality.py` generates controlled moving-disc footage
outside Git, substitutes only semantic seed selection, then exercises real native
tracking, application, Undo/Redo, MLT serialization and export. The 1920×1080
H.264 export contains exactly 120 frames at 30000/1001 FPS. Boundary centers
agree within 0.007 of the canvas and subject heights within 0.015; decoded export
and reloaded native geometry retain the alignment. The optional helper tests also
check nonzero source trims within three source pixels and reject flat patches.

Two existing Goa cuts (river/pool and pool/coast) returned explicit no-match
answers in 7.86 and 8.31 seconds. Their complete negative evidence is cached;
no forced alignment or effects are added. We have not searched all possible Goa
pairs or produced a better complete travel film. Tracking uses a conservative
appearance proxy, not a calibrated identity/confidence score. Boundary comparison
is static; review movement in Kdenlive's project monitor after Apply.

Validation: 140 passing Python checks plus two optional native helper checks;
38 Qt transport, seven chat checks, native `assistanttest`, and controlled rendered
QA pass. Beat/phrase analysis, candidate search, manual anchors, directional whips,
object wipes and speed ramps remain pending. Generic pacing presets remain in use.

## Estimated beat timing checkpoint

Align cuts now estimates a conservative steady rhythm locally, with cached,
observed anchors and explicit energy-accent fallback. Its native protocol-v4
proposal retains bounded shifts, original sources/order/span and one Undo.
Temporary multiselection no longer fails eligibility; permanent groups and
muted/hidden/effect-bearing media remain ineligible.

The reproducible 120 BPM fixture moved frames **48/102 → 50/100**. Native Apply,
Undo/Redo and serialization passed; decoding every frame of the **150-frame**
export confirmed the boundaries. Before/after PCM audio was byte-identical, and
audio attacks agree within one frame of both cuts. A ten-minute synthetic track
analyzed in approximately 1.65 seconds and reused complete cached evidence.

Read-only analysis of the existing short Goa instrumental returned
`weak_periodicity`, retaining honest accent fallback. No user project or footage
was changed and no generation request was made. This does not establish better
musical taste, phrase detection or a better complete travel film.

148 Python tests passed, two optional tracker tests skipped; 39 Qt transport and
seven chat checks passed. The editor/native tests built, and full native tests
plus the independent controlled render check passed. Reproduce the evidence and
read algorithm limitations in [music timing](music-timing.md). Claude's object
candidate improvements remain isolated and unmerged.
