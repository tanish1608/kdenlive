<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Match-cut stability report — S1–S5

Claude, October 1, 2026. Branch `codex/claude-match-stability`, based on the
integration plan commit `592a584`, which contains `8b6dcfc`. **Codex accepted the
stability improvement after independent integration checks.** The original branch
commit `69ad410` remains intact. See the acceptance note below. The figures are
engineering measurements on synthetic fixtures; they do not establish better
results on real footage or completion of the zoom-tracking target.

## S1 — Why the five-frame smoother still breathed

On the controlled disc, the native CSRT helper reports a constant 66 px subject
(at 640×360 analysis) at only a handful of box heights: 59, 60, 62, 63, 64 and 65 px.
It stays on each height for 3–7 frames before switching. The baseline smoother
averages over ±2 frames with weights 1-2-3-2-1. That window is shorter than a
typical run at one height, so most of the 8% swing passes through. Because
`scale = target height / box height`, each one-pixel change also changes the
zoom by about 1.6%. The measured result was an RMS per-frame scale change of 0.0129,
an RMS second difference of 0.0119, and a peak scale of 1.226.

The box center also jitters, by about 1.6 px of roughness, independently of the size.
I first suspected the box kept its top-left corner fixed when it changed size. A
roughness test on the real tracks disproved that: the measured center is the
smoothest reference point on both axes.

The private QA report now splits the reframing motion into three separate parts.
Definitions are in `match_stability.stability()`:

- **translation:** the RMS per-frame movement of the source frame's center on the
  canvas;
- **scale change:** the RMS per-frame change of the uniform scale;
- **scale acceleration:** the RMS second difference of the scale.

The existing crop, motion and cost metrics in `metrics()` are unchanged, so earlier
numbers remain comparable.

## S2 — Models compared and the selected fit

`align(tracks, model)` offers four deterministic models, all fitted to the same
tracks:

| Model | Subject size | Subject center |
| --- | --- | --- |
| `baseline` | five-frame triangle (the previous behavior, numerically equal) | measured |
| `triangle9` | nine-frame triangle | measured |
| `size-fit` | robust exponential trend | measured |
| `stable` (**default**) | robust exponential trend | local linear fit over ±4 frames |

How the size trend is fitted:

- The slope is a Theil–Sen estimate on log height.
- The level is the mean log height for that slope. A median level was biased by
  whichever quantized height happened to dominate.
- If the total change across the window is smaller than the residual RMS, the trend
  cannot be told apart from tracker jitter, so the size is held constant. Without
  this rule a constant disc picked up a false 2% zoom.

The trend is rejected, and the baseline sizes kept, when the residuals show
structure. Limits: RMS 0.05, peak 0.10, nine-frame mean of the residuals ("drift")
0.045. These were calibrated on the real CSRT sequences, which reached RMS 0.036,
peak 0.055 and drift 0.035. A 20% size step, a 12% approach-and-retreat and a 25%
approach-and-retreat all keep their measured size profile. The center fit is
rejected, and measured centers kept, when its residuals exceed 5% RMS or 12% peak
of the subject height. A real 20 px jump therefore stays a jump. Fits need at least
8 frames; shorter windows keep the baseline exactly.

What `align()` still guarantees:

- **Same acceptance as today:** it solves the baseline first, so every model accepts
  exactly the evidence the existing gates accept.
- **No forced fit:** if a model's boxes fail the zoom, coverage or step limits, the
  baseline result is returned instead.
- **Unchanged limits:** uniform aspect ratio, the 2× ceiling, full canvas coverage,
  steps of at most 0.08 in scale and 0.12 in position, the 1.25 height-ratio gate,
  and boundary alignment of target center and height.

There are no new dependencies and no extra provider or helper calls.

## S3 — Evidence exercised

Offline tests (mocked provider and tracker):

- constant moving subjects and genuine gradual zoom, each averaged over ten jitter
  phases;
