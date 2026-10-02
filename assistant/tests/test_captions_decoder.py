# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Opt-in: negative caption fixtures through the real decoder.

Set MONTAZ_FFMPEG to an FFmpeg with the lavfi device. Fixtures are generated
in a temporary directory; recognition must never be contacted for them.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

from montaz_assistant.captions import propose_captions
from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools

FFMPEG = os.environ.get("MONTAZ_FFMPEG", "")


@unittest.skipUnless(FFMPEG, "set MONTAZ_FFMPEG to run real decoder checks")
class RealDecoderCaptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        video = ["-f", "lavfi", "-i", "testsrc=s=160x90:r=25"]
        silence = ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono"]
        for name, arguments in {
            "silence.wav": [*silence, "-t", "4"],
            # lavfi's sine peaks near -18 dBFS; -55 dB more is far below the gate.
            "quiet.wav": [
                "-f",
                "lavfi",
                "-i",
                "sine=f=440:r=48000",
                "-af",
                "volume=-55dB",
                "-t",
                "4",
            ],
            "video.mp4": [*video, "-t", "4", "-pix_fmt", "yuv420p"],
            "silent-av.mp4": [*video, *silence, "-t", "4", "-pix_fmt", "yuv420p"],
            "short-audio.mp4": [
                *video,
                "-t",
                "4",
                *silence,
                "-t",
                "1.5",
                "-pix_fmt",
                "yuv420p",
            ],
        }.items():
            subprocess.run(
                [FFMPEG, "-v", "error", "-y", *arguments, str(cls.root / name)],
                check=True,
                timeout=60,
            )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def propose(self, name, stream=0, frames=100):
        snapshot = Snapshot(
            "project",
            "sequence",
            1,
            FrameRate(25, 1),
            (Asset("1", 100, False, True),),
            (Track("a", "audio"),),
            (OccupiedRange("clip", "a", 0, frames),),
        )
        spec = dict(
            clips=[
                dict(
                    instance_id="clip",
                    bin_id="1",
                    track_id="a",
                    start_frame=0,
                    in_frame=0,
                    duration_frames=frames,
                    audio_stream=stream,
                )
            ]
        )
        factory = Mock(side_effect=AssertionError("recognition contacted"))
        with tempfile.TemporaryDirectory() as cache:
            result = propose_captions(
                snapshot,
                "request",
                spec,
                {"1": str(self.root / name)},
                Path(cache),
                MediaTools(),
                lambda *_: None,
                factory,
            )
            self.assertEqual(list(Path(cache).iterdir()), [])
        factory.assert_not_called()
        return result

    def test_silent_and_quiet_audio_finish_locally(self):
        for name, stream, frames in (
            ("silence.wav", 0, 100),
            ("quiet.wav", 0, 100),
            ("silent-av.mp4", 1, 100),
            ("silence.wav", 0, 1),
        ):
            with self.subTest(name=name, frames=frames):
                result = self.propose(name, stream, frames)
                self.assertEqual(result["captions"], [])
                self.assertEqual(
                    (result["checked_clips"], result["skipped_clips"]), (1, 1)
                )

    def test_missing_or_truncated_audio_is_unavailable_not_silence(self):
        for name, stream in (
            ("video.mp4", 0),
            ("video.mp4", 1),
            ("short-audio.mp4", 1),
        ):
            with (
                self.subTest(name=name, stream=stream),
                self.assertRaises(AssistantError) as caught,
            ):
                self.propose(name, stream)
            self.assertEqual(caught.exception.code, "media_unreadable")


if __name__ == "__main__":
    unittest.main()
