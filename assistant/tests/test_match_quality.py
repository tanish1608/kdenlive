# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Regression tests for the native stability acceptance criteria."""

import importlib.util
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "check_match_quality", Path(__file__).resolve().parents[1] / "tools/check_match_quality.py"
)
QUALITY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(QUALITY)


def result(*, scale_change=0.0, decoded_jitter=0.004, outgoing=(1.3, 1.0), size_mean=0.02, size_max=0.03):
    return {
        "native_check": "pass",
        "transform": {"scale_change": scale_change},
        "decoded": {"size_change_rms": decoded_jitter},
        "outgoing_scale": outgoing,
        "truth_sweep": {"models": {"stable": {"cut_size_mean": size_mean, "cut_size_max": size_max}}, "rejected": {"helper": 0}},
    }


def comparison(*, helper_rejections=0, scale_change=0.0, decoded_jitter=0.004, zoom_size=0.02):
    stable = result(scale_change=scale_change, decoded_jitter=decoded_jitter, size_mean=zoom_size)
    return {
        "constant": {
            "models": {"stable": stable},
            "truth_sweep": {
                "rejected": {"helper": 0},
                "models": {"stable": {"cut_center_max": 0.004, "cut_center_mean": 0.002, "cut_size_mean": 0.01}},
            },
        },
        "zoom": {
            "models": {"stable": stable},
            "truth_sweep": {
                "rejected": {"helper": helper_rejections},
                "models": {"stable": {"cut_size_mean": zoom_size, "cut_size_max": 0.03}},
            },
        },
    }


class MatchQualityTests(unittest.TestCase):
    def test_absolute_zoom_checks_accept_verified_measurements(self):
        QUALITY.validate_stability(comparison())

    def test_old_direction_only_result_cannot_pass_when_helper_missed_windows(self):
        with self.assertRaisesRegex(RuntimeError, "rejected"):
            QUALITY.validate_stability(comparison(helper_rejections=6))

    def test_constant_output_has_absolute_jitter_limit(self):
        with self.assertRaisesRegex(RuntimeError, "jitter"):
            QUALITY.validate_stability(comparison(decoded_jitter=0.009))


if __name__ == "__main__":
    unittest.main()
