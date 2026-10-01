# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Small bounded REST client; credentials never enter wire messages or logs."""

import json
import os
import re
import socket
import urllib.error
import urllib.request
from pathlib import Path

from .protocol import MAX_MESSAGE_BYTES, decode


class AssistantError(ValueError):
    def __init__(self, code: str):
        self.code = code
        super().__init__(code)


class Gemini:
    def __init__(self, key: str, model: str = "gemini-3.8-flash"):
        if not isinstance(key, str) or not key.strip():
            raise AssistantError("missing_api_key")
        if not isinstance(model, str) or not re.fullmatch(r"gemini-[a-zA-Z0-9.-]+", model):
            raise AssistantError("invalid_settings")
        self._key = key.strip()
        self.model = model

    @classmethod
    def configured(cls):
        path = Path(
            os.environ.get("MONTAZ_ASSISTANT_CONFIG", Path.home() / ".config/montaz/assistant.json")
        )
        try:
            settings = decode(path.read_bytes()) if path.exists() else {}
        except (OSError, ValueError) as exc:
            raise AssistantError("invalid_settings") from exc
        return cls(
            os.environ.get("GEMINI_API_KEY") or settings.get("gemini_api_key", ""),
            os.environ.get("GEMINI_MODEL") or settings.get("gemini_model", "gemini-3.8-flash"),
        )

    def generate(self, parts: list[dict]) -> dict:
        body = json.dumps(
            {
                "contents": [{"role": "user", "parts": parts}],
                "generationConfig": {
                    "responseMimeType": "application/json",
                    "temperature": 0.2,
                    "maxOutputTokens": 8192,
                },
            }
        ).encode()
        request = urllib.request.Request(
            f"https://generativelanguage.googleapis.com/v1beta/models/{self.model}:generateContent",
            data=body,
            headers={"Content-Type": "application/json", "x-goog-api-key": self._key},
        )
        try:
            with urllib.request.urlopen(request, timeout=35) as response:
                data = decode(response.read(MAX_MESSAGE_BYTES + 1))
        except urllib.error.HTTPError as exc:
            code = "provider_auth" if exc.code in (400, 401, 403) else "provider_unavailable"
            raise AssistantError(code) from None
        except (TimeoutError, socket.timeout) as exc:
            raise AssistantError("provider_timeout") from exc
        except (urllib.error.URLError, OSError) as exc:
            raise AssistantError("provider_unavailable") from exc
        except ValueError as exc:
            raise AssistantError("invalid_model_response") from exc
        try:
            candidate = data["candidates"][0]
            if candidate.get("finishReason") != "STOP":
                raise AssistantError("invalid_model_response")
            text = "".join(
                p.get("text", "") for p in candidate["content"]["parts"] if not p.get("thought")
            )
            return decode(text.encode())
        except (KeyError, IndexError, TypeError, ValueError, AttributeError) as exc:
            raise AssistantError("invalid_model_response") from exc
