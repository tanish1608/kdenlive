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
as bounded silent video proxies sampled by Gemini at four frames per second.
It describes visible activity and suggests usable start/end offsets, then plans
an opening, varied middle and ending with a reason for each shot. These are
sampled observations and model-estimated trims, not frame-perfect detection or
speech transcription. The current edit inserts video only.

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
Analysis version `video-windows-4` probes visible stream length separately from
audio/container duration. Silent proxies are limited to six seconds, 512 pixels,
12 encoded frames/second and 4 MiB per window. Source clips below one second are
skipped before provider analysis; they cannot become watchable travel cuts.
Earlier still-frame caches intentionally do not satisfy this new analysis version.

Plans reject duplicate content, unknown windows, poor-quality observations,
source overflow, unreasonable cut durations, and excess requested duration.
Shorter edits are explicitly reported; footage is never stretched or repeated to
fill a brief. Missing keys and provider/parse failures stop the run before Apply.
Travel cuts below one second are rejected. The planner gets a feasible target
from distinct usable trimmed windows, rather than only a maximum requested
duration. The preview shows each shot's purpose/reason, a usable-duration estimate
and excluded-source count. This optional review metadata is independently checked
against the proposal before Apply; it cannot alter editor operations. Quality
scores are model judgments, not measured output quality. There is no permissive
critic fallback or caption job on silent footage.

Stop kills owned FFmpeg children and terminates the worker with a 750 ms grace
period. Media extraction has a 15-second deadline, requests 35 seconds, and the
native run three minutes. Completed cache entries survive Stop and later failures.
The **Run logs** button opens redacted timestamps, model, progress/cache counts,
terminal status, duration/shot/mean-score metrics and apply acknowledgments;
footage paths/descriptions/briefs/shot reasons and provider stderr are excluded.

Gemini uses the official [GenerateContent REST API](https://ai.google.dev/api/generate-content)
with JSON output, followed by strict local/native validation. Narration, source
audio pairing, sound effects, subtitle styling, and beat-based editing remain pending.

## Instrumental music

After applying the video edit, save the project and choose **Add music**. The
worker uses ElevenLabs' [Music API](https://elevenlabs.io/docs/api-reference/music/compose)
with `force_instrumental=true` and a duration derived from the actual video span,
independent of the requested draft duration. Choose a travel pacing preset for
the mood. The current brief adds editorial context. Listen to the proposed track,
then **Apply music** imports and inserts it on a free unlocked audio track. One
native Undo reverses both operations. Existing audio is preserved; automatic
ducking and sound effects are not implemented.

Set `ELEVENLABS_API_KEY` or `elevenlabs_api_key` in the same private assistant
settings. `ELEVENLABS_MUSIC_MODEL` / `elevenlabs_music_model` defaults to
`music_v2_5`. A missing key, denied music access or unusable output ends the run.
Generation has a 90-second network timeout and a three-minute native deadline;
Stop terminates local work but cannot revoke an already submitted provider job.
Provider usage may still be charged. No automatic provider retry occurs.

Generated WAVs and content-addressed reuse copies live in
`<saved-project-name>.assets/music/` beside the project. Keep that directory with
the project when moving or sharing it. Raw provider responses are retained too,
so encoding failures can retry locally without another generation. Successful prepared tracks are reused
for matching mood, brief, duration, model and FPS. Stop preserves completed
preparation. Invalid stored audio fails validation instead of silently requesting
another paid generation. No keys, audio or generated projects belong in Git.

Preparation uses stereo 48 kHz PCM, an -18 LUFS loudness target and half-second
opening/ending fades baked into the WAV. Native gain remains editable. Validation
rejects silence and output more than one project frame short. MP3 block rounding
may leave a smaller gap; only that final gap is padded before fading, never
stretched. Prepared WAV duration matches the timeline within one sample. Energy accents are
measured in project frames; they are not verified beats. Adding music keeps cuts
unchanged; the separate Align cuts review can propose bounded shifts.
Music preview uses Qt audio; Kdenlive remains the timeline playback authority.

## Occasional sound effects

**Add sound** asks for one description and a 0.5–10 second duration. Put the
playhead inside a shot first. The editor captures that position, limits the sound
to the shot and chooses a free unlocked audio track. Review its position and
Listen before **Apply sound**. One native Undo removes both clip and imported
asset; existing audio and video stay unchanged. Align cuts before adding effects;
additional audio makes the conservative timing refinement ineligible.

The dedicated ElevenLabs `eleven_text_to_sound_v2` endpoint uses the same private
API key. It requests no looping, voices or music. Raw responses persist before
preparation; repeated descriptions/durations reuse local audio without automatic
regeneration. Durable owner-only WAV/MP3 files live beside the saved project in
`<project>.assets/effects/`; keep that directory with the project. Audio is prepared
at -26 LUFS with soft edges and exact project-frame duration. Larger shortfalls
and silence are rejected; only a final shortfall within one project frame may
be padded. Native validation binds path, checksum, revision, interval and track.

This is explicit described-sound generation, not automatic event recognition or
an autonomous sound-design pass. Stop discards late results and terminates local
processing; an already submitted provider request may still complete. There is
one bounded generation request with no automatic network retry. Provider timeout
is 60 seconds and the entire native request has a three-minute deadline.

API reference: [Create sound effect](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert).

## Local cut timing

With assistant music on the timeline, **Align cuts** proposes small changes to
existing cut points using measured energy accents. It runs locally and makes no
provider calls. Review the before/after cut times, then **Apply timing**. One
native Undo restores the original cuts; music, shot order, source in-points and
the overall video span stay unchanged. Native clip IDs and bin assets are retained.

The first version requires one contiguous video track, at least two shots, one
assistant instrumental covering the edit, and shots lasting at least one second.
Grouped clips, other audio, effects, speed changes, transitions and subtitles are
ineligible. Each internal boundary can move at most one quarter second, rounded
down to project frames (minimum one frame). Source length limits and neighboring
shot durations can prevent a shift. No useful change is a terminal outcome.
Accents are not verified beats or musical phrases; review the result in playback.

`refine_timing` returns a separate protocol-v2 retiming proposal. Both validators
enforce the editor-issued clip identities, source starts, frame limits, matching
music checksum and unchanged total span. Apply rechecks native eligibility and
the audio checksum, then accumulates native resize/move undo functions in one
batch. Only affected clips are touched; a failed step rolls back the batch.

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
