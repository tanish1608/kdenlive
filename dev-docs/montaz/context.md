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
