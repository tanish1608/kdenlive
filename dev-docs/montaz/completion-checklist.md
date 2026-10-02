<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# App completion checklist

Updated October 2, 2026. This is the authoritative active checklist. Older
completion plans record history. Baseline: `a044759` on
`codex/ai-assistant-foundation`. Build on the existing assistant.

`[x]` means integrated and verified within the stated limits. `[ ]` means pending,
including delivered work awaiting review. Every completed item needs a commit
and an evidence link. Claude records delivery in its own report; Codex checks
items here after review, combined tests and applicable native/output verification.
Add newly discovered failures under the affected item. Passing tests alone does
not establish perceptual quality.

## Verified foundation

- [x] F01 — Right AI dock, native project preview, brief refinements, task states,
  Stop and redacted run logs. [Evidence](work-plan.md).
- [x] F02 — Cached visual observations, two-source analysis concurrency, visual
  duration bounds and partial-result preservation. [Evidence](quality-review.md).
- [x] F03 — Feasible-duration planning, grounded story guidance, one bounded
  review/underfill repair and explicit unavailable review. [Evidence](editorial-duration.md).
- [x] F04 — Replace the session-owned draft with one native Undo, preserving
  manual neighbors/audio/subtitles. [Evidence](draft-revision.md).
- [x] F05 — ElevenLabs instrumentals and occasional reviewed effects, reuse,
  loudness preparation and native audio insertion. [Evidence](quality-review.md).
- [x] F06 — Conservative estimated beat cuts with labelled accent fallback;
  one native Undo. Downbeats/phrases are unfinished. [Evidence](music-timing.md).
- [x] F07 — Selected-original-audio captions, silence skipping and stable
  Clean/Bold/Original style changes. [Evidence](quality-review.md).
- [x] F08 — Selected-pair landmark match cuts, guarded zoom tracking, source
  resolution caps, Compare/Apply/Undo/reopen/export. Perspective, crop and
  landmark precision remain limited. [Evidence](landmark-integration.md).

## Claude: independent packages assigned now

Each package can start from the same baseline and uses separate files. These
are analysis/data/test deliveries; editor integration remains a separate gate.
Detailed scope, branches and acceptance criteria: [Claude assignment](claude-next-tasks.md).

- [ ] C01 — Cached-evidence shot discovery: bounded, deterministic suggestions
  of possible matching subjects, excluded sources and explicit empty results.
  Scene labels alone must never count as verified object matches.
- [ ] C02 — Local motion/occlusion evidence: bounded low-resolution frame
  analysis, directional movement and occlusion candidates, with conservative
  negative fixtures and cancellation. No transition is applied by this module.
- [ ] C03 — Versioned travel recipe data: story slots, pacing, music arc,
  transition preferences and missing-footage fallbacks for postcard/adventure/diary.
- [ ] C04 — Audio quality regression pack: variable/ambiguous rhythm, silence,
  non-speech noise and speech timing/bounds. Deliver reproducible failures and
  measurements; no provider calls or changes to shared audio code.

## Codex: next integration milestones

- [ ] N01 — Review C01–C04, fix cross-boundary issues, merge each separately and
  update this checklist with evidence. No automatic acceptance of Claude reports.
- [ ] N02 — Add reviewed compatible-shot suggestions to the dock; bind source
  windows, current selection, revision and identities; budget provider/tracker
  work across the whole request. Depends on C01.
- [x] N03a — Native quick-montage foundation only: 8–12-frame copies, ordered
  bounded source trims, one common framing target, one Undo and rollback;
  synthetic frame geometry/MLT reload/export checks. Commit `21a6b32`.
  [Evidence](montage-foundation.md). No worker or dock action is exposed yet.
- [ ] N03 — Add an explicit quick match montage: 8–12-frame shots, one shared
  framing per shot, native reorder/trim/insertion contract and one Undo. Preserve
  source handles and resolve middle-shot framing jointly. Native foundation
  N03a is verified; discovery/solver/review and real footage remain. Depends on N02.
- [ ] N04 — Manual landmark correction in the review, with typed native scope,
  tracking/coverage gates and stale-result rejection.
- [ ] N05 — Refine wheel/rim landmarks locally in the native helper; measure
  full-subject boundary error and reject incompatible perspectives. Preserve
  genuine zoom and source-resolution caps.
- [ ] N06 — Integrate versioned travel recipes and truthful missing-shot
  explanations; retain source bounds and existing cached observations. Depends on C03.

