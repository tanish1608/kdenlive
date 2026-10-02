<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Montaz demo checklist

Updated October 2, 2026. Based on the user's supplied script and logistics list.
Target: 14 minutes of presentation plus one minute of buffer. Existing features
only; no packaging or new features. The user chooses the first work item.

`[x]` means confirmed within the stated scope; `[ ]` means preparation or demo
verification remains. Available footage is not automatically a finished demo.
Mark items complete with an artifact location or rehearsal result. Store footage,
recordings, projects and exports outside Git.

## Confirmed starting point

- [x] User reports the clips and demo materials are available; individual paths
  and which exports are final still need mapping in D01.
- [x] Maintenance fixes are merged; rebuilt native editor and regression checks
  pass. [Evidence](maintenance-integration-review.md).
- [x] Existing changes and agent roles are documented, including a Mermaid
  diagram. [README](../../README.md), commit `360172e`.
- [x] Implementation branch is on the user's GitHub fork:
  `codex/ai-assistant-foundation`, published checkpoint `360172e`.

## Presentation order

| Section | Length | Ends at | Material |
| --- | --- | --- | --- |
| Problem and solution | 1:00 | 1:00 | Raw trip clips and Montaz's purpose |
| Hackathon version one | 2:00 | 3:00 | Story plus 20–30 s of the Electron app |
| Why version one could not ship | 1:30 | 4:30 | Actual old Goa output and reproduced failures |
| Pivot to Kdenlive | 1:00 | 5:30 | Editor ownership and AI proposals |
| Goa workflow | 3:30 | 9:00 | Recorded brief, review, Apply/Undo, music and beat cuts |
| Interface and architecture | 2:00 | 11:00 | Labelled dock and agent diagram |
| Before/after examples | 2:00 | 13:00 | Speech captions, match cut and beat sync |
| Final Goa output and close | 1:00 | 14:00 | Finished reel full screen with audio |

## Work checklist

### D01 — Map prepared assets

- [ ] Record paths for Goa clips with audio, old-app recording/export, podcast
  speech, car images/clips, travel music, and any existing final comparisons.
- [ ] Identify the saved native projects, final exports and recordings to use;
  keep originals and use private project copies for rehearsal.
- [ ] Choose the Goa subset and profile deliberately. Mixed orientations currently
  pillarbox; select landscape footage if a full-frame landscape reel is required.

### D02 — Version-one story and recording

- [ ] Confirm hackathon event, build hours, team and outcome with the user.
- [ ] Rehearse the existing Electron app or use its saved recording; capture the
  useful 20–30 s segment without depending on old live providers.
- [ ] Find the actual September 27 Goa output and verify the quoted failures
  against [migration context](context.md): underfilled/repeated edit, failed
  analysis, duplicate application, captions on silence and invalid quality review.
- [ ] Separate the original demonstration from the reasons for the pivot.

### D03 — Goa workflow recording

- [ ] Use the prepared sources, one exact brief, preset and feasible duration.
- [ ] Record import → brief → shot reasons → review → Apply → playback → one
  Undo → Redo; show the actual duration and any underfill explanation.
- [ ] Preview/apply music, then request **Transitions → Beat-synced cuts**,
  review the timing and apply. The old script's “Align cuts” label is obsolete.
- [ ] Check that the chosen timeline is eligible for beat alignment. Added
  original audio, subtitles, effects or groups can make it ineligible.
- [ ] Condense the recording into the 3:30 segment; shorten waits transparently
  and keep the unedited recording as backup.

### D04 — Final Goa reel

- [ ] Decide which audio the final reel should contain. The AI visual planner
  inserts video only; it does not automatically preserve or mix original speech.
- [ ] Listen to music level, start/end fades and cuts; check for silence, clipping,
  abrupt endings, duplicates, frozen tails and unwanted borders.
- [ ] Export through Kdenlive; play the saved file from start to finish and ensure
  the full reel plus closing sentence fits the last minute.

### D05 — Speech-caption comparison

- [ ] Select a clear short podcast/speech excerpt from the prepared material.
- [ ] Recognize, review/correct and apply captions; compare Clean and Bold while
  checking words, timing and readability at presentation size.
