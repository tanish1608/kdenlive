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
from montaz_assistant.match_cuts import MODELS, align, native_frames
from montaz_assistant.match_stability import stability
from montaz_assistant.planning import MediaTools


def make_fixture(
    path, center=130, radius=16, background=(12, 30, 45), growth=0.0, jump=None
):
    """A moving textured disc. Ground-truth center at source frame n is center+n
    (plus jump[1] pixels from frame jump[0]); its radius is radius+growth*n."""
    frames = bytearray()
    for n in range(90):
        frames.extend(b"P6\n320 180\n255\n")
        offset = center + n + (jump[1] if jump and n >= jump[0] else 0)
        # The checker texture scales with the disc, like an approaching object.
        r = radius + growth * n
        cell = 4 * r / radius
        for y in range(180):
            for x in range(320):
                dx, dy = x - offset, y - 90
                if dx * dx + dy * dy <= r * r:
                    value = 245 if ((dx + r) // cell + (dy + r) // cell) % 2 else 105
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

    def test_subject_shrinking_below_the_minimum_rejects_only_this_candidate(self):
        # Width falls from 5.3% to about 3% of the frame: the helper must reject
        # the candidate (tracking_unavailable), not emit boxes Python refuses.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shrinking.mp4"
            make_fixture(path, center=60, radius=12, growth=-0.12)
            with self.assertRaises(AssistantError) as caught:
                native_frames(path, 30, 30, SPEC, RATE, MediaTools(), seed(90, 8.4))
            self.assertEqual(caught.exception.code, "tracking_unavailable")


RATE, SPEC = FrameRate(30000, 1001), dict(width=1920, height=1080)


def seed(x, radius=16):
    return [(x - radius) / 320, (90 - radius) / 180, 2 * radius / 320, 2 * radius / 180]


@unittest.skipUnless(
    os.environ.get("MONTAZ_TRACKER"), "Set MONTAZ_TRACKER to run native tracking checks"
)
class NativeMatchStabilityTests(unittest.TestCase):
    """Real CSRT boxes from the branch helper through every alignment model."""

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.root = Path(cls.directory.name)
        make_fixture(cls.root / "partner.mp4", center=100, background=(35, 14, 42))
        cls.partner = cls.track("partner.mp4", 0, 100)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    @classmethod
    def track(cls, name, start, x, radius=16):
        return native_frames(
            cls.root / name, start, 30, SPEC, RATE, MediaTools(), seed(x, radius)
        )["boxes"]

    def outgoing(self, name, x, seed_radius=16, **fixture):
        make_fixture(self.root / name, **fixture)
        return self.track(name, 30, x, seed_radius)

    def test_constant_moving_disc_halves_scale_change_and_keeps_its_pan(self):
        tracks = [self.outgoing("constant.mp4", 90, center=60), self.partner]
        base = stability(align(tracks, "baseline")[1])
        used = []
        _, transforms = align(tracks, "stable", used)
        stable = stability(transforms)
        self.assertEqual(used, ["size-trend", "center-trend"] * 2)
        self.assertLessEqual(stable["scale_change"], base["scale_change"] / 2)
        self.assertLess(stable["scale_acceleration"], base["scale_acceleration"] / 2)
        # Both discs move right 2 analysis px per frame; the reframing still follows.
        for values in transforms:
            steps = [b["x"] - a["x"] for a, b in zip(values, values[1:])]
            self.assertAlmostEqual(
                sum(steps) / len(steps), -values[0]["scale"] / 320, delta=0.0008
            )

    def test_strong_genuine_zoom_keeps_direction_in_every_model(self):
        # Radius 14.5 -> 18.85 px over the outgoing window (+30%).
        track = self.outgoing("zoom.mp4", 90, 14.5, center=60, radius=10, growth=0.15)
        self.assertGreater(track[-1][3] / track[0][3], 1.15)
        for model in MODELS:
            _, transforms = align([track, self.partner], model)
            scales = [k["scale"] for k in transforms[0]]
            self.assertGreater(scales[0] / scales[-1], 1.15, model)

    def test_discontinuous_motion_is_kept_or_rejected(self):
        moved = self.outgoing("jump20.mp4", 90, center=60, jump=(45, 20))
        used = []
        _, transforms = align([moved, self.partner], "stable", used)
        self.assertEqual(used[1], "center-measured")
        x = [k["x"] for k in transforms[0]]
        self.assertGreater(max(a - b for a, b in zip(x, x[1:])), 0.05)
        jumped = self.outgoing("jump40.mp4", 90, center=60, jump=(45, 40))
        for model in MODELS:
            with self.assertRaises(AssistantError) as caught:
                align([jumped, self.partner], model)
            self.assertEqual(caught.exception.code, "tracking_unavailable")

    def test_edge_constrained_disc_keeps_coverage_and_ceiling(self):
        track = self.outgoing("edge.mp4", 18, center=-12)
        for model in MODELS:
            _, transforms = align([track, self.partner], model)
            for values in transforms:
                for k in values:
                    self.assertTrue(1 <= k["scale"] <= 2)
                    self.assertLessEqual(max(k["x"], k["y"]), 0.000001)
                    self.assertGreaterEqual(min(k["x"], k["y"]) + k["scale"], 0.999999)