- short windows of 2–7 frames;
- edge-constrained subjects, both solvable at about 1.8× and impossible at about 8×;
- discontinuous position and size;
- approach-and-retreat;
- 400 random tracks checked for identical accept/reject results across all models
  and for the native keyframe limits;
- fallback to the baseline geometry;
- the pipeline paths: first pair rejected then second selected, cache reuse, and no
  reuse of `object-match-4` evidence.

The existing candidate, failure, Stop, budget and tie-breaking tests pass unchanged.

Real tracker tests using this branch's helper (opt-in):

- a constant moving disc halves its scale change and keeps its 2 px/frame pan;
- a disc growing 30% keeps the direction of its zoom in every model;
- a 20 px jump stays measured, and a 40 px jump is rejected by every model;
- a disc near the frame edge stays within 2× zoom with full coverage.

## S4 — Lifecycle and identity

The pipeline is unchanged: one Gemini call, at most two tracked pairs, the same
deadlines, Stop behavior, terminal error classification and strict protocol-v3
review. The cache version is now `object-match-5`, because the output geometry
changed. Scene, speech and music caches, and the native Transform
key-synchronization fix, are untouched. The match report adds `models` (which fits
were used) and the stability components. These are local diagnostics only; nothing
new goes over the wire.

## S5 — Controlled measurements

How to reproduce:

```sh
python3 assistant/tools/check_match_quality.py --craft-root … --build-root … \
  --output-dir ~/Movies/montaz-claude-match-stability
```

The QA tool runs in four steps:

1. Track each scenario once with the real helper.
2. Solve every model from those same boxes, and export each model through the
   native apply, Undo, Redo, serialization and export check. All nine runs pass,
   with 807 assertions each.
3. Decode each export to measure the disc on screen at frames 30–89.
4. Re-track ten shifted windows ("truth sweep") and score every model against the
   known disc center and radius on every frame.

The offline fixture provider made one call and the helper five. The selected pair
is the disc, at 1.16× zoom. The first run took 3.0 s; the cache reuse run took
0.003 s with no calls. Keyframes are timeline frames 30–59 and 60–89 around the cut
at frame 60.

**Constant-size moving disc**

| Metric | baseline | triangle9 | size-fit | **stable** |
| --- | --- | --- | --- | --- |
| Transform RMS scale change | 0.0129 | 0.0057 | 0.0000 | **0.0000** |
| Transform scale acceleration | 0.0119 | 0.0038 | 0.0000 | **0.0000** |
| Max scale / crop | 1.226 / 0.280 | 1.207 / 0.274 | 1.164 / 0.251 | **1.164 / 0.251** |
| Corner motion / translation | 0.0117 / 0.0052 | 0.0071 / 0.0047 | 0.0042 / 0.0046 | **0.0036 / 0.0036** |
| Decoded size change RMS | 1.13% | 0.64% | 0.43% | **0.42%** |
| Decoded centroid motion RMS | 0.0031 | 0.0031 | 0.0030 | **0.0009** |
| Decoded cut, center / size | 0.0036 / 4.96% | 0.0029 / 5.29% | 0.0026 / 0.96% | 0.0043 / **0.94%** |
| Truth sweep, cut center mean / max | 0.0030 / 0.0068 | 0.0029 / 0.0068 | 0.0029 / 0.0068 | **0.0030 / 0.0047** |
| Truth sweep, cut size mean | 2.99% | 2.98% | 1.24% | **1.24%** |
| Truth sweep, on-screen motion / size change | 0.0037 / 1.05% | 0.0036 / 0.47% | 0.0036 / 0.00% | **0.0005 / 0.00%** |

The provisional goal was to halve the RMS frame-to-frame scale change. It was met:
the transform figure fell from 0.0129 to zero, and the decoded figure from 1.13% to
0.42%. The decoded figure appears to be near the measurement's floor of about 0.4%.

The decoded size acceleration only fell from 1.06% to 0.78%. That figure is
dominated by pixel-row quantization of the measurement, not by the transform.

