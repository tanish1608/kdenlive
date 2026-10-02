<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Montaz completion plan — October 1, 2026

## Target and current evidence

Finish a useful editing assistant in Kdenlive: grounded travel stories, intentional
cuts, stable object alignment, music/sparse sound, reliable captions, and explicit
review before native application. Packaging, publication and generic portrait
framing remain deferred. Existing code is the foundation; do not restart it.

Integration commit `8b6dcfc` includes Claude M1–M5, reviewed error/cache fixes,
local beat estimates and synchronized native Transform keys. It passes 169 Python
checks, 39 Qt transport/seven chat checks, native tests and controlled exports.
These demonstrate integration, not a finished film. The latest Goa draft is only
8.008 seconds for a 20-second brief. Tracked discs still show approximately 1%
added canvas motion per frame, largely from scale jitter. Two existing Goa cuts
have no credible matching objects. Preserve those honest negative results.

## Ownership and order

| Milestone | Codex owns | Claude owns | Exit gate |
| --- | --- | --- | --- |
| 1 — Immediate, parallel | Duration feasibility, story/pacing, bounded underfill repair and explanations | Stabilize tracked match-cut scale/position without hiding genuine motion | Same-brief draft comparison and quantified tracking fixtures |
| 2 — Combine the workflow | Review/replace an assistant draft through native commands; explicit beat-then-match ordering; manual anchor review | Bounded candidate discovery design and implementation after a separate handoff | Complete native edit with Apply/Undo/Redo/save/reopen/export |
| 3 — Grounded travel transitions | Native cut/transition/time-remap contracts, UI and render validation | Bounded direction/occlusion evidence, then ramp eligibility after separate handoffs | Actual movement/occlusion fixtures, conservative negatives, decoded output |
| 4 — Completion audit | Speech-led project mode, original-audio handling, captions/sound integration and desktop QA | Focused failure/quality regressions assigned after integration | Both silent travel and speech-led projects meet the checks below |

Only milestone 1 is assigned now. Later milestones are sequenced work, not
permission for either engineer to change the other's files. Codex reviews and
merges every Claude delivery locally. No external messages, pushes or releases.

## Codex: immediate checklist

- [ ] Reproduce the 20-second Goa brief using existing cached evidence; record
  requested, usable, feasible and delivered frames and why shots were omitted.
- [ ] Make feasibility agree with the actual distinct-source/trim/recipe rules;
  summed sampled windows must not promise capacity the validator cannot use.
- [ ] Improve opening, development, details/activity and ending guidance using
  observed footage. Missing story material must be explained, never invented.
- [ ] Detect substantial avoidable underfill before review. Use a bounded shared
  repair allowance with explicit total call/deadline limits; no critic/retry loop.
  Preserve a shorter coherent result when usable relevant material is exhausted.
- [ ] Add regressions for the current underfill, impossible 60-second requests,
  short sources, duplicate source ranges, cancellation and unavailable review.
- [ ] Export before/after edits for the same brief and sources. Compare all cuts,
  duration, coverage, endings and music timing; document failures as well as wins.

Primary files: `planning.py`, `editorial.py`, their tests and necessary native
metadata/UI validation. Codex owns shared contracts, worker dispatch and media
utilities. Do not make Claude's match-cut improvements depend on this work.

## Claude: next assignment — stable object alignment

Use `/Users/tanishvardhineni/Documents/montaz/kdenlive-claude`. Preserve the
completed `codex/claude-match-cut-candidates` branch. With a clean worktree, create
`codex/claude-match-stability` from the current local
`codex/ai-assistant-foundation` after this plan is committed; it must contain
`8b6dcfc`. Do not switch the integration checkout or start from the old candidate
branch. Read root `AGENTS.md`, this plan, `collaboration.md`, `context.md`, and
`quality-review.md` before implementation.

- [ ] **S1 — Establish a reproducible baseline.** Separate translation, scale
  change and scale acceleration in private QA reports. Keep existing crop/motion
  metrics comparable. Explain why the present five-frame smoothing still breathes.
- [ ] **S2 — Stabilize without falsifying tracking.** Compare a small number of
  deterministic fits/smoothers. Reduce size jitter while preserving real zoom and
  subject movement, exact boundary alignment, uniform aspect ratio, 2× ceiling,
  full canvas coverage and existing rejection gates. Reject unsafe fits rather
  than forcing a match. Do not add CV/ML dependencies or provider calls.
