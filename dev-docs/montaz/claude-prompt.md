<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Prompt for Claude

Work as an implementation engineer on Montaz's Kdenlive assistant in
`/Users/tanishvardhineni/Documents/montaz/kdenlive-claude`.

Read root `AGENTS.md`, `dev-docs/montaz/completion-checklist.md`,
`dev-docs/montaz/claude-next-tasks.md`, `collaboration.md`, `architecture.md` and
the latest `context.md`. The new assignment supersedes historical M/S tasks.
Preserve the completed landmark branch. Start from the planning commit containing
these documents on local `codex/ai-assistant-foundation`; record its SHA and use
that same base for each independent branch.

Complete all four packages in the assignment:

1. C01: bounded cached-evidence compatible-shot suggestions.
2. C02: local movement/occlusion measurements with conservative negatives.
3. C03: versioned postcard/adventure/diary recipe data and truthful fallbacks.
4. C04: synthetic audio quality regressions and reproducible measurements.

Use the specified branches and allowed files. Work sequentially, or use separate
worktrees if running parallel workers. Codex owns existing shared modules, native
editor/tracker, worker/contracts and production integration. Your analysis/data
modules must not edit a timeline or claim unverified transitions. Document any
required cross-boundary changes for Codex.

Run focused and full offline Python checks, lint changed files, and report skips,
resource limits, failure cases and quality limitations. Commit each package
locally with its own report. Return branch/base/final SHA, changed files, interfaces,
test outcomes and private QA output paths. Do not mark the master checklist
complete; Codex will review, integrate and verify output first. Do not push,
merge, publish, use credentials, alter SDK/shared builds or overwrite user projects.
