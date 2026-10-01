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
Audio, captions, transitions, generators, and nested sequences are outside this
first slice. Only the deterministic diagnostic runs without provider credentials.

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

**Still unverified:** exported output, native insertion-failure injection,
all project-switch/relink/profile interactions, and linked audio. The feature
remains off by default. A Qt accessibility crash occurred while inspecting a
closing file dialog; this is an unresolved SDK/UI automation limitation.

The Gemini milestone adds 55 Python checks and 20 Qt transport checks (including
fixtures). Live synthetic runs verified analysis reuse and bounded proposals.
The desktop exercised Gemini planning/Apply, duration shortfall, Stop and retry.
These results demonstrate integration; they do not establish real-footage quality.

On this Mac use `assistant/tools/run_craft_macos.sh` to launch the development
build with Craft's library, MLT, and effects paths. The launcher does not package
or change cached dependencies. Native CTest configures the macOS SDK paths itself.

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
