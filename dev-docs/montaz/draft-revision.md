<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Native draft revisions — October 2, 2026

## Behavior and boundaries

Follow-up messages after Apply revise the last assistant draft in place. Review
shows **Revised draft** and **Replace draft**. The editor issues the clip scope;
the model returns only validated insertions within its exact track/start/end.
One native Undo restores the previous shots and ownership; Redo restores the
replacement. Neighboring video clips, music, other audio and subtitle events are
preserved. The source windows and pacing recipe still constrain every shot.

The native ledger checks IDs, bin/source trims, occupancy, locks, groups, mixes,
speed, effects, hidden tracks and source path/size/mtime/duration. A manually
edited draft cannot be replaced automatically. Undoing its manual changes can
restore eligibility. Existing compositions are conservatively ineligible.
Temporary selection is cleared before native deletion so selecting an unrelated
clip cannot cause group deletion. Failure rolls back the accumulated batch.

The ledger is deliberately session-only. Project/sequence switch or reopening
clears it; existing saved clips are never treated as owned automatically.
**New brief** appends a separate edit. The diagnostic demo remains append-only.
This does not yet support revising caption text, sound, effects or arbitrary
manual edits. Exact-span underfill fails as `revision_unavailable`; no filler,
silent shortening or recursive critic/retry is allowed. Provider budget remains
one planning call plus one bounded quality review, with cached scene evidence.

The visible order is story → music → Align cuts → Match cut → captions. Beat
alignment updates native ownership through Undo/Redo; an ensuing story revision
can change internal cut positions, requiring another timing review. Existing
effect/subtitle eligibility gates remain strict.

## Regression evidence

- Python: five new scope/planning checks cover fractional rates, preserved later
  audio, duplicate/unknown/moved/locked clips, escaping and underfilled proposals,
  exact frame targets, cached evidence and cancellation after a late provider.
- Qt: typed revision transport rejects invalid initial scope, appended/shortened/
  wrong-track output and late worker failure before enabling Apply.
- Native: replacement preserves neighboring clip IDs, audio and subtitle text,
  despite mixed temporary selection; one Undo/Redo restores native IDs/ownership.
  Trim/lock/stale/source-bound failures leave original clips intact. Serialization
  reopens successfully at 30000/1001 FPS. Accent and beat retiming exercise both
  manually inserted and assistant-owned drafts and verify ownership restoration.

Full checks: 202 Python passes and ten optional native-tracker skips (212
discovered), 40 Qt transport/seven chat passes, and the full native assistant
CTest target passes. Changed Python passes Ruff; the editor and tests compile.
The tracker itself is unchanged by this milestone.

## Private desktop review

In a copy of the saved Goa project, Apply appended a seven-shot, 506-frame
assistant draft after the original nine shots. The requested-duration control
remained 60 seconds during this UI check; the live model produced a shorter
16.88-second draft, reported honestly. This is not a new 20-second quality claim.

A follow-up displayed **Revised draft / Replace draft**. Saving invalidated the
pending proposal; it could not be applied until a fresh review against the saved
sequence. The subsequent replacement changed the draft to eight shots within
the exact same 506 frames. Total sequence length stayed 1,092 frames at
30000/1001 FPS. Inspection of the editor-saved XML found the original nine
source ranges identical. One desktop Undo restored all 16 original source ranges;
one Redo restored the 17-shot edit. The original project was untouched.

Private artifacts live under `/Users/tanishvardhineni/Movies/`:

- `montaz-draft-before-revision-20261002.kdenlive`
- `montaz-draft-undo-20261002.kdenlive`
- `montaz-draft-revision-review-20261002.kdenlive`
- `montaz-draft-revision-export-20261002.mp4`
- `montaz-draft-revision-shots-20261002.png`

The desktop H.264 export finished in 1:44. Decoding reports 1,092 frames,
1920×1080, 30000/1001 FPS and 36.4364 seconds, matching the native span. Eight
decoded draft midpoints show the reviewed bridge/journey/activity/coast sequence.
The ending is a coast view, not the specifically requested calm river: successful
transport/application does not establish that every editorial preference was met.
Fresh-process reopen shows the saved 36;12 drop-frame duration, clears chat/review
and Undo history, and presents no replacement claim on existing clips.

Final prompt validation also supplies the current draft's ordered bin IDs/trims/
durations as read-only model context, so references to an existing shot have
meaning. Native instance IDs stay outside that prompt. This context addition was
verified in the focused five-test suite after the live run; the exported draft
above predates it and is not evidence of its editorial improvement.

These checks verify edit mechanics, not finished film quality. This draft still
contains pillarboxed portrait footage; automatic object correspondence, musical
phrases, whips/wipes and ramps are outside this milestone.