In this one export, the decoded cut-center error was 0.0007 of the canvas worse than
the baseline (about half a pixel at 640). Ten real windows scored against ground
truth show the same mean (0.0030) and a better worst case (0.0047 versus 0.0068).
The native 0.007 tolerance passes for every model.

**Disc growing 30% (radius 14.5 → 18.85 px over the outgoing window)**

| Metric | baseline | stable |
| --- | --- | --- |
| Outgoing scale, first → last | 1.320 → 1.030 | 1.320 → 1.030 (direction retained) |
| Fits used | — | outgoing size kept as measured (step-like CSRT size); incoming trend |
| Decoded size change / centroid motion | 1.32% / 0.0032 | 1.05% / **0.0009** |
| Truth sweep, cut center / cut size mean | 0.0130 / 13.6% | 0.0121 / 11.6% |

Six of the ten zoom windows were rejected by the helper's appearance gate. In those
windows the disc grows up to about 42% within one second.

## Remaining limitations and notes for Codex

- **The tracker misses gradual zoom.** CSRT, as MLT configures it, does not measure
  zoom of 7–18% per second. On a disc truly growing 7%, it reported boxes shrinking
  from 64 to 50 px. Larger growth arrives late and in steps. Smoothing follows what
  the tracker measures and cannot recover a missed or delayed size.
- **Cut errors on zooming subjects are large for every model.** In the truth sweep
  they are about 0.012–0.013 of the canvas and 11–14% in size, above the native
  center tolerance. Improving the tracker's scale estimation (for example its
  scale-step settings, or a different size measurement) is native helper work for
  Codex.
- **Small size changes are invisible.** Real size changes under about 10% are
  indistinguishable from CSRT jitter and are held constant. Larger structured
  changes keep the baseline's breathing.
- **Window edges are one-sided.** The local linear center fit has less data at the
  ends of each window. Ground truth shows equal mean and better worst-case cut error
  on the constant disc. It is still smoothing, so very abrupt real turns within
  about 4 frames are slightly softened unless they exceed the residual guard.
- **Thresholds are untested on real footage.** They were calibrated on controlled
  discs only, and real Goa footage has not been re-run. Codex should review the
  exports in `~/Movies/montaz-claude-match-stability/stability/*/{baseline,stable}/`,
  and check Apply, Undo and Redo in the desktop editor, before claiming a visible
  improvement.
- **No native or protocol change is needed.**

## Codex acceptance — October 1, 2026

Reviewed all six changed files and independently reran the combined suite: 200
Python checks, including six real-helper checks; 39 Qt transport and seven chat
checks; full native `assistanttest`. The shared editor/helper build includes the
native synchronization fix and the separate duration improvement (`eb8d5a9`).

The quality checker independently reproduced all nine native Apply/Undo/Redo,
serialization and export cases in `~/Movies/montaz-stability-integration-20261001/`.
Constant-size decoded jitter is 1.1266% baseline versus 0.4172% stable; centroid
motion is 0.00306 versus 0.00087. The ten-window ground-truth sweep reproduces
0.0030 mean center error and improves worst error from 0.0068 to 0.0047. Zoom
results reproduce the stated direction and the poor sweep accuracy. Six zoom
windows still fail the helper gate; smoothing is accepted as a limited improvement,
while robust zoom correspondence remains an unmet quality goal.

Review correction: the checker now requires native success for every model in
both scenarios. Previously a zoom export failure could be ignored by the final
success condition. The corrected checker passes against the integrated build.

A separate desktop project at `~/Movies/montaz-stability-integration-review.kdenlive`
starts with two clean synthetic clips. A live Gemini run presents boundary
comparison; native Apply, one Undo/Redo, playback through the cut, save and fresh
process reopen pass. Both editable Transform effects load without a keyframe
repair dialog, with synchronized rotation/anchor keys. Session state resets.
This 320×180 canvas fixture is separate from the 1080p engineering comparison;
its incoming size trend varies, so it does not prove constant size on every
resolution or seed. Original Goa projects are untouched. Real footage and genuine
zoom tracking still need improvement before claiming finished match-cut quality.
