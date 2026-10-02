<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Existing app maintenance: integration review

Codex, October 2, 2026. Reviewed Claude's twelve commits from `c035ef2` to
`a05d783` on `codex/claude-existing-app-hardening`, then merged locally as
`cd4cbda` on `codex/ai-assistant-foundation`. Claude's branch and worktree remain
intact. Nothing was pushed, published or packaged. New features remain paused.

## Accepted changes

The production changes are focused on existing behavior: POSIX worker-child
cancellation, preserving pending captions after a refused/no-op style change,
frame allocation for crowded caption tails, explicit caption/empty-sound chat
requests, eviction of unusable generated audio, correct invalid-key/provider
messages, a 200-run log limit, and rejecting undersized tracked subjects per
candidate. No transition, discovery, recipe or narration feature was added.

The review checked production diffs, the associated regression tests and the
reported limitations. A caption transcript cache needs no version change when
only native frame allocation changes. The shrinking tracker result failed
validation before caching, so that fix does not require a cache migration.
Future tracker behavior changes still require an explicit cache-version audit.

## Independent validation

These checks were rerun against the candidate checkout and its own native build;
they are separate from [Claude's delivery report](claude-maintenance-report.md).

| Check | Result |
| --- | --- |
| Default Python suite | 290 run; 277 pass, 13 opt-in skips |
| Python with real tracker and FFmpeg | 290 pass, no skips |
| Qt client/commands and chat | 49 + 7 pass |
| Candidate editor, tracker, renderer and test build | Pass |
| Candidate native `assistanttest` | Pass, 8.20 s |
| Mixed-rate native frame QA | Both 30000/1001 and 25 fps pass |
| Changed Python Ruff / whitespace checks | Pass |

After merging, the primary Craft checkout rebuilt the editor, tracker, renderer
and test target successfully. Its `assistanttest` passed in 8.91 s. Production
code/tests match the independently validated candidate tree exactly; subsequent
review edits are documentation only. The rebuilt primary app was launched through
the development launcher against the private project copy, with the real Python
worker configured and demo mode unset. It opens with the dock ready to Send.

For each frame-QA profile, five lossless sources at 24/25/29.97/59.94/50 fps
produce 185 project frames. Timeline, Undo/Redo, reopened scene and native export
agree on all source frames and clip identities, including all four cuts. Export
has a small uniform luma offset; the check does not require byte-identical video.

Reproduce the opt-in Python checks by setting `MONTAZ_TRACKER` to the built
`bin/montaz-tracker`, `MONTAZ_FFMPEG` to Craft's `bin/ffmpeg`, and running the
normal unittest suite. Run `assistant/tools/check_playhead_agreement.py` with
`--craft-root`, `--build-root`, `--output-dir` and each supported `--profile`.
Fixtures and outputs must stay outside Git.

Claude's randomized sweeps and live Goa/caption provider measurements remain
attributed evidence; Codex did not repeat those provider calls. Neither engineer
ran paid music generation during this review.

## Candidate desktop check

The candidate's own staged app opened a private copy of the wheel-review project
with private settings. The original project was untouched. Computer-use checks
confirmed:

- The right dock retains Add music / Transitions / Captions without a fixed
  workflow or a separate sound button.
- `add a sound effect` and `add sound:` give guidance without a visual plan.
- `add captions please` on video-only shots finishes with No speech and later
  phases Not run; Stop disables and Send remains available.
- An explicit whip-pan request finishes with the unavailable-transition reply.
- Timecode seeking to frames 59 and 60 at 29.97 fps shows the gold and red wheel
  respectively, and the timeline needle crosses the shared cut boundary.
- A controlled stalled worker and child, both ignoring SIGTERM, are killed by
  the real Stop button. Chat settles as Stopped and Send re-enables. Closing the
  editor during a second such run also ends both processes.

The stalled-worker check uses a private local fixture, not a provider request.
Immediate inspection can precede the 750 ms kill timer; the subsequent settled
state and process absence were verified. This covers boundary seeking, not all
continuous-playback, zoom/scroll or time-remap needle behavior. Caption review
style switching remains native-test evidence rather than a desktop review test.

Private evidence: `~/Movies/montaz-maintenance-integration-review/`, including
`playhead-2997`, `playhead-25` and `gui` screenshots/process-check results.
Independent logs are `/tmp/montaz-maintenance-review-*.log`; rebuilding the
integrated checkout uses `/tmp/montaz-maintenance-integration-*.log`.

## Remaining maintenance priorities

1. Preserve a pre-existing custom subtitle force style when returning to
   Original; define save/reopen and Undo behavior and test pending review in the
   desktop. Current Original clears that global override; Undo restores it.
2. Support available speech when an audio stream ends well before its visual
   clip, without calling missing or damaged media silence.
3. Add native fault injection for generated-audio bin import followed by failed
   timeline insertion; confirm bin, timeline and Undo restoration.
4. Report unexpected worker failures with a redacted diagnostic code instead
   of only `worker_incomplete`.
5. Extend visible playhead checks to continuous playback, different timeline
   zoom/scroll positions, mixed-rate projects and existing time remaps.
6. Review cross-worker cold-cache deduplication and tracker-cache versioning.
   A single editor currently runs one worker at a time.

Portrait framing remains explicitly deferred. Broad app completion gates stay
open: this maintenance review does not establish a finished travel film,
speech-preserving workflow or all supported timeline rollback cases.
