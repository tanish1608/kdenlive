<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Montaz engineering collaboration

Updated October 1, 2026. This is the handoff for Claude as an implementation
engineer and Codex as the integration reviewer. Read it before changing code.
This document assigns work; unchecked tasks have **not** been implemented.

**Current assignment:** [completion plan](completion-plan.md). M1–M5 below are
historical and merged in `8b6dcfc`; Claude’s next branch is
`codex/claude-match-stability`. Follow its ownership and S1–S5 checklist.

## Workspace and branch

| Item | Location / value |
| --- | --- |
| Active project | `/Users/tanishvardhineni/Documents/montaz/kdenlive` |
| Integration branch | `codex/ai-assistant-foundation` |
| Feature baseline | `59887df` — reviewed native object-aligned match cuts |
| Claude branch | `codex/claude-match-cut-candidates` |
| Claude worktree | `/Users/tanishvardhineni/Documents/montaz/kdenlive-claude` |
| Preserved old editor | `/Users/tanishvardhineni/Documents/montaz/montaz` |
| Upstream baseline | Kdenlive `v26.08.1`, `55e16e85cd9a9c6e032cd27a621137b4da881a7c` |

The Claude branch starts after the handoff-document commit on the integration
branch. Use the isolated worktree; do not switch the integration checkout's
branch, edit the old Electron repository, or use its `dev.sh`/backend tests.

Only `upstream` (KDE Invent) is currently configured. Do not push feature work
there. No public fork, PR or release exists for this assignment. Commit locally
and return the branch and validation report. When a personal fork is configured,
open a PR against **`codex/ai-assistant-foundation` on that fork**, not KDE's main
branch. Codex reviews and merges after the gates below pass. Publication and
packaging remain deferred.

## Product goal and accepted decisions

Build a useful AI video editor on Kdenlive's existing interface and engine.
The user chose this route to stop maintaining an unreliable second timeline,
player and render pipeline. Keep Gemini for footage understanding/edit planning;
use ElevenLabs for instrumental music and occasional sound effects. Longer-term
projects should choose narration or visual highlights. Narration generation is
not implemented yet.

The assistant opens as a full-height right dock from the right-aligned AI button.
The Project Monitor remains the native playback authority; source selection does
not reopen the Clip Monitor. Chat should explain proposed edits and actual work,
with explicit Apply, Stop and honest empty/error states.

Travel editing should use observed footage, purposeful pacing and endings, and
eventually verified beat cuts, graphic matches, directional whips, object wipes
and speed ramps. The current object-match prototype is the first transition
milestone. General portrait framing is explicitly deferred; targeted cropping
for a reviewed match cut is allowed. Do not ship installers or publish the repo.

## Architecture and invariants

1. **Kdenlive/MLT owns mutable state:** bin, tracks, source trims, rational frame
   timing, monitor, subtitles, effects, Undo/Redo, save/load and export.
2. **C++ dock/adapter owns the boundary:** capture immutable snapshots, run one
   worker per editor process, preview proposals, revalidate on the GUI thread,
   and apply through native undoable commands. A successful batch creates one
   Undo entry; a failed batch rolls back. Failed rollback blocks further edits.
3. **Python owns analysis and planning:** standard-library worker, content caches,
   bounded provider requests and immutable proposals. It never overwrites live
   project files or executes model-provided code/effect expressions.
4. **Transport is versioned JSON lines over QProcess**, not HTTP, WebSockets,
   CRDT or another orchestration framework. Strict schemas reject unknown fields,
   duplicates, invalid numbers, stale revisions, changed sources and replay.
   Wire version 1 is distinct from operation-specific proposal versions 1/2/3/4.
5. **Stop and failure settle the run.** Own and terminate subprocesses, discard
   late results, preserve complete cached evidence, require a clean worker exit
   before Apply, and never label failed review as approval.
6. **Keep project data private.** Keys stay in environment/private settings;
   prompts, transcripts, frames, filenames, paths and worker stderr stay out of
   run logs. Git contains source and synthetic fixture generators, not media,
   caches, credentials, user projects or generated audio.

Read the root [AGENTS.md](../../AGENTS.md), [architecture](architecture.md),
[context](context.md), and
[assistant usage](../../assistant/README.md). The root AGENTS.md takes precedence;
older milestone sections in the integration guide are historical evidence.

## Implemented behavior and ownership map

