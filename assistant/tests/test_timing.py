# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import copy
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools
from montaz_assistant.protocol import decode, encode, to_wire
from montaz_assistant.timing import refine, refine_timing, timing_spec
from test_music import audio_bytes


class TimingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.music = Path(self.temp.name) / "music.wav"
        self.music.write_bytes(audio_bytes(seconds=6))
        self.snapshot = Snapshot(
            "project",
            "sequence",
            7,
            FrameRate(25),
            tuple(Asset(str(i), 80) for i in range(3)),
            (Track("1", "video"), Track("2", "audio")),
            tuple(OccupiedRange(str(i), "1", i * 50, 50) for i in range(3))
            + (OccupiedRange("music", "2", 0, 150),),
        )
        self.spec = dict(
            music=dict(
                path=str(self.music),
                instance_id="music",
                sha256=hashlib.sha256(self.music.read_bytes()).hexdigest(),
            ),
            max_shift_frames=6,
            clips=[
                dict(
                    instance_id=str(i),
                    bin_id=str(i),
                    track_id="1",
                    start_frame=i * 50,
                    in_frame=0,
                    duration_frames=50,
                )
                for i in range(3)
            ],
        )

    def test_small_accent_shifts_preserve_order_sources_and_total(self):
        result = refine(self.snapshot, "request", self.spec, [54, 94])
        self.assertEqual([x["start_frame"] for x in result["operations"]], [0, 54, 94])
        self.assertEqual(
            [x["duration_frames"] for x in result["operations"]], [54, 40, 56]
        )
        self.assertEqual(sum(x["duration_frames"] for x in result["operations"]), 150)
        self.assertEqual([x["in_frame"] for x in result["operations"]], [0, 0, 0])
        self.assertEqual(result["protocol_version"], 2)
        self.assertEqual(result["base_revision"], 7)

    def test_global_choice_respects_neighbor_source_handles(self):
        snapshot = replace(
            self.snapshot, assets=(Asset("0", 52), Asset("1", 55), Asset("2", 60))
        )
        result = refine(snapshot, "request", self.spec, [46, 54, 94])
        self.assertEqual([x["start_frame"] for x in result["operations"]], [0, 46, 94])

    def test_no_handles_no_nearby_accents_and_already_aligned_are_terminal_no_changes(
        self,
    ):
        for snapshot, accents in (
            (
                replace(
                    self.snapshot, assets=tuple(Asset(str(i), 50) for i in range(3))
                ),
                [54, 94],
            ),
            (self.snapshot, [30, 120]),
            (self.snapshot, [50, 100]),
            (self.snapshot, []),
        ):
            with (
                self.subTest(accents=accents),
                self.assertRaises(AssistantError) as caught,
            ):
                refine(snapshot, "request", self.spec, accents)
            self.assertEqual(caught.exception.code, "no_alignment_needed")

    def test_equal_distance_tie_is_deterministic_and_invariant_to_nonmatching_accents(
        self,
    ):
        first = refine(self.snapshot, "request", self.spec, [46, 54])
        second = refine(self.snapshot, "request", self.spec, [20, 46, 54, 130])
        self.assertEqual(first["operations"], second["operations"])
        self.assertEqual(first["operations"][1]["start_frame"], 46)

    def test_gaps_duplicate_ids_locked_tracks_and_additional_audio_are_ineligible(self):
        for mutate in (
            lambda s: s["clips"][1].update(start_frame=51),
            lambda s: s["clips"][1].update(instance_id="0"),
            lambda s: s.update(max_shift_frames=True),
            lambda s: s["clips"][0].update(in_frame=40),
        ):
            spec = copy.deepcopy(self.spec)
            mutate(spec)
            with self.assertRaises((AssistantError, ValueError)):
                timing_spec(self.snapshot, spec)
        for snapshot in (
            replace(
                self.snapshot, tracks=(Track("1", "video", True), Track("2", "audio"))
            ),
            replace(
                self.snapshot,
                occupied=self.snapshot.occupied
                + (OccupiedRange("speech", "2", 150, 10),),
            ),
        ):
            with self.assertRaises(AssistantError):
                timing_spec(snapshot, self.spec)

    def test_subsecond_shots_invalid_accents_and_source_overflow_are_rejected(self):
        for accents in ([True], [2.5], [94, 54], [54, 54], [150], [0]):
            with self.subTest(accents=accents), self.assertRaises(AssistantError):
                refine(self.snapshot, "request", self.spec, accents)
        snapshot = replace(self.snapshot, frame_rate=FrameRate(60))
        spec = dict(self.spec, max_shift_frames=15)
        with self.assertRaises((AssistantError, ValueError)):
            timing_spec(snapshot, spec)

    def test_music_change_and_stop_reject_before_measurement(self):
        self.music.write_bytes(b"changed")
        with self.assertRaises(AssistantError) as caught:
            refine_timing(self.snapshot, "request", self.spec, MediaTools())
        self.assertEqual(caught.exception.code, "media_changed")
        media = MediaTools()
        media.cancel()
        with self.assertRaises(AssistantError) as caught:
            refine_timing(self.snapshot, "request", self.spec, media)
        self.assertEqual(caught.exception.code, "cancelled")

    def test_music_changed_during_analysis_is_rejected_before_proposal(self):
        def change(*args):
            self.music.write_bytes(b"changed during decode")
            return dict(
                mode="beats",
                beats=[13, 25, 38, 50, 63, 75],
                bpm=120,
                periodicity=1,
                support=1,
                reason="none",
                cached=False,
                version="periodic-onsets-2",
            )

        with patch("montaz_assistant.timing.analyze", side_effect=change):
            with self.assertRaises(AssistantError) as caught:
                refine_timing(self.snapshot, "request", self.spec, MediaTools())
        self.assertEqual(caught.exception.code, "media_changed")

    def test_relative_anchors_translate_to_nonzero_timeline_start(self):
        clips = [dict(c, start_frame=c["start_frame"] + 20) for c in self.spec["clips"]]
        snapshot = replace(
            self.snapshot,
            occupied=tuple(
                replace(o, start_frame=o.start_frame + 20)
                for o in self.snapshot.occupied
            ),
        )
        result = refine(snapshot, "request", dict(self.spec, clips=clips), [54, 94])
        self.assertEqual(
            [o["start_frame"] for o in result["operations"]], [20, 74, 114]
        )

    @unittest.skipUnless(
        shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required"
    )
    def test_actual_worker_measures_existing_music_without_credentials_or_network(self):
        snapshot = replace(
            self.snapshot,
            occupied=(
                OccupiedRange("0", "1", 0, 48),
                OccupiedRange("1", "1", 48, 52),
                self.snapshot.occupied[2],
                self.snapshot.occupied[3],
            ),
        )
        spec = copy.deepcopy(self.spec)
        spec["clips"][0]["duration_frames"] = 48
        spec["clips"][1].update(start_frame=48, duration_frames=52)
        result = subprocess.run(
            [sys.executable, "-u", "-m", "montaz_assistant.worker"],
            input=encode(
                dict(
                    version=1,
                    type="refine_timing",
                    request_id="request",
                    snapshot=to_wire(snapshot),
                    timing=spec,
                )
            ),
            env=dict(
                os.environ,
                GEMINI_API_KEY="",
                ELEVENLABS_API_KEY="",
                MONTAZ_ASSISTANT_CONFIG=str(Path(self.temp.name) / "missing.json"),
                MONTAZ_ANALYSIS_CACHE=str(Path(self.temp.name) / "cache"),
            ),
            capture_output=True,
            timeout=8,
        )
        messages = [decode(line) for line in result.stdout.splitlines()]
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(messages[-1]["type"], "timing")
        proposal = messages[-1]["proposal"]
        self.assertEqual(proposal["protocol_version"], 4)
        self.assertEqual(proposal["analysis"]["mode"], "beats")
        self.assertEqual(proposal["analysis"]["bpm"], 120)
        self.assertEqual(proposal["operations"][1]["start_frame"], 50)
        self.assertEqual(
            [x["sequence"] for x in messages if x["type"] == "event"], [1, 2, 3, 4]
        )
        self.assertNotIn(str(self.music).encode(), result.stdout)


if __name__ == "__main__":
    unittest.main()
