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
- Still pending: export validation, additional native failure checks,
  narration, audio pairing, ElevenLabs music/effects, subtitle styling, and deeper
  travel recipe execution. The planner uses sampled visual evidence and inserts
  video only; real-footage editorial quality and exported output need review.
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
and retry were exercised. Continue with real-footage quality review and export.
