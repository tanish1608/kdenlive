<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Architecture

## Ownership

| Component | Owns | Does not own |
| --- | --- | --- |
| Kdenlive + MLT | Project/bin, sequence, tracks, playback, frame timing, undo/redo, subtitles, effects, rendering | Editorial model calls |
| Native assistant dock/adapter (experimental) | Snapshot capture, proposal preview, approval, revision checks, native command application, worker lifecycle | A second timeline or player |
| Python assistant | Analysis cache, provider requests, editorial plans, recipe constraints, run events | Mutable editor state or direct project-file writes |

At most one live worker per editor process, with a project-scoped active run. The
diagnostic implementation starts a fresh process for each request and waits for
its clean exit before enabling Apply. Use
versioned newline-delimited JSON over `QProcess` stdin/stdout, not another HTTP
server, WebSocket, CRDT, or agent messaging framework. Logs go to stderr or a
project-local run directory; stdout is protocol only. Bound message sizes and
validate both sides. Provider credentials stay in the worker's private settings.

The implemented visual worker accepts `plan_media` with a separate mapping of
editor bin IDs to local source paths plus a brief/duration/pacing recipe. Paths
are input media references, never edit operations. They stay out of run logs.
`plan_demo` retains the deterministic integration diagnostic. Both return the
same insertion proposal and require a clean worker exit before Apply.

`revise_media` adds an editor-issued `draft` scope to the same visual request.
The adapter retains the last applied draft's native IDs, source trims and
path/size/mtime/conformed-duration identity in memory. Python removes only that
scope from planning occupancy, targets its exact start/track/end and returns
v1 insertions with fresh proposal IDs. Both transport and adapter validate the
replacement against the original snapshot. The model cannot name deletions.
Native deletion/insertion forms one rollback-capable Undo, restoring ownership
alongside clip state. Temporary multi-selection is cleared before deletion to
prevent native group deletion from affecting neighbors. Edited/effected/grouped
drafts are ineligible; audio, subtitles and unrelated clips remain untouched.
Ownership clears on sequence/project switch or reopen and is not inferred from
a saved project's clips. Native assistant retiming updates the owned ledger.
Source identity here is a local metadata fingerprint, not a content checksum;
visual analysis uses its separate content-addressed cache.

`compose_music` is a separate resource request. The adapter issues an immutable
video span, free audio track and unique WAV destination beside the saved project.
The worker returns that same context, path, exact project-frame duration and
content checksum. The native adapter rechecks revision, track occupancy, checksum
and MLT stream/duration before importing and inserting in one undoable batch.
It never executes a provider-selected path or edits the project file. Music
generation has one bounded request; cache reuse avoids automatic regeneration.

`compose_effect` uses the same generated-audio result envelope and native
import/undo path as music, with a separate request validator. It captures a
0.5–10 second interval at the project playhead that fits inside one visual shot,
uses a free audio track and labels the imported asset as an effect. The role is
issued by the editor request, not inferred from provider output. Music-accent
retiming rejects additional effects/audio. Each sound is described explicitly;
no visual observation is treated as evidence of audible speech or an event.

The native dock routes only bounded, standalone explicit English tool aliases
through `chatcommands`: music, sound suggestions/described sounds, beat cuts and
pair matches. Normal briefs and negations retain visual planning. Sound requests
use chat instead of an extra toolbar menu. Transitions require a menu selection
or explicit request; unavailable whip/wipe/ramp aliases finish with a local reply.
Tool routing does not bypass worker transport, native review, revision checks,
Stop or Undo. Recipe transition preferences are data only; future wiring must
intersect implemented capabilities with transitions explicitly requested by users.

## Snapshot and proposal

The native adapter issues a snapshot with project ID, sequence ID, monotonically
increasing revision, rational project frame rate, imported bin assets, tracks,
occupied ranges, and editor-issued clip-instance IDs. Assets expose durations
already conformed to project frames by Kdenlive. Every edit, undo/redo, relevant
asset/profile change, and project switch invalidates the corresponding snapshot.

The worker proposes operations referencing these IDs. Protocol v1 only defines
explicit clip insertions; unsupported operations fail validation. A proposal
contains a unique request ID and its base project/sequence/revision. Kdenlive
revalidates immediately before application on its GUI thread and rejects stale
or replayed requests. No AI-supplied code, shell commands, effect expressions, or
arbitrary file paths are executable operations.