- [ ] **S3 — Exercise difficult evidence.** Include stable-size moving objects,
  genuine gradual zoom, short windows, edge-constrained objects, discontinuous
  motion/appearance, first-pair rejection, all failures and deterministic ties.
  Evaluate boundary error and motion throughout both windows, not only two stills.
- [ ] **S4 — Preserve lifecycle and identity.** Keep one Gemini call, at most two
  tracked pairs, current deadlines/Stop, terminal error classification and strict
  protocol-v3 output. Bump match cache version when output geometry changes;
  preserve scene/speech/music caches and the native synchronization fix.
- [ ] **S5 — Deliver evidence.** Extend controlled QA to export comparable
  baseline/candidate videos. Provisional goal: halve RMS frame-to-frame scale
  change on constant-size fixtures without worsening existing boundary tolerances;
  genuine zoom must retain direction and acceptable fit error. Record crop, maximum
  scale, scale acceleration, residual motion, calls, elapsed time and cache reuse.
  These are engineering criteria; visible improvement also needs Codex review.

Allowed files: `assistant/src/montaz_assistant/match_cuts.py`, a small adjacent
pure helper if useful, `assistant/tests/test_match_cuts.py`,
`assistant/tests/test_native_match_tracker.py` and
`assistant/tools/check_match_quality.py`. Record outcomes in a **new**
`dev-docs/montaz/match-stability-report.md`. Do not edit shared planning/media,
protocol/worker/native/dock, caption/music code or shared overview documents.
A necessary cross-boundary change should be reported for Codex to handle.

Run offline tests, Ruff on changed Python, both real tracker tests using the
branch's native helper, Qt checks and controlled native/render QA. Follow the
separate-build instructions in `collaboration.md`: the normal launcher resolves
to the shared build and cannot prove changed native branch code. No native change
is expected here. Return branch/SHA/base, changed files, test counts/skips, fixture
outputs, before/after measurements and limitations. Commit locally; do not merge.

## Subsequent implementation decisions

**A coherent editing workflow.** Beat alignment currently rejects effect-bearing
shots; create/revise the story, add music, review cut timing, then apply object
alignment. Reordering those steps needs explicit native support, not weakening
eligibility. Revisions must identify assistant-owned content and preserve manual
edits, subtitles, unrelated tracks and generated audio. Manual object anchors need
an editor-issued typed request, source/revision checks and the same tracker gates.

**Candidate discovery.** Search a bounded subset using cached observations to
propose useful neighbors before expensive tracking. Report source-window evidence,
call limits, cancellation and an empty result. Changing clip order belongs to
Codex's reviewed native proposal. Do not track every possible pair or fabricate
object correspondence from coarse scene labels.

**Whips and wipes, then ramps.** Same-direction camera motion and full-frame
occlusion must be measured locally near possible boundaries. Begin with
source-valid cuts concealed by actual movement/occlusion. Add effects only where
native output demonstrates a benefit. Speed ramps need real Kdenlive time-remap
operations, source handles, playback/export parity, audio policy and one Undo;
blurred canned effects alone do not satisfy the feature.

**Speech-led work.** Use verified source speech and complete thoughts for narrated
highlights; do not infer dialogue from visuals. Make project mode explicit. Test
silence/no audio, mixed rates, caption style changes and music/effect balance.
Generated narration and animated word captions are separate follow-ups unless
required by the agreed completion scope; do not silently add another provider.

## Completion checklist

- [ ] A silent travel project and a speech-led project each complete from import
  through review, native Apply and export; impossible briefs have clear explanations.
- [ ] Matched-brief exports show intentional openings/endings, varied grounded
  footage, no accidental repeats, no frozen tails and fewer jarring boundaries.
- [ ] Supported beat/match/motion transitions are demonstrated; unavailable matches
  finish cleanly. Speed ramps are either validated natively or explicitly unfinished.
- [ ] Stop/error/project-switch/stale-result paths terminate, preserve user edits
  and do not leak credentials or source content into run logs.
- [ ] Captions on silent media terminate; changing styles preserves text/timing/IDs.
- [ ] Native seeking, one Undo/Redo per applied proposal, save/reopen, playback and
  decoded exported frames agree, including fractional FPS.
- [ ] Codex reviews Claude's diffs, reruns relevant combined checks, records quality
  limits and commits the verified integration. Completion requires output review,
  not merely model approval or passing tests.
