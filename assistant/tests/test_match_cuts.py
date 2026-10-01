# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.match_cuts import (
    UNSUITABLE,
    align,
    box,
    match_spec,
    metrics,
    parse_candidates,
    propose_match,
    rank,
    window,
)
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
                candidates=[
                    dict(
                        reason="Round objects",
                        boxes=[[0.3, 0.3, 0.2, 0.2], [0.4, 0.4, 0.2, 0.2]],
                    )
                ],
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
                reason="No compatible objects", candidates=[]
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
                    return dict(reason="No match", candidates=[])

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


# Seed pairs in normalized coordinates of the two images Gemini sees.
CENTERED = ([0.45, 0.40, 0.10, 0.10], [0.45, 0.40, 0.10, 0.10])  # 1.03x zoom
OFFSET = ([0.30, 0.30, 0.20, 0.20], [0.40, 0.40, 0.20, 0.20])  # needs more zoom
HIGH = ([0.45, 0.10, 0.10, 0.10], [0.45, 0.10, 0.10, 0.10])  # ties CENTERED
SHAPES = ([0.30, 0.30, 0.10, 0.10], [0.30, 0.30, 0.40, 0.10])  # aspect mismatch
ZOOM = ([0.30, 0.30, 0.10, 0.10], [0.30, 0.30, 0.30, 0.30])  # beyond 2x


def answer(*pairs, reason="Candidate objects"):
    return dict(
        reason=reason,
        candidates=[
            dict(reason=f"Pair {i}", boxes=[list(a), list(b)])
            for i, (a, b) in enumerate(pairs)
        ],
    )


