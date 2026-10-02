# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Duration feasibility and one-call recovery of otherwise valid short drafts."""

import itertools
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import Asset, FrameRate, Snapshot, Track
from montaz_assistant.editorial import review_draft
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import (
    MediaTools,
    duration_target,
    feasible_frames,
    plan_media,
    shot_limits,
)


def window(length, identity="source", **changes):
    return dict(
        window_id=0,
        in_frame=0,
        duration_frames=length,
        trim_in=0,
        trim_out=length,
        quality=80,
        role="detail",
        description="Observed textured coastline",
        content_id=identity,
        **changes,
    )


def shot(bin_id, length):
    return dict(
        bin_id=bin_id,
        window_id=0,
        duration_frames=length,
        reason="An observed detail develops the coastal setting",
    )


class DurationTests(unittest.TestCase):
    def test_capacity_counts_one_window_per_distinct_content(self):
        evidence = {
            "1": [window(100), dict(window(100), window_id=1)],
            "duplicate": [window(90)],
            "2": [window(75, "other")],
            "poor": [dict(window(100, "poor"), quality=49)],
            "short": [window(24, "short")],
        }
        self.assertEqual(feasible_frames(evidence, FrameRate(25), "postcard"), 175)
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 150), 150)

    def test_short_fixed_windows_cannot_reach_a_target_in_the_gap(self):
        evidence = {str(i): [window(30, str(i))] for i in range(3)}
        self.assertEqual(feasible_frames(evidence, FrameRate(25), "postcard"), 90)
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 89), 60)
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 29), 0)

    def test_recipe_floor_prevents_an_impossible_one_second_plan(self):
        evidence = {"1": [window(100)]}
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 25), 0)
        self.assertEqual(duration_target(evidence, FrameRate(25), "adventure", 25), 25)

    def test_alternate_windows_are_choices_not_additional_source_uses(self):
        evidence = {"1": [window(30), dict(window(100), window_id=1)]}
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 45), 30)
        self.assertEqual(duration_target(evidence, FrameRate(25), "postcard", 110), 100)

    def test_interval_solver_matches_exhaustive_choices(self):
        rate = FrameRate(4)
        evidence = {
            str(i): [window(length, str(i))] for i, length in enumerate((4, 5, 9, 15))
        }
        choices = []
        for windows in evidence.values():
            low, high = shot_limits(windows[0], rate, "postcard")
            choices.append([0, *range(low, high + 1)])
        reachable = {sum(lengths) for lengths in itertools.product(*choices)}
        for requested in range(35):
            with self.subTest(requested=requested):
                self.assertEqual(
                    duration_target(evidence, rate, "postcard", requested),
                    max(n for n in reachable if n <= requested),
                )

    def test_fractional_rate_and_discarded_edges_match_validation(self):
        evidence = {"1": [dict(window(120), trim_in=15, trim_out=66)]}
        self.assertEqual(
            shot_limits(evidence["1"][0], FrameRate(30000, 1001), "postcard"), (51, 51)
        )
        self.assertEqual(
            duration_target(evidence, FrameRate(30000, 1001), "postcard", 50), 0
        )
        self.assertEqual(
            duration_target(evidence, FrameRate(30000, 1001), "postcard", 60), 51
        )


