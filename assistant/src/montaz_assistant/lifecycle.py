# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
"""Explicit run lifecycle; no inferred phases or callbacks after termination."""

from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType


class Status(str, Enum):
    PENDING = "pending"
    RUNNING = "running"
    COMPLETED = "completed"
    SKIPPED = "skipped"
    FAILED = "failed"
    CANCELLED = "cancelled"


TERMINAL = frozenset({Status.COMPLETED, Status.SKIPPED, Status.FAILED, Status.CANCELLED})


@dataclass(frozen=True)
class Event:
    run_id: str
    sequence: int
    stage: str | None
    status: Status
    reason: str


class Run:
    """One run's event ledger. OS/provider cancellation is the worker's responsibility."""

    def __init__(self, run_id: str, stages: tuple[str, ...]):
        if (
            not run_id
            or not stages
            or len(stages) != len(set(stages))
            or any(not s for s in stages)
        ):
            raise ValueError("Run and stage IDs must be nonempty; stages must be unique")
        self.run_id = run_id
        self._status = Status.RUNNING
        self._stages = dict.fromkeys(stages, Status.PENDING)
        self._events: list[Event] = []
        self._emit(None, self._status, "")

    @property
    def status(self):
        return self._status

    @property
    def stages(self):
        return MappingProxyType(self._stages)

    @property
    def events(self):
        return tuple(self._events)

    def _emit(self, stage: str | None, status: Status, reason: str):
        self._events.append(Event(self.run_id, len(self._events) + 1, stage, status, reason))

    def transition(self, stage: str, status: Status, reason: str = "") -> bool:
        """Return False for a late callback; reject invalid transitions in a live run."""
        if self._status in TERMINAL:
            return False
        if not isinstance(status, Status):
            raise ValueError("Stage status must be a Status value")
        if stage not in self._stages:
            raise ValueError("Unknown stage")
        previous = self._stages[stage]
        allowed = {
            Status.PENDING: {Status.RUNNING, Status.SKIPPED},
            Status.RUNNING: {Status.COMPLETED, Status.SKIPPED, Status.FAILED},
        }
        if status not in allowed.get(previous, set()):
            raise ValueError(f"Invalid transition: {previous.value} -> {status}")
        if status in {Status.SKIPPED, Status.FAILED} and not reason.strip():
            raise ValueError("Skipping or failing a stage requires a reason")
        self._stages[stage] = status
        self._emit(stage, status, reason)
        if status == Status.FAILED:
            self._settle(Status.FAILED, reason)
        return True

    def finish(self):
        if self._status in TERMINAL:
            return False
        if any(s not in {Status.COMPLETED, Status.SKIPPED} for s in self._stages.values()):
            raise ValueError("Cannot complete a run with unfinished stages")
        self._status = Status.COMPLETED
        self._emit(None, self._status, "")
        return True

    def cancel(self, reason: str = "Stopped by user"):
        if self._status in TERMINAL:
            return False
        if not reason.strip():
            raise ValueError("Cancellation requires a reason")
        self._settle(Status.CANCELLED, reason)
        return True

    def _settle(self, status: Status, reason: str):
        for stage, current in self._stages.items():
            if current not in TERMINAL:
                terminal = Status.SKIPPED if current == Status.PENDING else Status.CANCELLED
                self._stages[stage] = terminal
                self._emit(stage, terminal, reason)
        self._status = status
        self._emit(None, status, reason)
