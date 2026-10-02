# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Private QA: measure beat and caption behaviour on generated fixtures.

Bounded and reproducible: fixed seeds, at most 12 s of 8 kHz audio per fixture
and about 30 fixtures in total. No provider, network or real speech: decoder and
provider boundaries are mocked exactly as in the regression tests. Writes a
JSON report and mono WAV renders (for listening) to --output-dir, outside Git.

  python3 assistant/tools/check_audio_quality.py --output-dir /private/qa/audio
"""

import argparse
import json
import random
import sys
import time
import unittest
import wave
from array import array
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    sys.path[:0] = [str(source / "assistant/src"), str(source / "assistant/tests")]
    from test_audio_quality_regressions import grid, render, score, syncopated, tone
    from montaz_assistant.beats import estimate, onset_envelope
    from montaz_assistant.captions import is_silent
    from montaz_assistant.gemini import AssistantError
    from montaz_assistant.planning import MediaTools

    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()

    def listen(name: str, raw: bytes) -> None:
        bands = array("h")
        bands.frombytes(raw)
        mono = array(
            "h",
            (
                max(-32767, min(32767, bands[i] + bands[i + 1] + bands[i + 2]))
                for i in range(0, len(bands), 3)
            ),
        )
        with wave.open(str(root / f"{name}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(8000)
            w.writeframes(mono.tobytes())

    def measure(name, raw, truth, fix_start=False):
        try:
            envelope = onset_envelope(raw, MediaTools())
        except AssistantError as exc:
            return dict(fixture=name, mode="unavailable", reason=exc.code)
        if fix_start:
            envelope[0] = 0.0  # Simulates the proposed onset_envelope fix.
        result = estimate(envelope, MediaTools())
        row = dict(
            fixture=name,
            mode=result["mode"],
            reason=result["reason"],
            bpm=result["bpm"],
            periodicity=result["periodicity"],
            support=result["support"],
            beats=len(result["beats"]),
        )
        if result["mode"] == "beats" and truth:
            precision, recall, error = score(result, truth)
            errors = [min(abs(b / 100 - t) for t in truth) for b in result["beats"]]
            row.update(
                precision=round(precision, 3),
                recall=round(recall, 3),
                mean_error_ms=round(1000 * error, 1),
                max_error_ms=round(1000 * max(errors), 1),
            )
        return row

    fixtures = {}
    for bpm in (72, 97.5, 120, 128, 160):
        pulse = grid(bpm, 12)
        fixtures[f"steady-{bpm}"] = (
            render(
                12,
                [(t, "kick", 16000) for t in pulse]
                + [(t + 30 / bpm, "hat", 4000) for t in pulse],
            ),
            pulse,
        )
    events, pulse = syncopated()
    fixtures["syncopated-120"] = (render(12, events), pulse)
    fixtures["half-time-70-hats-140"] = (
        render(
            12,
            [(t, "kick", 16000) for t in grid(70, 12)]
            + [(t, "hat", 5000) for t in grid(140, 12)],
        ),
        grid(70, 12),
    )
    accelerando, t = [], 0.1
    while t < 11.9:
        accelerando.append(t)
        t += 60 / (100 + 30 * t / 12)
    fixtures["accelerando-100-130"] = (
        render(12, [(t, "kick", 16000) for t in accelerando]),
        accelerando,
    )
    fixtures["music-pad-only"] = (render(12, [], pad=6000), [])
    fixtures["noise-only"] = (render(12, [], noise=2000), [])
    fixtures["short-2.5s"] = (
        render(2.5, [(t, "kick", 16000) for t in grid(120, 2.5)]),
        grid(120, 2.5),
    )
    fixtures["digital-silence"] = (render(4, []), [])
    rows = []
    for name, (raw, truth) in fixtures.items():
        listen(name, raw)
        rows.append(measure(name, raw, truth))

    sweeps = dict(noise=[], jitter=[], pad=[])
    pulse = grid(120, 12)
    for sigma in (0, 500, 1000, 2000, 3000, 4000):
        raw = render(12, [(t, "kick", 16000) for t in pulse], noise=sigma)
        sweeps["noise"].append(
            dict(
                noise_sigma=sigma,
                **measure(f"noise-{sigma}", raw, pulse),
                proposed_fix=measure(f"noise-{sigma}", raw, pulse, fix_start=True),
            )
        )
    for jitter_ms in (0, 5, 10, 20, 30, 40):
        rng = random.Random(jitter_ms)
        shaken = [t + rng.uniform(-jitter_ms, jitter_ms) / 1000 for t in pulse]
        sweeps["jitter"].append(
            dict(
                jitter_ms=jitter_ms,
                **measure(
                    f"jitter-{jitter_ms}",
                    render(12, [(t, "kick", 16000) for t in shaken]),
                    shaken,
                ),
            )
        )
    pulse = grid(110, 12)
    for pad in (0, 1000, 2500, 5000, 8000):
        raw = render(12, [(t, "kick", 16000) for t in pulse], pad=pad)
        sweeps["pad"].append(
            dict(
                pad_peak=pad,
                current=measure(f"pad-{pad}", raw, pulse),
                proposed_fix=measure(f"pad-{pad}", raw, pulse, fix_start=True),
            )
        )

    gate = [
        dict(
            dbfs=d,
            noise_silent=is_silent(tone(d)),
            music_silent=is_silent(tone(d, "music")),
        )
        for d in (-90, -70, -62, -58, -50, -40, -20)
    ]

    bugs = []
    loader = unittest.defaultTestLoader
    for test in (
        "test_known_bug_music_audible_from_the_first_sample_hides_every_onset",
        "test_known_bug_sub_frame_segment_fails_the_whole_request",
        "test_known_bug_unmappable_answer_is_cached_and_never_retried",
    ):
        suite = loader.loadTestsFromName(
            f"test_audio_quality_regressions.{'Beat' if 'music' in test else 'Caption'}RegressionTests.{test}"
        )
        outcome = unittest.TestResult()
        suite.run(outcome)
        bugs.append(
            dict(
                test=test,
                reproduced=bool(outcome.expectedFailures),
                fixed=bool(outcome.unexpectedSuccesses),
            )
        )

    report = dict(
        beats=rows,
        sweeps=sweeps,
        caption_gate=gate,
        known_bugs=bugs,
        seconds=round(time.monotonic() - started, 1),
    )
    (root / "audio-quality-report.json").write_text(json.dumps(report, indent=2))
    for r in rows:
        extra = (
            f" P={r.get('precision')} R={r.get('recall')} err={r.get('mean_error_ms')}ms"
            if "precision" in r
            else ""
        )
        print(
            f"{r['fixture']:24} {r['mode']:11} {r['reason']:20} bpm={r.get('bpm', '-')}{extra}"
        )
    print(
        "noise",
        [
            (
                r["noise_sigma"],
                r["mode"],
                r["bpm"],
                r.get("recall"),
                "fix:",
                r["proposed_fix"]["mode"],
                r["proposed_fix"].get("recall"),
            )
            for r in sweeps["noise"]
        ],
    )
    print(
        "jitter",
        [(r["jitter_ms"], r["mode"], r.get("recall")) for r in sweeps["jitter"]],
    )
    print(
        "pad",
        [
            (s["pad_peak"], s["current"]["mode"], s["proposed_fix"]["mode"])
            for s in sweeps["pad"]
        ],
    )
    print("gate", [(g["dbfs"], g["noise_silent"]) for g in gate])
    print(
        "bugs",
        [
            (
                b["test"][15:60],
                "reproduced" if b["reproduced"] else "fixed" if b["fixed"] else "?",
            )
            for b in bugs
        ],
    )
    print(f"Report: {root / 'audio-quality-report.json'} ({report['seconds']} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