class DurationReviewTests(unittest.TestCase):
    def setUp(self):
        self.rate = FrameRate(25)
        self.snapshot = Snapshot(
            "project",
            "sequence",
            1,
            self.rate,
            tuple(Asset(str(i), 100) for i in range(3)),
            (Track("v", "video"),),
        )
        self.evidence = {str(i): [window(100, str(i))] for i in range(3)}
        self.brief = dict(
            duration_seconds=12, recipe="postcard", instruction="Coastal trip"
        )
        self.draft = dict(shots=[shot("0", 50)])
        self.provider = Mock(model="offline")
        self.media = MediaTools()
        self.revised = dict(
            decision="revise",
            reason="Use observed details and a resolved view",
            shots=[shot(str(i), 100) for i in range(3)],
        )

    def review(self, **changes):
        args = dict(
            snapshot=self.snapshot,
            request_id="request",
            draft=self.draft,
            evidence=self.evidence,
            brief=self.brief,
            provider=self.provider,
            media=self.media,
            target_frames=300,
        )
        args.update(changes)
        return review_draft(**args)

    def test_existing_review_expands_short_draft_without_an_extra_request(self):
        self.provider.generate.return_value = self.revised
        raw, assessment = self.review()
        self.assertEqual(sum(s["duration_frames"] for s in raw["shots"]), 300)
        self.assertEqual(assessment["status"], "revised")
        self.assertEqual(self.provider.generate.call_count, 1)
        prompt = self.provider.generate.call_args.args[0][0]["text"]
        self.assertIn("only 50 of a reachable 300", prompt)
        self.assertIn("outside the brief", prompt)
        self.assertEqual(self.draft["shots"], [shot("0", 50)])

    def test_restrictive_short_draft_can_be_kept_with_an_explanation(self):
        self.provider.generate.return_value = dict(
            decision="keep",
            reason="The remaining city views are outside the coastal brief",
            shots=[],
        )
        raw, assessment = self.review()
        self.assertIs(raw, self.draft)
        self.assertEqual(assessment["status"], "kept")
        self.assertEqual(self.provider.generate.call_count, 1)

    def test_recovery_still_rejects_shrink_overshoot_duplicates_and_bad_windows(self):
        for shots, target in (
            ([shot("0", 40)], 300),
            ([shot(str(i), 100) for i in range(3)], 250),
            ([shot("0", 100), shot("0", 100)], 300),
            ([dict(shot("0", 100), window_id=5)], 300),
        ):
            with self.subTest(shots=shots):
                self.provider.generate.return_value = dict(
                    decision="revise", reason="Change", shots=shots
                )
                raw, assessment = self.review(target_frames=target)
                self.assertIs(raw, self.draft)
                self.assertEqual(assessment["code"], "invalid_review")

    def test_near_target_review_preserves_span(self):
        self.draft = dict(shots=[shot("0", 100), shot("1", 100), shot("2", 60)])
        self.provider.generate.return_value = self.revised
        raw, assessment = self.review()
        self.assertIs(raw, self.draft)
        self.assertEqual(assessment["code"], "invalid_review")
        self.assertIn(
            "EXACTLY 260", self.provider.generate.call_args.args[0][0]["text"]
        )

    def test_stop_during_recovery_discards_late_success(self):
        def late(_):
            self.media.cancel()
            return self.revised

        self.provider.generate.side_effect = late
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.review()

    def test_end_to_end_cached_planning_uses_two_text_calls_and_one_review_phase(self):
        self.provider.generate.side_effect = [self.draft, self.revised]
        progress, reviews = [], []

        def cached(asset, *_):
            return self.evidence[asset.bin_id], True

        with (
            tempfile.TemporaryDirectory() as root,
            patch("montaz_assistant.planning.analyze", side_effect=cached),
        ):
            result = plan_media(
                self.snapshot,
                "request",
                {str(i): f"/source{i}" for i in range(3)},
                self.brief,
                Path(root),
                self.provider,
                self.media,
                lambda *event: progress.append(event),
                reviews.append,
            )
        self.assertEqual(sum(op.duration_frames for op in result.operations), 300)
        self.assertEqual(self.provider.generate.call_count, 2)
        self.assertEqual(sum(e[0] == "reviewing" for e in progress), 1)
        self.assertEqual(reviews[0]["available_frames"], 300)
        self.assertEqual(reviews[0]["assessment"]["status"], "revised")

    def test_impossible_one_second_request_stops_before_planning_or_review(self):
        brief = dict(self.brief, duration_seconds=1)
        with (
            tempfile.TemporaryDirectory() as root,
            patch(
                "montaz_assistant.planning.analyze",
                side_effect=lambda a, *_: (self.evidence[a.bin_id], True),
            ),
        ):
            with self.assertRaisesRegex(AssistantError, "insufficient_footage"):
                plan_media(
                    self.snapshot,
                    "request",
                    {str(i): f"/source{i}" for i in range(3)},
                    brief,
                    Path(root),
                    self.provider,
                    self.media,
                    lambda *_: None,
                )
        self.provider.generate.assert_not_called()

    def test_impossible_sixty_seconds_uses_capacity_without_repeating_sources(self):
        self.provider.generate.side_effect = [self.draft, self.revised]
        reviews = []
        with (
            tempfile.TemporaryDirectory() as root,
            patch(
                "montaz_assistant.planning.analyze",
                side_effect=lambda a, *_: (self.evidence[a.bin_id], True),
            ),
        ):
            result = plan_media(
                self.snapshot,
                "request",
                {str(i): f"/source{i}" for i in range(3)},
                dict(self.brief, duration_seconds=60),
                Path(root),
                self.provider,
                self.media,
                lambda *_: None,
                review=reviews.append,
            )
        self.assertEqual(sum(op.duration_frames for op in result.operations), 300)
        self.assertEqual(len({op.bin_id for op in result.operations}), 3)
        self.assertEqual(reviews[0]["requested_frames"], 1500)
        self.assertEqual(reviews[0]["available_frames"], 300)
        self.assertEqual(self.provider.generate.call_count, 2)

    def test_failed_recovery_stays_unreviewed_without_retry(self):
        self.provider.generate.side_effect = AssistantError("provider_timeout")
        raw, assessment = self.review()
        self.assertIs(raw, self.draft)
        self.assertEqual(
            assessment, dict(status="unavailable", reason="", code="provider_timeout")
        )
        self.assertEqual(self.provider.generate.call_count, 1)