class MatchCandidateTests(unittest.TestCase):
    """Bounded multi-candidate selection with mocked Gemini and tracker."""

    def setUp(self):
        MatchCutsTests.setUp(self)
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.cache = self.root / "cache"
        self.paths = {str(i): str(self.root / f"{i}.mp4") for i in (1, 2)}
        for p in self.paths.values():
            Path(p).write_bytes(b"fixture")
        self.provider = Mock(model="gemini-test")
        # Seed tuple -> "fail" (helper rejects), "drift" (align rejects) or callable.
        self.behavior = {}
        self.seeds = []
        self.now = [0.0]
        self.tracker = Mock(side_effect=self.native)
        patcher = patch.object(MediaTools, "visual_frames", return_value=200)
        patcher.start()
        self.addCleanup(patcher.stop)

    def native(self, path, start, length, spec, rate, media, seed=None):
        if seed is None:
            return dict(first="jpeg", last="jpeg", boxes=[])
        self.seeds.append(seed)
        action = self.behavior.get(tuple(seed))
        if callable(action):
            action(media)
        elif action == "fail":
            raise AssistantError("tracking_unavailable")
        if action == "drift":
            moved = [seed[0] + 0.13, *seed[1:]]
            return dict(boxes=[seed if i % 2 else moved for i in range(length)])
        return dict(boxes=[list(seed)] * length)

    def run_match(self, revision=2, media=None, report=None):
        with patch("montaz_assistant.match_cuts.native_frames", self.tracker):
            return propose_match(
                replace(self.snapshot, revision=revision),
                "r",
                self.spec,
                self.paths,
                self.cache,
                self.provider,
                media or MediaTools(),
                clock=lambda: self.now[0],
                report=report,
            )

    def assert_uncached(self):
        self.assertEqual(list(self.cache.glob("*.json")), [])

    def test_strict_candidate_schema_rejects_malformed_answers(self):
        good = answer(CENTERED)
        self.assertEqual(
            parse_candidates(good)[1][0]["boxes"], list(map(list, CENTERED))
        )
        self.assertEqual(parse_candidates(answer())[1], [])
        self.assertEqual(parse_candidates(answer(reason="\ud800"))[0], "\ud800")
        bad = [
            None,
            dict(reason="Old schema", boxes=list(CENTERED)),
            {**good, "extra": 1},
            dict(candidates=[]),
            dict(reason="  ", candidates=[]),
            dict(reason="x" * 601, candidates=[]),
            # 301 code points, but 602 UTF-16 units in the native QString.
            dict(reason="\U0001f30a" * 301, candidates=[]),
            dict(reason="Not a list", candidates={}),
            answer(CENTERED, OFFSET, HIGH, SHAPES),
            dict(reason="r", candidates=["pair"]),
            dict(reason="r", candidates=[dict(reason="r", boxes=[CENTERED[0]])]),
            dict(reason="r", candidates=[dict(reason="", boxes=list(CENTERED))]),
            dict(reason="r", candidates=[dict(reason="x" * 201, boxes=list(CENTERED))]),
            dict(reason="r", candidates=[dict(boxes=list(CENTERED))]),
            dict(reason="r", candidates=[{**good["candidates"][0], "score": 0.9}]),
        ]
        for value in (True, float("nan"), float("inf"), "0.4", None):
            bad.append(answer(([value, 0.4, 0.1, 0.1], CENTERED[1])))
        bad.append(answer(([0.95, 0.4, 0.1, 0.1], CENTERED[1])))
        for raw in bad:
            with self.subTest(raw=raw), self.assertRaises(AssistantError) as caught:
                parse_candidates(raw)
            self.assertEqual(caught.exception.code, "invalid_match_evidence")

    def test_malformed_provider_output_is_terminal_before_tracking(self):
        for raw in (
            dict(reason="Old schema", boxes=list(CENTERED)),
            answer(([True, 0.4, 0.1, 0.1], CENTERED[1])),
            answer(([float("nan"), 0.4, 0.1, 0.1], CENTERED[1])),
            answer(CENTERED, OFFSET, HIGH, SHAPES),
        ):
            self.provider.generate.return_value = raw
            self.tracker.reset_mock()
            with self.subTest(raw=raw), self.assertRaises(AssistantError) as caught:
                self.run_match()
            self.assertEqual(caught.exception.code, "invalid_match_evidence")
            self.assertEqual(self.tracker.call_count, 2)
            self.assertEqual(self.seeds, [])
            self.assert_uncached()

    def test_rank_orders_by_geometry_drops_unsuitable_and_duplicates(self):
        nudged = ([0.46, 0.41, 0.10, 0.10], [0.45, 0.40, 0.10, 0.10])
        found = parse_candidates(answer(OFFSET, SHAPES, CENTERED))[1]
        self.assertEqual(rank(found), [2, 0])
        found = parse_candidates(answer(OFFSET, nudged, CENTERED))[1]
        # The nudged pair is first by crop; CENTERED overlaps it on both sides.
        self.assertEqual(rank(found), [1, 0])
        found = parse_candidates(answer(CENTERED, CENTERED, ZOOM))[1]
        self.assertEqual(rank(found), [0])

    def test_metrics_define_crop_and_reframing_motion(self):
        still = [[dict(x=-0.25, y=-0.25, scale=1.5)] * 3, [dict(x=0, y=0, scale=1.0)]]
        measured = metrics(still)
        self.assertAlmostEqual(measured["zoom"], 1.5)
        self.assertAlmostEqual(measured["crop"], (3 * (1 - 1 / 2.25)) / 4)
        self.assertEqual(measured["motion"], 0)
        moving = [[dict(x=-0.1, y=-0.1, scale=1.2), dict(x=-0.12, y=-0.1, scale=1.2)]]
        self.assertAlmostEqual(metrics(moving)["motion"], 0.02)
        zooming = [[dict(x=-0.1, y=-0.1, scale=1.2), dict(x=-0.1, y=-0.1, scale=1.25)]]
        self.assertAlmostEqual(metrics(zooming)["motion"], 0.05 * 2**0.5)
        self.assertAlmostEqual(
            metrics(moving)["cost"], metrics(moving)["crop"] + 10 * 0.02
        )

    def test_first_candidate_unstable_second_is_selected_and_cached(self):
        self.provider.generate.return_value = answer(OFFSET, CENTERED)
        self.behavior[tuple(CENTERED[0])] = "fail"
        report = {}
        review = self.run_match(report=report)
        self.assertEqual(review["status"], "matched")
        self.assertTrue(review["reason"].startswith("Pair 0. Chose ranked pair 2 of 2"))
        self.assertLessEqual(len(review["reason"]), 600)
        # Frames, the rejected outgoing seed (incoming skipped), then both OFFSET seeds.
        self.assertEqual(self.tracker.call_count, 5)
        self.assertEqual(self.seeds, [CENTERED[0], *OFFSET])
        self.assertEqual(report["selected"], 0)
        self.assertEqual(
            [t["outcome"] for t in report["tracked"]], ["unstable", "valid"]
        )
        self.assertEqual(
            [o["keyframes"][0]["frame"] for o in review["operations"]], [30, 0]
        )
        again = self.run_match(revision=9)
        self.assertTrue(again["cached"])
        self.assertEqual(again["base_revision"], 9)
        self.assertEqual(again["operations"], review["operations"])
        self.assertEqual(again["reason"], review["reason"])
        self.assertEqual(self.provider.generate.call_count, 1)
        self.assertEqual(self.tracker.call_count, 5)

    def test_lowest_cost_wins_over_provider_order_within_call_ceilings(self):
        self.provider.generate.return_value = answer(OFFSET, HIGH, CENTERED)
        report = {}
        review = self.run_match(report=report)
        self.assertEqual(review["status"], "matched")
        # One request, two frame reads, and four tracks; OFFSET is never tracked.
        self.assertEqual(self.provider.generate.call_count, 1)
        self.assertEqual(self.tracker.call_count, 6)
        self.assertEqual(self.seeds, [*HIGH, *CENTERED])
        self.assertEqual(report["ranked"], [1, 2, 0])
        # HIGH and CENTERED tie on every criterion; provider order breaks it.
        self.assertEqual(report["selected"], 1)
        self.assertAlmostEqual(review["target"]["y"], 0.15)
        self.provider.generate.return_value = answer(OFFSET, CENTERED, HIGH)
        self.cache = self.root / "other-cache"
        report = {}
        review = self.run_match(report=report)
        self.assertEqual(report["selected"], 1)
        self.assertAlmostEqual(review["target"]["y"], 0.45)

    def test_moving_track_loses_to_steadier_pair(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)
        original = self.native

        def native(path, start, length, spec, rate, media, seed=None):
            result = original(path, start, length, spec, rate, media, seed)
            if seed == CENTERED[0]:
                # CENTERED ranks first by seed crop but drifts 0.01 per frame.
                result["boxes"] = [
                    [seed[0] - 0.01 * i, *seed[1:]] for i in range(length)
                ]
            return result

        self.tracker.side_effect = native
        report = {}
        review = self.run_match(report=report)
        self.assertEqual(report["ranked"], [0, 1])
        self.assertEqual(report["selected"], 1)
        self.assertEqual(review["status"], "matched")
        first, second = report["tracked"]
        # Canvas motion is the source drift magnified by the uniform zoom.
        self.assertAlmostEqual(first["motion"], 0.01 * first["zoom"])
        self.assertAlmostEqual(second["motion"], 0)
        self.assertGreater(first["cost"], second["cost"])

    def test_all_geometrically_unsuitable_is_a_cached_no_match_without_tracking(self):
        self.provider.generate.return_value = answer(SHAPES, ZOOM)
        review = self.run_match()
        self.assertEqual(
            (review["status"], review["reason"], review["operations"]),
            ("no_match", UNSUITABLE, []),
        )
        self.assertEqual(self.tracker.call_count, 2)
        self.assertTrue(self.run_match()["cached"])
        self.assertEqual(self.provider.generate.call_count, 1)

    def test_empty_candidates_are_a_cached_semantic_answer(self):
        self.provider.generate.return_value = answer(reason="No compatible objects")
        review = self.run_match()
        self.assertEqual(
            (review["status"], review["reason"]), ("no_match", "No compatible objects")
        )
        self.assertTrue(self.run_match()["cached"])
        self.assertEqual(
            (self.provider.generate.call_count, self.tracker.call_count), (1, 2)
        )

    def test_every_tracking_failure_is_terminal_and_never_cached(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET, HIGH)
        for seed in (CENTERED[0], OFFSET[1], HIGH[0]):
            self.behavior[tuple(seed)] = "fail"
        for attempt in (1, 2):
            with self.assertRaises(AssistantError) as caught:
                self.run_match()
            self.assertEqual(caught.exception.code, "tracking_unavailable")
            self.assert_uncached()
            self.assertEqual(self.provider.generate.call_count, attempt)
        # Two tracked candidates per run: CENTERED out, HIGH out; OFFSET untouched.
        self.assertEqual(self.seeds, [CENTERED[0], HIGH[0]] * 2)

    def test_rejected_alignment_with_unstable_partner_is_cached_no_match(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)
        self.behavior[tuple(CENTERED[0])] = "drift"
        self.behavior[tuple(OFFSET[0])] = "fail"
        report = {}
        review = self.run_match(report=report)
        self.assertEqual((review["status"], review["reason"]), ("no_match", UNSUITABLE))
        self.assertEqual(
            [t["outcome"] for t in report["tracked"]], ["rejected", "unstable"]
        )
        self.assertTrue(self.run_match()["cached"])

    def test_stop_between_candidates_launches_nothing_else(self):
        media = MediaTools()
        self.provider.generate.return_value = answer(CENTERED, OFFSET)
        self.behavior[tuple(CENTERED[1])] = lambda m: m.cancel()
        with self.assertRaises(AssistantError) as caught:
            self.run_match(media=media)
        self.assertEqual(caught.exception.code, "cancelled")
        self.assertEqual(self.seeds, list(CENTERED))
        self.assert_uncached()

    def test_source_change_during_candidate_tracking_invalidates_run(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)

        def mutate(media):
            Path(self.paths["2"]).write_bytes(b"changed")

        self.behavior[tuple(OFFSET[0])] = mutate
        with self.assertRaises(AssistantError) as caught:
            self.run_match()
        self.assertEqual(caught.exception.code, "media_changed")
        self.assert_uncached()

    def test_budget_exhaustion_skips_second_candidate_and_does_not_cache(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)

        def slow(media):
            self.now[0] += 25

        self.behavior[tuple(CENTERED[0])] = slow
        report = {}
        review = self.run_match(report=report)
        self.assertEqual(review["status"], "matched")
        self.assertFalse(report["complete"])
        self.assertEqual(self.seeds, list(CENTERED))
        self.assert_uncached()
        # Within budget, the next run evaluates both pairs and caches them.
        self.now[0] = 0
        self.behavior.clear()
        report = {}
        self.run_match(report=report)
        self.assertTrue(report["complete"])
        self.assertEqual(self.provider.generate.call_count, 2)
        self.assertEqual(len(list(self.cache.glob("*.json"))), 1)

    def test_budget_exhaustion_after_failures_is_explicit(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)

        def slow(media):
            self.now[0] += 50

        def slow_fail(media):
            slow(media)
            raise AssistantError("tracking_unavailable")

        original = self.native

        def native(path, start, length, spec, rate, media, seed=None):
            result = original(path, start, length, spec, rate, media, seed)
            if seed == CENTERED[1]:
                # A 0.13 jump between frames fails align()'s drift gate.
                result["boxes"] = [
                    [seed[0] + (0.13 if i % 2 else 0), *seed[1:]] for i in range(length)
                ]
            return result

        self.tracker.side_effect = native
        self.behavior[tuple(CENTERED[1])] = slow
        review = self.run_match()
        self.assertEqual(review["status"], "no_match")
        self.assertEqual(
            review["reason"],
            UNSUITABLE + " Remaining pairs were not tracked within the time limit.",
        )
        self.assertEqual(self.seeds, list(CENTERED))
        self.assert_uncached()
        self.now[0] = 0
        self.behavior[tuple(CENTERED[0])] = slow_fail
        with self.assertRaises(AssistantError) as caught:
            self.run_match()
        self.assertEqual(caught.exception.code, "tracking_unavailable")
        self.assert_uncached()

    def test_inconsistent_cache_entries_are_misses(self):
        self.provider.generate.return_value = answer(CENTERED, OFFSET)
        self.run_match()
        (entry,) = self.cache.glob("*.json")
        evidence = json.loads(entry.read_text())
        for corrupt in (
            "not json",
            json.dumps({**evidence, "tracked": [None, None]}),
            json.dumps({**evidence, "tracked": evidence["tracked"][:1]}),
            json.dumps({**evidence, "candidates": evidence["candidates"] * 2}),
            json.dumps({**evidence, "boxes": []}),
        ):
            entry.write_text(corrupt)
            with self.subTest(corrupt=corrupt[:40]):
                self.assertFalse(self.run_match()["cached"])
        self.assertEqual(self.provider.generate.call_count, 6)
        self.assertTrue(self.run_match()["cached"])
