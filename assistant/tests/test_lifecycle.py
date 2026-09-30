# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors

import unittest

from montaz_assistant.lifecycle import TERMINAL, Run, Status


class RunTests(unittest.TestCase):
    def test_silent_footage_skips_captions_and_finishes_without_spinner(self):
        run = Run("run-a", ("analysis", "captions"))
        run.transition("analysis", Status.RUNNING)
        run.transition("analysis", Status.COMPLETED)
        run.transition("captions", Status.SKIPPED, "No verified speech in selected audio")
        run.finish()
        self.assertEqual(run.status, Status.COMPLETED)
        self.assertTrue(all(s in TERMINAL for s in run.stages.values()))
        self.assertEqual([e.sequence for e in run.events], list(range(1, 6)))

    def test_critic_parse_error_is_failed_not_approved(self):
        run = Run("run-a", ("critic", "render"))
        run.transition("critic", Status.RUNNING)
        run.transition("critic", Status.FAILED, "Invalid provider response")
        self.assertEqual(run.status, Status.FAILED)
        self.assertEqual(run.stages["render"], Status.SKIPPED)
        self.assertFalse(run.finish())

    def test_optional_review_unavailable_is_explicitly_skipped(self):
        run = Run("run-a", ("critic",))
        run.transition("critic", Status.RUNNING)
        run.transition("critic", Status.SKIPPED, "Review unavailable; user review required")
        self.assertEqual(run.events[-1].status, Status.SKIPPED)

    def test_stop_settles_running_and_queued_work_preserving_completed_stages(self):
        run = Run("run-a", ("analysis", "planner", "music", "captions"))
        run.transition("analysis", Status.RUNNING)
        run.transition("analysis", Status.COMPLETED)
        run.transition("planner", Status.RUNNING)
        run.transition("music", Status.RUNNING)
        run.cancel()
        self.assertEqual(run.stages["analysis"], Status.COMPLETED)
        self.assertEqual(run.stages["planner"], Status.CANCELLED)
        self.assertEqual(run.stages["music"], Status.CANCELLED)
        self.assertEqual(run.stages["captions"], Status.SKIPPED)
        self.assertEqual(run.status, Status.CANCELLED)

    def test_late_completion_cannot_resurrect_cancelled_run(self):
        run = Run("run-a", ("planner",))
        run.transition("planner", Status.RUNNING)
        run.cancel()
        before = run.events
        self.assertFalse(run.transition("planner", Status.COMPLETED))
        self.assertFalse(run.cancel())
        self.assertEqual(run.events, before)

    def test_stage_failure_settles_parallel_work(self):
        run = Run("run-a", ("speech", "vision"))
        run.transition("speech", Status.RUNNING)
        run.transition("vision", Status.RUNNING)
        run.transition("vision", Status.FAILED, "Model unavailable")
        self.assertEqual(run.stages["speech"], Status.CANCELLED)

    def test_run_cannot_finish_with_work_pending(self):
        run = Run("run-a", ("planner",))
        with self.assertRaisesRegex(ValueError, "unfinished"):
            run.finish()

    def test_failed_or_skipped_stage_requires_reason(self):
        run = Run("run-a", ("planner",))
        with self.assertRaisesRegex(ValueError, "reason"):
            run.transition("planner", Status.SKIPPED)

    def test_observers_cannot_mutate_stage_status(self):
        run = Run("run-a", ("planner",))
        with self.assertRaises(TypeError):
            run.stages["planner"] = Status.COMPLETED

    def test_raw_status_strings_cannot_enter_event_ledger(self):
        run = Run("run-a", ("planner",))
        with self.assertRaisesRegex(ValueError, "Status value"):
            run.transition("planner", "running")
        self.assertEqual(run.stages["planner"], Status.PENDING)

    def test_completed_stage_cannot_start_again(self):
        run = Run("run-a", ("planner",))
        run.transition("planner", Status.RUNNING)
        run.transition("planner", Status.COMPLETED)
        with self.assertRaisesRegex(ValueError, "Invalid transition"):
            run.transition("planner", Status.RUNNING)


if __name__ == "__main__":
    unittest.main()
