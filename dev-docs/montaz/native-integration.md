<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Native integration diagnostic

## What exists

The experimental AI Assistant dock requests a deterministic edit from a local
Python worker. It previews up to six distinct imported video sources and appends
their first two seconds (or their shorter available duration) to an unlocked
video track. This is a diagnostic, not shot selection or an AI quality claim.
Audio, captions, transitions, generators, and nested sequences are outside this
first slice. No provider credentials are needed.

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
dock through the normal View menu. Import six small videos, select a sequence,
choose **Preview shots**, inspect the list, and choose **Apply edit**.
This development build reads the Python package from this checkout's source
directory. Relocatable worker installation belongs to later distribution work.

Run records are JSON lines under the application's cache directory,
`montaz/runs/<request-id>.jsonl`. On macOS this is normally under
`~/Library/Caches/kdenlive/`. Records contain timing/status/revision/apply metadata;
source paths, transcripts, environment variables, and worker stderr are omitted.
The request-level timeout is 30 seconds. Stop terminates the diagnostic process,
then kills it after 750 ms if necessary. The current worker creates no children;
future media subprocesses require their own cancellation integration.

## Verification boundary

Locally verified: Python contracts/wire/worker tests and compiled Qt-only tests
for native validation, single batch commit/undo/redo functions, rollback,
duplicate/stale rejection, real worker communication, timeout, forced Stop,
malformed/oversized/incomplete responses, launch failure/retry, and run records.

**Unverified:** the dock/editor adapter compilation and the full Kdenlive timeline
test (`tests/assistanttest.cpp`). Qt-only tests do not link KDE or MLT and do not
prove playback, actual native undo, save/reopen, or export. The host currently lacks
the complete KDE/MLT build environment; the feature remains off by default.

Before connecting providers, pass `assistanttest` and manually check:

- Six unique clips, exact cut frames, no implicit audio or extra tracks.
- One Undo removes the edit; Redo restores it; duplicate delivery adds nothing.
- Edit/undo/relink/profile/sequence changes invalidate a pending proposal.
- Stop and worker failure leave the timeline untouched and buttons settled.
- Save/reopen preserves the edit; seeking and export agree at every cut.
- Portrait/landscape and mixed frame rates use Kdenlive's project timebase.

Add native failure-injection and save/reopen/export tests when the full build is
available. Keep the diagnostic feature disabled until these gates pass.