The [quick-montage native foundation](montage-foundation.md) separately reserves
a protocol-v5 review, without extending worker dispatch or exposing a dock action
yet. The editor issues 2–6 selected video-only sources, bounded trims, per-source
zoom limits, an 8–12-frame shot length and an append position beyond the sequence.
The review can select/reorder these sources only, accounting for every exclusion.
One uniform Transform keyframe per shot frame must map its measured anchor to a
common target. Native Apply regenerates selection scope, checks all source hashes
and inserts transformed copies in one rollback-capable Undo. Original edits remain
in place; scene reload and frame-by-frame exports are checked independently of
the future AI discovery/solver. Existing worker protocol support is unchanged.

The separate `refine_timing` request returns protocol-v4 retiming operations for
existing native clip IDs, with strict rhythm measurements and observed anchors.
Protocol-v2 accent proposals remain accepted. It measures the placed music locally and proposes
bounded boundary shifts while retaining source starts, order and total span.
Native eligibility excludes grouped clips, speed/effect/transition/subtitle
edits, other audio and noncontiguous/multiple video tracks. Source bounds and
the music checksum are validated again before mutation. Apply uses native resize
and move commands; affected clips are briefly shortened to avoid overlap during
the atomic batch, then moved and resized, retaining their identities. One Undo
restores the edit. Recurring multi-band onsets support a conservative steady beat
estimate; ambiguous music falls back to labelled energy accents. Audio-content,
FPS, duration and algorithm-version caching reuse the estimate. Neither method
identifies downbeats or musical phrases; see [music timing](music-timing.md).

Application must be one atomic, undoable batch using Kdenlive's command APIs.
Prevalidate every operation; accumulate undo/redo functions; roll back all prior
operations if any step fails; push one undo entry only after full success. Store
the applied request ID with the sequence revision. Do not assume an undo macro
provides rollback. Linked audio/video groups and locked tracks need native tests.

## Time and media

Use integer project frames and rational FPS throughout the contract. Ranges are
half-open: `[in_frame, in_frame + duration_frames)`. The native adapter explicitly
converts to any inclusive MLT boundaries. Provider seconds are converted once
with documented rounding. Never infer timeline time from a percentage of a
render's duration. The editor's playhead is authoritative.

Clips, transitions, speech, music, and caption events share that timebase. Keep
source audio classifications separate: speech, music, environmental events, and
silence. A primary visual shot does not imply dialogue. Caption style changes
edit native subtitle styles; burning happens at export when requested. Skip
captions when selected audio contains no verified words.

## Editorial workflow (planned)

1. Preflight required providers; do not keep calling an unavailable model.
2. Analyze media incrementally. Cache transcript and visual analysis separately
   using content identity, model, prompt/schema version, and sampling parameters.
   Save successful partial work; never cache failures as valid scene evidence.
3. Resolve mode, orientation, feasible duration, and optional travel recipe.
4. Produce a grounded story plan from candidate shots. For visual highlights,
   generate/select music before final cut timing; measure its actual beat/phrase
   structure. Narration prioritizes complete thoughts and source meaning.
5. Validate source bounds, shot availability, repetition, pacing, and recipe
   requirements deterministically. A bounded optional critic can suggest one
   revision; provider/parsing failure is review-unavailable, never approval.
6. Present an editable proposal; apply only against its matching editor revision.

Templates are versioned data: story slots, shot duration ranges, transition
allowlist, framing, text style, music arc, and fallback rules. Begin with travel
postcard, adventure montage, and travel diary. Do not fabricate missing footage
or repeat shots merely to reach a requested duration.

The visual planner implements three pacing presets, two-video analysis concurrency
and a content-addressed observations cache. It supplies bounded silent proxies
for each first/middle/last six-second window, sampled at four FPS by Gemini.
Analysis version `video-windows-4` includes suggested usable trim offsets; local
checks bound them to visible source frames. Sub-second sources are skipped and
low-quality windows excluded. Plans use distinct usable ranges, opening/detail/
activity/ending guidance, and per-shot reasons. Sampled analysis cannot guarantee
frame-perfect motion or good storytelling. Instrumental music can follow an
applied visual edit; a separate reviewed local refinement can move cuts to energy
accents within native source limits. Reviewed speech recognition and native caption
style presets are implemented below. Narration, word alignment, beat/phrase
alignment, framing and transitions remain pending. Portrait framing is deferred
by the user.

Optional editorial review metadata accompanies the existing insertion contract;
native validation binds its shot identities and durations to the proposal. The UI
shows reasons, usable duration and excluded sources; run logs keep only numeric
metrics. It has no authority to change the edit contract.

