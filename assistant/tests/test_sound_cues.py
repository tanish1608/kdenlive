# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import hashlib
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock

from montaz_assistant.contracts import Asset, FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools, analyze
from montaz_assistant.sound_cues import cue_spec, suggest_sounds


class SoundCueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root / "cache"
        self.provider = Mock(model="gemini-test")
        self.media = MediaTools()
        self.media.visual_frames = Mock(side_effect=lambda path, rate, maximum: maximum)
        self.media.video = Mock(return_value={"text": "sampled silent video"})
        self.assets = tuple(Asset(str(i), 75, True, False) for i in range(1, 4))
        self.sources = {}
        self.clips = []
        for i, asset in enumerate(self.assets):
            p = self.root / f"{i}.mp4"
            p.write_bytes(f"synthetic-{i}".encode())
            self.sources[asset.bin_id] = str(p)
            self.clips.append(
                dict(
                    instance_id=f"shot-{i}",
                    bin_id=asset.bin_id,
                    track_id="video",
                    start_frame=100 * i,
                    in_frame=0,
                    duration_frames=75,
                )
            )
        self.snapshot = Snapshot(
            "project",
            "sequence",
            4,
            FrameRate(25),
            self.assets,
            (Track("video", "video"), Track("audio", "audio")),
            tuple(
                OccupiedRange(c["instance_id"], "video", c["start_frame"], 75)
                for c in self.clips
            ),
        )
        self.spec = dict(clips=self.clips)
        self.prime()
        self.provider.generate.reset_mock()
        self.media.video.reset_mock()
        self.provider.generate.return_value = dict(
            cues=[
                dict(instance_id="shot-0", category="waves"),
                dict(instance_id="shot-1", category="kart_engine"),
            ]
        )

    def prime(self):
        self.provider.generate.return_value = dict(
            windows=[
                dict(
                    window_id=0,
                    description="Visible small coastal waves beside a walkway",
                    role="detail",
                    quality=80,
                    trim_in=0,
                    trim_out=75,
                )
            ]
        )
        for asset in self.assets:
            analyze(
                asset,
                self.snapshot.frame_rate,
                self.sources[asset.bin_id],
                self.cache,
                self.provider,
                self.media,
            )

    def suggest(self, **changes):
        args = dict(
            snapshot=self.snapshot,
            request_id="cues-request",
            raw_spec=self.spec,
            sources=self.sources,
            cache=self.cache,
            provider=self.provider,
            media=self.media,
            progress=Mock(),
        )
        return suggest_sounds(**(args | changes))

    def test_only_cached_selected_evidence_is_used_with_native_intervals(self):
        progress = Mock()
        result = self.suggest(progress=progress)
        self.assertEqual(result["cached_assets"], 3)
        self.assertEqual(result["unavailable_assets"], 0)
        self.assertEqual([c["start_frame"] for c in result["cues"]], [0, 100])
        self.assertEqual([c["duration_frames"] for c in result["cues"]], [50, 50])
        self.assertEqual(
            result["cues"][0]["source_sha256"],
            hashlib.sha256(Path(self.sources["1"]).read_bytes()).hexdigest(),
        )
        self.assertEqual(
            result["cues"][0]["reason"], "Visible small coastal waves beside a walkway"
        )
        self.assertIn("SILENT", self.provider.generate.call_args.args[0][0]["text"])
        self.assertEqual(self.provider.generate.call_count, 1)
        self.media.video.assert_not_called()
        self.assertEqual(progress.call_args.args, (3, 3, 3))

    def test_missing_stale_or_invalid_cache_is_never_reanalyzed(self):
        Path(self.sources["1"]).write_bytes(b"changed")
        self.provider.generate.return_value = dict(
            cues=[dict(instance_id="shot-1", category="wind")]
        )
        result = self.suggest()
        self.assertEqual(result["cached_assets"], 2)
        self.assertEqual(result["unavailable_assets"], 1)
        self.media.video.assert_not_called()
        for file in self.cache.glob("*.json"):
            file.write_bytes(b"not valid cache")
        self.provider.generate.reset_mock()
        with self.assertRaises(AssistantError) as error:
            self.suggest()
        self.assertEqual(error.exception.code, "analysis_unavailable")
        self.provider.generate.assert_not_called()
        self.media.video.assert_not_called()

    def test_empty_suggestions_and_unavailable_audio_space_finish_without_generation(
        self,
    ):
        self.provider.generate.return_value = dict(cues=[])
        self.assertEqual(self.suggest()["cues"], [])
        self.provider.generate.reset_mock()
        occupied = self.snapshot.occupied + (OccupiedRange("music", "audio", 0, 300),)
        self.assertEqual(
            self.suggest(snapshot=replace(self.snapshot, occupied=occupied))["cues"], []
        )
        self.provider.generate.assert_not_called()

    def test_invented_ids_categories_times_repeats_and_excess_cues_are_rejected(self):
        for raw in [
            dict(cues=[dict(instance_id="invented", category="waves")]),
            dict(cues=[dict(instance_id="shot-0", category="voices")]),
            dict(cues=[dict(instance_id="shot-0", category="waves", start_frame=100)]),
            dict(cues=[dict(instance_id="shot-0", category="waves")] * 2),
            dict(
                cues=[
                    dict(instance_id="shot-0", category="waves"),
                    dict(instance_id="shot-1", category="waves"),
                ]
            ),
            dict(cues=[dict(instance_id="shot-0", category="waves")] * 4),
            dict(cues="invalid"),
        ]:
            self.provider.generate.return_value = raw
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                self.suggest()

    def test_clip_context_bounds_and_source_map_are_exact(self):
        for change in [
            dict(instance_id="unknown"),
            dict(bin_id="unknown"),
            dict(in_frame=2),
            dict(start_frame=1),
            dict(duration_frames=True),
            dict(extra=1),
        ]:
            clips = [dict(self.clips[0], **change), *self.clips[1:]]
            with self.subTest(change=change), self.assertRaises(ValueError):
                cue_spec(self.snapshot, dict(clips=clips))
        for sources in [
            dict(self.sources, extra="/unused.mp4"),
            {},
            dict(self.sources, **{"1": 123}),
        ]:
            with self.assertRaises(AssistantError):
                self.suggest(sources=sources)

    def test_effects_fit_usable_source_overlap_and_fractional_frame_rate(self):
        rate = FrameRate(30000, 1001)
        snapshot = replace(self.snapshot, frame_rate=rate)
        self.provider.generate.return_value = dict(
            windows=[
                dict(
                    window_id=0,
                    description="Visible water",
                    role="detail",
                    quality=80,
                    trim_in=12,
                    trim_out=70,
                )
            ]
        )
        for asset in self.assets:
            analyze(
                asset,
                rate,
                self.sources[asset.bin_id],
                self.cache,
                self.provider,
                self.media,
            )
        self.provider.generate.return_value = dict(
            cues=[dict(instance_id="shot-0", category="water")]
        )
        result = self.suggest(snapshot=snapshot)
        self.assertEqual(result["cues"][0]["start_frame"], 12)
        self.assertEqual(result["cues"][0]["duration_frames"], 58)
        self.assertLessEqual(result["cues"][0]["duration_frames"] * 1001, 2 * 30000)

    def test_low_quality_or_too_short_evidence_and_stop_make_no_provider_call(self):
        for file in self.cache.glob("*.json"):
            import json

            data = json.loads(file.read_bytes())
            data["windows"][0]["quality"] = 20
            file.write_text(json.dumps(data))
        self.assertEqual(self.suggest()["cues"], [])
        self.provider.generate.assert_not_called()
        self.media.cancel()
        with self.assertRaises(AssistantError) as error:
            self.suggest()
        self.assertEqual(error.exception.code, "cancelled")

    def test_source_read_failures_do_not_become_editorial_evidence(self):
        Path(self.sources["1"]).unlink()
        with self.assertRaises(AssistantError):
            self.suggest()
        self.provider.generate.assert_not_called()
