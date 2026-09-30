# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors

import os
import subprocess
import sys
import unittest
from dataclasses import replace

from montaz_assistant.contracts import (
    Asset,
    FrameRate,
    OccupiedRange,
    ProposalError,
    Snapshot,
    Track,
)
from montaz_assistant.protocol import MAX_FRAME, decode, encode, snapshot_from_wire, to_wire
from montaz_assistant.worker import propose_demo


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = Snapshot(
            "project",
            "sequence",
            2,
            FrameRate(30000, 1001),
            tuple(Asset(str(i), 300) for i in range(1, 8)),
            (Track("1", "video"), Track("2", "audio")),
            (OccupiedRange("existing", "1", 0, 30),),
        )

    def test_round_trip_snapshot(self):
        self.assertEqual(snapshot_from_wire(decode(encode(to_wire(self.snapshot)))), self.snapshot)

    def test_distinct_sources_append_at_exact_project_frames(self):
        proposal = propose_demo(self.snapshot, "request")
        self.assertEqual(len(proposal.operations), 6)
        self.assertEqual(len({op.bin_id for op in proposal.operations}), 6)
        self.assertEqual(proposal.operations[0].start_frame, 30)
        self.assertEqual(proposal.operations[-1].start_frame, 330)
        self.assertEqual(proposal.operations[0].duration_frames, 60)

    def test_short_source_is_not_stretched_or_repeated(self):
        snapshot = replace(self.snapshot, assets=(Asset("1", 5),))
        self.assertEqual(propose_demo(snapshot, "request").operations[0].duration_frames, 5)

    def test_no_video_or_locked_tracks_fail(self):
        for snapshot in (
            replace(self.snapshot, tracks=(Track("1", "video", True),)),
            replace(self.snapshot, assets=(Asset("1", 30, False, True),)),
        ):
            with self.subTest(snapshot=snapshot), self.assertRaises(ProposalError):
                propose_demo(snapshot, "request")

    def test_overflow_rejected_before_native_application(self):
        snapshot = replace(
            self.snapshot, occupied=(OccupiedRange("existing", "1", MAX_FRAME - 5, 5),)
        )
        with self.assertRaises(ProposalError):
            propose_demo(snapshot, "request")

    def test_wire_rejects_unknown_fields_and_boolean_frames(self):
        for key, value in (("executable", "anything"), ("revision", True)):
            raw = to_wire(self.snapshot)
            raw[key] = value
            with self.subTest(key=key), self.assertRaises(ProposalError):
                snapshot_from_wire(raw)

    def test_json_duplicate_keys_rejected(self):
        with self.assertRaises(ProposalError):
            decode(b'{"version":1,"version":2}')

    def test_actual_worker_process_finishes_with_protocol_only_output(self):
        request = {
            "version": 1,
            "type": "plan_demo",
            "request_id": "request",
            "snapshot": to_wire(self.snapshot),
        }
        result = subprocess.run(
            [sys.executable, "-u", "-m", "montaz_assistant.worker"],
            input=encode(request),
            capture_output=True,
            timeout=5,
            env=os.environ.copy(),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        messages = [decode(line) for line in result.stdout.splitlines()]
        self.assertEqual(messages[0]["type"], "ready")
        self.assertEqual(messages[-1]["type"], "proposal")
        events = [m for m in messages if m["type"] == "event"]
        self.assertEqual([m["sequence"] for m in events], [1, 2, 3, 4])
        self.assertEqual(events[-1]["status"], "completed")

    def test_malformed_input_returns_bounded_nonreflective_error(self):
        result = subprocess.run(
            [sys.executable, "-u", "-m", "montaz_assistant.worker"],
            input=b'{"secret":"do-not-log"}',
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertNotIn(b"do-not-log", result.stdout + result.stderr)
        self.assertEqual(decode(result.stdout.splitlines()[-1])["type"], "error")


if __name__ == "__main__":
    unittest.main()