`editorial.py` now reviews each validated visual draft once against the existing
observations. Its single text request returns keep or one revised shot plan.
Revisions reuse `proposal_from_plan`, preserving all source/recipe constraints
and the original total duration, except that drafts below 85% of a locally
reachable target may grow toward that target in the same review call. The target
uses interval unions over one allowed window per distinct source, with the same
recipe floors/ceilings as validation. Restrictive briefs may retain shorter edits
with an explanation. Review cannot directly modify an already applied edit;
an explicit reviewed `revise_media` request uses the native replacement path.
Oversized context skips review; provider/parse/invalid-revision failures explicitly
retain an unreviewed original draft. Cancellation never takes that fallback.

Strict native review metadata distinguishes kept/revised/unavailable with bounded
reasons and allowlisted diagnostic codes. Ordered analysis/planning/reviewing
progress and a clean exit are required before Apply. Entering reviewing starts
a 45-second native cap without extending the run deadline; a forced timeout fails
the run. Only status/code are
logged. No recursive critic, reanalysis or automatic revision retry is present.

## Runs and cancellation

Each run has a run ID; each event has an increasing sequence number and explicit
stage/status. Statuses are `pending`, `running`, `completed`, `skipped`, `failed`,
and `cancelled`. The UI never guesses completion from phase spelling or predicts
the next stage. At a terminal run event all stages are terminal. Late events are
ignored; reconnect reads a snapshot rather than replaying actions.

The native Stop control cancels the worker's current run and its subprocesses.
Use cooperative cancellation, then terminate/kill with bounded grace periods.
Editor-native render cancellation stays owned by Kdenlive. Preserve completed
analysis; never label a stopped job complete. Cache cleanup is independent of
editor undo. Apply batches execute atomically; stopping cannot leave half an edit.

Run records capture provider/model versions, durations, cache hits, validation
failures, critic outcome, proposal IDs, and editor acknowledgments. Redact secrets
from structured and text logs. Footage/transcripts are project data, not telemetry.

The diagnostic implementation records timestamps, run/stage outcomes, revision,
asset count, and apply result in the application's cache at `montaz/runs/*.jsonl`.
It does not record bin paths, source text, environment variables, worker stderr,
or free-form event reasons. Provider/model/cache progress is recorded. Starting
a run retains the newest 200 UUID-named logs and leaves unrelated files untouched.
Broader measured editorial quality evaluation remains pending.

## Native integration locations

- `src/mainwindow.cpp`: `MainWindow::addDock`, using KDDockWidgets in this release.
- `src/project/projectmanager.h`: active timeline and document undo stack.
- `src/timeline2/model/timelinemodel.hpp`: `requestClipInsertion`, resize/move
  requests, and overloads accumulating undo/redo functions.
- `src/core.h`: `Core::pushUndo` for a successful batch.
- `src/otio/`: interchange boundary; OTIO is not the live synchronization channel.
- `tests/`: native timeline, trimming, mix, subtitle, and OTIO regression suites.

Keep the adapter small and feature-gated. Prefer upstreamable changes and preserve
Kdenlive's license/attribution. Do not transplant an unreviewed community fork or
depend on Linux D-Bus for the macOS path.

## Cached sound suggestions

The explicit chat request `suggest sounds` captures all native video-only instances on one
track (up to 64). Groups, clip effects, speed changes and transitions are excluded;
existing audio must be assistant music/effects. Only the selected unique sources
are resolved, hashed and probed. Cache-only analysis reads current content/model/
frame-rate/version entries with two local readers; missing or invalid entries are
skipped, never silently reanalyzed. No valid entries is a terminal unavailable
state. One Gemini text request selects at most three distinct foley categories
from cached silent observations; an empty selection is valid.

Cue intervals are derived locally from each original source trim and cached usable
window, bounded to 0.5–2 seconds, at least three seconds apart, and inside one shot
with a free unlocked audio interval. The model supplies neither timestamps nor
file paths. Review shows the exact observation and proposed generator prompt;
these are editorial sounds, not detected or recovered location audio.

Generating the selected cue rechecks native revision/shot identity and source hash
(with a 128 MiB native source-verification limit), obtains a native destination and track,
and uses the existing cancellable ElevenLabs effect worker. Generation cannot
apply audio; Listen and native Apply/one Undo remain separate. A changed source or
sequence invalidates the review. Structured logs include cache/cue counts and
model identity, never observations, prompts or file paths. Progress and clean
worker exit are required even for zero suggestions; Stop discards late results.


## Selected-audio caption boundary

