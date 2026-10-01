# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import copy
import unittest
from dataclasses import replace
from unittest.mock import Mock

from montaz_assistant.contracts import Asset, FrameRate, Snapshot, Track
from montaz_assistant.editorial import review_draft
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools


class EditorialTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = Snapshot(
            "project",
            "sequence",
            7,
            FrameRate(30000, 1001),
            (Asset("1", 120), Asset("2", 120)),
            (Track("v", "video"),),
        )
        self.evidence = {
            bid: [
                dict(
                    window_id=0,
                    in_frame=10,
                    duration_frames=100,
                    trim_in=5,
                    trim_out=100,
                    quality=80,
                    role="detail",
                    description="Visible coastline",
                    content_id=bid,
                )
            ]
            for bid in ("1", "2")
        }
        self.draft = dict(
            shots=[
                dict(
                    bin_id=bid,
                    window_id=0,
                    duration_frames=60,
                    reason="Observed coast connects the place",
                )
                for bid in ("1", "2")
            ]
        )
        self.brief = dict(
            duration_seconds=10, recipe="postcard", instruction="Coastal trip"
        )
        self.provider = Mock()
        self.media = MediaTools()
        self.provider.generate.return_value = dict(
            decision="keep", reason="A coherent pair of views", shots=[]
        )

    def review(self):
        return review_draft(
            self.snapshot,
            "request",
            self.draft,
            self.evidence,
            self.brief,
            self.provider,
            self.media,
        )

    def test_keep_is_one_call_and_preserves_original(self):
        draft, assessment = self.review()
        self.assertIs(draft, self.draft)
        self.assertEqual(assessment["status"], "kept")
        self.assertEqual(self.provider.generate.call_count, 1)
        prompt = self.provider.generate.call_args.args[0][0]["text"]
        self.assertIn("EXACTLY 120 total project frames", prompt)
        self.assertIn("FPS=30000/1001", prompt)
        self.assertIn("untrusted data", prompt)

    def test_one_revision_can_reorder_without_changing_span(self):
        self.provider.generate.return_value = dict(
            decision="revise",
            reason="Observed detail opens the place",
            shots=list(reversed(self.draft["shots"])),
        )
        draft, assessment = self.review()
        self.assertEqual(draft["shots"][0]["bin_id"], "2")
        self.assertEqual(assessment["status"], "revised")
        self.assertEqual(self.provider.generate.call_count, 1)
        self.assertEqual(self.draft["shots"][0]["bin_id"], "1")

    def test_redistributed_duration_obeys_recipe_and_source_bounds(self):
        shots = copy.deepcopy(self.draft["shots"])
        shots[0]["duration_frames"], shots[1]["duration_frames"] = 59, 61
        # At 29.97 FPS the postcard floor rounds to 60, so 59 is invalid.
        self.provider.generate.return_value = dict(
            decision="revise", reason="Give the closing shot room", shots=shots
        )
        draft, assessment = self.review()
        self.assertIs(draft, self.draft)
        self.assertEqual(assessment["status"], "unavailable")
        self.brief["recipe"] = "adventure"
        self.assertEqual(self.review()[1]["status"], "revised")

    def test_unknown_duplicate_discarded_edges_and_duration_change_are_not_approval(
        self,
    ):
        for update in (
            dict(bin_id="invented"),
            dict(bin_id="2"),
            dict(window_id=True),
            dict(window_id=6),
            dict(duration_frames=61),
            dict(duration_frames=101),
            dict(reason=""),
            dict(command="run code"),
        ):
            with self.subTest(update=update):
                shots = copy.deepcopy(self.draft["shots"])
                shots[0].update(update)
                self.provider.generate.return_value = dict(
                    decision="revise", reason="Change", shots=shots
                )
                draft, assessment = self.review()
                self.assertIs(draft, self.draft)
                self.assertEqual(
                    assessment,
                    dict(status="unavailable", reason="", code="invalid_review"),
                )

    def test_reason_only_revision_is_not_a_changed_edit(self):
        shots = copy.deepcopy(self.draft["shots"])
        shots[0]["reason"] = "A new explanation with no actual cut change"
        self.provider.generate.return_value = dict(
            decision="revise", reason="Better", shots=shots
        )
        self.assertEqual(self.review()[1]["status"], "unavailable")

    def test_invalid_decisions_and_reasons_are_explicitly_unavailable(self):
        for result in (
            dict(decision="approved", reason="Good", shots=[]),
            dict(decision=True, reason="Good", shots=[]),
            dict(decision="keep", reason=" ", shots=[]),
            dict(decision="keep", reason="x" * 601, shots=[]),
            dict(decision="keep", reason="Good", shots=self.draft["shots"]),
            dict(decision="keep", reason="Good", shots=[], retry=True),
            dict(decision="revise", reason="Good", shots=[]),
            None,
        ):
            with self.subTest(result=result):
                self.provider.generate.return_value = result
                self.assertEqual(self.review()[1]["status"], "unavailable")

    def test_provider_failures_do_not_retry_or_approve(self):
        for code in (
            "provider_timeout",
            "provider_auth",
            "provider_unavailable",
            "invalid_model_response",
        ):
            with self.subTest(code=code):
                self.provider.generate.reset_mock()
                self.provider.generate.side_effect = AssistantError(code)
                draft, assessment = self.review()
                self.assertIs(draft, self.draft)
                self.assertEqual(
                    assessment, dict(status="unavailable", reason="", code=code)
                )
                self.assertEqual(self.provider.generate.call_count, 1)

    def test_stop_during_provider_call_discards_late_success(self):
        def late(parts):
            self.media.cancel()
            return dict(decision="keep", reason="Late", shots=[])

        self.provider.generate.side_effect = late
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.review()

    def test_cancelled_provider_is_not_an_available_draft(self):
        self.provider.generate.side_effect = AssistantError("cancelled")
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.review()

    def test_stopped_review_does_not_call_provider(self):
        self.media.cancel()
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.review()
        self.provider.generate.assert_not_called()

    def test_evidence_limit_skips_locally(self):
        self.evidence.update({str(i): self.evidence["1"] for i in range(3, 34)})
        self.assertEqual(
            self.review()[1], dict(status="unavailable", reason="", code="review_limit")
        )
        self.provider.generate.assert_not_called()

    def test_reviewed_plan_remains_bound_to_locked_native_tracks(self):
        self.snapshot = replace(self.snapshot, tracks=(Track("v", "video", True),))
        with self.assertRaises(AssistantError):
            self.review()
        self.provider.generate.assert_not_called()