| Area | Current behavior | Main files |
| --- | --- | --- |
| Chat / workspace | Right dock, contextual brief refinements, session-only conversation, task states, Stop and run logs | `src/assistant/assistantwidget.*`, `chatview.*`; `src/mainwindow.cpp` |
| Native boundary | Snapshot/revision, source verification, reviewed native commands, rollback, replay prevention | `src/assistant/editoradapter.*`, `proposal.*`, `editbatch.*` |
| Worker / lifecycle | One request per process, strict messages, bounded deadlines, ordered progress, clean exit | `src/assistant/workerclient.*`; Python `worker.py`, `protocol.py`, `lifecycle.py`, `contracts.py` |
| Visual planning | Two-source analysis concurrency, sampled silent windows, usable trims, content cache, postcard/adventure/diary pacing, no repeated source content | Python `planning.py`, `gemini.py` |
| Draft review | One cached-evidence review and at most one same-duration revision; unavailable remains explicit | Python `editorial.py` |
| Music / timing | ElevenLabs instrumental, raw/prepared reuse, native audio insertion, -18 LUFS target, fades; small cut shifts to estimated beats with accent fallback | Python `music.py`, `beats.py`, `timing.py`; native adapter |
| Sound effects | Described ElevenLabs sound at playhead; up to three cached-visual foley suggestions, separately generated/reviewed | Python `effects.py`, `sound_cues.py` |
| Captions | Selected original audio only, silence skipping, separate speech cache, reviewed native subtitles; Clean/Bold/Original without recreating events | Python `captions.py`; native adapter and subtitle model |
| Object match | Two adjacent video-only clips; one Gemini request for ≤3 seed pairs, ≤2 native CSRT-tracked pairs, lowest crop/motion cost, bounded uniform Transform animations, static comparison, one Undo | Python `match_cuts.py`; `src/assistant/matchtracker.cpp`; dock/adapter/contracts |
| Local dependencies | Craft runtime staging and optional scoped Qt Cocoa accessibility ownership backport | `assistant/tools/prepare_craft_macos.py`, `run_craft_macos.sh`, `qt-cocoa/` |

Python filenames above are under `assistant/src/montaz_assistant/`.
Protocol changes require both Python and native validators/tests; do not add
fields to an existing envelope assuming they will be ignored.

The latest match cache version is `object-match-5` after the reviewed stability
integration; see `match-stability-report.md`. It keys complete candidate
evidence by source hashes, source windows, canvas, rational FPS, Gemini model and
algorithm version. Empty or geometrically unsuitable candidate sets are cacheable;
provider failures, runs where every tracked pair failed, and budget-truncated runs
are not.
Only selected sources are read, with a 128 MiB per-source prototype limit.

Match eligibility requires normal-speed, adjacent VideoOnly instances on one
unlocked video track, no existing bin/clip effects, persistent groups, mixes or
compositions, and a progressive square-pixel project at up to 60 FPS. Temporary
multi-selection is supported. Track the last outgoing/first incoming second at
every project frame. A video-stream bound check rejects audio-only tails.
Appearance/shape/motion gates are proxies, not calibrated identity confidence.
Size smoothing retains measured centers. Uniform scaling is capped at 2× with
canvas coverage; existing letterboxing can remain. Timing/order/IDs stay fixed.

## Known quality gaps and measured evidence

- The 15 private Goa sources total about 30.81 seconds. A 60-second edit requires
  repetition or generated material, which the current planner forbids.
- The most recent reviewed Goa draft is **240 frames / 8.008 seconds** at
  30000/1001 FPS despite a 20-second brief and a 708-frame feasible estimate.
  A model's “kept” decision is not proof of a good film. Duration fulfillment,
  story coverage, abrupt camera movement and endings still need improvement.
- Music accents are energy peaks, **not verified musical beats or phrases**.
  The measured earlier music export was -18.1 LUFS / -5.0 dBFS true peak.
- Caption times are approximate speech intervals, not word alignment. Narration,
  ducking, processed/overlapping audio, and animated word captions remain open.
- Two tested Goa cuts returned no compatible objects. They stayed unchanged;
  we have not searched every Goa pairing or produced a better full travel film.
- Controlled disc tracking, native Apply/Undo/Redo, serialization and a 120-frame
  H.264 export pass. This proves narrow integration, not arbitrary footage quality.
- The macOS Qt AX crash required a scoped development backport. Do not modify
  installed SDK binaries or present this as a packaged dependency fix.

