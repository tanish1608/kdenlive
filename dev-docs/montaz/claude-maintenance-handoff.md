<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Claude: improve the existing app

Assigned October 2, 2026. The user has paused new features and delegated the
maintenance queue to Claude while Codex is away. Work autonomously through this
queue; commit completed fixes and continue to the next task without waiting for
Codex. Codex reviews and merges the delivered branch when available again.

## Scope and authority

Improve existing behavior only: correctness, output quality, efficiency,
diagnostics and usability. This assignment supersedes the historical file-owner
restrictions in `claude-next-tasks.md` and `collaboration.md`: Claude may change
shared assistant Python modules, existing native assistant/adapter/tracker code,
their tests and QA tools where an evidenced fix requires it. Codex is not editing
these files during this maintenance assignment. Architectural invariants remain.

Do not add compatible-shot suggestions, quick montage UI, manual landmark UI,
recipe integration, whips/wipes/ramps, new speech modes, audio pairing/ducking,
animated captions, portrait support, new providers or dependencies. Do not
implement an unfinished checklist feature under the name of a refactor. Existing
pair matches, beat cuts, captions, music/effects and visual editing are in scope.
Keep transitions optional and explicit. Packaging and publication remain deferred.

Kdenlive owns live project state, frame time, playback, subtitles, Undo and export.
Use its existing native commands; never rewrite a live project XML file. Preserve
revision checks, source bounds, source-resolution zoom caps, rollback, redaction
and honest unavailable results. Do not redesign the editor or introduce another
player, service, agent framework or automatic provider retry loop.

## Checkout and baseline

Use `/Users/tanishvardhineni/Documents/montaz/kdenlive-claude`, after checking for
uncommitted work and active processes. Preserve all previous Claude branches,
including `codex/claude-match-landmarks`. Never reset/clean or switch Codex's
integration checkout. A reserved branch, `codex/claude-existing-app-hardening`,
contains this assignment and the current integrated code.

The code baseline is `a6ffb86f8aa67d3ae6438731c0a527e5b74d54da`; the assignment
commit adds documentation only. Verify that baseline is an ancestor of your
branch. Do not start from an old C04/C01 branch: all four packages and Codex's
audio/chat fixes are already integrated. Read root `AGENTS.md`, `architecture.md`,
latest `context.md`, `claude-integration-review.md`, `quality-review.md`,
`draft-revision.md`, `landmark-integration.md` and `completion-checklist.md`.

Baseline validation: 281 Python tests, 271 pass and ten optional native-tracker
skips; 47 Qt client/command checks and seven chat checks; native `assistanttest`
passes. Reproduce relevant baselines before changing them. There are no remaining
expected-failure audio tests to fix blindly.

## Priorities and work packages

Each item needs a reproducible case, the smallest useful fix, targeted regression
coverage where appropriate, and honest validation. If the behavior already works,
record the audit evidence and continue; do not manufacture changes. Preserve
working controls and current eligibility limits. Keep focused commits per fix.

### H01 — Stop, deadlines and terminal states (first)

Audit visual analysis, planner, critic/review, music, effects, beat analysis,
captions, pair matching and native preview. Test Stop at each phase, provider
stalls/errors, decoder/helper hangs, worker exit without a result, rapid Stop/send
and project/sequence changes. Fix endless task rows, orphan child processes,
duplicate completion, late results and controls remaining disabled. Every run
must settle once; no recursive reviews or unbounded repair attempts.

Primary files: `lifecycle.py`, `worker.py`, `planning.py`, `gemini.py`,
`src/assistant/workerclient.*`, `assistantwidget.cpp`, related tests.

### H02 — Silent/no-audio caption completion

Reproduce silence, video-only sequences, absent streams, tiny clips, nearly
silent PCM, music-only fixtures and decoder errors. Ensure silent/unavailable
requests return clearly and do not contact recognition unnecessarily. Prevent
invented subtitles in deterministic negative fixtures; do not claim a general
speech detector from amplitude or promise a provider cannot hallucinate.

Primary files: `captions.py`, `worker.py`, caption/worker/native UI tests.

### H03 — Caption style and lifecycle consistency

Cycle Clean → Bold → Original repeatedly with several cues and existing styles.
Preserve IDs, words and frame intervals; verify native Undo/Redo, save/reopen and
export. Test Stop and stale results during recognition/style changes. Fix style
leaks, duplication, wrong subtitle targeting and unsettled controls. No animated
or word-aligned caption feature.

Primary files: `captions.py`, `editoradapter.cpp`, `assistantwidget.cpp`,
`proposal.cpp`, `tests/assistanttest.cpp`, related caption tests.

### H04 — Caption frame bounds and readability

