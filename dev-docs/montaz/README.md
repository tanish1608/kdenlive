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

## Current state

- Upstream: KDE Invent, tag `v26.08.1`, commit
  `55e16e85cd9a9c6e032cd27a621137b4da881a7c`.
- Branch: `codex/ai-assistant-foundation`; remote `upstream` is read-only by intent.
- Implemented and tested: proposal contracts, strict wire parsing, a deterministic
  Python worker, Qt process transport, bounded Stop/deadline handling, native-side
  validation, batch rollback/commit logic, and redacted run diagnostics.
- Compiled and exercised behind `BUILD_MONTAZ_ASSISTANT=ON`:
  dock, sequence snapshot/revision bridge, proposal list, and undoable application
  through Kdenlive's own insertion API. It is disabled in default builds.
- Native tests pass; desktop preview, Apply, Undo/Redo, save/reopen, and seeking
  were exercised with six silent synthetic clips at 24/25/30 fps, including
  portrait sources. macOS SDK/test loading fixes are included.
- Still pending: export validation, additional native failure checks,
  provider integrations, audio pairing, and travel recipe execution. The current
  worker is a video-only integration diagnostic, not an editorial model.
- See [native integration](native-integration.md) for exact verification limits.

## First integration milestone

A native dock starts a local Python worker using `QProcess`. The dock sends an
immutable snapshot of the active sequence, receives an edit proposal, previews
the proposed changes, and applies an approved proposal as one undoable native
operation. A manual edit invalidates an outstanding proposal. Replayed messages
cannot insert a second copy of the sequence.

The current implementation uses a deterministic six-clip proposal before connecting any
model. Verify apply, undo, redo, seek, save/reopen, cancellation, and export.
Then connect the planner and evaluate real footage. This isolates integration
failures from model-quality failures.