`captions.py` recognizes only editor-issued placed original audio intervals and
selected absolute stream indices. It does not use visual descriptions to infer
speech. Normalized PCM speech evidence is cached separately from silent-video
observations; silence and absent audio terminate locally without Gemini setup.
Recognition returns bounded plain text and approximate millisecond intervals,
converted to the native rational frame grid. The reviewed caption envelope
carries original-source hashes and project/sequence/revision identity, never a
path or arbitrary subtitle markup to execute.

`EditorAdapter` rechecks source identities, exact original audio selection and an
empty writable subtitle model before native insertion. Kdenlive's subtitle model
allocates IDs, manages timing, rendering, files and native Undo. Its undoable add
helper now returns actual insertion/removal failures. The assistant batches
native commands with rollback and commits one Undo only after complete success.
Subtitle edits invalidate outstanding reviews. Clean/Bold presets use the native
global force-style property; clearing it restores saved per-event styles without
recreating events. Original currently clears a pre-existing custom global
override as well; Undo restores it. Preserving that override is checklist M01.
Empty recognition creates no subtitle model. Recognition and
style presets are bounded prototypes; neither establishes word timing accuracy.
Headless project loading initializes the native subtitle model directly when no
main window exists; desktop loading retains the existing subtitle editor path.
Both paths preserve native subtitle properties for rendering.

## Object-match boundary (experimental)

`propose_match` captures exactly two selected adjacent video-only clips, on one
unlocked track, with their original trims and a progressive square-pixel canvas.
Existing clip/bin effects, real groups, speed changes and compositions are excluded;
Kdenlive's temporary multi-selection group is supported. No clips are reordered,
retimed or inserted. General portrait framing remains deferred.

A separate `montaz-tracker` executable reuses the installed MLT/OpenCV decoder and
CSRT tracker; Python needs no vision package. Its native project-frame decoding
supplies bounded images to one schema-constrained Gemini whole-subject/landmark
request. Valid corresponding landmarks supply a centroid and RMS spread; a
central 60% subject region carries their rigid translation and scale. Landmarks
reference the native seed frame; there is no frame-one size compensation. Weak
landmarks fall back to subject-box alignment, not independent landmark tracking.
One-second boundary
windows are tracked at every project frame (up to 60 FPS). Low-frequency patch
correlation rejects flat/disappearing regions; shape, motion and scale checks reject
unstable tracks. These checks are conservative proxies, not calibrated object
identity or tracking-confidence guarantees.

Content-addressed pair evidence includes source hashes, absolute source trims,
canvas, rational FPS, model and algorithm version. Valid empty pairs are cached;
tracking/provider failures are not accepted as scene evidence. Only selected
sources are read, with a prototype 128 MiB per-source limit. Existing visual and
speech analyses are preserved. Ordered finding/tracking progress, a 90-second run
cap, a clean worker exit and explicit terminal outcomes keep Apply unavailable
while work is running; Stop terminates native helper work and discards late results.

After native tracking, `match_stability.py` fits a robust log-height trend and
local-linear centers under residual guards. Baseline acceptance gates run first;
failed fitted geometry falls back to the original solution. At most two tracked
pairs and one provider call remain. `object-match-7` invalidates changed geometry
without touching visual/speech/music evidence. A native fixed-seed scale-registration
pass measures size around CSRT's predicted center with bounded search, confidence
and ambiguity gates; it rejects disappearing or scale-ambiguous subjects instead of
accepting background tracking. See the stability report.

Protocol-v3 reviews carry only allowlisted uniform numeric transform keyframes,
source identities, native clip IDs and bounded JPEG boundary previews. Position and
size share a common subject anchor, with source-resolution zoom limits (1.6× for
1080p on 1080p, up to 2× for 4K) and canvas coverage checks. The review target size
remains a fraction no larger than one, including landmark spreads.
The dock compares original/aligned boundary images; it is not a second player.
The adapter rechecks selection, revision, sources, eligibility and native effect
availability before adding two editable `qtblend` effects in one rollback-capable
Undo batch. Existing source letterboxing can remain. Beat cuts are implemented
through the separate timing contract. Whips, occlusion
wipes, speed ramps, manual anchors and multi-pair sequence optimization remain
planned; no arbitrary model-provided effect expression is executed.

Native size estimates receive guarded smoothing/fitting while retaining measured
motion; large jumps still reject. The worker checks video-stream duration before
tracking an uncached pair, so a longer audio/container tail cannot silently become
a frozen visual match. Complete negative pairs are cached; tracking/provider
errors are terminal and do not populate the cache.
