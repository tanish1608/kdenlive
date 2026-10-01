<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Migration context — 2026-09-29

## Accepted direction

The user chose Kdenlive's interface and engine with a native AI assistant.
Start the integration cleanly and reuse proven ideas, not the old editor's
architecture. Packaging/public desktop releases remain deferred. Keep AI mode
selectable per project: narration or visual highlights. ElevenLabs should supply
background music and occasional effects. Travel templates are desired.

The old repository lives at `../montaz`, branch `version-2`, preserved commit
`7a3c28a` (99 backend tests, six frontend tests, Ruff and TypeScript checks passed).
It contains useful analysis/cache/provider experiments. Do not delete it or its
user projects. No credentials or private footage were copied into this checkout.

## Evidence that must become regression tests

The latest inspected Goa run was saved on September 27, run prefix `18740cee`.
Private footage remains outside this repository. Findings:

- Fifteen files total 30.804 seconds; the brief asked for 60 seconds.
- All visual evidence failed (NVIDIA returned HTTP 410), yet scripting continued
  with unsupported scene descriptions. Failures must not become editorial facts.
- A six-clip, 14.72-second draft became twelve clips with repeated instance IDs
  during approval. Independently inserting a snapshot through backend CRDT and
  frontend event paths reproduced this duplication offline.
- The critic returned invalid JSON after 15.58 seconds and was treated as approved.
  Its empty completion phase left a misleading UI spinner.
- All six selected sources had `has_speech=false`, yet twelve noise-event labels
  were burned as captions. The labels also created a false speech-ducking window
  covering 0–29.44 seconds. Caption encoding wasted 41.91 seconds.
- Preview, timeline geometry, and rendering used different time calculations.
  Proportional mapping, minimum clip widths, transition-button widths, duplicate
  IDs, and stale previews all undermined accurate seeking.
- The final file was approximately 29.5 seconds, repeated the montage, and showed
  inconsistent portrait/landscape treatment. These are not prompt-only problems.

The old unit suites passed despite these failures: native end-to-end tests and
real output review are required. Use synthetic media for committed regressions.

## Implementation order and exit gates

1. **Foundation (this branch):** ownership documents, typed proposal validation,
   explicit job lifecycle, offline tests. No claim of native integration yet.
2. **Native vertical slice:** assistant dock + QProcess worker; snapshot one
   sequence, preview six deterministic inserts, apply one undoable batch. Pass
   duplicate delivery, stale revision, locked track, rollback, undo/redo, stop,
   project switching, and save/reopen checks before connecting providers.
   The source for this slice now exists behind an off-by-default build option.
   The Craft build now compiles the dock and adapter. Native `assistanttest` and
   `utilstest` pass. Desktop preview/apply/undo/redo/save/reopen and seeking at the
   first cut were exercised with synthetic mixed-rate and portrait sources.
   Export and additional failure/sequence-switch checks remain open.
3. **Grounded analysis/planner:** separate speech/visual caches, provider health,
   feasible-duration handling, generated music and honest review outcomes.
4. **Travel recipes and captions:** postcards/adventure/diary, framing presets,
   native styles; silent clips skip subtitles and ducking. Preserve user edits.
5. **Quality gate:** compare corrected Goa output with the preserved result;
   check pacing, framing, music, captions, and exact cut points. Test mixed frame
   rates, silent/no-audio media, portrait/landscape, interrupted jobs, and reopen.

## Build context

This checkout pins `v26.08.1`, not moving upstream master. Its CMake requirements
are Qt >= 6.10, KDE Frameworks >= 6.21, and MLT >= 7.38. Use the checkout's
`CMakeLists.txt` and `dev-docs/build.md` as authority when these change.

The initial host is Apple Silicon. CMake and Craft were not on PATH; the complete
KDE/MLT SDK was not provisioned. About 9 GB of disk space was available at setup.
The Python foundation needs no SDK or new dependencies. Native compilation and
GUI behavior remain unverified until the toolchain is available. Do not report
passing Python tests as proof that a Kdenlive build works.

The existing Homebrew Qt tools allowed compiled transport tests on this host.
They linked Qt 6.9.0; this is sufficient for the transport test target but below
the editor's minimum. The host's newer macOS SDK needed an explicit `arm_acle.h`
include in the standalone test target. No system toolchain files were changed.

No public fork remote or release was created. `upstream` points to KDE Invent;
do not push feature commits there. The clone is shallow; fetch additional history
only when needed for a deliberate upstream update.

