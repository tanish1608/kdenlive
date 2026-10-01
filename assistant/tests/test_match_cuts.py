# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import copy
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.match_cuts import align, box, match_spec, propose_match, window
from montaz_assistant.planning import MediaTools


class MatchCutsTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = Snapshot(
            "p",
            "s",
            2,
            FrameRate(30000, 1001),
            (Asset("1", 200), Asset("2", 200)),
            (Track("1", "video"),),
            (OccupiedRange("11", "1", 0, 60), OccupiedRange("12", "1", 60, 60)),
        )
        self.spec = dict(
            width=1920,
            height=1080,
            clips=[
                dict(
                    instance_id=str(11 + i),
                    bin_id=str(1 + i),
                    track_id="1",
                    start_frame=i * 60,
                    in_frame=10,
                    duration_frames=60,
                )
                for i in range(2)
            ],
        )

    def test_uniform_tracking_aligns_centers_and_heights_without_exposed_edges(self):
        tracks = [
            [[0.30 + i * 0.001, 0.32, 0.2, 0.2] for i in range(30)],
            [[0.43 + i * 0.001, 0.39, 0.15, 0.15] for i in range(30)],
        ]
        target, transforms = align(tracks)
        for boxes, values in zip(tracks, transforms):
            for b, k in zip(boxes, values):
                self.assertAlmostEqual(
                    k["x"] + k["scale"] * (b[0] + b[2] / 2), target["x"]
                )
                self.assertAlmostEqual(
                    k["y"] + k["scale"] * (b[1] + b[3] / 2), target["y"]
                )
                self.assertAlmostEqual(k["scale"] * b[3], target["height"])
                self.assertLessEqual(k["x"], 0.000001)
                self.assertGreaterEqual(k["x"] + k["scale"], 0.999999)
                self.assertLessEqual(k["scale"], 2)

    def test_pixel_size_jitter_does_not_become_breathing_zoom(self):
        sizes = [
            64,
            60,
            64,
            65,
            64,
            60,
            64,
            60,
            59,
            59,
            59,
            63,
            64,
            60,
            59,
            59,
            59,
            63,
            64,
            64,
            60,
            59,
            59,
            59,
            62,
            60,
            59,
            59,
            59,
            63,
        ]
        tracks = [
            [[0.30 - n / 1280, 0.5 - n / 720, n / 640, n / 360] for n in sizes],
            [
                [0.34 - n / 1280, 0.5 - n / 720, n / 640, n / 360]
                for n in reversed(sizes)
            ],
        ]
        _, transforms = align(tracks)
        for values in transforms:
            self.assertLess(
                max(abs(a["scale"] - b["scale"]) for a, b in zip(values, values[1:])),
                0.08,
            )

    def test_incompatible_shapes_excessive_zoom_and_drift_rejected(self):
        for tracks in (
            [[[0.3, 0.3, 0.1, 0.1]], [[0.3, 0.3, 0.4, 0.1]]],
            [[[0.3, 0.3, 0.1, 0.1]], [[0.3, 0.3, 0.3, 0.3]]],
            [[[0.3, 0.3, 0.2, 0.2], [0.6, 0.3, 0.2, 0.2]], [[0.3, 0.3, 0.2, 0.2]]],
        ):
            with self.subTest(tracks=tracks), self.assertRaises(AssistantError):
                align(tracks)

    def test_bad_boxes_nonfinite_and_booleans_rejected(self):
        for b in (
            [True, 0.1, 0.2, 0.2],
            [float("nan"), 0.1, 0.2, 0.2],
            [0.9, 0.1, 0.2, 0.2],
            [0.1, 0.1, 0, 0.2],
        ):
            with self.assertRaises(AssistantError):
                box(b)

    def test_request_checks_placed_sources_adjacency_locks_and_bounds(self):
        self.assertEqual(match_spec(self.snapshot, self.spec), self.spec)
        for key, value in (
            ("start_frame", 61),
            ("in_frame", 180),
            ("instance_id", "unknown"),
        ):
            bad = copy.deepcopy(self.spec)
            bad["clips"][1][key] = value
            with self.assertRaises(ValueError):
                match_spec(self.snapshot, bad)
        with self.assertRaises(ValueError):
            match_spec(
                replace(self.snapshot, tracks=(Track("1", "video", True),)), self.spec
            )
        self.assertEqual(
            window(self.spec["clips"][0], 0, self.snapshot.frame_rate), (30, 30)
        )
        self.assertEqual(
            window(self.spec["clips"][1], 1, self.snapshot.frame_rate), (0, 30)
        )

    @patch.object(MediaTools, "visual_frames", return_value=200)
    def test_content_cache_survives_native_identity_change_and_preserves_local_frames(
        self,
        probe,
    ):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {str(i): str(root / f"{i}.mp4") for i in (1, 2)}
            for p in paths.values():
                Path(p).write_bytes(b"fixture")
            provider = Mock(model="gemini-test")
            provider.generate.return_value = dict(
                reason="Two round objects",
                boxes=[[0.3, 0.3, 0.2, 0.2], [0.4, 0.4, 0.2, 0.2]],
            )

            def native(*args):
                seed = args[6] if len(args) > 6 else None
                return dict(
                    first="jpeg", last="jpeg", boxes=[seed] * args[2] if seed else []
                )

            with patch(
                "montaz_assistant.match_cuts.native_frames", side_effect=native
            ) as helper:
                review = propose_match(
                    self.snapshot,
                    "r1",
                    self.spec,
                    paths,
                    root / "cache",
                    provider,
                    MediaTools(),
                )
                self.assertEqual(review["status"], "matched")
                self.assertFalse(review["cached"])
                again = propose_match(
                    replace(self.snapshot, revision=8),
                    "r2",
                    self.spec,
                    paths,
                    root / "cache",
                    provider,
                    MediaTools(),
                )
                self.assertTrue(again["cached"])
                self.assertEqual(again["base_revision"], 8)
                self.assertEqual(provider.generate.call_count, 1)
                self.assertEqual(helper.call_count, 4)
                self.assertEqual(
                    [o["keyframes"][0]["frame"] for o in review["operations"]], [30, 0]
                )
                self.assertEqual(
                    [o["keyframes"][-1]["frame"] for o in review["operations"]],
                    [59, 29],
                )

    @patch.object(MediaTools, "visual_frames", return_value=200)
    def test_empty_pair_finishes_and_source_changes_invalidate_evidence(self, probe):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {str(i): str(root / f"{i}.mp4") for i in (1, 2)}
            for p in paths.values():
                Path(p).write_bytes(b"fixture")
            provider = Mock(model="gemini-test")
            provider.generate.return_value = dict(
                reason="No compatible objects", boxes=[]
            )
            with patch(
                "montaz_assistant.match_cuts.native_frames",
                return_value=dict(first="jpeg", last="jpeg", boxes=[]),
            ):
                review = propose_match(
                    self.snapshot,
                    "r",
                    self.spec,
                    paths,
                    root / "cache",
                    provider,
                    MediaTools(),
                )
                self.assertEqual(review["status"], "no_match")
                self.assertEqual(review["operations"], [])

                def mutate(parts):
                    Path(paths["1"]).write_bytes(b"changed")
                    return dict(reason="No match", boxes=[])

                provider.generate.side_effect = mutate
                Path(paths["2"]).write_bytes(b"force cache miss")
                with self.assertRaises(AssistantError) as caught:
                    propose_match(
                        self.snapshot,
                        "r",
                        self.spec,
                        paths,
                        root / "cache",
                        provider,
                        MediaTools(),
                    )
                self.assertEqual(caught.exception.code, "media_changed")

    @patch.object(MediaTools, "visual_frames", return_value=55)
    def test_audio_tail_cannot_become_a_frozen_object_track(self, probe):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "source.mp4"
            path.write_bytes(b"fixture")
            provider = Mock(model="gemini-test")
            with self.assertRaises(AssistantError) as caught:
                propose_match(
                    self.snapshot,
                    "r",
                    self.spec,
                    {"1": str(path), "2": str(path)},
                    root / "cache",
                    provider,
                    MediaTools(),
                )
            self.assertEqual(caught.exception.code, "invalid_match_request")
            provider.generate.assert_not_called()
            self.assertEqual(list((root / "cache").glob("*.json")), [])