Extend coverage of the new same-frame phrase fix at 24000/1001, 30000/1001 and
60000/1001 FPS, nonzero source/timeline offsets, selected ranges and end frames.
Verify every recognized word is retained when representable, cues stay ordered
and bounded, and invalid answers do not poison cache reuse. Improve existing
segmentation/line breaks only within current text/schema/native limits. Never
invent dialogue or silently remove words to make tests pass.

Primary files: `captions.py`, caption/audio regression tests and QA tool.

### H05 — Safe native Apply, replacement and Undo

Failure-inject the existing visual insertion/replacement, generated audio,
retiming, captions and pair-transform paths. Exercise stale/replayed requests,
locked tracks, selection/sequence/profile changes, source relinks, groups,
occupied tracks and manual neighboring edits. Verify one Undo, rollback and
unrelated content preservation. Fix supported-path bugs; keep unsupported
complex timelines explicitly ineligible rather than adding support.

Primary files: `editoradapter.*`, `editbatch.*`, `proposal.*`,
`tests/assistanttest.cpp`, contracts/draft/transport tests.

### H06 — Existing chat intent and interface polish

Keep Add music / Transitions / Captions; no Add sound button or fixed workflow.
Audit tool aliases for negation, punctuation, empty descriptions, invalid
durations, multiline/composite briefs and accidental visual planning. Make
malformed explicit requests give useful replies. Ensure menu/keyboard activation,
Enter versus Shift+Enter, double-send, Stop, New brief and error recovery behave
consistently. Do not turn this into an unbounded LLM action router. Do not enable
the coming-soon transitions. Test only justified command/layout improvements.

Primary files: `chatcommands.*`, `chatview.*`, `assistantwidget.*`,
native/client/chat tests. Desktop capture previously dismissed the popup;
the visible-widget native test checks menu activation and item states.

### H07 — Existing visual script and cut quality

Use identical briefs, evidence windows and source sets to compare current
planning/review output before and after changes. Correct abrupt/invalid trims,
accidental repeated footage, weak opening/ending selection, frozen tails,
infeasible duration claims and insufficient shot variety when the current
evidence actually permits improvement. Keep grounded reasons, bounded review
and explicit underfill. No hidden transition insertion, extra model passes,
new recipe wiring or new edit modes. Prefer deterministic/local fixes and prompt
changes supported by measured examples over architectural churn.

Primary files: `planning.py`, `editorial.py`, existing planning/duration/editorial
tests, `check_editorial_quality.py`. Record real perceptual limitations separately
from contract/test correctness.

### H08 — Cached analysis and incremental imports

Measure cold/warm runs, adding one new clip, altered content at the same path,
duplicate files, corrupt/incomplete entries, cancellation during cache writes
and concurrent requests. Verify unchanged footage reuses observations, modified
footage invalidates, cache writes are atomic and partial results survive Stop.
Fix duplicate decoding/provider work and unnecessary cache misses while retaining
content identity and existing bounded concurrency. Record counts and latency;
do not simply increase parallelism or lower analysis quality.

Primary files: `planning.py`, provider/media helpers, relevant tests/tools.

### H09 — Existing beat-cut correctness

Extend the integrated synthetic regression pack for initial audible music,
weak/ambiguous attacks, short/silent input, fractional FPS and source handles.
Check before/after cut frames and beat errors on the same fixtures. Fix incorrect
beat claims, octave mistakes when a conservative decision is possible, offset
mapping, lost duration or incorrect native retiming. Retain labelled accent
fallback. No new variable-tempo/downbeat/phrase feature; bump affected cache
versions if behavior changes and record why.

Primary files: `beats.py`, `timing.py`, retiming adapter/proposal code,
beat/timing/audio/native tests and existing QA tools.

### H10 — Music and effect preparation/reuse

Audit existing raw/prepared reuse, exact project-frame length, offsets, edge
fades, silence/truncation, clipping, gain and native import checks. Use synthetic
PCM or already generated cached audio; compare Listen/native playback/export.
Fix resampling/rounding, cache identity and corrupt prepared files. Keep sparse
reviewed effect choices grounded and bounded. No narration, automatic ducking
or new paid generations required for this assignment.

Primary files: `music.py`, `effects.py`, `sound_cues.py`, audio/native tests,
adapter code and existing audio QA tools.

### H11 — Logs, API failures and cache budgets

Trace existing per-run diagnostics: stage times, cache hits/misses, provider/
helper counts, fallback reasons and final state. Fix missing-key/invalid-key/
quota/network failures being reported misleadingly or leaving tasks active.
Add bounded retention/size limits to existing assistant-owned logs where missing;
never delete projects, media or unrelated files. Keep keys, prompts, transcripts,
paths and raw provider responses out of reports/logs. Do not build a telemetry
service or add a diagnostics dashboard.

