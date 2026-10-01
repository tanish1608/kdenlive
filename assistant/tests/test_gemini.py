# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Provider failures stay precise, terminal, and free of private response text."""

import json
import unittest
from unittest.mock import Mock, patch
from urllib.error import HTTPError

from montaz_assistant.gemini import AssistantError, Gemini


class GeminiTests(unittest.TestCase):
    def generate(self, payload):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=None)
        response.read.return_value = json.dumps(payload).encode()
        with patch("urllib.request.urlopen", return_value=response) as request:
            try:
                return Gemini("private-key").generate([{"text": "private brief"}])
            finally:
                self.assertEqual(request.call_count, 1)

    def test_partial_answers_and_thoughts_are_never_accepted(self):
        for finish, code in (
            ("MAX_TOKENS", "provider_output_limit"),
            ("SAFETY", "provider_blocked"),
            ("RECITATION", "provider_blocked"),
            ("BLOCKLIST", "provider_blocked"),
            ("PROHIBITED_CONTENT", "provider_blocked"),
            ("SPII", "provider_blocked"),
            ("private unknown reason", "invalid_model_response"),
        ):
            with self.subTest(finish=finish):
                with self.assertRaises(AssistantError) as failure:
                    self.generate({"candidates": [{"finishReason": finish, "content": {
                        "parts": [{"text": '{"valid":true}'}]
                    }}], "finishMessage": "private-key"})
                self.assertEqual(str(failure.exception), code)
        with self.assertRaisesRegex(AssistantError, "provider_empty_response"):
            self.generate({"candidates": [{"finishReason": "STOP", "content": {
                "parts": [{"thought": True, "text": '{"valid":true}'}]
            }}]})

    def test_empty_and_blocked_requests_have_distinct_diagnoses(self):
        for payload, code in (
            ({"candidates": []}, "provider_empty_response"),
            ({"promptFeedback": {"blockReason": "SAFETY"}}, "provider_blocked"),
            ({"candidates": [None]}, "invalid_model_response"),
            ({"candidates": [{"finishReason": []}]}, "invalid_model_response"),
            ({"candidates": [{"finishReason": "STOP", "content": {
                "parts": [{"text": "private malformed answer"}]
            }}]}, "invalid_model_response"),
        ):
            with self.subTest(payload=payload), self.assertRaises(AssistantError) as failure:
                self.generate(payload)
            self.assertEqual(str(failure.exception), code)

    def test_bad_requests_are_not_reported_as_missing_credentials(self):
        for status, code in ((400, "provider_request_rejected"), (401, "provider_auth"),
                             (403, "provider_auth"), (429, "provider_unavailable")):
            with self.subTest(status=status), patch("urllib.request.urlopen", side_effect=
                HTTPError("private-key", status, "private response", {}, None)):
                with self.assertRaises(AssistantError) as failure:
                    Gemini("private-key").generate([])
                self.assertEqual(str(failure.exception), code)


if __name__ == "__main__":
    unittest.main()
