<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Assistant implementation plan — October 1, 2026

## Goal and boundaries

Build a practical travel editing assistant inside Kdenlive. Use its docks,
actions, palette, project bin, timeline, playback and undo. Keep Gemini for footage
understanding. Packaging and publication remain deferred. Chat creates reviewed
edit proposals; messages never directly execute code or modify project files.

## Current milestone: native chat

- [x] Add an AI toolbar action using Kdenlive's existing dock action system.
- [x] Replace the form with a conversation, multiline composer, Send and Stop.
- [x] Keep refinements within the active project/sequence; New brief clears them.
- [x] Show a task list driven by real analysis/planning/application events.
- [x] Present filenames and durations; apply only the current reviewed proposal.
- [x] Compile, exercise the interface, test cancellation and project isolation.

## Workspace refinement

- [x] Align the AI toggle at the far right of the top toolbar.
- [x] Keep the assistant in a full-height right dock after layout restoration.
- [x] Hide the source Clip Monitor; retain native timeline playback and preview.
- [x] Verify opening/closing, asset selection and reopening the saved Goa project.

The native build and `assistanttest` pass. The desktop checks restored the old
tabbed assistant to the right, reclaimed editing space on close, kept source
selection from reopening the Clip Monitor, and moved Library alongside the bin.
The launcher now stages upstream runtime resources into the development bundle.
Logging/Editing/Audio/Effects/Color presets are available; switching to Editing
keeps the assistant on the right and the source monitor hidden.

Conversations are bounded and session-only. Secrets, prompts and source filenames
stay out of run logs. A pending review is visibly distinct from background work;
Stop, failure and invalidation leave no task marked as working.

## Real-footage checkpoint

Use the user-supplied `~/Downloads/goa-clips-test` outside version control in a
separate test project. Fifteen clips contain approximately **30.81 seconds** total,
including portrait/landscape, variable frame rates and sub-second shots. A
60-second request cannot be fulfilled without repetition or generated material.

- [x] Run Gemini on all clips and verify reuse on a second brief.
- [x] Review visible observations, selected source windows, sequence and durations.
- [x] Check the native preview, apply/undo, exact cut seeking and save/reopen.
- [x] Identify abrupt-cut and framing problems before expanding edit operations.

## Findings and completed fixes

- Video stream length can be shorter than audio/container length. Sampling v3
  probes visual duration, bounds candidate windows, and avoids boundary samples.
  The corrected sampler decoded all fifteen sources; a synthetic audio-tail
  regression exercises the same failure without committing private footage.
- The first valid draft used four scenic shots (9.48 seconds). Feasible-target
  guidance plus a chat refinement produced eight shots (17.85 seconds). All
  fifteen analyses were reused; the repeated run took 23.15 seconds versus 49.16
  seconds for initial analysis. These are observed runs, not a controlled quality
  benchmark: the refinement also changed the brief.
- Travel plans now reject sub-second cuts. The native draft remains video-only;
  portrait footage is pillarboxed in the landscape test project. Framing and
  sound need explicit work; passing integration checks does not make this a
  finished travel film.
- Native Apply/Undo/Redo, seeking at the first cut, saving, Stop and project
  conversation reset were exercised. The review project is kept with the supplied
  clips under `montaz-review/goa-highlight.kdenlive`, outside this repository.

## Next implementation gates, in order

1. **Editorial quality — first iteration verified:** sampled video windows,
   validated usable trims, micro-clip exclusion, feasible-duration estimates and
   shot explanations. Native Goa planning/apply/undo/save/reopen/export was checked;
   [quality review](quality-review.md) records findings and limitations.
