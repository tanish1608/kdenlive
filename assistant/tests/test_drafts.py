# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Bounded draft replacement preserves occupied content and rational timing."""

import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import (
    Asset,
    FrameRate,
    InsertClip,
    OccupiedRange,
    Proposal,
    ProposalError,
    Snapshot,
    Track,
)
from montaz_assistant.drafts import draft_context, validate_draft_proposal
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools, plan_media
from test_duration_planning import shot, window


class DraftTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = Snapshot(
            "project",
            "sequence",
            5,
            FrameRate(30000, 1001),
            (Asset("1", 150), Asset("2", 150)),
            (Track("1", "video"), Track("2", "audio")),
            (
                OccupiedRange("10", "1", 30, 60),
                OccupiedRange("11", "1", 90, 60),
                OccupiedRange("12", "1", 150, 30),
                OccupiedRange("20", "2", 0, 800),
            ),
        )
        self.spec = dict(
            clips=[
                dict(
                    instance_id=str(10 + i),
                    bin_id=str(1 + i),
                    track_id="1",
                    start_frame=30 + 60 * i,
                    in_frame=0,
                    duration_frames=60,
                )
                for i in range(2)
            ]
        )
        self.proposal = Proposal(
            "revision",
            "project",
            "sequence",
            5,
            self.snapshot.frame_rate,
            (
                InsertClip("new:0", "2", "1", 30, 0, 90),
                InsertClip("new:1", "1", "1", 120, 3, 30),
            ),
        )

    def test_scope_removes_only_owned_occupancy_and_keeps_fractional_rate(self):
        planning, placement = draft_context(self.snapshot, self.spec)
        self.assertEqual(planning.occupied, self.snapshot.occupied[2:])
        self.assertEqual(planning.frame_rate, FrameRate(30000, 1001))
        self.assertEqual(
            placement, dict(track_id="1", start_frame=30, duration_frames=120)
        )
        validate_draft_proposal(self.snapshot, self.spec, self.proposal)

    def test_scope_rejects_unknown_duplicate_moved_locked_and_noncontiguous_clips(self):
        for change in (
            dict(instance_id="999"),
            dict(instance_id="10"),
            dict(start_frame=91),
            dict(track_id="2"),
            dict(extra="delete"),
        ):
            spec = dict(
                clips=[self.spec["clips"][0], dict(self.spec["clips"][1], **change)]
            )
            with self.subTest(change=change), self.assertRaises(ProposalError):
                draft_context(self.snapshot, spec)
        with self.assertRaises(ProposalError):
            draft_context(
                replace(
                    self.snapshot,
                    tracks=(Track("1", "video", True), Track("2", "audio")),
                ),
                self.spec,
            )

    def test_revision_rejects_shorter_longer_wrong_track_colliding_and_stale_results(
        self,
    ):
        for op in (
            replace(self.proposal.operations[-1], duration_frames=29),
            replace(self.proposal.operations[-1], duration_frames=31),
            replace(self.proposal.operations[-1], track_id="2"),
            replace(self.proposal.operations[-1], instance_id="10"),
            replace(self.proposal.operations[-1], start_frame=151),
        ):
            with self.subTest(op=op), self.assertRaises(ProposalError):
                validate_draft_proposal(
                    self.snapshot,
                    self.spec,
                    replace(
                        self.proposal, operations=(self.proposal.operations[0], op)
                    ),
                )
        with self.assertRaises(ProposalError):
            validate_draft_proposal(
                replace(self.snapshot, revision=6), self.spec, self.proposal
            )

    def plan(self, draft, review, *, cancel=False):
        provider = Mock(model="gemini-test")
        media = MediaTools()
        provider.generate.side_effect = [draft, review]
        if cancel:

            def late(_, schema=None):
                media.cancel()
                return draft

            provider.generate.side_effect = late
        evidence = {str(i): [window(150, str(i))] for i in (1, 2)}
        with (
            tempfile.TemporaryDirectory() as root,
            patch(
                "montaz_assistant.planning.analyze",
                side_effect=lambda a, *_: (evidence[a.bin_id], True),
            ),
        ):
            return plan_media(
                self.snapshot,
                "revision",
                {"1": "/one", "2": "/two"},
                dict(
                    duration_seconds=1,
                    recipe="postcard",
                    instruction="Reverse the story",
                ),
                Path(root),
                provider,
                media,
                lambda *_: None,
                draft=self.spec,
            ), provider

    def test_cached_revision_targets_owned_span_instead_of_appending_after_audio(self):
        draft = dict(shots=[shot("2", 60), shot("1", 60)])
        result, provider = self.plan(
            draft, dict(decision="keep", reason="Coherent changed opening", shots=[])
        )
        self.assertEqual([op.start_frame for op in result.operations], [30, 90])
        self.assertEqual(sum(op.duration_frames for op in result.operations), 120)
        self.assertEqual(provider.generate.call_count, 2)
        self.assertIn(
            "EXACTLY 120 project frames",
            provider.generate.call_args_list[0].args[0][0]["text"],
        )
        prompt = provider.generate.call_args_list[0].args[0][0]["text"]
        current = prompt.split(
            "Current draft in timeline order (read-only source context): "
        )[1]
        current = json.JSONDecoder().raw_decode(current)[0]
        self.assertEqual(
            current,
            [
                dict(bin_id="1", in_frame=0, duration_frames=60),
                dict(bin_id="2", in_frame=0, duration_frames=60),
            ],
        )
        self.assertNotIn("instance_id", current[0])

    def test_underfilled_keep_cannot_be_applied_and_stop_discards_late_result(self):
        draft = dict(shots=[shot("2", 60)])
        with self.assertRaisesRegex(AssistantError, "revision_unavailable"):
            self.plan(
                draft, dict(decision="keep", reason="Short coherent edit", shots=[])
            )
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.plan(dict(shots=[shot("2", 60), shot("1", 60)]), {}, cancel=True)


if __name__ == "__main__":
    unittest.main()
