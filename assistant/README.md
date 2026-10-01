<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Assistant foundation

Python 3.11+, standard library only. This package validates proposals, models run
events, and provides a Gemini visual analysis/planning worker for the native adapter.

```sh
PYTHONPATH=assistant/src python3 -m unittest discover -s assistant/tests -v
```

`contracts.py` defines immutable editor snapshots and insert proposals. Validation
checks project/sequence/revision identity, replay, clip IDs, imported assets,
track capabilities/locks, source ranges, and occupied frames. Inputs are project
frames with rational FPS; existing occupancy comes from Kdenlive. The same checks
also run at the native mutation boundary, not only in Python.

`lifecycle.py` defines explicit, sequenced run events and terminal stage states.
Cancellation settles pending/running stages; late callbacks cannot revive a run.
This models status only. Process cancellation and deadlines need real integration
tests; those now run in the Qt-only suite below.

`protocol.py` rejects duplicate/unknown fields, oversized messages, invalid numeric
types, and frame overflow. `worker.py` accepts one request per process. `plan_demo`
chooses up to six distinct imported video sources, and appends bounded two-second
shots at exact project frames. It uses no providers, sound, transitions, or captions.

## Gemini visual planning

The native dock sends a `plan_media` request with editor-issued asset IDs, local
source paths, a brief, requested duration, and one of three travel pacing presets.
`planning.py` samples the first/middle/last six-second windows (or a shorter source),
with two frames per window. Gemini describes only these observations, then chooses
source windows and cut lengths. This is sampled visual analysis, not complete video
understanding or speech transcription. The current edit inserts video only.

The native **AI** button opens a chat dock using Kdenlive's actions and palette.
Enter sends; Shift+Enter adds a line. Follow-up messages refine the active brief,
and **New brief** resets it. Conversations are bounded, session-only and cleared
when the project/sequence changes. A task list tracks actual analysis, planning,
review and application; reviewing a proposal is not a background job.

Set `GEMINI_API_KEY` and optionally `GEMINI_MODEL`, or use the private settings file
`~/.config/montaz/assistant.json` with `gemini_api_key` and `gemini_model` fields.
Keep it owner-readable/writable (`chmod 600`); never add it to the repository.
`MONTAZ_ASSISTANT_CONFIG` overrides its path. The user's existing Gemini settings
were migrated locally; no key is transmitted over the native protocol or logged.

Successful analyses are cached under the application's cache at `montaz/analysis`.
Keys include full content SHA-256, model, analysis/sampling version, conformed
duration, and rational FPS. A renamed or re-imported source reuses its evidence.
Two videos are analyzed concurrently; identical content shares one analysis even
when imported twice. Cache writes are atomic and owner-only. Failed analysis is
never a valid cache entry. The brief and recipe do not invalidate visual evidence.
Sampling v3 probes the video stream duration separately from audio/container
duration, bounds candidate windows to visible video, and places the second sample
90% inside each window. This avoids empty tail samples from short phone clips.

Plans reject duplicate content, unknown windows, poor-quality observations,
source overflow, unreasonable cut durations, and excess requested duration.
Shorter edits are explicitly reported; footage is never stretched or repeated to
fill a brief. Missing keys and provider/parse failures stop the run before Apply.
Travel cuts below one second are rejected. The planner gets a feasible target
from distinct usable windows, rather than only a maximum requested duration.
There is no permissive critic fallback or caption job on silent footage.

Stop kills owned FFmpeg children and terminates the worker with a 750 ms grace
period. Media extraction has a 15-second deadline, requests 35 seconds, and the
native run three minutes. Completed cache entries survive Stop and later failures.
The **Run logs** button opens redacted timestamps, model, progress/cache counts,
terminal status, and apply acknowledgments; footage paths/descriptions/briefs and
provider stderr are excluded.

Gemini uses the official [GenerateContent REST API](https://ai.google.dev/api/generate-content)
with JSON output, followed by strict local/native validation. Narration, source
audio pairing, music/effects, subtitle styling, and beat-based editing remain pending.

Compile and exercise the actual Qt process transport, native validator, and
batch rollback logic using an installed Qt development environment:

```sh
python3 assistant/tools/run_native_tests.py --qmake /path/to/Qt/bin/qmake
```

On this Mac the runner defaults to Homebrew's qmake. It builds in a temporary
directory, calls the real Python worker, and tests failure, timeout, bounded
output, cancellation of an uncooperative process, and redacted run records.
These tests do not instantiate Kdenlive's timeline. See the native integration
guide for the full application test gate.

Protocol v1 deliberately supports only insertion. Mixing, subtitles, effects,
trim/move, linked AV groups, and replacement proposals require explicit contract
extensions plus native tests. A successful validation is not an editor apply
acknowledgment or an assessment of editorial quality.