2. **Sound:** ElevenLabs music and occasional effects, measured beat/phrase timing,
   native audio insertion and gain/fades. Stop and errors must cleanly terminate.
   Instrumental generation, raw/prepared reuse, preview and native undoable
   import/insertion are implemented. The Goa music export retains all 474 decoded
   visual frames and measures -18.1 LUFS / -5.0 dBFS true peak. Local energy-accent
   cut proposals and native undoable retiming are implemented; each boundary shift
   is limited to one quarter second and available source length. These are not
   verified beat/phrase boundaries. Explicit sound descriptions now produce one
   previewable ElevenLabs effect at the playhead, using the existing native audio
   import/undo path. Cached visual observations now support up to three sparse
   reviewed foley suggestions; a selected cue must be generated and previewed
   separately before native Apply. Live Goa verification is recorded in the
   quality review. Downbeat/phrase analysis, ducking and native fade-effect proposals
   remain pending. A conservative local rhythm estimate now supports reviewed
   cut timing, with cached observed anchors and explicit accent fallback;
   downbeats, phrases and variable-tempo tracking still need work. See
   [music timing](music-timing.md).
3. **Narration and captions — first iteration:** separately cached selected-audio
   speech recognition, local silence skipping, terminal empty reviews, reviewed
   native subtitle batches and Clean/Bold/Original render presets are implemented.
   Native tests cover one Undo/Redo, stable subtitle IDs/text/timing across repeated
   styles, source changes and insertion failures; transport tests cover Stop,
   progress completeness and clean exits. A controlled speech fixture verified
   live recognition, cached retry, native Apply/Undo/Redo, style switching,
   save/reopen and desktop/command-line exports. Headless subtitle loading no
   longer dereferences an absent main window. Word alignment, narration generation,
   processed/overlapping audio and per-project dialogue modes remain pending.
4. **Quality review — first draft iteration:** one cached-observation model
   review, at most one same-duration revision, strict original plan validation,
   explicit unavailable states and bounded Stop/deadlines are implemented.
   Offline tests cover failed/invalid reviews, cancellation, native progress and
   privacy. Full-bin cached-Goa planning/review and desktop Apply/Undo/save/reopen/
   export now pass with a scoped upstream Qt Cocoa ownership backport. The
   three-shot/240-frame draft still undershoots a feasible 20-second brief; these
   checks establish integration, not film quality. Finished-sequence replacement, mixed-media
   perceptual review and measured quality comparison remain pending. Publication
   comes later.

**Deferred by the user:** portrait framing. Later add explicit project orientation
and native framing proposals for mixed portrait/landscape; preserve manual framing
and test exact undo/redo. Do not let this block sound and editorial improvements.

Native phone conversion also needed a fix: preserve recognized audio streams,
exclude unsupported auxiliary audio, and reject nonzero encoder exits even when
a partial file exists. Synthetic native conversion regressions and the supplied
phone clips exercise it. The original footage and earlier project remain preserved.

Each gate needs focused regressions plus an actual editor/output check. Provider
success and passing unit tests alone do not establish editorial quality.

## Object-match prototype (October 1)

- [x] Select two adjacent native video instances; keep frame timing unchanged.
- [x] Reuse MLT decoding/OpenCV CSRT in a bounded, cancellable helper process.
- [x] Ask Gemini for one object pair; allow an explicit empty answer.
- [x] Cache successful pair evidence separately by content, trim, canvas, FPS,
  model and algorithm version; preserve existing scene/speech caches.
- [x] Solve uniform position/scale keyframes with a 2× zoom ceiling and no newly
  exposed canvas edges. Reject flat regions, unstable appearance and geometry.
- [x] Add boundary comparison and reviewed native Transform application.
- [x] Verify native Apply/Undo/Redo, serialization and exported geometry checks.
- [x] Exercise desktop comparison, Apply, Undo/Redo and a fresh-process reopen.
- [ ] Search multiple candidate pairs, allow manual anchor correction, measure
  musical beats/phrases, then add directional motion/wipe/ramp proposals.

Two adjacent cuts in the latest Goa draft returned no compatible objects. The
controlled moving-disc pair returned a tracked match. Neither is a travel-film
quality approval. Generic travel pacing remains until transition selection is
verified beyond this deliberately narrow prototype.
