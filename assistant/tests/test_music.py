# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
import hashlib
import io
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import wave
from array import array
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from montaz_assistant.contracts import FrameRate, OccupiedRange, Snapshot, Track
from montaz_assistant.gemini import AssistantError
from montaz_assistant.music import ElevenLabs, compose_music, music_spec
from montaz_assistant.planning import MediaTools
from montaz_assistant.protocol import decode, encode, to_wire


def audio_bytes(seconds=4, silent=False):
    samples = array("h")
    for i in range(round(seconds * 48000)):
        # Synthetic pulses exercise energy measurements and edge fades.
        amplitude = 0 if silent else (10000 if i % 24000 < 4800 else 800)
        value = round(amplitude * math.sin(2 * math.pi * 440 * i / 48000))
        samples.extend((value, value))
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
        audio.writeframes(samples.tobytes())
    return output.getvalue()


class MusicTests(unittest.TestCase):
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
            path=str(self.root / "generated.wav"),
            track_id="audio",
            start_frame=20,
            duration_frames=100,
            recipe="postcard",
            instruction="A relaxed coastal trip",
        )
        self.provider = Mock(model="music_v2_5")
        self.media = MediaTools()

    def compose(self, spec=None, progress=None):
        return compose_music(
            self.snapshot,
            "music-request",
            spec or self.spec,
            self.provider,
            self.media,
            progress or Mock(),
        )

    def test_locked_occupied_video_and_unknown_audio_tracks_are_rejected(self):
        for snapshot, spec in (
            (
                replace(
                    self.snapshot,
                    tracks=(Track("video", "video"), Track("audio", "audio", True)),
                ),
                self.spec,
            ),
            (
                replace(
                    self.snapshot,
                    occupied=self.snapshot.occupied + (OccupiedRange("sound", "audio", 119, 20),),
                ),
                self.spec,
            ),
            (self.snapshot, dict(self.spec, track_id="video")),
            (self.snapshot, dict(self.spec, track_id="unknown")),
        ):
            with (
                self.subTest(snapshot=snapshot, spec=spec),
                self.assertRaises(AssistantError),
            ):
                music_spec(snapshot, spec)
        adjacent = replace(
            self.snapshot,
            occupied=self.snapshot.occupied + (OccupiedRange("sound", "audio", 120, 20),),
        )
        self.assertEqual(music_spec(adjacent, self.spec), self.spec)

    def test_output_path_exact_visual_span_and_duration_limits(self):
        for change in (
            dict(path="relative.wav"),
            dict(path="/tmp/out.mp3"),
            dict(start_frame=True),
            dict(start_frame=0),
            dict(duration_frames=99),
            dict(recipe="unknown"),
            dict(instruction="x" * 2001),
            dict(extra="field"),
        ):
            with (
                self.subTest(change=change),
                self.assertRaises((AssistantError, ValueError)),
            ):
                music_spec(self.snapshot, dict(self.spec, **change))
        # At 29.97 fps 89 frames is under 3 s and 17983 frames exceeds 600 s.
        for duration, valid in ((89, False), (90, True), (17982, True), (17983, False)):
            snapshot = replace(
                self.snapshot,
                frame_rate=FrameRate(30000, 1001),
                occupied=(OccupiedRange("shot", "video", 20, duration),),
            )
            spec = dict(self.spec, duration_frames=duration)
            if valid:
                self.assertEqual(music_spec(snapshot, spec), spec)
            else:
                with self.assertRaises(AssistantError):
                    music_spec(snapshot, spec)

    def test_existing_destination_and_cancelled_request_never_call_provider(self):
        self.root.joinpath("generated.wav").write_bytes(b"preserve me")
        with self.assertRaises(AssistantError):
            self.compose()
        self.provider.compose.assert_not_called()
        self.assertEqual(self.root.joinpath("generated.wav").read_bytes(), b"preserve me")
        self.media.cancel()
        with self.assertRaises(AssistantError) as caught:
            self.compose(dict(self.spec, path=str(self.root / "cancelled.wav")))
        self.assertEqual(caught.exception.code, "cancelled")
        self.provider.compose.assert_not_called()

    def test_provider_uses_instrumental_duration_model_and_private_header(self):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = b"audio"
        with patch("urllib.request.urlopen", return_value=response) as request:
            self.assertEqual(ElevenLabs("private-key").compose("warm travel", 4000), b"audio")
        actual = request.call_args.args[0]
        self.assertEqual(actual.get_header("Xi-api-key"), "private-key")
        self.assertEqual(
            json.loads(actual.data),
            dict(
                prompt="warm travel",
                music_length_ms=4000,
                model_id="music_v2_5",
                force_instrumental=True,
            ),
        )
        self.assertEqual(request.call_args.kwargs["timeout"], 90)

    def test_auth_timeout_and_empty_audio_have_bounded_errors(self):
        for fault, code in (
            (
                urllib.error.HTTPError("url", 401, "secret provider response", {}, None),
                "music_auth",
            ),
            (
                urllib.error.HTTPError("url", 422, "secret provider response", {}, None),
                "music_request_rejected",
            ),
            (
                urllib.error.HTTPError("url", 429, "secret provider response", {}, None),
                "music_unavailable",
            ),
            (TimeoutError("secret detail"), "music_timeout"),
            (urllib.error.URLError(TimeoutError("secret detail")), "music_timeout"),
        ):
            with (
                self.subTest(code=code),
                patch("urllib.request.urlopen", side_effect=fault),
            ):
                with self.assertRaises(AssistantError) as caught:
                    ElevenLabs("key").compose("prompt", 4000)
                self.assertEqual(caught.exception.code, code)
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        for data in (b"", b"x" * 11):
            response.read.return_value = data
            with (
                patch("urllib.request.urlopen", return_value=response),
                patch("montaz_assistant.music.MAX_AUDIO_BYTES", 10),
            ):
                with self.assertRaises(AssistantError) as caught:
                    ElevenLabs("key").compose("prompt", 4000)
                self.assertEqual(caught.exception.code, "invalid_music_audio")

    def test_settings_fail_without_key_and_reject_non_object(self):
        config = self.root / "settings.json"
        with patch.dict(
            os.environ,
            {"ELEVENLABS_API_KEY": "", "MONTAZ_ASSISTANT_CONFIG": str(config)},
        ):
            with self.assertRaises(AssistantError) as caught:
                ElevenLabs.configured()
            self.assertEqual(caught.exception.code, "missing_music_key")
            config.write_text("[]")
            with self.assertRaises(AssistantError) as caught:
                ElevenLabs.configured()
            self.assertEqual(caught.exception.code, "invalid_settings")

    def test_actual_worker_missing_key_settles_music_without_an_audio_result(self):
        environment = dict(
            os.environ,
            ELEVENLABS_API_KEY="",
            MONTAZ_ASSISTANT_CONFIG=str(self.root / "missing.json"),
        )
        result = subprocess.run(
            [sys.executable, "-u", "-m", "montaz_assistant.worker"],
            input=encode(
                dict(
                    version=1,
                    type="compose_music",
                    request_id="request",
                    snapshot=to_wire(self.snapshot),
                    music=self.spec,
                )
            ),
            capture_output=True,
            env=environment,
            timeout=5,
        )
        messages = [decode(line) for line in result.stdout.splitlines()]
        self.assertEqual(result.returncode, 2)
        self.assertEqual(messages[-1], dict(version=1, type="error", code="missing_music_key"))
        self.assertEqual(messages[-2]["stage"], "music")
        self.assertFalse(any(x["type"] == "soundtrack" for x in messages))
        self.assertNotIn(b"coastal", result.stdout + result.stderr)
        self.assertFalse(Path(self.spec["path"]).exists())

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_real_audio_preparation_exact_frames_fades_and_cache_reuse(self):
        self.provider.compose.return_value = audio_bytes()
        progress = Mock()
        result = self.compose(progress=progress)
        path = Path(result["path"])
        self.assertEqual(result["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(result["duration_frames"], 100)
        self.assertFalse(result["cached"])
        self.assertGreater(len(result["accents"]), 0)
        self.assertTrue(all(0 < x < 100 for x in result["accents"]))
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(progress.call_args_list[0].args, ("generation", False))
        self.assertEqual(progress.call_args_list[1].args, ("measurement", False))
        with wave.open(str(path)) as prepared:
            self.assertEqual(prepared.getparams()[:4], (2, 2, 48000, 192000))
            samples = array("h", prepared.readframes(prepared.getnframes()))
            self.assertLess(
                max(abs(x) for x in samples[:960]),
                max(abs(x) for x in samples[24000:25000]),
            )
            self.assertLess(abs(samples[-1]), 10)
        second = self.compose(dict(self.spec, path=str(self.root / "reuse.wav")))
        self.assertTrue(second["cached"])
        self.assertEqual(result["sha256"], second["sha256"])
        self.provider.compose.assert_called_once()

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_short_or_silent_audio_never_produces_reviewable_output(self):
        for index, audio in enumerate((audio_bytes(seconds=3.9), audio_bytes(silent=True))):
            self.provider.compose.return_value = audio
            with self.assertRaises(AssistantError):
                self.compose(dict(self.spec, instruction=f"separate test {index}"))
            self.assertFalse(Path(self.spec["path"]).exists())

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_short_or_silent_provider_audio_is_not_reused_by_a_later_request(self):
        # Reusing such audio could never pass; the next explicit request
        # generates again instead of failing forever without a provider call.
        for index, bad in enumerate((audio_bytes(seconds=3.9), audio_bytes(silent=True))):
            self.provider.compose.reset_mock()
            self.provider.compose.side_effect = [bad, audio_bytes()]
            spec = dict(self.spec, instruction=f"retry {index}")
            with self.subTest(index=index):
                with self.assertRaises(AssistantError) as caught:
                    self.compose(dict(spec, path=str(self.root / f"bad{index}.wav")))
                self.assertEqual(caught.exception.code, "invalid_music_audio")
                result = self.compose(dict(spec, path=str(self.root / f"good{index}.wav")))
                self.assertFalse(result["cached"])
                self.assertEqual(self.provider.compose.call_count, 2)

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_one_frame_codec_shortfall_is_padded_but_not_stretched(self):
        self.provider.compose.return_value = audio_bytes(seconds=3.98)
        result = self.compose()
        with wave.open(result["path"]) as prepared:
            self.assertEqual(prepared.getnframes(), 192000)
            prepared.setpos(191100)
            self.assertEqual(set(prepared.readframes(900)), {0})

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_encoding_failure_reuses_paid_raw_response(self):
        self.provider.compose.return_value = audio_bytes()
        with patch.object(self.media, "_output", side_effect=AssistantError("media_unreadable")):
            with self.assertRaises(AssistantError):
                self.compose()
        self.assertFalse(Path(self.spec["path"]).exists())
        result = self.compose()
        self.assertTrue(result["cached"])
        self.provider.compose.assert_called_once()

    @unittest.skipUnless(shutil.which(os.environ.get("MONTAZ_FFMPEG", "ffmpeg")), "FFmpeg required")
    def test_stop_after_preparation_keeps_music_and_can_reuse_without_charging(self):
        self.provider.compose.return_value = audio_bytes()

        def stop_at_measurement(phase, cached):
            if phase == "measurement":
                self.media.cancel()

        with self.assertRaises(AssistantError) as caught:
            self.compose(progress=stop_at_measurement)
        self.assertEqual(caught.exception.code, "cancelled")
        self.assertFalse(Path(self.spec["path"]).exists())
        self.media = MediaTools()
        result = self.compose()
        self.assertTrue(result["cached"])
        self.provider.compose.assert_called_once()


if __name__ == "__main__":
    unittest.main()