- [ ] Save a roughly 40 s before/after presentation clip with audible speech.
- [ ] Describe this as **auto-captions on speech**. Silence removal, filler-word
  editing, speaker switching and speech synthesis are not current features.

### D06 — Match-cut comparison

- [x] Map the supplied 26 photo-derived side-profile clips and prepare native
  6-frame/12-frame mash-cut projects and exports. [Evidence](demo-car-mashcuts.md).
- [x] Choose a compatible sedan pair for a real selected-pair review; keep viewpoint,
  whole-subject position and scale comparable. If using car photographs, prepare
  video sources and describe them honestly as photo-derived shots.
- [x] Use the existing selected-pair Match cut review; inspect whole-subject
  alignment, zoom softness and frames immediately before/after the cut.
  The tested proposal worsened already aligned inputs and was rejected.
- [ ] Record plain versus aligned versions with identical timing and sources.
  The prepared mash-cut reel is ready; a convincing **AI alignment improvement**
  still needs an unaligned source pair.
- [ ] Use 20–40 s only if the result reads clearly; give unused time to beat sync.
  No new automatic multi-shot montage is required.

### D07 — Beat-cut comparison

- [ ] Use the prepared travel sequence/music and current merged beat fix.
- [ ] Confirm actual cut-to-beat improvement by listening and inspecting cuts.
  If the result reports energy accents, present it as accent alignment.
- [ ] Save before/after with the same music, shot order and total duration; allow
  roughly 40 s and keep audio on during playback.

### D08 — Interface screenshot and architecture

- [ ] Capture a clean AI dock showing chat, task states, shot reasons and Apply;
  label native Undo separately in the editor toolbar.
- [ ] Adapt the existing README diagram into a readable presentation graphic:
  footage/brief → analysis → plan → one critic review → user review → native
  validation → one undoable application. Show optional tool paths separately.
- [ ] Explain cached evidence, revision checks, bounded review and silence handling
  without claiming the model can never guess or make a mistake.
- [ ] Update the script's old “about 240 tests” figure to the verified **290 Python
  tests with media checks enabled, 49 Qt client/command and 7 chat checks**;
  mention native checks separately. Credit the Codex/Claude implementation and
  review work without implying every task ran in parallel.

### D09 — Engineering changes and slides

- [ ] Select concise change bullets from the README: native ownership, chat,
  caching/concurrency, grounded planning, bounded critic, draft replacement,
  ElevenLabs audio, beat/match tools, captions, cancellation and regression tests.
- [ ] Prepare slides and speaker notes in the user's eight-section order.
- [ ] Link the implementation branch in the demo; the fork's default `master`
  still displays upstream content.
- [ ] Keep unfinished whip/wipe/ramp, portrait, narration and montage features out
  of the demonstrated feature list.

### D10 — Zoom and playback logistics

- [ ] Test Zoom screen sharing with computer audio using the actual final reel;
  have someone confirm they can hear speech and music and read captions.
- [ ] Check microphone commentary, full-screen playback and shared video smoothness.
- [ ] Arrange the editor, slides and exported clips in presentation order; close
  distracting windows and notifications before presenting.
- [ ] Keep local recordings/exports available if provider calls or playback fail.

### D11 — Complete rehearsal and freeze

- [ ] Run all eight sections with a timer; finish by 14:00 and preserve the buffer.
- [ ] Confirm every media file opens and plays with sound in the chosen setup.
- [ ] Save the tested projects and note the exact build/commit used.
- [ ] Freeze that build. Only fix a demonstrated rehearsal blocker before the demo.

## Working log

Add completed item IDs, artifact paths, results and any concrete blockers here.

- October 2: D01 car sources mapped; D06 prepared 26-shot native projects and
  1080p/30 fps exports in `~/Movies/montaz-demo-mashcuts-20261002/`.
  The 6-frame reel is 156 frames/5.2 s; the 12-frame reel is 312 frames/10.4 s.
  Every exported frame matches its expected source. Source credits retained.
  One real Gemini pair review proposed transforms that added 9–10 px wheel drift;
  preserved the supplied geometry. No new montage feature or AI improvement
  claimed. One Qt accessibility crash during render-job insertion remains open;
  reloading the saved project and retrying the export succeeded.
