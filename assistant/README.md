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

After Apply, a follow-up revises the last assistant draft in place. The native
adapter supplies its exact clip IDs/source trims and span through `revise_media`;
the worker returns ordinary v1 insertions within that span, never deletions.
**Replace draft** removes only those owned clips and inserts the reviewed shots
as one native Undo. Neighboring clips, audio and caption events remain intact.
Manual trim/move, locks, groups, mixes, effects or changed source identity refuse
replacement. Ownership is session-only and clears on project/sequence switch or
reopen; **New brief** creates a separate appended edit. Demo mode remains append-only.
See [draft revision checks](../dev-docs/montaz/draft-revision.md).

The optional toolbar offers **Add music**, **Transitions** and **Captions**;
sound requests use chat. There is no fixed tool order. **Beat-synced cuts** and
**Match cut** live in Transitions and require an explicit request. Timing
alignment rejects effect-bearing shots and existing
subtitles. Its native Undo/Redo updates owned draft timing, so later refinements
can preserve the aligned span; refinements may change the internal boundaries.

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
from one permitted window per distinct source, including recipe duration floors
and gaps between fixed short windows, rather than only a maximum requested duration. The preview shows each shot's purpose/reason, a usable-duration estimate
and excluded-source count. This optional review metadata is independently checked
against the proposal before Apply; it cannot alter editor operations. Quality
scores are model judgments, not measured output quality. There is no permissive
critic fallback or caption job on silent footage.

Stop kills owned FFmpeg children and terminates the worker with a 750 ms grace
period. Media extraction has a 15-second deadline, requests 35 seconds, and the
native run three minutes. Review is additionally capped at 45 seconds without
extending that overall deadline. Completed cache entries survive Stop and later failures.
The **Run logs** button opens redacted timestamps, model, progress/cache counts,
terminal status, duration/shot/mean-score metrics and apply acknowledgments;
footage paths/descriptions/briefs/shot reasons and provider stderr are excluded.