See [output review](quality-review.md) for historical exports and limitations.
Baseline validation: 142 Python checks discovered (140 pass, two optional native
checks skip without the helper); both optional tracking checks pass separately;
38 Qt transport and seven chat checks pass; native `assistanttest` and controlled
render QA pass. Re-run relevant checks and report the new counts.

## Claude's first PR: bounded match candidate selection

Complete **M1–M5 together in one focused PR**. Improve selection within the two
already selected clips; do not search/reorder the whole project in this PR.

Status (October 1, 2026): implemented by Claude on `codex/claude-match-cut-candidates`
and reviewed by Codex. The local integration includes the corrections below and
the separate beat-timing work; no upstream push or public PR is planned.

Codex committed Python review corrections as `856dd6c` on
`codex/review-match-cut-candidates`:
reject enormous and slightly out-of-frame model coordinates before tracking;
validate cached previews against native canonical base64/JPEG boundaries; retain
media-child exit status so only explicit seeded subject rejection (status 5)
permits trying another pair. Decoder/filter failures, crashes and timeouts stay
terminal. This is a small internal `MediaChildError` addition to the shared media
utility; the public error codes, native helper and final protocol-v3 envelope are
unchanged. Cache version 4 prevents reusing version-3 evidence with ambiguous
failed-track provenance. Added regression checks cover each correction. Desktop reload also exposed a
pre-existing Transform keyframe inconsistency: position was animated while
rotation/rotation-anchor remained at their default key. Integration synchronizes
these constant parameters with position, with a native loading regression.

- [x] **M1 — Offer multiple credible seeds in one Gemini call.** Request at most
  three candidate object pairs from the same two native seed images. Define a
  strict bounded internal schema and validate coordinates, reasons, lengths,
  numeric types and duplicates. Empty candidates remain valid. Do not add an
  automatic provider retry or loosen protocol-v3 output validation.
  *Outcome:* `parse_candidates()` accepts exactly `{reason, candidates}` with at
  most three `{reason, boxes:[outgoing, incoming]}` entries; unknown/missing
  fields, blank or over-long reasons (UTF-16 units, as native counts), bool,
  nonfinite, string or out-of-frame coordinates fail as `invalid_match_evidence`
  before any tracking. Duplicate JSON keys fail in the decoder; repeated pairs
  are collapsed in M2. Malformed answers previously surfaced as `invalid_request`.
- [x] **M2 — Choose locally, with bounded tracking.** Deduplicate candidates and
  deterministically order them using explicit geometric criteria. Track at most
  two candidate pairs (four tracked windows), reusing the original seed images.
  Select the valid result with the lowest documented crop/motion cost and stable
  tie-breaking. Reuse existing tracking/alignment gates; no new CV dependency,
  arbitrary “confidence” score, retiming, source substitution or automatic Apply.
  *Outcome:* `rank()` runs each seed pair through `align()`; failures are skipped
  untracked. Order: least seed crop, closest aspect, larger smaller subject, then
  provider order. A pair overlapping a better pair by ≥0.5 IoU on both sides is a
  duplicate. The top two are tracked from the same seed frames. `metrics()`
  defines zoom, crop = mean `1 - 1/scale²` and motion = mean per-frame largest
  canvas displacement of any source corner; cost = crop + 10 × motion. Lowest
  rounded cost wins, ties keep the earlier rank.
- [x] **M3 — Preserve lifecycle/cache semantics.** Respect the current 90-second
  native deadline and local cancellation; do not launch another candidate without
  sufficient remaining budget. Cancellation, authentication, malformed output,
  media changes and timeouts remain terminal. An unstable candidate may permit
  trying the next bounded candidate; an entirely failed tracking run must not be
  cached as a valid empty semantic answer. Bump match cache identity when its
  meaning changes. Preserve scene/speech caches and the existing final wire
  envelope plus finding/tracking progress. New native progress fields are out of
  scope. No second model request for alternate objects.
  *Outcome:* the first pair is always tracked as before; a second starts only if
  35 s (two 15 s helper timeouts plus a re-hash) remain of an 80 s Python budget
  inside the native 90 s deadline. Stop is checked before each pair. A helper
  rejection (`tracking_unavailable`) moves to the next pair; every other error is
  terminal. If every tracked pair fails, the run fails `tracking_unavailable`
  and nothing is cached. Budget-truncated runs return their result uncached.
  Cache version `object-match-3`; the wire review and finding/tracking progress
  are unchanged.
