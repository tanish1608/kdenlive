<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Native integration diagnostic

## What exists

The experimental AI Assistant dock requests Gemini visual planning from a local
Python worker using a brief, duration and travel pacing preset. It reuses cached
sampled observations, previews source windows and appends approved cuts to an
unlocked video track. See `assistant/README.md` for sampling and provider limits.
The deterministic six-source diagnostic remains available by setting
`MONTAZ_ASSISTANT_DEMO=1`; it appends each source's first two seconds.
ElevenLabs instrumentals and local music-accent cut refinement extend this slice.
Captions, transitions, generators and nested sequences remain outside it. The
deterministic diagnostic and existing-music timing run without provider credentials.

The editor adapter captures the actual bin/sequence, uses a monotonically
increasing revision, rejects stale/replayed proposals, and revalidates source
ranges and occupancy on Apply. Explicit `V`/`A` prefixes prevent implicit mirror
clips and track creation. Insertions accumulate Kdenlive undo/redo functions;
failure reverts preceding insertions and success pushes exactly one undo entry.
A rollback failure blocks further assistant edits until the document is reopened.

## Build and run

Provision the complete upstream toolchain first, following `dev-docs/build.md`.
Then configure this checkout:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_MONTAZ_ASSISTANT=ON
cmake --build build
ctest --test-dir build -R assistanttest --output-on-failure
```

Launch the built editor with Python 3.11+ on PATH. If needed, set
`KDENLIVE_ASSISTANT_PYTHON` to an absolute interpreter path. Open the AI Assistant
dock through the **AI** toolbar action or normal View menu. Import small videos,
select a sequence, enter a chat message and choose **Send** (or Enter). Inspect
the proposed shots and choose **Apply edit**. Shift+Enter adds a line. Follow-up
messages refine the current brief; **New brief** starts over. Conversations are
session-only and clear on project/sequence changes. The edit task list uses actual
worker progress and native application results; pending review has no running job.
This development build reads the Python package from this checkout's source
directory. Relocatable worker installation belongs to later distribution work.

The **AI** toggle sits at the far right of the top toolbar. The assistant occupies
the full right side, beside both the preview and timeline; closing it returns
that space to editing. Restoring a workspace or project repositions the assistant
outside asset tabs and hides the Clip Monitor, keeping the Project Monitor for
timeline playback. Asset selection leaves that playback active. The Clip Monitor
remains available explicitly through View when source inspection is needed.

Run records are JSON lines under the application's cache directory,
`montaz/runs/<request-id>.jsonl`. On macOS this is normally under
`~/Library/Caches/kdenlive/`. Records contain timing/status/revision/apply metadata;
source paths, transcripts, environment variables, and worker stderr are omitted.
The request-level timeout is three minutes for Gemini planning, 30 seconds for
the diagnostic. Stop kills owned FFmpeg children, terminates Python, then kills
it after 750 ms if necessary. Gemini network calls are not remotely cancelled;
late results are discarded. Completed analysis cache entries survive cancellation.

## Verification boundary

Locally verified: Python contracts/wire/worker tests and compiled Qt-only tests
for native validation, single batch commit/undo/redo functions, rollback,
duplicate/stale rejection, real worker communication, timeout, forced Stop,
malformed/oversized/incomplete responses, launch failure/retry, and run records.

On September 30 the full Craft build compiled the dock/editor adapter. Both
`assistanttest` and upstream `utilstest` passed. The assistant test uses the real
Kdenlive timeline to check six inserts, exact cut frames, one Undo/Redo, replay
rejection, lock/unlock invalidation, and source bounds. Desktop preview, Apply,
Undo/Redo, save/reopen, and seeking to the first cut were exercised using six
silent synthetic clips with mixed frame rates and orientations.

**Still unverified:** native insertion-failure injection,
all project-switch/relink/profile interactions, and linked audio. The feature
remains off by default. A Qt accessibility crash occurred while inspecting a
closing file dialog; this is an unresolved SDK/UI automation limitation.

The Gemini milestone adds 55 Python checks and 20 Qt transport checks (including
fixtures). Live synthetic runs verified analysis reuse and bounded proposals.
The desktop exercised Gemini planning/Apply, duration shortfall, Stop and retry.
These results demonstrate integration; they do not establish real-footage quality.

On this Mac use `assistant/tools/run_craft_macos.sh` to launch the development
build with Craft's library, MLT, and effects paths. The launcher stages upstream runtime data and the built render helper into the
development bundle; it does not package or change cached dependencies. Native CTest configures the macOS SDK paths itself.

Before connecting providers, pass `assistanttest` and manually check:

- Six unique clips, exact cut frames, no implicit audio or extra tracks.
- One Undo removes the edit; Redo restores it; duplicate delivery adds nothing.
- Edit/undo/relink/profile/sequence changes invalidate a pending proposal.
- Stop and worker failure leave the timeline untouched and buttons settled.
- Save/reopen preserves the edit; seeking and export agree at every cut.
- Portrait/landscape and mixed frame rates use Kdenlive's project timebase.

Add native failure-injection and save/reopen/export tests when the full build is
available. Keep the diagnostic feature disabled until these gates pass.


The October 1 chat milestone passes 58 Python checks and 27 Qt transport/chat
checks (including fixtures), plus native `assistanttest`. Live Goa checks used
fifteen supplied clips, verified analysis reuse, a four-shot baseline and an
eight-shot refined draft, Apply/Undo/Redo, first-cut seeking, Stop and project
conversation reset. The brief refinements stay within the 2,000-character worker
contract; oversized drafts are retained and explained. These remain sampled
visual edits, not verified final films with framing, audio or captions.

The temporal-analysis milestone passes 63 Python checks, 22 Qt transport checks
and 7 chat checks (including fixtures), plus native `assistanttest` and
`transcodetest`. The latter performs real FFmpeg conversions with synthetic
multi-stream/unsupported-audio/silent inputs and a failing encoder. A native Goa
run reused all 12 eligible analyses, skipped 3 micro-clips, and proposed 7 shots
(474 frames). Apply/Undo/Redo, save/reopen, preset switching and first-cut seeking
were exercised. The native H.264 export contains exactly 474 frames at 30000/1001
FPS; all six exported cut boundaries were inspected. See
[quality review](quality-review.md). This remains a silent, partially pillarboxed
draft. An accessibility inspection immediately after closing Rendering reproduced
the SDK/macOS AX hierarchy crash; reopening the saved project succeeded. This
limitation is still unresolved and must not be counted as a clean UI test run.

The instrumental music milestone passes **75 Python checks**, **24 Qt transport
checks** and **7 chat checks** (including fixtures), plus native `assistanttest`.
Synthetic audio checks exercise actual encoding, silence/shortfall rejection,
one-frame MP3 padding, fades, raw/prepared reuse, missing credentials and Stop.
Transport checks reject malformed/context-mismatched results and dirty exits;
music Stop kills an uncooperative worker. Native tests import a real PCM WAV,
place it at exact project frames, reject changed files/stale locks, and verify one
Undo/Redo for both bin and timeline while preserving the visual edit and file.

The live Goa project exercised ElevenLabs generation, Qt audio preview start/stop,
native Apply/Undo/Redo, save/reopen with relative media references, and timeline
playback with active audio meters. Native H.264/AAC
export retains all 474 decoded video frames from the silent draft and measures
-18.1 LUFS / -5.0 dBFS true peak. See [quality review](quality-review.md) for the
real MP3 duration failure and local reuse correction. This does not verify musical
cut alignment, ducking, sound effects or narration; those remain pending.


The local timing milestone passes **83 Python checks**, **26 Qt transport checks**
and **7 chat checks** (including fixtures), plus native `assistanttest`. Protocol-v2
retiming checks preserve identities, source starts, order and total span, reject
changed music/source ranges and stale context, and limit boundary shifts to one
quarter second. Synthetic checks exercise actual FFmpeg music measurement without
credentials, globally constrained neighboring lengths, terminal no-change, malformed
responses, dirty exits and forced Stop. Native tests resize/move real clips, verify
one Undo/Redo and replay rejection, and roll back a native resize after an injected
batch failure. This is a rollback test of the shared batch helper; it does not
inject failures at every step of the adapter.

Timing accepts only a contiguous normal-speed video track and one whole-span
assistant instrumental, with no groups, clip effects, transitions, subtitles or
other audio. It measures energy accents locally with a 30-second request deadline.
No useful shift terminates as No change. Native Apply rechecks eligibility and the
file checksum; it never overwrites the live project file. Musical beat/phrase
verification and sound effects remain pending.

The live Goa review proposed only frame 75 → 79 and exercised Apply, one Undo,
Redo, save/reopen, native cut seeking, terminal no-change and export. The export retains 474 video frames
and identical decoded audio. See [quality review](quality-review.md).


The described-sound milestone passes **92 Python checks**, **27 Qt transport
checks** and **7 chat checks** (including fixtures), plus native `assistanttest`.
Synthetic checks cover the actual worker with encoded audio, quiet preparation,
exact duration, raw/prepared reuse, silence/shortfall rejection, occupied/locked
tracks, gaps, provider failures and Stop. The transport's clean-exit, dirty-exit,
malformed-result and uncooperative-Stop matrix now covers both music and effects.
Native generated-audio tests exercise music and short effects, exact insertion,
role markers, stable video, one Undo/Redo, changed-file rejection and stale locks.
The effect uses the same native import/undo implementation as music; no second
audio engine or timeline is introduced.

Live Goa checks exercised the dedicated provider endpoint, preview completion,
Apply/Undo/Redo, relative effect resources, save/reopen and native export. The
60-frame sound starts at frame 79 and leaves all 474 exported video frames
unchanged. The mixed export measures -18.0 LUFS / -4.7 dBFS true peak. The user
still chooses the sound and position explicitly; this is not automatic sound
design, verified sound-event recognition, ducking or caption generation.

## Cached cue selection gate

`Suggest sounds` uses selected-source cached visual evidence, followed by a
separate Gemini text selection. No visual provider call, audio generation or
native mutation is permitted at this step. Python regressions cover cache-only
misses/stale entries, selected-source maps, source failures, usable trims,
fractional FPS, sparse spacing, known categories, no audio space and Stop.
Qt tests bind cues to native shot intervals, current context, hashes and complete
cache progress; empty success, malformed results, failed exit and uncooperative
Stop are exercised. Native tests verify cue capture does not change the timeline
or undo history, and a source missing its evidence hash cannot start generation.

The updated native editor and `assistanttest` compile and pass on macOS arm64;
100 Python, 29 Qt transport and 7 chat checks pass. Live Goa verification reuses
three valid analyses, skips four unavailable entries, generates one selected
pool-water cue, and checks preview completion, Apply/one Undo/Redo, saving,
reopening and native export. See [output review](quality-review.md) for the exact
interval and exported-frame/audio measurements. This is proposed foley from
silent video, not verified audible-event detection or autonomous sound design.