Primary files: `lifecycle.py`, `worker.py`, `gemini.py`, provider modules,
`workerclient.*`, diagnostics/lifecycle/worker tests.

### H12 — Native playhead and export agreement

Audit supported existing edits at mixed source rates and fractional project FPS:
seek each cut and adjacent frames, compare native monitor/timeline time and
exported pixels/counts, then repeat after Apply/Undo/Redo and reopen. Reproduce
the reported inaccurate needle before proposing a fix. Use native timing APIs;
do not add another timer/player. Limit changes to an evidenced assistant-related
bug or a narrowly demonstrated upstream defect; otherwise deliver reproduction
and diagnosis for review. Speed remaps/new montage UI remain out of scope.

Primary files: native tests and existing QA tools; assistant adapter if implicated.
Any necessary upstream timeline/monitor patch must be isolated, small and explained.

### H13 — Existing pair match/tracker stability

Test current selected-pair matches under small zooms, moving subjects, missing or
inconsistent landmarks, frame-edge boxes, seed-frame differences and different
source resolutions. Fix exceptions, stale/misapplied transforms, cache mismatch,
unbounded tracking and unjustified confidence. Measure full-subject geometry and
crop before/after; preserve real zoom and resolution caps. Do not force incompatible
cars to align, raise zoom caps to get matches, add manual correction/discovery,
or build the future joint montage solver.

Primary files: `match_cuts.py`, `match_stability.py`, `matchtracker.cpp`,
`scaleregistration.*`, related native/Python tests and existing match QA tools.

### H14 — Final regression sweep and current-workflow review

Rebase no branches automatically. On the single cumulative maintenance branch,
run combined Python/Qt/native checks and applicable existing controlled render
checks. Review one supported silent travel workflow and one already-supported
placed-audio/caption workflow, including eligible music/beat/pair-match tools only
when explicitly requested. Record source/profile/frame counts, settled states,
cut errors and visible/audible defects. Do not call unfinished speech-edit modes
or the full app complete. Classify unresolved findings by severity and give exact
reproductions. Keep generation/export outputs outside Git.

## Efficient execution and validation

Start H01–H05; then H06–H11; then H12/H13 and H14. Record newly found blocking
bugs under their package and fix them before cosmetic tuning. If a task cannot
be validated, report the limitation and continue independent tasks. Avoid broad
refactors, universal invented quality thresholds or tests that merely mirror code.

Use offline fixtures first. Optional existing Gemini checks may use authorized
test footage and private settings, with an explicit total budget of at most eight
provider requests for this assignment; never loop on failures or expose keys.
Reuse cached audio instead of requesting paid music/effect regeneration. If a
real clip/provider requirement blocks proof, leave that proof pending honestly.

Use the build/test instructions in `collaboration.md`. The independent native
build is `/Users/tanishvardhineni/Library/Caches/montaz/claude-native-build`:
compile the changed branch there, not in Codex's Craft integration build. Do not
install over Craft or change the global SDK/plugin. The normal development
launcher selects the shared integration build; it cannot prove your branch's UI.
Stage your own app resources using the existing tools with explicit build roots,
and validate those options first. Coordinate use of the desktop; preserve live
projects and use private fixture copies. If GUI inspection is unavailable, native
tests are useful but label desktop validation as pending.

Use real helper binaries from the branch being validated for opt-in tracker
tests. Run the combined Python suite after each related group, Qt checks when
transport/chat changes, and build/CTest for native changes. Broaden testing after
new failures or related changes, not merely to run the same checks repeatedly.
Respect `.clang-format`; the commit hook needs Craft `bin` on `PATH`. No new
dependencies. Keep artifacts under a new `~/Movies/montaz-claude-maintenance/`
directory, preserving earlier files. Never commit credentials/media/caches.

If you use parallel subagents, isolate worktrees and nonoverlapping files. Shared
worker/planning/adapter changes must be serialized and checked together. The
primary Claude engineer owns the cumulative branch and integration tests.

## Delivery and ongoing progress

Commit locally as you go. Do not push, publish, merge into Codex's branch, launch
a release, or automatically message another chat. Keep your own
`dev-docs/montaz/claude-maintenance-report.md` with this checklist:

| Item | State | Reproduction/fix or clean audit | Commit | Validation and limits |
| --- | --- | --- | --- | --- |
| H01–H14 (one row each) | pending / working / delivered / blocked | Evidence | SHA | Exact outcomes, skips and artifact paths |

Update it after each completed group, and continue without waiting for review.
Do not mark the master checklist complete or rewrite historical evidence. Your
final handoff must list branch/base/head, focused commits, changed files,
combined tests, measured before/after quality, native/desktop checks actually
performed, remaining bugs and deferred validation. Delivered means ready for
Codex review; it does not mean merged or app complete.
