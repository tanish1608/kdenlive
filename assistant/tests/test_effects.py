# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import json
import os
import tempfile
import unittest
import urllib.error
import wave
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.effects import ElevenLabsEffects, compose_effect, effect_spec
from montaz_assistant.gemini import AssistantError
from montaz_assistant.planning import MediaTools
from test_music import audio_bytes


class EffectTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.snapshot = Snapshot(
            "project",
            "sequence",
            4,
            FrameRate(25),
            (),
            (Track("video", "video"), Track("audio", "audio")),
            (OccupiedRange("shot", "video", 20, 100),),
        )
        self.spec = dict(
            path=str(self.root / "sound.wav"),
            track_id="audio",
            start_frame=30,
            duration_frames=50,
            instruction="Gentle coastal waves",
        )
        self.provider = Mock(model="eleven_text_to_sound_v2")
        self.provider.compose.return_value = audio_bytes(2)
        self.media = MediaTools()

    def compose(self, spec=None):
        return compose_effect(
            self.snapshot,
            "effect-request",
            spec or self.spec,
            self.provider,
            self.media,
            Mock(),
        )

    def test_interval_description_track_and_duration_preflight(self):
        self.assertEqual(effect_spec(self.snapshot, self.spec), self.spec)
        for change in [
            dict(path="relative.wav"),
            dict(path="/tmp/out.mp3"),
            dict(instruction=" "),
            dict(instruction="a" * 1001),
            dict(recipe="postcard"),
            dict(track_id="video"),
            dict(start_frame=19),
            dict(start_frame=71),
            dict(duration_frames=12),
            dict(duration_frames=True),
            dict(duration_frames=251),
        ]:
            with (
                self.subTest(change=change),
                self.assertRaises((AssistantError, ValueError)),
            ):
                effect_spec(self.snapshot, dict(self.spec, **change))
        for duration, valid in [(14, False), (15, True), (299, True), (300, False)]:
            snapshot = replace(
                self.snapshot,
                frame_rate=FrameRate(30000, 1001),
                occupied=(OccupiedRange("shot", "video", 20, 400),),
            )
            if valid:
                effect_spec(snapshot, dict(self.spec, duration_frames=duration))
            else:
                with self.assertRaises(AssistantError):
                    effect_spec(snapshot, dict(self.spec, duration_frames=duration))

    def test_locked_occupied_and_gapped_positions_do_not_generate(self):
        for snapshot in [
            replace(
                self.snapshot,
                tracks=(Track("video", "video"), Track("audio", "audio", True)),
            ),
            replace(
                self.snapshot,
                occupied=self.snapshot.occupied
                + (OccupiedRange("music", "audio", 79, 5),),
            ),
            replace(
                self.snapshot,
                occupied=(
                    OccupiedRange("a", "video", 20, 20),
                    OccupiedRange("b", "video", 50, 70),
                ),
            ),
        ]:
            with self.assertRaises(AssistantError):
                compose_effect(
                    snapshot, "request", self.spec, self.provider, self.media, Mock()
                )
        self.provider.compose.assert_not_called()

    def test_provider_request_limits_errors_and_configuration(self):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = b"audio"
        with patch("urllib.request.urlopen", return_value=response) as request:
            self.assertEqual(
                ElevenLabsEffects("private-key").compose("waves", 2), b"audio"
            )
        sent = request.call_args.args[0]
        self.assertEqual(sent.get_header("Xi-api-key"), "private-key")
        self.assertEqual(
            json.loads(sent.data),
            dict(
                text="waves",
                duration_seconds=2,
                model_id="eleven_text_to_sound_v2",
                loop=False,
                prompt_influence=0.3,
            ),
        )
        self.assertEqual(request.call_args.kwargs["timeout"], 60)
        for status, code in [
            (401, "effect_auth"),
            (403, "effect_auth"),
            (422, "effect_request_rejected"),
            (429, "effect_unavailable"),
        ]:
            with (
                patch(
                    "urllib.request.urlopen",
                    side_effect=urllib.error.HTTPError(
                        "url", status, "private", {}, None
                    ),
                ),
                self.assertRaises(AssistantError) as error,
            ):
                ElevenLabsEffects("key").compose("waves", 2)
            self.assertEqual(error.exception.code, code)
        with (
            patch("urllib.request.urlopen", side_effect=TimeoutError()),
            self.assertRaises(AssistantError) as error,
        ):
            ElevenLabsEffects("key").compose("waves", 2)
        self.assertEqual(error.exception.code, "effect_timeout")
        with patch.dict(
            os.environ,
            {
                "MONTAZ_ASSISTANT_CONFIG": str(self.root / "missing.json"),
                "ELEVENLABS_API_KEY": "",
            },
        ):
            with self.assertRaises(AssistantError) as error:
                ElevenLabsEffects.configured()
        self.assertEqual(error.exception.code, "missing_effect_key")

    def test_exact_audio_soft_edges_quiet_peak_and_prepared_reuse(self):
        result = self.compose()
        self.assertFalse(result["cached"])
        self.assertEqual(result["accents"], [])
        self.assertEqual(result["start_frame"], 30)
        with wave.open(self.spec["path"]) as audio:
            self.assertEqual(audio.getnframes(), 96000)
            self.assertEqual(audio.getparams()[:3], (2, 2, 48000))
            from array import array

            samples = array("h", audio.readframes(audio.getnframes()))
        self.assertLess(max(map(abs, samples)), 32768 * 0.51)
        self.assertLess(max(map(abs, samples[:20])), max(map(abs, samples[500:1500])))
        second = self.compose(dict(self.spec, path=str(self.root / "second.wav")))
        self.assertTrue(second["cached"])
        self.assertEqual(second["sha256"], result["sha256"])
        self.provider.compose.assert_called_once()
        self.assertEqual(Path(self.spec["path"]).stat().st_mode & 0o777, 0o600)

    def test_raw_audio_survives_preparation_failure_without_regeneration(self):
        with patch.object(
            self.media, "_output", side_effect=AssistantError("media_timeout")
        ):
            with self.assertRaises(AssistantError):
                self.compose()
        self.assertEqual(len(list(self.root.glob("effect-*.mp3"))), 1)
        self.assertFalse(Path(self.spec["path"]).exists())
        self.assertTrue(self.compose()["cached"])
        self.provider.compose.assert_called_once()

    def test_silence_shortfall_and_invalid_audio_cannot_be_applied(self):
        for audio in [audio_bytes(2, silent=True), audio_bytes(1), b"not audio"]:
            self.provider.compose.return_value = audio
            # A unique description makes each rejected sample an independent cache entry.
            spec = dict(self.spec, instruction=str(len(audio)))
            with self.assertRaises(AssistantError):
                self.compose(spec)
            self.assertFalse(Path(spec["path"]).exists())

    def test_short_or_silent_effect_audio_is_regenerated_on_request(self):
        for index, bad in enumerate((audio_bytes(1), audio_bytes(2, silent=True))):
            self.provider.compose.reset_mock()
            self.provider.compose.side_effect = [bad, audio_bytes(2)]
            spec = dict(self.spec, instruction=f"retry {index}")
            with self.subTest(index=index):
                with self.assertRaises(AssistantError):
                    self.compose(dict(spec, path=str(self.root / f"bad{index}.wav")))
                result = self.compose(dict(spec, path=str(self.root / f"good{index}.wav")))
                self.assertFalse(result["cached"])
                self.assertEqual(self.provider.compose.call_count, 2)

    def test_stop_and_existing_destination_do_not_generate(self):
        Path(self.spec["path"]).write_bytes(b"preserved")
        with self.assertRaises(AssistantError):
            self.compose()
        self.assertEqual(Path(self.spec["path"]).read_bytes(), b"preserved")
        self.media.cancel()
        with self.assertRaises(AssistantError):
            self.compose(dict(self.spec, path=str(self.root / "stop.wav")))
        self.provider.compose.assert_not_called()

    def test_stop_after_generation_retains_raw_audio(self):
        def generation(*args):
            self.media.cancel()
            return audio_bytes(2)

        self.provider.compose.side_effect = generation
        with self.assertRaises(AssistantError):
            self.compose()
        self.assertEqual(len(list(self.root.glob("effect-*.mp3"))), 1)
        self.assertFalse(Path(self.spec["path"]).exists())

    def test_actual_worker_emits_ordered_effect_progress_and_bound_result(self):
        import io
        from types import SimpleNamespace
        from montaz_assistant.protocol import encode, to_wire
        from montaz_assistant.worker import main

        request = dict(
            version=1,
            type="compose_effect",
            request_id="effect-worker",
            snapshot=to_wire(self.snapshot),
            effect=self.spec,
        )
        output = io.BytesIO()
        with (
            patch("sys.stdin", SimpleNamespace(buffer=io.BytesIO(encode(request)))),
            patch("sys.stdout", SimpleNamespace(buffer=output)),
            patch("signal.signal"),
            patch(
                "montaz_assistant.worker.ElevenLabsEffects.configured",
                return_value=self.provider,
            ),
        ):
            self.assertEqual(main(), 0)
        messages = [json.loads(line) for line in output.getvalue().splitlines()]
        events = [m for m in messages if m["type"] == "event"]
        self.assertEqual([m["sequence"] for m in events], [1, 2, 3, 4])
        self.assertEqual(events[1]["stage"], "effect")
        self.assertEqual(
            [m["phase"] for m in messages if m["type"] == "music_progress"],
            ["generation", "measurement"],
        )
        self.assertEqual(messages[-1]["type"], "soundtrack")
        self.assertEqual(messages[-1]["soundtrack"]["path"], self.spec["path"])
        self.assertEqual(messages[-1]["soundtrack"]["duration_frames"], 50)