On September 30 the user completed the full Craft build. Native test startup
needed a scoped VPX library search path and correct discovery of Craft's
`plugins/mlt` directory. Relocatable MLT ignores `MLT_REPOSITORY` unless the app
passes it explicitly. The development launcher supplies SDK paths without
modifying cached dependency binaries. A Qt macOS accessibility crash occurred
when inspecting an import dialog as it closed; importing via the command line
and inspecting the settled editor worked. The user accepted the working editor
and requested moving on to assistant development; export testing was deferred.

The user then selected **Keep Gemini**. The native dock now sends local media
references and briefs to a bounded worker. Visual observations are content-cached,
with two videos processed concurrently. Three travel pacing presets constrain
model cuts. Repeated content, unsupported windows, overflow, poor observations,
and excessive durations are rejected. API configuration was migrated to a private
user settings file. Live synthetic Gemini runs took 9.16 s and 2.14 s (all three
analyses cached on retry), each proposing 225 frames at 25 fps. Desktop Plan/Apply,
Stop, cached retry, and duration-shortfall reporting were exercised. Narration,
ElevenLabs music/effects, captions, full temporal analysis and quality review remain
open; do not describe this first visual planner as a completed production editor.


On October 1 the user supplied fifteen Goa test clips and requested a native AI
button, clean chat interface and an implementation checklist before continuing.
The dock now uses Kdenlive's existing actions/palette and provides multiline
messages, contextual brief refinements, session-only project isolation, actual
edit task statuses, readable shot previews, Apply, Stop and run logs. Opening a
new project cleared the previous conversation in the actual editor.

Testing the real clips exposed empty boundary samples: one source's video stream
ends at 0.667 s but its audio/container ends at 0.810 s. The worker now bounds
visual windows to video stream duration and samples inside each window, with a
new cache version and synthetic regression. Fifteen observations were obtained;
the first valid four-shot draft lasted 9.48 s. Feasible-duration guidance and a
refinement produced eight shots totaling 535 frames (17.85 s at 30000/1001 fps),
with all fifteen analyses cached. The initial successful run took 49.16 s; the
cached refinement took 23.15 s. This comparison also changes the brief and is not
a controlled editorial-quality benchmark. Sub-second travel cuts are rejected.
Native Apply/Undo/Redo, first-cut seeking, saving, Stop and conversation reset were
exercised. Mixed-orientation framing, audio, temporal evidence, shot rationale and
exported output remain open. See work-plan.md for the staged checklist.

The subsequent workspace refinement puts the AI toggle at the far right and
opens a full-height native right dock. Restored layouts move it out of asset tabs;
Library stays with the bin, the source Clip Monitor is hidden, and source
selection keeps the timeline monitor active. Native build/`assistanttest` and
desktop open/close, source selection and saved-project restoration passed.
Preset workspace switching remains a separate manual check (the local Load
Layout submenu was empty). Packaging and releases remain deferred.

## Temporal evidence and native export — October 1, 2026

Analysis version `video-windows-4` replaces two-still sampling with bounded silent
video windows sampled at four FPS. It records validated usable offsets, skips
sub-second sources, and adds per-shot editorial reasons plus duration feasibility.
A live retry preserved successful partial analysis after a provider response
failure; that initial failure remains undiagnosed. The native repeat reused 12
analyses and skipped 3 micro-clips, with no further footage analysis calls.

A 7-shot native draft exported 474 frames (15.8158 s) at 30000/1001 FPS. Its six
cut boundaries, one Undo/Redo, saved-project reopening, and first-cut seeking were
checked. The test project is `montaz-review/goa-temporal-review.kdenlive` beside
the supplied clips; the exported review is `~/Movies/goa-temporal-review.mp4`.
They stay outside Git. Different briefs produced different lengths; these runs
are integration evidence, not a controlled quality benchmark.

Development runtime presets/render helper were absent from the app bundle; the
launcher now stages them. Native conversion also failed on phone auxiliary audio
with an unknown codec. Conversion now maps recognized streams and rejects
nonzero process exits. Synthetic native regressions cover unsupported audio,
multiple valid tracks, silence and partial failed output. Real phone conversion
and native render completed. SDK/macOS accessibility inspection after closing a
dialog still crashes; the saved project reopened successfully.

The output is a visual prototype: substantial pillarboxing and no sound are its
largest remaining quality gaps. Continue with explicit orientation/native framing,
then ElevenLabs music and measured audio timing. Narration/captions and bounded
revision follow; publication and packaging remain deferred.

## Sparse cached sound cues — October 1, 2026

The Add sound menu now offers described sound and cached-footage suggestions.
Only selected timeline sources are resolved; unrelated missing imported assets
cannot block cue lookup. Current visual cache entries are read locally with two
readers, never reanalyzed. Gemini selects up to three known sparse foley categories;
local code derives usable intervals, spacing and audio availability. A selected
cue requires separate ElevenLabs generation, Listen and native Apply. Reviews
are bound to source hash and native context; missing evidence and zero cues end
explicitly, and Stop discards late results. This is not audible-event detection.

