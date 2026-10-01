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
