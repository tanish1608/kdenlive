# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Ground-truth native scale recovery and conservative appearance rejection."""

import math
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from montaz_assistant.gemini import AssistantError
from montaz_assistant.match_cuts import align, native_frames
from montaz_assistant.planning import MediaTools
from test_native_match_tracker import RATE, SPEC, make_fixture, seed


def textured_fixture(path, growth=0.0, disappear=False, gradient=False):
    """An asymmetric textured rectangle, with changing exposure and a uniform zoom.

    Bounds at frame n: center=(75+n, 90), half-size=(18, 12)*(1+growth*n).
    This has no circular silhouette, checker cells or tracker-specific texture.
    """
    frames = bytearray()
    for n in range(90):
        frames.extend(b"P6\n320 180\n255\n")
        image = bytearray((13, 27, 38)) * (320 * 180)
        scale, cx = 1 + growth * n, 75 + n
        if gradient:
            image = bytearray(
                v for _ in range(180) for x in range(320) for v in (30 + x // 2,) * 3
            )
        elif not (disappear and n >= 43):
            # Integrate silhouette coverage over each pixel. Hard binary edges
            # otherwise turn a subpixel shrink into two-pixel height jumps.
            for y in range(
                math.ceil(90 - 12 * scale - 0.5), math.floor(90 + 12 * scale + 0.5) + 1
            ):
                ay = max(
                    0, min(y + 0.5, 90 + 12 * scale) - max(y - 0.5, 90 - 12 * scale)
                )
                for x in range(
                    math.ceil(cx - 18 * scale - 0.5),
                    math.floor(cx + 18 * scale + 0.5) + 1,
                ):
                    ax = max(
                        0, min(x + 0.5, cx + 18 * scale) - max(x - 0.5, cx - 18 * scale)
                    )
                    u, v = (x - cx) / scale, (y - 90) / scale
                    value = (
                        120
                        + 35 * math.sin(u * 0.43 + v * 0.17)
                        + 25 * math.cos(v * 0.57 - u * 0.11)
                    ) * (1 + 0.1 * math.sin(n / 13))
                    rgb = (value, value * 0.72, value * 0.47)
                    alpha = ax * ay
                    index = 3 * (y * 320 + x)
                    image[index : index + 3] = bytes(
                        round(c * alpha + bg * (1 - alpha))
                        for c, bg in zip(rgb, (13, 27, 38))
                    )
        frames.extend(image)
    subprocess.run(
        [
            os.environ.get("MONTAZ_FFMPEG", "ffmpeg"),
            "-v",
            "error",
            "-y",
            "-f",
            "image2pipe",
            "-c:v",
            "ppm",
            "-r",
            "30000/1001",
            "-i",
            "pipe:0",
            "-an",
            "-c:v",
            "libx264",
            "-crf",
            "12",
            "-pix_fmt",
            "yuv420p",
            str(path),
        ],
        input=frames,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


@unittest.skipUnless(
    os.environ.get("MONTAZ_TRACKER"), "Set MONTAZ_TRACKER for native zoom checks"
)
class NativeZoomTests(unittest.TestCase):
    def test_growing_and_shrinking_discs_follow_true_size_throughout_window(self):
        with tempfile.TemporaryDirectory() as directory:
            for radius, growth in ((16, 0.04), (16, 0.1), (10, 0.15), (22, -0.1)):
                with self.subTest(radius=radius, growth=growth):
                    path = Path(directory) / "zoom.mp4"
                    make_fixture(path, center=60, radius=radius, growth=growth)
                    initial = radius + growth * 30
                    boxes = native_frames(
                        path, 30, 30, SPEC, RATE, MediaTools(), seed(90, initial)
                    )["boxes"]
                    expected_ratio = (initial + 29 * growth) / initial
                    self.assertAlmostEqual(
                        boxes[-1][3] / boxes[0][3], expected_ratio, delta=0.035
                    )
                    for i, b in enumerate(boxes):
                        self.assertAlmostEqual(
                            b[3] * 90 / (initial + i * growth), 1, delta=0.065
                        )
                        self.assertAlmostEqual(
                            (b[0] + b[2] / 2) * 320, 90 + i, delta=1.5
                        )
                        self.assertAlmostEqual((b[1] + b[3] / 2) * 180, 90, delta=1.5)

    def test_asymmetric_texture_and_exposure_changes_retain_zoom(self):
        with tempfile.TemporaryDirectory() as directory:
            for growth in (0.004, -0.003):
                with self.subTest(growth=growth):
                    path = Path(directory) / "rectangle.mp4"
                    textured_fixture(path, growth=growth)
                    initial = 1 + growth * 30
                    box = [
                        (105 - 18 * initial) / 320,
                        (90 - 12 * initial) / 180,
                        36 * initial / 320,
                        24 * initial / 180,
                    ]
                    boxes = native_frames(path, 30, 30, SPEC, RATE, MediaTools(), box)[
                        "boxes"
                    ]
                    for i, b in enumerate(boxes):
                        self.assertAlmostEqual(
                            b[3] * 180 / (24 * (1 + growth * (30 + i))), 1, delta=0.08
                        )
                        self.assertAlmostEqual(
                            (b[0] + b[2] / 2) * 320, 105 + i, delta=1.2
                        )
                    _, transforms = align([boxes, boxes], "stable")
                    scales = [k["scale"] for k in transforms[0]]
                    self.assertEqual(scales[-1] < scales[0], growth > 0)
                    shown = [
                        k["scale"] * 24 * (1 + growth * (30 + i)) / 180
                        for i, k in enumerate(transforms[0])
                    ]
                    # The reviewed transform, not an isolated noisy raw box, must
                    # keep true displayed size stable throughout the whole window.
                    mean = sum(shown) / len(shown)
                    self.assertLess(max(abs(h / mean - 1) for h in shown), 0.03)

    def test_disappearing_subject_is_rejected_instead_of_tracking_background(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "disappearing.mp4"
            textured_fixture(path, disappear=True)
            with self.assertRaises(AssistantError) as caught:
                native_frames(
                    path,
                    30,
                    30,
                    SPEC,
                    RATE,
                    MediaTools(),
                    [87 / 320, 78 / 180, 36 / 320, 24 / 180],
                )
            self.assertEqual(caught.exception.code, "tracking_unavailable")

    def test_scale_ambiguous_gradient_is_not_accepted_as_a_subject(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gradient.mp4"
            textured_fixture(path, gradient=True)
            with self.assertRaises(AssistantError) as caught:
                native_frames(
                    path, 30, 30, SPEC, RATE, MediaTools(), [0.25, 0.25, 0.3, 0.3]
                )
            self.assertEqual(caught.exception.code, "tracking_unavailable")