100 Python, 29 Qt transport and 7 chat checks pass; native `assistanttest` passes.
The separate Goa cue project reused three analyses, skipped four unavailable
entries and generated one 51-frame pool-water cue at frame 188. Preview,
Apply/one Undo/Redo, exact pool seeking, save/reopen, Stop and native export were
checked. All 474 decoded visual frames match the previous sound export; the mix
measures -17.9 LUFS / -4.7 dBFS true peak. See quality-review.md for limitations.
Next gates remain separately cached speech/narration, silent-input skipping and
native caption styles, plus bounded editorial revision. Portrait framing and
packaging/publication remain deferred.

## Reviewed speech captions — October 1, 2026

The Captions menu now recognizes bounded placed original audio, reviews plain
words and approximate timing, and applies one native undoable subtitle batch.
Separate normalized-PCM evidence caching retains valid empty results; local
silence/absent audio finishes without Gemini setup. Assistant music/effects and
muted audio are excluded. Processed/overlapping audio and existing subtitles are
explicitly unsupported in this first iteration. Stop, progress and failures have
terminal states; logs contain counts, never speech text.

114 Python, 32 Qt transport and 7 chat checks pass, together with native
`assistanttest` and upstream `subtitlestest`. Native tests exercise source changes,
rollback, fractional timing, style stability and headless loading. A duplicate
subtitle insertion now reports failure instead of creating a false Undo success.
Command-line subtitle loading no longer dereferences an absent main window.

The separate Goa caption test uses a controlled synthetic speech fixture. Live
recognition completed in 3.94 seconds; retry reused speech evidence. Desktop
Apply/one Undo/Redo, Clean/Bold/Original switching and save/reopen passed. Style
changes preserved the subtitle file exactly. Desktop and command-line exports
both decoded 474 frames at 30000/1001 FPS, with readable captions. Desktop frames
152/153 confirm the reviewed exclusive end boundary. This fixture has a black
prefix from manual audio insertion; it is not a new travel-quality benchmark.
Original footage, prior reviews and exports remain preserved outside Git.

Approximate recognition timing is editable, not forced word alignment. Narration,
ducking and one bounded editorial revision remain open. The known Qt/macOS
accessibility inspection crash remains unresolved. Portrait framing and
packaging/publication remain deferred.

## Bounded draft quality review — October 1, 2026

Each validated visual plan now receives one Gemini text review using its existing
observations. It may keep the draft or propose one complete revision, preserving
total frames and passing the original source/recipe checks. There is no reanalysis,
critic loop or automatic retry. Failed/oversized review is explicitly unavailable;
the original source-valid draft remains unreviewed and requires manual inspection.
Stop discards late results. Native review is capped at 45 seconds within the
existing three-minute run deadline; logs keep only outcome/code.

127 Python, 34 Qt transport and 7 chat checks pass; the native editor rebuild and
`assistanttest` pass. Controlled real planning on five cache-matching Goa sources
returned four shots / 193 frames at 30000/1001 FPS and a kept review in 7.70 seconds.
A second real native Qt/Python transport run reused those five entries, returned
the same shot count/span, passed strict metadata/progress checks and exited cleanly
in 14.32 seconds. Different briefs/runtimes make these observed timings, not a
controlled performance or quality comparison.

The separate desktop project adds an empty test sequence, preserving earlier
edits. Its full-bin run failed during Gemini analysis before review. Subsequent
inspection reproduced the macOS accessibility crash; the crash stack starts in
`NSAccessibilityAttributeAccessorInfo` and AX hierarchy traversal. Successful new
desktop review/Apply/export remains unverified. No revised travel film is claimed.
Finished-sequence replacement, narration/ducking and perceptual comparison remain
pending; portrait framing and packaging/publication stay deferred.

## Run diagnosis — October 1, 2026

Gemini failures now preserve distinct safe codes for output limits, blocked or
empty answers and rejected requests. Partial JSON is never accepted, requests
are not retried automatically, and failed quality review remains explicitly
unavailable. HTTP 400 is no longer indiscriminately described as an authentication
failure. Native transport and run records preserve these codes without response
text, credentials or prompts. 130 Python, 35 Qt transport and 7 chat checks pass;
the native editor rebuild and `assistanttest` pass.

The full Goa bin cache discrepancy is explained by source conversion: seven
resources changed from originals to new encoded files (some also changed length).
All seven originals still match their old content-addressed entries; the converted
files correctly require new evidence. A fresh two-source-concurrent analysis
completed in 20.65 seconds: five hits, seven new successful entries, three
sub-second sources skipped. This does not establish the cause of the earlier
generic model failure, which cannot be reconstructed from the old logs.

