# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Optional real MLT/OpenCV checks with controlled moving objects, outside source media."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from montaz_assistant.contracts import FrameRate
from montaz_assistant.gemini import AssistantError
from montaz_assistant.match_cuts import native_frames
from montaz_assistant.planning import MediaTools


def make_fixture(path, center=130, radius=16, background=(12, 30, 45)):
    """A moving textured disc. Ground-truth center at source frame n is center+n."""
    frames = bytearray()
    for n in range(90):
        frames.extend(b"P6\n320 180\n255\n")
        for y in range(180):
            for x in range(320):
                dx, dy = x - center - n, y - 90
                if dx * dx + dy * dy <= radius * radius:
                    value = (
                        245 if ((dx + radius) // 4 + (dy + radius) // 4) % 2 else 105
                    )
                    frames.extend((value, value, 15))
                else:
                    frames.extend(background)
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
    os.environ.get("MONTAZ_TRACKER"), "Set MONTAZ_TRACKER to run native tracking checks"
)
class NativeMatchTrackerTests(unittest.TestCase):
    def test_nonzero_source_trim_tracks_every_frame_in_project_coordinates(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "moving.mp4"
            make_fixture(path)
            # Native analysis is 640x360, so normalized coordinates stay unchanged.
            seed = [(140 - 16) / 320, (90 - 16) / 180, 32 / 320, 32 / 180]
            result = native_frames(
                path,
                10,
                30,
                dict(width=1920, height=1080),
                FrameRate(30000, 1001),
                MediaTools(),
                seed,
            )
            self.assertEqual(len(result["boxes"]), 30)
            for i, b in enumerate(result["boxes"]):
                self.assertAlmostEqual((b[0] + b[2] / 2) * 320, 140 + i, delta=3)
                self.assertAlmostEqual((b[1] + b[3] / 2) * 180, 90, delta=3)
            self.assertNotEqual(result["first"], result["last"])

    def test_textureless_background_is_not_a_verified_subject(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "moving.mp4"
            make_fixture(path)
            with self.assertRaises(AssistantError) as caught:
                native_frames(
                    path,
                    10,
                    30,
                    dict(width=1920, height=1080),
                    FrameRate(30000, 1001),
                    MediaTools(),
                    [0.02, 0.02, 0.12, 0.12],
                )
            self.assertEqual(caught.exception.code, "tracking_unavailable")
