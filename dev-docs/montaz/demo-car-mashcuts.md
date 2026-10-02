<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Car mash-cut demo evidence

October 2, 2026. Existing editor only; no product changes. Build from code
checkpoint `360172e`, with documentation checkpoint `ea976ae`.

## Inputs and presentation artifacts

Inputs: `~/Movies/montaz-car-side-profiles/clips/`, 26 static photo-derived
1920×1080/30 fps clips, each two seconds. Claude already aligned front/rear wheel
centres to approximately (460.8, 756) and (1459.2, 756). Mirrored extensions and
photographic perspective differences remain visible. Credits copied unchanged.

Outputs: `~/Movies/montaz-demo-mashcuts-20261002/`.

| File | Native sequence |
| --- | --- |
| `car-mash-6.mp4` / `.kdenlive` | 26 shots × 6 frames = 156 frames, 5.2 seconds |
| `car-mash-12.mp4` / `.kdenlive` | 26 shots × 12 frames = 312 frames, 10.4 seconds |

Both presentation projects use HD 1080p/30 fps. Built-in OpenTimelineIO import
creates native clip/track objects; Save produces editable projects referencing
the original clips, retaining their two-second source handles. Native MP4-H264
export uses the full project. No transforms, music or new montage action added.

## Verification and alignment decision

- Earlier native adapter checks passed Apply/Undo/Redo and export at 29.97 fps
  (`native-6/`, `native-12/`); these are separate from the 30 fps presentation files.
- Both final exports decoded completely. All 156/312 frames identify as the
  expected original source at 96×54 comparison resolution; no gaps or wrong shots.
  Maximum mean RGB error versus source is 3.28/255, consistent with encoding.
- The saved 12-frame project reopened, displayed picture and played. The saved
  6-frame project reopened and exported successfully after a retry.
- One actual Gemini/native-tracker review of clips 16/17 produced a matched
  proposal. Applying its proposed cut transforms analytically to the known wheel
  centres gives 9.65/9.42 px discontinuities versus zero in the prepared inputs.
  Rejected this proposal; did not apply it to the timeline. Evidence lives in
  `ai-pair/known-wheel-check.json`, `review.json` and `diagnostics.json`.

Use the fast reel as a **prepared mash-cut montage**. It is not evidence of AI
improving alignment and does not expose an automatic 26-shot montage feature.
Current selected-pair AI alignment still needs a separate before/after example.

## Synchronized side-by-side comparison

`car-mash-before-after-5.2s.mp4` is a labelled 1920×720 presentation export, 156
frames at 30 fps. Left: the corresponding original photos from `full/`, fitted
to each panel without mirroring, horizon correction or landmark alignment.
Right: the verified native `car-mash-6.mp4`, scaled to the same panel dimensions.
Both sides use the same 26 sources, order and six-frame cut timing. The video is
silent and plays once. Labels describe **match alignment**, with prepared wheel
alignment identified in the footer; no AI improvement claim is made.

Every frame on both sides was checked against the expected source segment;
all 312 panel-frame checks passed. The label layout was visually inspected.
Private build inputs, provenance and validation are in the `comparison/` folder.

## Remaining rehearsal issue

One 6-frame export attempt crashed while constructing the render-job row after
importing a second project in the same session. The macOS report
`kdenlive-2026-10-02-151710.ips` shows `QAccessible::updateAccessibility` and
`QTreeWidgetItem::insertChild` above `RenderWidget::createRenderJob`.
Reopening the saved project and exporting succeeded. Cause is not established
beyond that stack; no crash fix is claimed. Use verified exports for presentation.
