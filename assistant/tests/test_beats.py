# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import hashlib
import io
import math
import os
import random
import shutil
import tempfile
import unittest
import wave
from array import array
from pathlib import Path
from unittest.mock import patch

from montaz_assistant.beats import analyze, estimate, unavailable, validate
from montaz_assistant.contracts import FrameRate
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools


def pulses(bpm=120, seconds=12, phase=0.17, missing=()):
    return [
        phase + n * 60 / bpm
        for n in range(math.ceil(seconds * bpm / 60))
        if phase + n * 60 / bpm < seconds and n not in missing
    ]


def rhythm_audio(points, seconds=12, tone=False):
    """Known kick-like attacks; fixtures carry no provider or real-media assumptions."""
    samples = array("h", [0]) * round(seconds * 8000)
    if tone:
        for i in range(len(samples)):
            samples[i] = round(5000 * math.sin(2 * math.pi * 440 * i / 8000))
    for t in points:
        start = round(t * 8000)
        for i in range(min(640, len(samples) - start)):
            samples[start + i] = round(
                18000 * math.exp(-i / 180) * math.sin(2 * math.pi * 120 * i / 8000)
            )
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        audio.writeframes(samples.tobytes())
    return output.getvalue()


class BeatTests(unittest.TestCase):
    def envelope(self, points, seconds=12):
        result = [0.0] * round(seconds * 100)
        for p in points:
            result[round(p * 100)] = 1.0
        return result

    def test_fractional_tempos_and_missing_onsets_do_not_invent_beats(self):
        for bpm in (60, 72, 90, 120, 150, 180):
            with self.subTest(bpm=bpm):
                points = pulses(bpm, missing=(5,))
                result = estimate(self.envelope(points), MediaTools())
                self.assertEqual(result["mode"], "beats", result)
                self.assertAlmostEqual(result["bpm"], bpm, delta=0.3)
                self.assertEqual(result["beats"], [round(t * 100) for t in points])

    def test_irregular_timing_tempo_changes_and_short_passages_are_ambiguous(self):
        randomizer = random.Random(9)
        irregular = sorted(randomizer.sample(range(10, 1190), 25))
        changing = pulses(90, seconds=6) + [6 + p for p in pulses(150, seconds=6)]
        for onset in (
            [0.0] * 1200,
            [1.0] * 1200,
            self.envelope(changing),
            self.envelope([t / 100 for t in irregular]),
            self.envelope(pulses(seconds=2), seconds=2),
        ):
            result = estimate(onset, MediaTools())
            self.assertEqual(result["mode"], "accents", result)
            self.assertEqual(result["beats"], [])

    def test_cancelled_analysis_and_malformed_cache_evidence_are_rejected(self):
        media = MediaTools()
        media.cancel()
        with self.assertRaises(AssistantError) as caught:
            estimate([1.0] * 1200, media)
        self.assertEqual(caught.exception.code, "cancelled")
        for key, value in (
            ("mode", []),
            ("reason", []),
            ("support", True),
            ("bpm", float("nan")),
            ("beats", [True]),
            ("beats", [3, 3]),
        ):
            result = unavailable("weak_periodicity")
            result[key] = value
            with self.subTest(key=key), self.assertRaises(AssistantError):
                validate(result, 300)

    @unittest.skipUnless(
        shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required"
    )
    def test_decoded_beats_match_known_attacks_and_cache_is_content_fps_span_bound(
        self,
    ):
        with tempfile.TemporaryDirectory() as temp:
            path, cache = Path(temp) / "music.wav", Path(temp) / "beats"
            for bpm, rate in (
                (72, FrameRate(25)),
                (120, FrameRate(30000, 1001)),
                (180, FrameRate(60)),
            ):
                points = pulses(bpm)
                path.write_bytes(rhythm_audio(points))
                sha = hashlib.sha256(path.read_bytes()).hexdigest()
                duration = rate.seconds_to_frame("12")
                result = analyze(path, rate, duration, sha, cache, MediaTools())
                self.assertEqual(result["mode"], "beats", result)
                self.assertAlmostEqual(result["bpm"], bpm, delta=0.3)
                expected = [rate.seconds_to_frame(str(t)) for t in points]
                self.assertEqual(len(result["beats"]), len(expected))
                self.assertTrue(
                    all(abs(a - b) <= 1 for a, b in zip(result["beats"], expected))
                )
                self.assertFalse(result["cached"])
                with patch.object(
                    MediaTools,
                    "_output",
                    side_effect=AssertionError("decoded cache hit"),
                ):
                    hit = analyze(path, rate, duration, sha, cache, MediaTools())
                self.assertEqual(dict(hit, cached=False), result)
            self.assertEqual(len(list(cache.glob("*.json"))), 3)
            # Same audio at a different FPS/span needs a separate frame map.
            sha = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertFalse(
                analyze(path, FrameRate(25), 250, sha, cache, MediaTools())["cached"]
            )
            self.assertFalse(
                analyze(path, FrameRate(30), 360, sha, cache, MediaTools())["cached"]
            )
            for entry in cache.glob("*.json"):
                entry.write_text('{"mode": []}')
            self.assertFalse(
                analyze(path, FrameRate(25), 250, sha, cache, MediaTools())["cached"]
            )
            # Constant tones must not turn filter startup/ripple into a rhythmic claim.
            path.write_bytes(rhythm_audio([], tone=True))
            sha = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(
                analyze(path, FrameRate(25), 300, sha, cache, MediaTools())["mode"],
                "accents",
            )
            path.write_bytes(rhythm_audio([]))
            with self.assertRaises(AssistantError) as caught:
                analyze(path, FrameRate(25), 300, "b" * 64, cache, MediaTools())
            self.assertEqual(caught.exception.code, "invalid_music_audio")

    @unittest.skipUnless(
        shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required"
    )
    def test_source_change_during_analysis_does_not_publish_a_poisoned_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            path, cache = Path(temp) / "music.wav", Path(temp) / "beats"
            path.write_bytes(rhythm_audio(pulses()))
            sha = hashlib.sha256(path.read_bytes()).hexdigest()

            def change(*args):
                path.write_bytes(b"changed during estimation")
                return unavailable("weak_periodicity")

            with patch("montaz_assistant.beats.estimate", side_effect=change):
                with self.assertRaises(AssistantError) as caught:
                    analyze(path, FrameRate(25), 300, sha, cache, MediaTools())
            self.assertEqual(caught.exception.code, "media_changed")
            self.assertEqual(list(cache.iterdir()), [])

    def test_late_beats_remain_available_for_ten_minute_edits(self):
        result = estimate(
            self.envelope(pulses(180, seconds=600), seconds=600), MediaTools()
        )
        self.assertEqual(result["mode"], "beats")
        self.assertGreater(len(result["beats"]), 128)
        self.assertGreater(result["beats"][-1], 59900)


if __name__ == "__main__":
    unittest.main()
