<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Duration and editorial review checkpoint — October 1, 2026

## Change and bounds

The previous reviewer had to preserve the initial draft's exact span, so it could
not repair an otherwise valid 8-second draft for a 20-second request. A draft below
85% of the locally reachable target can now expand during the existing single
review. No extra request, analysis, retry or deadline is added. Near-target drafts
still preserve their span. Relevant footage may be insufficient for a restrictive
brief; keeping a shorter edit must explain the unsuitable or redundant material.

The target uses one permitted window per content identity and the validator's
quality, trim, one-second minimum and recipe floors/ceilings. Interval unions
handle fixed short windows whose sums cannot fill every requested duration.
Per-window frame budgets and observed journey/activity/ending guidance are sent
to planning. The UI reports actual duration without implying every short result
exhausted the bin. Protocol and native edit ownership are unchanged.

## Same-input live observations

Fifteen supplied Goa sources: twelve cached analyses, three micro-clips skipped,
708 globally usable frames, requested 599 frames (20 seconds at 30000/1001 FPS).
Baseline is immutable commit `592a584`; both versions use Gemini and the same
cached evidence. Each run uses two text requests. These are individual stochastic
runs, not a controlled perceptual benchmark or a performance guarantee.

| Brief | Baseline | Candidate | Outcome |
| --- | --- | --- | --- |
| Restrictive calm coast | 165 frames / 5.51 s / 2 shots | 240 / 8.01 s / 3 | Kept; city/night/transit/activity material outside the brief |
| Varied journey/place/activity postcard | 472 / 15.75 s / 7 | 563 / 18.79 s / 8 | Revised from 502 frames during the same review |

The latter review adds cricket, extends usable driving/traffic and excludes night
views for lighting continuity. All first/middle/last shot frames were inspected.
The pool provides a settled ending; the preceding fort pan still cuts before it
resolves. The train remains a one-second glimpse, and urban traffic is visually
busy. Portrait pillarboxing and absence of a mixed soundtrack remain major limits.
No match cut, beat alignment or music was added by this duration check. Duration
coverage improved; a complete improved travel film is not yet established.

## Native and desktop checks

`assistant/tools/check_editorial_quality.py` runs captured proposals without a
provider. Native Apply/one Undo/Redo, exact timeline occupancy, MLT serialization
and reload, exact-zone rendering and decoded timing pass for baseline, first
candidate and final candidate. Final export is exactly 563 frames at 30000/1001.
Kdenlive's serialized background may extend beyond occupied media; render the
measured zone instead of treating background padding as an extra edit.

A separate fresh desktop run returned nine shots / 19.6 seconds using cached
analysis. Apply, one Undo/Redo, save and fresh-process reopen pass; the prior
seven-shot sequence is preserved in the private copy. Stop was also exercised.
Session UI resets on reopen. This desktop sample differs from the CLI comparison.

Fifteen regressions cover duration gaps, duplicate content, fractional FPS,
discarded edges, unavailable review, cancellation, unchanged near-target span,
impossible one-second and 60-second requests, and the two-request lifecycle.
Earlier full validation passed 182 Python checks, 39 Qt transport/seven chat
checks and native `assistanttest`; the combined integration repeats the updated
suite after two additional duration regressions.

Private captures, source maps, contact sheets and exports are under
`~/Movies/montaz-editorial-duration-20261001/`; desktop copy is
`~/Movies/montaz-duration-desktop-review.kdenlive`. No footage, paths to individual
sources, prompts, credentials or captures are committed. Captures are manual QA,
not production logs; production logs remain redacted.
