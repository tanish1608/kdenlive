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
9. [Completion plan](completion-plan.md): current Codex/Claude ownership, immediate checklists and finish criteria.
10. [Music timing](music-timing.md): conservative beat estimates, accent fallback, caching and native export checks.

## Current state

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
- Local **Align cuts** proposes bounded shifts to estimated beats in existing music,
  with explicit energy-accent fallback.
  It preserves shot order, source starts, clip IDs, music and total span; reviewed
  changes use native resize/move operations and one Undo. Complex timelines are
  ineligible. Estimates do not identify downbeats or phrases.
- **Add sound** generates one explicitly described ElevenLabs sound effect at the
  playhead, with bounded duration, private raw/prepared reuse, preview and native
  undoable audio insertion. Its **Suggest sounds** action reuses cached visual
  observations for sparse reviewed foley cues, followed by separate selected
  generation. This is not audio-event recognition.
- **Captions** recognizes bounded original timeline audio, skips silence, caches
  speech separately, and applies reviewed native subtitles. Clean/Bold/Original
  presets preserve event text and timing. Word alignment remains pending.
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
  calibration. See [stability evidence](match-stability-report.md).
  This is a selected-pair prototype; it does not search the entire bin.
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
