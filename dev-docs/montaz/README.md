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

## Current state

- Upstream: KDE Invent, tag `v26.08.1`, commit
  `55e16e85cd9a9c6e032cd27a621137b4da881a7c`.
- Branch: `codex/ai-assistant-foundation`; remote `upstream` is read-only by intent.
- Implemented: offline proposal validation and explicit run/stage lifecycle.
- Not yet implemented: native AI dock, editor snapshot bridge, atomic application
  of proposals, provider integrations, travel recipe execution, or GUI regression
  validation. The foundation is not yet a working AI editor.
- No native editor core has been replaced or modified in this foundation.

## First integration milestone

A native dock starts a local Python worker using `QProcess`. The dock sends an
immutable snapshot of the active sequence, receives an edit proposal, previews
the proposed changes, and applies an approved proposal as one undoable native
operation. A manual edit invalidates an outstanding proposal. Replayed messages
cannot insert a second copy of the sequence.

Start with a deterministic six-clip synthetic proposal before connecting any
model. Verify apply, undo, redo, seek, save/reopen, cancellation, and export.
Then connect the planner and evaluate real footage. This isolates integration
failures from model-quality failures.
