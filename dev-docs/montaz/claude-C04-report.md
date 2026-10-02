<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# C04 — Audio quality regression evidence (delivered, pending review)

Claude, October 2, 2026. Base `cbf834b`, branch `codex/claude-audio-regressions`.
The checklist item stays open until Codex reviews and integrates the branch.

## Files

New files only:

- `assistant/tests/test_audio_quality_regressions.py`
- `assistant/tools/check_audio_quality.py` (private QA)
- this report

`beats.py`, `captions.py`, provider and native code are **unchanged**. Bugs are
delivered as failing evidence with proposed fixes, as described below.

## Method

**Synthetic fixtures.** Band-split PCM matching the decoder's `beats.analyze`
output: three interleaved channels (low, mid, high) of s16 at 8 kHz.

- Kicks go in the low band, snares in the mid band, hats in the high band.
- Optional sustained pads and Gaussian noise; fixed seeds throughout.

**What runs for real.**

- **Beats:** the real `onset_envelope()` and `estimate()`, and `analyze()` with only
  the FFmpeg boundary (`MediaTools._output`) mocked.
- **Captions:** the real `propose_captions()`, with only the decoder output and the
  provider mocked.
- **Scoring:** generated attack times are ground truth; a claimed beat counts when
  it is within 30 ms of one.

No credentials, network or real speech are used.

**Known bugs as `@unittest.expectedFailure`.** These tests assert the correct
behaviour. When Codex fixes a bug, the test becomes an unexpected success, which
fails the suite until the decorator is removed. No existing test was changed or
disabled.

## Findings

### Bug 1 — audio already audible at the first sample hides most onsets (`beats.onset_envelope`)

`previous` starts at zero for every band, so the first 10 ms block scores the whole
bed's log-level as an "onset". Under a sustained pad that is 19.7, against about
1.9 per kick. `estimate()` normalizes by that peak, so real attacks fall below its
0.15 floor.

| Kicks under a bed | Now | First-block rise removed |
| --- | --- | --- |
| pad peak 2500 or 5000 (110 BPM) | `insufficient_onsets` | beats, 110 BPM, precision and recall 1.0 |
| noise σ = 500 (120 BPM) | beats, recall 0.96 | beats, recall 1.0 |
| noise σ = 1000 | **60 BPM** (octave error), recall 0.46, an unsupported beat at tick 0 | beats, 120 BPM, precision and recall 1.0 |
| noise σ = 2000–3000 | `insufficient_onsets` | beats, precision and recall 1.0 |
| noise σ = 4000 | `insufficient_onsets` | beats, 121.1 BPM, precision 1.0, recall 0.875 |
| pad peak 8000 (louder than the kicks) | fallback | fallback (`unstable_tempo`) |

The impact is likely broad: generated instrumentals usually start audibly. The
fallback is labelled, but its reason ("insufficient onsets") is wrong. Analysis
frames that start at frame 0 drop the tick-0 beat, but the halved tempo is still
reported.

**Smallest proposed fix (Codex):** in `onset_envelope`, keep updating `previous`
on the first block but append `0.0` instead of its rise (for example
`onset.append(strength if offset else 0.0)`). Then bump `beats.VERSION`, so cached
results computed with the old code aren't reused. The tests
`test_the_start_spike_is_the_only_cause` and the QA tool's `proposed_fix` columns
show this single change restores every case above without adding false beats
(precision 1.0 at every noise level).

### Bug 2 — a sub-frame caption segment fails the whole request (`captions.propose_captions`)

Valid, non-overlapping segments that land in the same frame after outward rounding
make `end <= start` after clamping to the previous caption's end. The code then
raises `invalid_captions` for the entire request. Example at 30000/1001: "We made
it." at 0–1010 ms, then "Oh!" at 1010–1030 ms.

**Smallest proposed fix:** when a mapped caption collapses but `start` is still
inside the clip, use `end = start + 1` (later captions are already clamped after
it). Otherwise merge its text into the previous caption when `caption_text`
accepts the combination. Only raise if neither is possible.

### Bug 3 — an unmappable answer is cached and never retried

