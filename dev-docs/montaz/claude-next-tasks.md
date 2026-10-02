<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Claude assignment: independent completion packages

Assigned October 2, 2026. Current tasks are C01–C04 in the
[active checklist](completion-checklist.md); old M/S assignments are complete.
Codex owns integration, native contracts/editor/helper, worker/transport,
planning/editorial/provider/media utilities and the active checklist.

## Branches and boundaries

Use `/Users/tanishvardhineni/Documents/montaz/kdenlive-claude`. Preserve
`codex/claude-match-landmarks` (`e186bc1`); its reviewed merge is `a044759`.
Start each package from the planning commit on local
`codex/ai-assistant-foundation` containing this assignment. Record the exact base
SHA before starting; use that same immutable base for all four packages.
Check for uncommitted work and preserve it; do not reset/clean it or switch the
integration checkout. Create one branch per package:

| ID | Branch | Allowed code/test files |
| --- | --- | --- |
| C01 | `codex/claude-shot-discovery` | `assistant/src/montaz_assistant/shot_discovery.py`, `assistant/tests/test_shot_discovery.py` |
| C02 | `codex/claude-motion-evidence` | `assistant/src/montaz_assistant/motion_evidence.py`, `assistant/tests/test_motion_evidence.py`, `assistant/tools/check_motion_evidence.py` |
| C03 | `codex/claude-travel-recipes` | `assistant/src/montaz_assistant/travel_recipes.py`, `assistant/tests/test_travel_recipes.py` |
| C04 | `codex/claude-audio-regressions` | `assistant/tests/test_audio_quality_regressions.py`, `assistant/tools/check_audio_quality.py` |

Each may add only its own `dev-docs/montaz/claude-C01-report.md` (or C02/C03/C04).
Do not change existing modules, shared overview/checklist, C++/Qt, SDK, project
files or dependencies. If integration needs a shared change, document the proposed
interface and exact reason for Codex. These modules are not public wire contracts.
Do not push, publish, merge your own work or contact another task automatically.

Read root `AGENTS.md`, `collaboration.md`, `architecture.md`, latest `context.md`,
`landmark-integration.md` and `handoff-match-sequencing.md`. This assignment
supersedes their historical task ownership, not the architectural invariants.

## C01 — Cached-evidence shot discovery

Build a pure deterministic shortlist over already-validated visual windows:
source/bin ID, window ID, absolute source in/duration, description, role and
quality; use project `FrameRate`. Specify your input/output types in the report.
Accept caller-issued evidence records rather than locating project caches or
decoding media. No network, filesystem mutation, tracking or provider calls.

Default ceiling: 24 input windows, six subject groups, six candidates per group,
24 suggested pairs overall. Reject excessive/malformed input rather than
silently doing unbounded work. Check caller cancellation inside bounded loops.
Collapse duplicate content when caller supplies a content identity; avoid
same-source/overlapping-range pair suggestions. Missing identity must not imply
two sources are identical. Stable ties use native IDs and source frames.

Use conservative documented text/role evidence to suggest possible shared
subjects, with reasons and exclusions. Wheel/car examples and unrelated travel
scenes need tests. Coarse labels cannot establish common landmarks, geometry,
perspective compatibility or a verified match. No all-pairs tracking or sequence
reordering. Codex will obtain/review exact geometry with a separate total budget.

## C02 — Local movement and occlusion evidence

Build a standard-library pure analyser for caller-supplied grayscale frame
buffers and integer project-frame positions. Bound width/height to at most
96 pixels and frames to 24 per window; reject invalid shapes, nonfinite values,
unordered frames and oversized inputs. Check caller cancellation during search.
No media decoder changes, model calls, new CV dependency or native operations.

Use fixed documented search/iteration caps (for example an eight-pixel search
radius); at most eight windows per QA invocation. Measure dominant translation
through bounded local correlation/search with
texture and ambiguity checks; report actual measurements and an explicit
unavailable outcome. Detect potential occlusion intervals conservatively using
temporal/spatial evidence, not mean brightness alone. Distinguish static texture,
translation directions, independently moving foreground, noise, fade-to-black,
partial occlusion and complete occlusion in synthetic fixtures. A textureless
window does not prove an object wipe.

Return evidence with frame bounds and measurement limits, not an instruction to
apply a transition. A pure pair assessor may propose same-direction boundaries;
camera pan versus moving foreground remains uncertain. If full object occlusion
cannot be established from these inputs, report it as unverified rather than
claiming detection. The private QA tool may use existing `MediaTools`/FFmpeg to
decode bounded fixture buffers; runtime integration belongs to Codex.

## C03 — Versioned travel recipe data

Define typed versioned recipe records for postcard, adventure and diary, with
opening/development/detail-or-action/ending slots, relative duration ranges,
music arc, transition preferences and honest missing-footage fallbacks. Preserve
the current minimum shot duration and distinct-source rules; the explicit quick
match montage is a separate mode, not a hidden default recipe change.

Recipes express preferences, not permission to use unimplemented transitions or
invent footage. Resolve preferences against a caller-supplied implemented-feature
allowlist; unsupported motion effects fall back to a hard cut with an explanation.
Provide deterministic validation and examples for short/impossible requests and
missing roles. No planner changes, UI controls, media analysis or Gemini prompts
are wired by this package. Codex integrates data into planning and native review.

## C04 — Audio quality regression evidence

Exercise existing `beats.py` and `captions.py` through public helpers with
synthetic PCM/rhythmic data. Cover variable tempo, syncopation, ambiguous/weak
onsets, silent/music-only/non-speech inputs, short audio and source timing bounds.
Mock provider/decoder boundaries when needed. Verify that silence avoids speech
requests, claimed rhythm is supported, fallback is labelled, and frame offsets
respect rational FPS. No credentials, external requests or real user speech.

Record measured precision/recall/timing errors against known generated fixtures
where meaningful. Do not impose invented universal thresholds, approve music
quality from amplitude alone, or promise music/noise never causes hallucinated
provider speech. Add a bounded reproducible QA tool and honest failure report.
For discovered bugs, deliver failing evidence with the smallest proposed fix for
Codex; do not disable tests or modify existing audio/provider/native modules.

## Validation and delivery

Use dependency-free unittest, existing Ruff and the isolated-build instructions
in `collaboration.md`. Focus tests on behaviour, invalid inputs, cancellation,
resource ceilings, stable ties and conservative negatives. Run the full Python
suite after each package; explicitly distinguish optional native skips from
passes. No changed native code means no claim of branch-native validation.

Commit each package locally. In each report include base/branch/SHA, allowed-file
changes, public interface/example, exact test outcomes/skips, measured budgets,
private artifact paths and limitations. Independent reports may say delivered;
the active checklist remains pending until Codex reviews and integrates them.
If parallel workers are available, give each a separate worktree and package;
otherwise finish sequentially, creating each branch from the recorded common base.
