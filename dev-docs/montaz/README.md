<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Montaz on Kdenlive

Montaz adds AI-assisted editing to Kdenlive's existing interface and media engine.
Its focus is grounded shot selection, coherent narration or visual highlights,
travel recipes, and ElevenLabs music/effects. Kdenlive remains the editor.

## Start here

1. [Architecture](architecture.md): ownership, data flow, and integration contract.
2. [Migration context](context.md): decisions, previous failures, and work sequence.
3. [Assistant foundation](../../assistant/README.md): implemented contracts and tests.
4. [Upstream build guide](../build.md): native toolchain requirements.
5. [macOS setup](macos-setup.md): beginner instructions for dependencies, build, and launch.
6. [Implementation checklist](work-plan.md): chat milestone and footage-quality gates.
7. [Output review](quality-review.md): exported footage, measurements and remaining quality gaps.
8. [Engineering collaboration](collaboration.md): Claude's scoped assignment, isolated branch, validation and Codex review/merge workflow.
9. [Active completion checklist](completion-checklist.md): pending work, owners, dependencies and evidence; [current Claude assignment](claude-maintenance-handoff.md) prioritizes fixes and existing output quality while new features are paused. [Earlier completion plan](completion-plan.md) records history.
10. [Music timing](music-timing.md): conservative beat estimates, accent fallback, caching and native export checks.
11. [Draft revisions](draft-revision.md): native ownership, replacement, Undo and preservation checks.

## Current state

- The [maintenance integration review](maintenance-integration-review.md) records
  Claude's H01–H14 delivery, local merge `cd4cbda`, independent 290-test media
  suite, native builds and candidate desktop Stop/chat/cut-boundary checks.
  The [active checklist](completion-checklist.md) retains remaining caption,
  rollback and playback gaps. New features and releases remain paused.
- Upstream: KDE Invent, tag `v26.08.1`, commit
  `55e16e85cd9a9c6e032cd27a621137b4da881a7c`.
- Branch: `codex/ai-assistant-foundation`; remote `upstream` is read-only by intent.
- Implemented and tested: proposal contracts, strict wire parsing, a deterministic
  Python worker, Qt process transport, bounded Stop/deadline handling, native-side
  validation, batch rollback/commit logic, and redacted run diagnostics.
- Gemini visual analysis/planning now drives the dock: brief, requested duration,
  travel postcard/adventure/diary pacing, content-addressed evidence caching,
  two-video concurrency, provider errors, Stop, and a run-log shortcut.
- A native AI toolbar action opens a chat dock. Send or Enter requests a plan;
  Shift+Enter inserts a line. Refinements reuse the current brief, New brief
  clears it, and changing project/sequence clears session-only conversation.
  The task list distinguishes active work, review, Stop, failure and application.
- Follow-ups after Apply can **Replace draft** within the same exact span through
  one native Undo. Manual edits/effects/groups make replacement unavailable;
  neighbors, audio and subtitles are preserved. Ownership is session-only.
  Music, transitions and captions are optional tools; the panel imposes no fixed
  workflow. Transitions are requested explicitly from one menu or through chat.
- Compiled and exercised behind `BUILD_MONTAZ_ASSISTANT=ON`:
  dock, sequence snapshot/revision bridge, proposal list, and undoable application
  through Kdenlive's own insertion API. It is disabled in default builds.
- Native tests pass; desktop preview, Apply, Undo/Redo, save/reopen, and seeking
  were exercised with six silent synthetic clips at 24/25/30 fps, including
  portrait sources. macOS SDK/test loading fixes are included.
- Temporal analysis uses bounded silent video windows and validated usable trims.
  The preview explains shot choices and footage feasibility; logs retain numeric
  editorial metrics. A native Goa export was reviewed at all six cut boundaries.
- ElevenLabs instrumental music follows the applied visual edit: private provider
  settings, raw/prepared reuse, exact timeline duration, loudness/edge preparation,
  Listen/Stop and one native undoable import/insertion. The Goa music export keeps
  all 474 decoded visual frames unchanged and measures -18.1 LUFS / -5.0 dBFS.
