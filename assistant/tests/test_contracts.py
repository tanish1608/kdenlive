# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Regression cases for the native editor/assistant boundary."""

import unittest
from dataclasses import FrozenInstanceError, replace

from montaz_assistant.contracts import (
    Asset,
    FrameRate,
    InsertClip,
    OccupiedRange,
    Proposal,
    ProposalError,
    Snapshot,
    Track,
    validate_proposal,
)


class ProposalTests(unittest.TestCase):
    def setUp(self):
        self.rate = FrameRate(30000, 1001)
        self.snapshot = Snapshot(
            "project-a",
            "sequence-a",
            7,
            self.rate,
            (Asset("source-a", 300), Asset("silent", 100, has_audio=False)),
            (Track("v1", "video"), Track("a1", "audio")),
        )
        self.clip = InsertClip("clip-a", "source-a", "v1", 0, 10, 30)
        self.proposal = Proposal(
            "request-a",
            "project-a",
            "sequence-a",
            7,
            self.rate,
            (self.clip,),
        )

    def test_six_clip_proposal_is_immutable_and_does_not_modify_editor_snapshot(self):
        clips = tuple(
            replace(self.clip, instance_id=f"clip-{i}", start_frame=30 * i) for i in range(6)
        )
        proposal = replace(self.proposal, operations=clips)
        self.assertEqual(validate_proposal(self.snapshot, proposal), clips)
        self.assertEqual(self.snapshot.occupied, ())
        with self.assertRaises(FrozenInstanceError):
            proposal.base_revision = 8

    def test_replayed_delivery_cannot_duplicate_six_clip_edit(self):
        with self.assertRaisesRegex(ProposalError, "already applied"):
            validate_proposal(self.snapshot, self.proposal, frozenset({"request-a"}))

    def test_manual_edit_or_undo_invalidates_outstanding_proposal(self):
        with self.assertRaisesRegex(ProposalError, "Stale"):
            validate_proposal(replace(self.snapshot, revision=8), self.proposal)

    def test_project_and_sequence_switch_rejects_old_results(self):
        for changes in ({"project_id": "project-b"}, {"sequence_id": "sequence-b"}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ProposalError, "another"):
                validate_proposal(replace(self.snapshot, **changes), self.proposal)

    def test_profile_change_cannot_reinterpret_frame_numbers(self):
        with self.assertRaisesRegex(ProposalError, "frame rate"):
            validate_proposal(replace(self.snapshot, frame_rate=FrameRate(60)), self.proposal)

    def test_duplicate_instance_ids_rejected_even_at_different_positions(self):
        second = replace(self.clip, start_frame=30)
        with self.assertRaisesRegex(ProposalError, "Duplicate"):
            validate_proposal(self.snapshot, replace(self.proposal, operations=(self.clip, second)))

    def test_existing_instance_id_cannot_be_reused(self):
        snapshot = replace(self.snapshot, occupied=(OccupiedRange("clip-a", "v1", 100, 30),))
        with self.assertRaisesRegex(ProposalError, "Duplicate"):
            validate_proposal(snapshot, self.proposal)

    def test_locked_tracks_are_preserved(self):
        snapshot = replace(self.snapshot, tracks=(Track("v1", "video", locked=True),))
        with self.assertRaisesRegex(ProposalError, "locked"):
            validate_proposal(snapshot, self.proposal)

    def test_unknown_assets_and_tracks_do_not_trigger_implicit_import(self):
        for changes in ({"bin_id": "/private/unimported.mov"}, {"track_id": "v7"}):
            with self.subTest(changes=changes), self.assertRaisesRegex(ProposalError, "Unknown"):
                op = replace(self.clip, **changes)
                validate_proposal(self.snapshot, replace(self.proposal, operations=(op,)))

    def test_silent_video_cannot_insert_a_nonexistent_audio_stream(self):
        op = replace(self.clip, bin_id="silent", track_id="a1")
        with self.assertRaisesRegex(ProposalError, "matching stream"):
            validate_proposal(self.snapshot, replace(self.proposal, operations=(op,)))

    def test_source_outpoint_is_exclusive_and_exact_boundary_is_valid(self):
        op = replace(self.clip, in_frame=270, duration_frames=30)
        self.assertEqual(
            validate_proposal(self.snapshot, replace(self.proposal, operations=(op,))), (op,)
        )
        with self.assertRaisesRegex(ProposalError, "exceeds"):
            validate_proposal(
                self.snapshot, replace(self.proposal, operations=(replace(op, duration_frames=31),))
            )

    def test_adjacent_clips_are_valid_but_one_frame_overlap_is_rejected(self):
        op = replace(self.clip, instance_id="clip-b", start_frame=30)
        validate_proposal(self.snapshot, replace(self.proposal, operations=(self.clip, op)))
        with self.assertRaisesRegex(ProposalError, "overlap"):
            validate_proposal(
                self.snapshot,
                replace(self.proposal, operations=(self.clip, replace(op, start_frame=29))),
            )

    def test_existing_media_cannot_be_silently_overwritten(self):
        snapshot = replace(self.snapshot, occupied=(OccupiedRange("existing", "v1", 20, 30),))
        with self.assertRaisesRegex(ProposalError, "overlap"):
            validate_proposal(snapshot, self.proposal)

    def test_separate_tracks_can_overlap(self):
        snapshot = replace(self.snapshot, occupied=(OccupiedRange("music", "a1", 0, 300),))
        validate_proposal(snapshot, self.proposal)

    def test_rejects_unsupported_protocol_and_executable_operations(self):
        for changes in ({"protocol_version": 2}, {"operations": ({"shell": "anything"},)}):
            with self.subTest(changes=changes), self.assertRaises(ProposalError):
                replace(self.proposal, **changes)

    def test_frames_cannot_be_floats_booleans_or_negative(self):
        for value in (True, 1.5, -1, "30"):
            with self.subTest(value=value), self.assertRaises(ProposalError):
                replace(self.clip, start_frame=value)

    def test_snapshot_collections_cannot_mutate_after_validation(self):
        with self.assertRaisesRegex(ProposalError, "immutable"):
            replace(self.snapshot, assets=list(self.snapshot.assets))


class FrameRateTests(unittest.TestCase):
    def test_ntsc_uses_rational_rate_without_decimal_drift(self):
        rate = FrameRate(30000, 1001)
        self.assertEqual(rate.seconds_to_frame("1001"), 30000)
        self.assertEqual(FrameRate(60000, 2002), rate)

    def test_half_frames_round_up(self):
        self.assertEqual(FrameRate(30).seconds_to_frame("0.05"), 2)

    def test_invalid_timestamps_are_rejected(self):
        for value in ("NaN", "Infinity", "-0.1", "1/0", 0.05, True, None):
            with self.subTest(value=value), self.assertRaises(ProposalError):
                FrameRate(30).seconds_to_frame(value)


if __name__ == "__main__":
    unittest.main()
