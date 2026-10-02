<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Claude package integration and optional chat tools

Reviewed October 2, 2026 on `codex/ai-assistant-foundation`. Claude's branches,
reports and landmark work are preserved. Each package was merged separately;
the historical delivery reports describe their original branch results.

## Integrated packages

| Package | Delivery | Merge | Verified scope |
| --- | --- | --- | --- |
| C01 | `7bff9cf` | `9d910a6` | Deterministic cached-label suggestions, exclusions and bounded work; suggestions stay unverified |
| C02 | `19ac8fd` | `97d274e` | Bounded translation, fade/cut/foreground measurements and unverified occlusion candidates |
| C03 | `31753a6` | `2b6e7f3` | Versioned postcard/adventure/diary data, feasible slots and explicit transition fallback |
| C04 | `20f0b41` | `9e4563c` | Synthetic rhythm/silence/caption regressions and reproducible diagnostic tools |

These are library/data/test integrations. Compatible-shot UI, joint montage
framing, recipe wiring and applied whips/wipes/ramps remain separate checklist
items. Text similarity does not establish a geometrically compatible match.

## Review corrections

Commit `b616333` fixes all three reproduced audio failures:

- The first audio block establishes the onset baseline instead of inventing a
  huge attack. Sustained pads/noise no longer suppress most actual kicks. Beat
  cache version advances to `periodic-onsets-2`.
- Caption phrases that quantize to the same frame retain their words in ordered
  nonempty intervals. If the last frame is occupied, phrases combine only within
  the existing text constraints; impossible text still fails cleanly.
- Caption mapping validates before cache publication. Unusable cached transcripts
  are discarded before one fresh provider request. There is no provider retry
  loop. Caption cache version advances to `speech-captions-2`.

Additional review fixes reserve a source for every fitting story slot before
distributing extras, handle non-ASCII numeric-looking source IDs deterministically,
and impose 60-second deadlines on the motion tool's fixture subprocesses.

The three historical expected-failure tests now pass normally. New regressions
cover final-frame phrase combination and invalid-cache recovery. A deliberately
unmappable answer proves failure leaves no cache entry and a later valid answer
can succeed.

## Measurements and validation

Private artifacts are in `~/Movies/montaz-claude-integration-20261002/`:
`audio-before/`, `audio-after/`, and `motion/`. Nothing generated enters Git.

- Combined Python suite: **281 run, 271 pass, 10 opt-in native-tracker skips**;
  no expected failures. Changed Python files pass Ruff.
- Baseline audio diagnostic reproduced three bugs; the corrected diagnostic
  reports all three fixed. Steady 72–160 BPM fixtures retain precision/recall
  1.0 and maximum 9.2 ms error. Sustained-pad fixtures recover their kicks.
- The 120 BPM noise sweep retains precision/recall 1.0 through sigma 3000;
  at 4000 recall is 0.875. This is measured synthetic tolerance, not a verdict
  on arbitrary generated music. Syncopation, tempo changes and large jitter
  retain labelled fallbacks; downbeat/phrase detection remains unfinished.
- All eight decoded synthetic motion fixtures produced the expected categories.
  Leftward movement measures −1.501 pixels/frame against −1.5. Complete/partial
  cover remains unverified; fade and independent motion do not become wipes.
  Positive real whips/occluders still need footage validation.
- Qt transport/command tests: **47 pass**; chat tests: **7 pass**. Craft builds
  the editor and `assistanttest`; native CTest passes, including toolbar/menu
  and no-document command lifecycle checks.

No live provider was used for the new regressions. Native tracker/export
validation from the previous montage and landmark checkpoints remains historical
evidence; the ten opt-in Python checks were not rerun here.

## Optional tools in chat

Commit `d09fb2d` keeps **Add music**, one **Transitions** menu and **Captions**
in the right dock.
Beat-synced cuts and Match cut use their existing native review paths. Whip pan,
Object wipe and Speed ramp are disabled and labelled coming soon. The fixed
Story → Music → Align cuts → Match cut → Captions instruction is removed.
Normal visual planning applies no transition automatically.

Sound requests use chat: `suggest sounds`, `add sound: quiet footsteps`, or
`add a 2 second sound of gentle waves`. They retain existing native eligibility,
Listen/Apply, revision checks and Undo. The Add sound button/menu is removed.
Standalone English tool aliases are anchored and bounded; negated requests,
discussion and composite editing briefs remain briefs. This is a small explicit
command router, not a general conversation planner. Unsupported transition
aliases and invalid sound durations settle with a clear reply without starting
visual analysis.

The rebuilt macOS dock and local error replies were inspected. A native regression
opens/closes the visible widget's Transitions menu through button activation;
the computer-use capture did not retain the popup, so its item states are verified
by the native test rather than a desktop screenshot. Stop and all tool controls
retain the worker's existing busy lifecycle. Further end-to-end quality gates
remain in [the checklist](completion-checklist.md).