- [x] **M4 — Add meaningful regressions.** Cover first candidate unusable / second
  valid, all unsuitable, all tracking failures, empty candidates, duplicates,
  malformed/nonfinite/bool coordinates, tie-breaking, source changes, Stop
  between candidates, budget exhaustion and content-cache reuse. Verify provider
  and tracker invocation ceilings. Use synthetic evidence; mock external services.
  *Outcome:* 16 new tests in `MatchCandidateTests` cover each listed case,
  including inconsistent cache entries treated as misses, steadier-track preference,
  and one provider call with at most six helper calls.
- [x] **M5 — Return reproducible output evidence.** Extend the controlled QA tool
  to exercise the new selection path, preserving its native apply/Undo/export
  checks. Report chosen-candidate rationale, zoom, crop/motion metric definitions,
  call counts, elapsed time, exact frames and cache reuse. Separate the offline
  provider fixture from any optional live Gemini result. Update this checklist,
  assistant usage and quality notes with actual outcomes and remaining limits.
  *Outcome:* `check_match_quality.py` now offers flat, disc and zoom-heavy pairs.
  Real tracking rejects the flat pair, selects the disc and never tracks the third,
  then the native apply/Undo/export checks pass. `selection-report.json` records the
  metrics; see [quality review](quality-review.md#multi-candidate-match-selection).

Expected changes: `match_cuts.py`, a small helper module if useful,
`assistant/tests/test_match_cuts.py`, optional native-helper fixture tests,
`assistant/tools/check_match_quality.py`, and related documentation. Keep public
`propose_match` and its protocol-v3 final review compatible with the native code.
Do not refactor `MediaTools`, the worker dispatcher, dock/adapter, timeline,
caption/music code or upstream SDK as part of this assignment. If a necessary
contract change appears, document it for Codex instead of silently expanding scope.

## Subsequent backlog — not assigned in this PR

| Priority | Next work | Exit evidence |
| --- | --- | --- |
| 1 | Manual anchor correction and wider match candidate discovery | Native snapshot/source checks, review, one Undo, bounded searching; no forced matches |
| 2 | Feasible duration + richer travel story coverage | Synthetic underfill regression and matched-brief before/after exports; no filler/repetition |
| 3 | Verified beat/phrase analysis and hard cuts | Known rhythmic fixtures, exact native cut frames, ambiguous music fallback |
| 4 | Directional pans/whips and full occlusion wipes | Observed motion/occlusion evidence, conservative eligibility, native Undo/export |
| 5 | Speed ramps / motion blur | Source handles, native time remap, audio policy, save/load/export and Stop |
| 6 | Narration, word captions, native audio ducking | Real audio-grounded timing, silent/empty terminal states, stable style/Undo |
| Later | Portrait framing and releases | User-deferred; do not start now |

## Build and validation on this Apple Silicon Mac

Python: `/Library/Frameworks/Python.framework/Versions/3.12/bin/python3`.
Craft SDK: `/Users/tanishvardhineni/CraftRoot`. The shared integration build is
`$MONTAZ_CRAFT_ROOT/build/kde/kdemultimedia/kdenlive/work/build`; its CMake source
is the integration checkout. **Do not rebuild/reconfigure that directory for
Claude's branch or install Claude binaries into the SDK.** Use a separate build.

From Claude's worktree, run the dependency-free tests first:

```sh
cd /Users/tanishvardhineni/Documents/montaz/kdenlive-claude
PYTHONPATH=assistant/src /Library/Frameworks/Python.framework/Versions/3.12/bin/python3 \
  -m unittest discover -s assistant/tests -v
/Library/Frameworks/Python.framework/Versions/3.12/bin/python3 \
  assistant/tools/run_native_tests.py \
  --qmake /Users/tanishvardhineni/CraftRoot/bin/qmake
```

Use `.clang-format` and four-space typed Python. Existing Ruff is available at
`/Users/tanishvardhineni/Documents/montaz/montaz/.venv/bin/ruff`; lint changed
Python files without introducing unrelated whole-tree formatting changes.
The Git formatting hook needs Craft's `bin` on `PATH` when committing.

For a separate native build, activate Craft then explicitly return to the worktree
(Craft activation can change the working directory):

```sh
export MONTAZ_CRAFT_ROOT=/Users/tanishvardhineni/CraftRoot
export CLAUDE_REPO=/Users/tanishvardhineni/Documents/montaz/kdenlive-claude
export CLAUDE_BUILD=/Users/tanishvardhineni/Library/Caches/montaz/claude-native-build
export CRAFT_PYTHON_BIN=/Library/Frameworks/Python.framework/Versions/3.12/bin/python3
source "$MONTAZ_CRAFT_ROOT/craft/craftenv.sh"
cd "$CLAUDE_REPO"
cmake -S "$CLAUDE_REPO" -B "$CLAUDE_BUILD" -G Ninja \
  -DBUILD_TESTING=ON -DBUILD_MONTAZ_ASSISTANT=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$MONTAZ_CRAFT_ROOT" \
  -DCMAKE_INSTALL_PREFIX="$MONTAZ_CRAFT_ROOT"
cmake --build "$CLAUDE_BUILD" --target kdenlive assistanttest montaz-tracker -j4
export DYLD_FALLBACK_LIBRARY_PATH="$MONTAZ_CRAFT_ROOT/lib"
export MLT_REPOSITORY="$MONTAZ_CRAFT_ROOT/plugins/mlt"
export MLT_DATA="$MONTAZ_CRAFT_ROOT/share/mlt"
export MLT_PROFILES_PATH="$MLT_DATA/profiles"
export QT_QPA_PLATFORM=offscreen
ctest --test-dir "$CLAUDE_BUILD" -R '^assistanttest$' --output-on-failure
"$CRAFT_PYTHON_BIN" assistant/tools/check_match_quality.py \
  --craft-root "$MONTAZ_CRAFT_ROOT" --build-root "$CLAUDE_BUILD" \
  --output-dir /Users/tanishvardhineni/Movies/montaz-claude-match-quality
```

The native tracker tests opt in with `MONTAZ_TRACKER="$CLAUDE_BUILD/bin/montaz-tracker"`
and `MONTAZ_FFMPEG="$MONTAZ_CRAFT_ROOT/bin/ffmpeg"`. Keep the MLT/library environment
above and run unittest discovery with `-p test_native_match_tracker.py`.
Use the helper built from the branch under test; baseline binaries cannot prove
changed native behavior. If prerequisites are unavailable, report that honestly.

The normal `run_craft_macos.sh` launcher calls `cb kdenlive`, which resolves to the
**shared integration build**, even when invoked from another worktree. Do not use
it to claim Claude's GUI changes were tested. Codex will exercise the candidate
branch's own staged app with the scoped Cocoa fix before merge. Coordinate use
of the shared desktop; never overwrite/relink the user's live project.

Private Goa input: `/Users/tanishvardhineni/Downloads/goa-clips-test`.
Preserve its `montaz-review/` projects. Use a new native Save As project if testing
live footage; do not put inputs, exports or cache files into either worktree.
The controlled baseline video is at
`/Users/tanishvardhineni/Movies/montaz-match-quality/native-alignment.mp4`.
Provider credentials are already in private settings; do not print/read their
values into reports. Offline work requires no provider account or new packages.

## Completion report, review and merge

Claude should return:

1. Branch name, final commit SHA, base SHA and PR URL if a personal fork exists.
2. Completed M1–M5 checklist, concise behavior explanation and changed-file list.
3. Exact test commands/outcomes, skipped checks, actual helper/build paths and
   controlled output paths. Include cache/call-budget evidence and failure cases.
4. Any remaining quality limits or protocol/dependency changes needing review.

Codex reviews the diff against `codex/ai-assistant-foundation`, checks schemas,
provider/child budgets, cancellation, cache identity, native revision/source
validation and privacy, then reruns relevant tests and native rendered QA from
the candidate branch. Exercise review/Apply/Undo/Redo and saved native effects
in the editor before accepting output-quality claims. Fix or return actionable
findings; merge only after required gates pass. Claude must not merge its own PR,
force-push, broaden the assignment or publish artifacts.

Local review commands (after committing all Claude changes):

```sh
git -C /Users/tanishvardhineni/Documents/montaz/kdenlive \
  diff codex/ai-assistant-foundation...codex/claude-match-cut-candidates
git -C /Users/tanishvardhineni/Documents/montaz/kdenlive \
  log --oneline codex/ai-assistant-foundation..codex/claude-match-cut-candidates
```

## Ready-to-use instruction for Claude

> Work in `/Users/tanishvardhineni/Documents/montaz/kdenlive-claude` on
> `codex/claude-match-cut-candidates`. Read root `AGENTS.md` and
> `dev-docs/montaz/collaboration.md`, then complete M1–M5 as one focused change.
> Preserve the native protocol and ownership boundaries. Run the specified
> regressions and controlled render checks, commit your work, and return the
> completion report for Codex to review and merge. Do not push to KDE upstream,
> publish releases, overwrite user footage/projects, or merge your own work.
