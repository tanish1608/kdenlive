# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import hashlib
import random
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock

from montaz_assistant.captions import (
    caption_spec,
    map_segments,
    propose_captions,
    validate_segments,
)
from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools


class CaptionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "speech.wav"
        self.path.write_bytes(b"synthetic source")
        self.snapshot = Snapshot(
            "project",
            "sequence",
            2,
            FrameRate(30000, 1001),
            (Asset("1", 300, False, True),),
            (Track("2", "audio"),),
            (OccupiedRange("spoken", "2", 100, 90),),
        )
        self.spec = dict(
            clips=[
                dict(
                    instance_id="spoken",
                    bin_id="1",
                    track_id="2",
                    start_frame=100,
                    in_frame=30,
                    duration_frames=90,
                    audio_stream=0,
                )
            ]
        )
        self.media = MediaTools()
        self.pcm = b"\x00\x10" * (90 * 1001 * 16000 // 30000)
        self.media._output = Mock(return_value=self.pcm)
        self.provider = Mock(model="gemini-test")
        self.provider.generate.return_value = dict(
            segments=[dict(start_ms=100, end_ms=1600, text="We arrived at the beach.")]
        )
        self.factory = Mock(return_value=self.provider)
        self.progress = Mock()

    def propose(self, **changes):
        args = dict(
            snapshot=self.snapshot,
            request_id="captions-1",
            raw_spec=self.spec,
            sources={"1": str(self.path)},
            cache=self.root / "cache",
            media=self.media,
            progress=self.progress,
            provider_factory=self.factory,
        )
        args.update(changes)
        return propose_captions(**args)

    def test_empty_audio_finishes_without_provider_or_media(self):
        result = self.propose(raw_spec=dict(clips=[]), sources={})
        self.assertEqual(result["captions"], [])
        self.assertEqual(result["source_sha256"], {})
        self.factory.assert_not_called()
        self.media._output.assert_not_called()
        self.progress.assert_called_once_with(0, 0, 0, 0)

    def test_silence_finishes_locally_without_configuration(self):
        self.media._output.return_value = b"\0" * len(self.pcm)
        result = self.propose()
        self.assertEqual(result["captions"], [])
        self.assertEqual(result["skipped_clips"], 1)
        self.factory.assert_not_called()
        self.assertFalse((self.root / "cache").exists())

    def test_noise_can_finish_with_empty_speech_and_reuse_negative_cache(self):
        self.provider.generate.return_value = dict(segments=[])
        self.assertEqual(self.propose()["skipped_clips"], 1)
        result = self.propose()
        self.assertEqual(result["cached_clips"], 1)
        self.assertEqual(result["skipped_clips"], 1)
        self.provider.generate.assert_called_once()

    def test_captions_map_source_audio_to_exact_native_frame_intervals(self):
        result = self.propose()
        self.assertEqual(
            result["captions"],
            [
                dict(
                    instance_id="spoken",
                    start_frame=102,
                    duration_frames=46,
                    text="We arrived at the beach.",
                )
            ],
        )
        self.assertEqual(
            result["source_sha256"]["1"],
            hashlib.sha256(self.path.read_bytes()).hexdigest(),
        )
        argv = self.media._output.call_args.args[0]
        self.assertEqual(argv[argv.index("-map") + 1], "0:0")
        self.assertAlmostEqual(float(argv[argv.index("-ss") + 1]), 1.001)
        self.assertEqual(self.propose()["cached_clips"], 1)
        self.provider.generate.assert_called_once()

    def test_six_second_fractional_offsets_round_outward_by_one_frame(self):
        snapshot = replace(
            self.snapshot,
            frame_rate=FrameRate(30),
            occupied=(OccupiedRange("spoken", "2", 100, 200),),
        )
        clip = {**self.spec["clips"][0], "duration_frames": 200, "in_frame": 0}
        self.media._output.return_value = b"\0\x10" * (200 * 16000 // 30)
        self.provider.generate.return_value = dict(
            segments=[dict(start_ms=1, end_ms=6001, text="Spoken words")]
        )
        self.assertEqual(
            self.propose(snapshot=snapshot, raw_spec=dict(clips=[clip]))["captions"][0][
                "duration_frames"
            ],
            181,
        )

    def test_long_phrases_crowding_the_last_frame_stay_separate_and_inside(self):
        # Phrases that quantize into the clip's final frame cannot be combined
        # within 84 characters; earlier spare frames keep every phrase.
        for rate in (
            FrameRate(24000, 1001),
            FrameRate(30000, 1001),
            FrameRate(60000, 1001),
        ):
            length = 3 * rate.numerator // rate.denominator
            end_ms = length * rate.denominator * 1000 // rate.numerator
            segments = [
                dict(start_ms=0, end_ms=end_ms - 30, text="a" * 60),
                dict(start_ms=end_ms - 3, end_ms=end_ms - 2, text="b" * 60),
                dict(start_ms=end_ms - 2, end_ms=end_ms - 1, text="c" * 60),
                dict(start_ms=end_ms - 1, end_ms=end_ms, text="d" * 60),
            ]
            snapshot = replace(
                self.snapshot,
                frame_rate=rate,
                assets=(Asset("1", 1000, False, True),),
                occupied=(OccupiedRange("spoken", "2", 500, length),),
            )
            clip = {
                **self.spec["clips"][0],
                "start_frame": 500,
                "in_frame": 211,
                "duration_frames": length,
            }
            self.media._output.return_value = b"\0\x10" * (
                length * rate.denominator * 16000 // rate.numerator
            )
            self.provider.generate.return_value = dict(segments=segments)
            with self.subTest(rate=rate):
                captions = self.propose(
                    snapshot=snapshot,
                    raw_spec=dict(clips=[clip]),
                    cache=self.root / f"crowded{rate.numerator}",
                )["captions"]
                self.assertEqual(
                    [c["text"] for c in captions], [s["text"] for s in segments]
                )
                self.assertEqual(
                    [c["start_frame"] + c["duration_frames"] for c in captions][-3:],
                    [500 + length - 2, 500 + length - 1, 500 + length],
                )
                for before, after in zip(captions, captions[1:]):
                    self.assertLessEqual(
                        before["start_frame"] + before["duration_frames"],
                        after["start_frame"],
                    )
                self.assertTrue(all(c["duration_frames"] >= 1 for c in captions))
                argv = self.media._output.call_args.args[0]
                self.assertAlmostEqual(
                    float(argv[argv.index("-ss") + 1]),
                    211 * rate.denominator / rate.numerator,
                )

    def test_representable_transcripts_keep_every_word_in_bounded_order(self):
        rng = random.Random(3)
        words = "we made it to the beach the water is warm look at that".split()
        for _ in range(3000):
            rate = rng.choice(
                (FrameRate(24000, 1001), FrameRate(30000, 1001), FrameRate(60000, 1001))
            )
            length = rng.randint(1, 120)
            duration_ms = length * rate.denominator * 16000 // rate.numerator // 16
            segments, cursor = [], rng.choice((0, max(0, duration_ms - 60)))
            while len(segments) < min(length, 8) and cursor < duration_ms:
                start = cursor + rng.choice((0, rng.randint(0, 400)))
                end = min(duration_ms, start + rng.choice((1, rng.randint(1, 3000))))
                if end <= start:
                    break
                text = " ".join(rng.choices(words, k=rng.randint(1, 12)))[:84].strip()
                segments.append(dict(start_ms=start, end_ms=end, text=text))
                cursor = end
            if not segments:
                continue
            validate_segments(dict(segments=segments), duration_ms)
            clip = dict(
                instance_id="c", start_frame=rng.choice((0, 37)), duration_frames=length
            )
            captions = map_segments(segments, clip, rate)
            self.assertEqual(
                [c["text"] for c in captions], [s["text"] for s in segments]
            )
            previous = clip["start_frame"]
            for c in captions:
                self.assertGreaterEqual(c["start_frame"], previous)
                self.assertGreaterEqual(c["duration_frames"], 1)
                previous = c["start_frame"] + c["duration_frames"]
            self.assertLessEqual(previous, clip["start_frame"] + length)

    def test_model_and_audio_change_invalidate_cached_recognition(self):
        self.propose()
        self.provider.model = "gemini-other"
        self.propose()
        self.media._output.return_value = b"\0\x20" * (len(self.pcm) // 2)
        self.propose()
        self.assertEqual(self.provider.generate.call_count, 3)

    def test_corrupt_cache_retries_without_accepting_bad_text(self):
        self.propose()
        for p in (self.root / "cache").glob("*.json"):
            p.write_text(
                '{"segments":[{"start_ms":0,"end_ms":1000,"text":"{override}"}]}'
            )
        self.assertEqual(self.propose()["cached_clips"], 0)
        self.assertEqual(self.provider.generate.call_count, 2)

    def test_decode_failure_is_an_error_not_no_speech(self):
        for pcm in (b"", b"x", b"\0" * 100):
            self.media._output.return_value = pcm
            with self.assertRaisesRegex(AssistantError, "media_unreadable"):
                self.propose()
        self.factory.assert_not_called()

    def test_invalid_provider_segments_do_not_enter_cache(self):
        cases = [
            dict(start_ms=True, end_ms=1000, text="Words"),
            dict(start_ms=0, end_ms=4000, text="Words"),
            dict(start_ms=0, end_ms=1000, text="{\\pos(1,2)}"),
            dict(start_ms=0, end_ms=1000, text=""),
            dict(start_ms=0, end_ms=1000, text="a\rb"),
            dict(start_ms=0, end_ms=1000, text="x" * 85),
        ]
        for segment in cases:
            self.provider.generate.return_value = dict(segments=[segment])
            with self.assertRaises(ValueError):
                self.propose()
        self.assertFalse((self.root / "cache").exists())

    def test_overlaps_and_excessive_segments_rejected(self):
        with self.assertRaises(ValueError):
            validate_segments(
                dict(
                    segments=[
                        dict(start_ms=0, end_ms=1000, text="One"),
                        dict(start_ms=999, end_ms=2000, text="Two"),
                    ]
                ),
                3000,
            )
        with self.assertRaises(ValueError):
            validate_segments(
                dict(
                    segments=[
                        dict(start_ms=i, end_ms=i + 1, text="Word") for i in range(129)
                    ]
                ),
                3000,
            )

    def test_changed_source_or_stop_cannot_publish_result_or_cache(self):
        def change(_):
            self.path.write_bytes(b"changed source")
            return dict(segments=[])

        self.provider.generate.side_effect = change
        with self.assertRaisesRegex(AssistantError, "media_changed"):
            self.propose()
        self.assertFalse((self.root / "cache").exists())
        self.provider.generate.side_effect = lambda _: (
            self.media.cancel(),
            dict(segments=[]),
        )[1]
        with self.assertRaisesRegex(AssistantError, "cancelled"):
            self.propose()
        self.assertFalse((self.root / "cache").exists())

    def test_empty_provider_key_only_fails_when_recognition_needed(self):
        self.factory.side_effect = AssistantError("missing_api_key")
        with self.assertRaisesRegex(AssistantError, "missing_api_key"):
            self.propose()
        self.assertFalse((self.root / "cache").exists())

    def test_request_rejects_wrong_track_stream_source_bounds_and_duration(self):
        for key, value in [
            ("audio_stream", -1),
            ("audio_stream", 64),
            ("audio_stream", True),
            ("in_frame", 250),
            ("duration_frames", 91),
            ("start_frame", 101),
        ]:
            clip = {**self.spec["clips"][0], key: value}
            with self.assertRaises(ValueError):
                caption_spec(self.snapshot, dict(clips=[clip]))
        with self.assertRaises(ValueError):
            caption_spec(
                replace(self.snapshot, tracks=(Track("2", "video"),)), self.spec
            )
        with self.assertRaises(ValueError):
            caption_spec(self.snapshot, dict(clips=self.spec["clips"] * 2))
        with self.assertRaises(ValueError):
            self.propose(sources={})

    def test_second_placed_use_reuses_audio_cache_and_keeps_new_timeline_offset(self):
        self.propose()
        second = {
            **self.spec["clips"][0],
            "instance_id": "spoken-again",
            "start_frame": 200,
        }
        snapshot = replace(
            self.snapshot,
            occupied=self.snapshot.occupied
            + (OccupiedRange("spoken-again", "2", 200, 90),),
        )
        result = self.propose(
            snapshot=snapshot, raw_spec=dict(clips=self.spec["clips"] + [second])
        )
        self.assertEqual(result["checked_clips"], 2)
        self.assertEqual(result["cached_clips"], 2)
        self.assertEqual(result["captions"][1]["start_frame"], 202)
        self.provider.generate.assert_called_once()