## Remaining transitions and editorial quality

- [ ] T01 — Use measured same-direction movement for reviewed whip/pan cuts;
  validate positive and negative real footage and native cut-frame exports.
  Depends on C02 and a native transition contract.
- [ ] T02 — Use measured full-frame occlusion for object wipes; partial foreground
  motion, darkness or fades must not automatically count as a valid wipe.
- [ ] T03 — Native speed ramps with valid source handles, time-remap Undo,
  audio policy and save/reopen/playback/export agreement. Add blur only when
  rendered evidence shows a benefit.
- [ ] T04 — Improve cut-point selection, shot variety, openings/endings and
  movement continuity; compare identical briefs/sources before and after changes.
- [ ] T05 — Evaluate landmark variability, small zooms, deformation and heavy
  crops; extend real-footage evaluation beyond the single wheel pair.
- [ ] T06 — Improve musical timing for variable tempo/downbeats/phrases with
  measured evidence; retain honest accent fallback when unsupported. Uses C04.

## Speech, captions and sound

- [ ] A01 — Explicit per-project visual-highlight versus source-speech mode;
  speech-led edits use verified complete thoughts, never inferred dialogue.
- [ ] A02 — Preserve original speech/ambient audio with native audio/video
  pairing, valid trims and one Undo; handle processed/overlapping audio explicitly.
- [ ] A03 — Speech-aware music/effect balance and native gain/fade/ducking
  proposals; verify rendered loudness, intelligibility and absence of clipping.
- [ ] A04 — Final caption lifecycle/style audit: silent/no-audio projects settle,
  repeated style switching preserves IDs/text/timing, Stop and changed projects
  discard late results, and save/reopen/export agree. Existing simple cases work.
- [ ] A05 — Improve caption segmentation/readability and speech interval accuracy;
  separate optional word-aligned animation from existing style presets.
- [ ] A06 — Review sparse sound choices, timing and playback/export balance on
  complete travel and speech-led projects, beyond individual effect checks.

## Reliability, efficiency and completion audit

- [ ] Q01 — Native failure injection and preservation checks across every new
  operation: rollback, replay, relink, profile/sequence/project switch, locked
  tracks, linked groups, source changes and unsupported timelines.
- [ ] Q02 — Audit all long-running phases for deadlines, Stop, orphan children,
  settled task states and safe retry. Reproduce earlier endless critic/caption
  failures; no recursive review or automatic provider retry loops.
- [ ] Q03 — Measure cold/warm analysis and incremental import: unchanged clips
  reuse caches, changed clips invalidate correctly, duplicates avoid redundant
  work and concurrency stays bounded. Record provider/helper counts and latency.
- [ ] Q04 — Add bounded run-log retention and useful numeric quality/budget
  diagnostics while preserving redaction; missing/invalid keys fail clearly.
- [ ] Q05 — Native monitor/playhead and exported cut-frame audit across mixed
  rates, fractional FPS, new short montages and time remaps. No second player.
- [ ] Q06 — Complete a silent travel project from import through story/music/
  timing/transitions/captions decision to reviewed native export.
- [ ] Q07 — Complete a speech-led project through audio-preserving edit, music,
  captions and native export; impossible briefs finish with clear explanations.
- [ ] Q08 — Final matched-input output review: intentional story/endings, fewer
  jarring cuts, no accidental repeats/frozen tails, useful sound/captions, and
  documented negatives. Record actual pending features before calling the app complete.

## Deferred or optional

These do not block the current app milestone and are not assigned for implementation.

- [ ] D01 — General portrait/reframing support: deferred by the user.
- [ ] D02 — Public repository, licensing/release audit, installers and desktop
  releases: deferred until the app is ready.
- [ ] D03 — Generated narration and animated word captions: optional follow-ups;
  current speech-led scope edits original speech.
- [ ] D04 — Conversation persistence and broader complex-timeline support:
  later product decisions; retain current explicit eligibility/session limits.

## Execution order

Claude starts C01–C04 independently. Codex starts N03's native contract and tests
against synthetic reviewed inputs, then integrates C01 through N02 and validates
N03 on real clips. After that: N04/N05 and N06, measured T01/T02, then T03;
speech/audio work and Q checks complete the app audit. Claude's independent
packages may proceed sequentially or in isolated branches in parallel. Neither
engineer changes the other's owned files without an explicit updated handoff.