Gemini uses the official [GenerateContent REST API](https://ai.google.dev/api/generate-content)
with JSON output, followed by strict local/native validation. Narration, source
audio pairing and phrase-aware editing remain pending. Reviewed speech captions and
native subtitle presets are described below.

## One draft quality review

Each valid visual draft receives one additional Gemini text review using its
existing sampled observations. It can keep the draft or propose one complete
revision of shot selection/order/durations, with a short reason. Revised shots
pass the original recipe, quality, distinct-content and source-range checks and
normally preserve the draft's exact total frame count. When the draft falls below
85% of the locally reachable target, that same review may expand it up to the
target using relevant distinct footage. A shorter coherent edit can be kept with
a reason explaining unsuitable or redundant material. This threshold triggers
editorial inspection; it is not a quality score. No footage is reanalyzed and no
automatic critic/revision loop, extra request or retry runs.

Review is limited to 24 draft shots, 32 candidate sources and 96 KiB of context;
larger inputs skip it explicitly. A provider/response/validation failure retains
the original validated draft as **quality review unavailable**, with manual
inspection before Apply. Failures never imply model approval. Stop discards late
results. The native review phase has a 45-second deadline in addition to the
provider's 35-second socket timeout; a forced timeout fails the run. Completed
analysis stays cached. Logs contain only review status/code, not its explanation.

The review checks a proposed draft, not a rendered finished sequence. Sampled
observations cannot verify exact action endings, musical phrasing or perceptual
quality. Apply appends a reviewed draft or replaces an eligible session-owned
draft through native one-batch Undo, as described above.

## Instrumental music

After applying the video edit, save the project and choose **Add music**. The
worker uses ElevenLabs' [Music API](https://elevenlabs.io/docs/api-reference/music/compose)
with `force_instrumental=true` and a duration derived from the actual video span,
independent of the requested draft duration. Choose a travel pacing preset for
the mood. The current brief adds editorial context. Listen to the proposed track,
then **Apply music** imports and inserts it on a free unlocked audio track. One
native Undo reverses both operations. Existing audio is preserved; automatic
ducking is not implemented; short effects use the separate sound workflow below.

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
preparation. Short or silent provider audio is evicted after validation fails;
the next explicit request can generate fresh audio. Encoding failures, timeouts
and Stop preserve reusable paid output. No automatic generation retry runs.
No keys, audio or generated projects belong in Git.

Preparation uses stereo 48 kHz PCM, an -18 LUFS loudness target and half-second
opening/ending fades baked into the WAV. Native gain remains editable. Validation
rejects silence and output more than one project frame short. MP3 block rounding
may leave a smaller gap; only that final gap is padded before fading, never
stretched. Prepared WAV duration matches the timeline within one sample. Energy accents are
measured in project frames; they are not verified beats. Adding music keeps cuts
unchanged; the separate Align cuts review can propose bounded shifts.
Music preview uses Qt audio; Kdenlive remains the timeline playback authority.

## Occasional sound effects

**Add sound > Describe a sound** asks for one description and a 0.5–10 second duration. Put the
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

### Suggestions from existing footage

Choose **Add sound > Suggest sounds** after visual planning. This checks only the
video sources already used on the timeline, reusing current cached observations;
missing entries are skipped without reanalysis. One Gemini text request proposes
up to three spaced sounds from a small category list. The review shows each
observation, position, duration and generator prompt. Silent visual evidence
cannot establish which sounds were recorded.

Select a cue and choose **Generate selected sound**, then Listen and Apply as
above. The selected source hash and native context are checked again before
requesting audio. Suggestions alone never contact ElevenLabs or change the edit.
Zero cues, no cached evidence, Stop and worker failure all finish explicitly.
The conservative eligibility gate accepts one track of up to 64 normal-speed,
video-only shots without clip effects, groups or transitions; existing audio must
be assistant music/effects, and each cue needs free unlocked audio space.

## Local cut timing

With assistant music on the timeline, **Align cuts** estimates a steady rhythm
from recurring onsets across three frequency bands. Supported attacks become
proposed cut anchors; ambiguous music falls back to explicitly labelled energy
accents. It runs locally and makes no provider calls. Review the before/after cut
times and listen in the native monitor, then **Apply timing**. One
native Undo restores the original cuts; music, shot order, source in-points and
the overall video span stay unchanged. Native clip IDs and bin assets are retained.

The first version requires one contiguous video track, at least two shots, one
assistant instrumental covering the edit, and shots lasting at least one second.
Permanent groups, other audio, effects, muted/hidden tracks, speed changes,
transitions and subtitles are ineligible; selecting several clips is supported.
Each internal boundary can move at most one quarter second, rounded
down to project frames (minimum one frame). Source length limits and neighboring
shot durations can prevent a shift. No useful change is a terminal outcome.
The beat estimate is conservative and does not identify downbeats or musical
phrases. It can confuse subdivisions or decline music with variable tempo.
Analysis is cached by audio content, FPS, duration and algorithm version.

`refine_timing` returns a separate protocol-v4 retiming proposal with strict
analysis measurements and up to 2,048 observed beat anchors. Legacy protocol-v2
accent proposals remain valid. Both validators
enforce the editor-issued clip identities, source starts, frame limits, matching
music checksum and unchanged total span. Apply rechecks native eligibility and
the audio checksum, then accumulates native resize/move undo functions in one
batch. Only affected clips are touched; a failed step rolls back the batch.
See [music timing](../dev-docs/montaz/music-timing.md) for reproducible native
export checks and remaining musical-quality limits.

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

Protocol v1 supports insertion; reviewed timing changes, captions and object
alignment use separate, validated extensions described below. Mixing, linked AV
editing and sequence replacement still require additional contracts and native
tests. Successful validation is not an editor apply acknowledgment or an
assessment of editorial quality.

## Reviewed speech captions

Choose **Captions → Generate captions** after placing original audio on the
native timeline. Only audible AudioOnly clips on audio tracks are eligible;
imported video audio, muted/disabled audio, assistant music and effects are
excluded. This first iteration supports an empty, unlocked, enabled subtitle
track and up to eight non-overlapping audio clips, each no longer than 30 seconds
and 120 seconds total, with normal speed and no clip/track effects or mixes.
Sources must be readable local files no larger than 128 MiB. Unsupported edits
are explained before recognition; existing subtitles are never replaced.

The worker decodes only each selected source interval/stream to bounded 16 kHz
mono PCM. An RMS energy gate skips silence locally without requiring Gemini
configuration. Other eligible audio uses the configured Gemini
[audio GenerateContent API](https://ai.google.dev/gemini-api/docs/generate-content/audio).
This is model speech recognition, not verified voice detection or forced word
alignment. Music, noise and uncertain speech may return an empty result.
**No speech to caption** is terminal, creates no subtitle track and leaves no
working task. Decode failures are errors, not silence.

Native subtitles render through both desktop and command-line export. Headless
project loading restores their model and properties without opening a UI.

Speech evidence has its own `montaz/analysis/speech` cache, keyed by normalized
PCM content, Gemini model and caption schema version. Valid empty recognition
results are reusable. Atomic owner-only writes retain completed evidence after
Stop; invalid or interrupted results are not cached. Recognition text stays out
of run logs. Progress logs include checked, cached, skipped and caption counts.

Review the exact words and approximate timing before **Apply captions**. Source
hashes, native clips, sequence revision, rational FPS, text and frame bounds are
rechecked before a single native undoable subtitle batch. One Undo removes the
added events; an empty native subtitle lane can remain. The native insertion
helper now propagates add/remove failures so failed batches can roll back.

**Clean caption style**, **Bold caption style**, and **Original caption styles**
use Kdenlive's global subtitle rendering override with one native Undo. They do
not recreate caption events or change IDs, text or timing. Presets affect all
current captions; Original clears the override to restore their individual saved
styles. A custom global override set before the assistant is currently cleared
by Original; Undo restores it. Preserving that override across preset changes
is an open maintenance item. Refine recognition and timing in Kdenlive's subtitle editor. Animated
word captions, narrator generation, overlapping/processed audio and forced
alignment remain future work.

## Object-aligned match cuts (prototype)

Select two adjacent video-only shots on one unlocked track and choose **Match
cut**. One schema-constrained Gemini request suggests up to three whole-subject
pairs with up to three corresponding landmarks. Valid landmarks align their
centroid and spacing; missing or inconsistent landmarks retain subject-box
alignment. Landmarks follow a tracked central subject region, rather than being
tracked individually. The
assistant ranks them by the crop each pair needs, drops shape/zoom failures and
near-duplicates, and the native MLT/OpenCV helper tracks at most two pairs over
short windows around the cut. Among pairs that pass the tracking and alignment
gates, the lowest crop/motion cost wins; the review reason names the chosen pair,
its zoom, cropped area and added reframing motion. These are geometric costs, not
a confidence score. **Compare alignment** shows original and proposed boundary
frames. **Apply alignment** adds editable Transform effects to
both clips in one Undo. Source trims, shot timing and ordering stay unchanged.

Tracked subject size now uses a guarded robust trend fit, with local position
smoothing. It retains the baseline when fitting cannot explain measured motion
or satisfy existing coverage/zoom gates. Landmarks reference the exact native
seed frame, preserving real first-frame translation and zoom. Cache version `object-match-7`
invalidates older alignment geometry; other evidence caches are unchanged.
The October 1 constant-size exports reduced size jitter from 1.13% to 0.42% per
frame. Native fixed-seed scale registration now recovers the ten controlled zoom
windows, with maximum boundary size error 3.39%; the constant fixture now measures
0.70% size jitter per frame. Real footage and very small zooms still need calibration;
review motion after Apply. See [stability evidence](../dev-docs/montaz/match-stability-report.md).

An empty answer finishes as **No match**. Unstable tracking, excessive cropping,
missing media or provider errors never apply an effect. If the helper rejects
every tracked pair, the run fails rather than reporting No match. A second pair is
tracked only while enough of the 90-second limit remains. Stop kills helper work
and starts no further pair. Complete candidate evidence and empty answers are
cached separately from scene/speech analysis; failed or time-truncated runs are not. Run logs contain phases, model, cache use and outcome,
without frames, paths or descriptions. The prototype requires progressive,
square-pixel projects at up to 60 FPS, sources no larger than 128 MiB, and no
existing bin/clip effects, persistent groups, speed changes or compositions.
Zoom is limited by source resolution: 1.6× for 1080p on a 1080p canvas,
up to the native 2× ceiling for 4K. Existing source letterboxing may remain.
The combined implementation has a real-wheel desktop Apply/Undo/Redo/reopen/export
check; perspective differences and inaccurate landmarks still affect quality.
See [landmark integration](../dev-docs/montaz/landmark-integration.md).

The development launcher supplies `MONTAZ_TRACKER` from the build's `bin/`
directory. Building the editor with `BUILD_MONTAZ_ASSISTANT=ON` also builds the
helper. No Python vision dependency or provider call is needed for the reproducible
native quality check:

```sh
python3 assistant/tools/check_match_quality.py \
  --craft-root /path/to/CraftRoot \
  --build-root /path/to/kdenlive/build \
  --output-dir /private/review/match-cut-check
```

This generates moving textured objects outside Git and offers three fixed seed
pairs. Real tracking rejects a flat-background pair, selects the disc pair and
never tracks the zoom-heavy third. It then exercises source-frame offsets, native
application, Undo/Redo, effect serialization and export. `selection-report.json`
records call counts, timing, frames, metrics and cache reuse. `--live-gemini`
adds one separately labelled request with the configured key on the same
synthetic frames; that answer never feeds the native checks. General portrait
framing, manual anchor correction, wider (whole-project) candidate search,
phrase/downbeat detection, whip pans, object wipes and speed ramps remain future work.

Captured before/after drafts can be verified without provider calls using
`assistant/tools/check_editorial_quality.py --craft-root … --build-root …
--input-dir /private/captured-draft`. Inputs are `result.json` (proposal/review)
and `private-sources.json` (bin IDs to absolute files). Native Apply, Undo/Redo,
reload, exact-zone export and decoded timing are checked. Keep captures and media
outside Git; see [duration evidence](../dev-docs/montaz/editorial-duration.md).