- **Transitions → Beat-synced cuts** proposes bounded shifts to estimated beats in existing music,
  with explicit energy-accent fallback.
  It preserves shot order, source starts, clip IDs, music and total span; reviewed
  changes use native resize/move operations and one Undo. Complex timelines are
  ineligible. Estimates do not identify downbeats or phrases.
- A chat request such as **add a 2 second sound of waves** generates one
  explicitly described ElevenLabs sound effect at the
  playhead, with bounded duration, private raw/prepared reuse, preview and native
  undoable audio insertion. Asking **suggest sounds** reuses cached visual
  observations for sparse reviewed foley cues, followed by separate selected
  generation. This is not audio-event recognition.
- The Transitions menu also offers **Match cut**. Whip pan, Object wipe and Speed
  ramp are labelled coming soon and disabled. Reviewed Claude analysis/data/test
  packages and audio corrections are recorded in [integration evidence](claude-integration-review.md);
  library integration does not complete these transitions or quick montage UI.
- **Captions** recognizes bounded original timeline audio, skips silence, caches
  speech separately, and applies reviewed native subtitles. Clean/Bold/Original
  presets preserve event text and timing. Word alignment remains pending.
  Original currently clears a pre-existing custom global subtitle override;
  Undo restores it. Full restoration is tracked as M01.
- A visual draft receives one cached-observation quality review before Apply.
  One revision preserves total frames unless a substantially underfilled draft
  can grow toward the locally reachable target in that same bounded call. Source/recipe
  checks remain; failed review is explicitly unavailable, never approval.
  See [duration evidence](editorial-duration.md).
- **Match cut** proposes object alignment for two selected adjacent video-only
  shots. One Gemini call offers up to three pairs; at most two are tracked by
  native MLT/CSRT and the lowest crop/motion cost wins.
  Bounded uniform Transform keyframes preserve timing. Comparison, one Undo,
  caching, stale-result rejection and native rendered geometry are exercised.
  Guarded size/center fitting and native fixed-seed scale registration reduce
  controlled jitter and recover bounded gradual zoom. Real footage still needs
  calibration. Schema-constrained whole-subject landmarks now align centroid and
  spacing using the current native seed geometry, with resolution-based zoom caps.
  A real wheel pair passed desktop review, Apply/Undo/Redo, restart and native export.
  Perspective and landmark accuracy remain limits; see
  [landmark integration](landmark-integration.md) and [stability evidence](match-stability-report.md).
  This is a selected-pair prototype; it does not search the entire bin.
- The [quick-montage native foundation](montage-foundation.md) adds a validated
  8–12-frame multi-shot insertion API with shared framing, source bounds, one
  Undo and synthetic native scene reload/export checks. Discovery, the worker
  solver, dock review and real-footage acceptance remain pending; there is no
  montage button yet. This completes checklist subgate N03a only.
- Still pending: deferred portrait framing, additional native failure checks,
  narration, audio pairing/ducking, musical phrase detection, word alignment,
  and deeper travel recipe execution. Portrait footage remains pillarboxed;
  passing integration checks does not make it a finished travel film.
- See [native integration](native-integration.md) for exact verification limits.

## First integration milestone

A native dock starts a local Python worker using `QProcess`. The dock sends an
immutable snapshot of the active sequence, receives an edit proposal, previews
the proposed changes, and applies an approved proposal as one undoable native
operation. A manual edit invalidates an outstanding proposal. Replayed messages
cannot insert a second copy of the sequence.

The diagnostic remains available with `MONTAZ_ASSISTANT_DEMO=1`. The normal panel
uses Gemini. Synthetic live checks produced three grounded shots; a repeat reused
all analyses (9.16 s first run, 2.14 s cached). Native UI planning, Apply, Stop,
and retry were exercised. The subsequent Goa output check is recorded in
[quality review](quality-review.md).