A standalone Qt tree/list app reproduces the selected-children macOS crash at
the same Cocoa plugin offset as Kdenlive. Upstream QTBUG-149612/change 765434
addresses synthesized table elements incorrectly deleting the parent interface.
A scoped development plugin backport now builds reproducibly and passes native
selected-row/list-replacement checks. The opt-in launcher verifies source/patch,
Qt SDK version/architecture/location and SDK/plugin binary hashes. Craft's global
plugin remains intact. See `assistant/tools/qt-cocoa/README.md`; this is an
under-review upstream patch, not an officially released Qt dependency fix.

The full-bin native planning/review completed in 20.21 seconds with twelve cache
hits and a kept three-shot/240-frame draft. Desktop Apply/one Undo/Redo, save and
fresh-instance reopening pass. Native Rendering finished in 11 seconds; output
is exactly 240 frames/8.008 seconds at 30000/1001 FPS. First/last and both sides of
cuts at 75/126 contain imagery, with no added black frames. Native seeking at 75
matches the exported pool shot. The earlier 474-frame sequence is preserved.
132 Python, 35 Qt transport, 7 chat tests and native `assistanttest` pass.

The draft still undershoots its 20-second brief despite a 708-frame feasible
estimate. This is an integration checkpoint, not evidence of improved film
quality. Native existing-edit replacement/refinement, explicit recipe coverage,
perceptual comparison and narration/ducking remain pending. Portrait framing and
packaging/publication remain deferred.


## Object-match milestone — October 1

The user accepted a first object-aligned match-cut prototype before the remaining
beat/whip/wipe/ramp work. Two adjacent selected video-only instances are captured
against the native revision. Gemini selects one compatible object pair or an
explicit empty answer; a separate bounded `montaz-tracker` uses existing MLT and
OpenCV CSRT to decode/track every frame of the outgoing last/incoming first second.
Uniform native Transform crops align centers and apparent heights, capped at 2×,
with shape, appearance, motion and canvas-coverage gates. Size smoothing prevents
integer tracking-box jitter becoming breathing zoom. Video-stream tail checks
avoid frozen audio-only tails. The separate `object-match-2` cache retains complete
pair evidence by content, trim, FPS, canvas and model; source hashes are rechecked.

The right dock has Match cut, a two-by-two original/aligned boundary comparison,
explicit Apply and Stop. Native application preserves source/timing/IDs, checks
selection, revision, replay and file identity, and commits one Undo. Persistent
clip groups remain ineligible; temporary multi-selection is supported through a
small TimelineModel query. No manual anchor correction, full-bin candidate search,
beat measurement, whip/wipe recognition or time-remap proposal exists yet.

The editor/helper compile with Craft. 142 Python checks run (140 pass, two optional
native checks skipped); both optional real tracker checks pass separately. 38 Qt
transport and seven chat checks pass. Native `assistanttest` passes; generated
moving-disc QA exercises real tracking, native Apply/Undo/Redo, MLT serialization
and a 120-frame H.264 export at 30000/1001 FPS, with boundary center/height checks.
Desktop Gemini selection, comparison, Apply and Undo/Redo passed; the saved test
project contains both 30-frame native animations. A fresh-process desktop reopen loaded both native effects and cleared the
assistant session; the original Goa review project remains untouched. The headless tests enable Qt video effects only for this
target; the initial crashes were test setup/cleanup defects, not editor failures.
Two selected cuts in the latest Goa draft returned no compatible objects, which
correctly leaves their timeline unchanged. These checks prove the narrow
integration, not better travel-film quality or identity tracking on all footage.

### Local rhythm checkpoint, October 1

While Claude works on object candidates in the isolated collaboration branch,
Codex added conservative local beat estimates for Align cuts. Multi-band onset
recurrence supports 60–180 BPM estimates; ambiguous music retains labelled energy
accents. Content/FPS/span/version caching, strict protocol-v4 timing metadata,
redacted measurements and post-analysis music verification are implemented.
Legacy timing-v2 remains accepted; object-match protocol-v3 is unchanged.

Temporary selection now works for native retiming. A controlled native export
moved cuts 48/102 to 50/100, preserved all 150 frames and byte-identical audio,
and passed one Undo/Redo and serialization. Real short Goa music returned weak
periodicity without changing the project. 148 Python checks pass (two optional
tracker checks skipped), 39/7 Qt transport/chat checks pass, and the editor/full
native tests plus the independent render check pass. See [music timing](music-timing.md).
Downbeats/phrases, variable-tempo evaluation and complete film quality remain open;
Claude's branch is not reviewed or merged until the user reports completion.