Segments are written to the speech cache *before* frame mapping is checked. After
bug 2, every later run on the same audio reuses the cached answer, with **zero**
provider calls, and fails again. The failure is permanent until the cache is
cleared.

**Smallest proposed fix:** map the clip's segments to frames first, and write the
cache only after mapping succeeds. With bug 2 fixed, this becomes defence in depth.

### Measured behaviour that works, with recorded limits

**Steady rhythm** (72, 97.5, 120, 128 and 160 BPM, kicks plus off-beat hats):
beats; tempo within 0.1 BPM; precision and recall 1.0; mean timing error 0–4.4 ms
and maximum 9.2 ms, which is within the 10 ms analysis tick.

**Half-time feel** (70 BPM kicks under 140 BPM hats): beats at 70 BPM; precision and
recall 1.0. A quiet pad (peak 1000) under kicks keeps the pulse.

**Labelled fallbacks, with no beats claimed:**

| Input | Reason |
| --- | --- |
| syncopated pop pattern over a steady 120 pulse | `unstable_tempo` (a recall limitation, not an error) |
| accelerando 100→130 BPM | `weak_periodicity` |
| timing jitter of ±30 ms | `unstable_tempo` |
| timing jitter of ±40 ms | `weak_periodicity` |
| pad only | `insufficient_onsets` |
| noise only | `insufficient_onsets` |
| 2.5 s of steady kicks | `insufficient_onsets` |
| digital silence | `invalid_music_audio` (unavailable, never an empty grid) |

Jitter up to ±20 ms still yields beats, with precision and recall 1.0.

**Frame conversion** (`analyze()` at 24000/1001, 25, 30000/1001 and 60000/1001):

- Every beat is a sorted, unique frame strictly inside the placed duration.
- Each lies within half a frame plus one 10 ms tick of a real attack.
- Attacks after the placed 9.5 s are excluded.

**Captions.**

- **Silence gate:** audio at −90, −70 and −62 dBFS never configures a provider and
  finishes as one skipped clip.
- **Above the gate:** noise at −58 dBFS and louder, and music-like chords at −20
  dBFS, *are* sent for recognition. This is by design: the gate measures energy, not
  voice. Empty answers finish without captions. Nothing here shows whether a real
  provider hallucinates speech from music or noise.
- **Frame mapping:** at the same four rates, every caption's frame interval covers
  its spoken interval, rounds outward by less than one frame each side, stays inside
  the clip, and keeps order.

## Validation

| Check | Result |
| --- | --- |
| `test_audio_quality_regressions` | 14 tests: 11 pass, 3 expected failures (bugs 1–3) |
| Full suite | 239 run, 226 pass, 10 skipped, 3 expected failures |
| Ruff on both files | clean |

The 10 skips are the opt-in native-tracker checks that need `MONTAZ_TRACKER`. There
is no native code here.

**QA tool:**

```sh
python3 assistant/tools/check_audio_quality.py --output-dir /private/qa/audio
```

- **What it runs:** 12 fixtures, plus noise, jitter and pad sweeps (each with the
  proposed-fix variant where relevant) and a caption gate sweep.
- **Bug status:** it re-runs the three known-bug tests and reports each as
  reproduced or fixed.
- **Outputs:** `audio-quality-report.json` and mono WAV renders of each fixture.
- **Runtime:** about 10 s on this Mac.

Artifacts are in `~/Movies/montaz-claude-audio-quality/`.

## Limitations

- **Synthetic only.** The fixtures are idealized percussion, pads and noise, not
  real music: no real bass lines, swing, reverb tails, fades or mixing. Real recall
  could be lower or higher.
- **The 30 ms scoring window is mine.** It is a scoring choice for these reports,
  not a quality threshold. No universal thresholds are imposed, and amplitude alone
  says nothing about music quality.
- **Captions were tested with tones and noise only.** No real or synthesized speech
  was used, so quiet real speech near the −60 dBFS gate and provider behaviour
  (including hallucination) are untested.
- **Fixes are proposals.** None were applied: each needs Codex's change plus cache
  version handling, and the three expected-failure decorators then come off.
