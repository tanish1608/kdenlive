<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Assistant foundation

Python 3.11+, standard library only. This package validates proposals, models run
events, and provides a deterministic diagnostic worker for the native adapter.

```sh
PYTHONPATH=assistant/src python3 -m unittest discover -s assistant/tests -v
```

`contracts.py` defines immutable editor snapshots and insert proposals. Validation
checks project/sequence/revision identity, replay, clip IDs, imported assets,
track capabilities/locks, source ranges, and occupied frames. Inputs are project
frames with rational FPS; existing occupancy comes from Kdenlive. The same checks
must run at the future native mutation boundary, not only in Python.

`lifecycle.py` defines explicit, sequenced run events and terminal stage states.
Cancellation settles pending/running stages; late callbacks cannot revive a run.
This models status only. Process cancellation and deadlines need real integration
tests; those now run in the Qt-only suite below.

`protocol.py` rejects duplicate/unknown fields, oversized messages, invalid numeric
types, and frame overflow. `worker.py` accepts one `plan_demo` request per process,
chooses up to six distinct imported video sources, and appends bounded two-second
shots at exact project frames. It uses no providers, sound, transitions, or captions.

Compile and exercise the actual Qt process transport, native validator, and
batch rollback logic using an installed Qt development environment:

```sh
python3 assistant/tools/run_native_tests.py --qmake /path/to/Qt/bin/qmake
```

On this Mac the runner defaults to Homebrew's qmake. It builds in a temporary
directory, calls the real Python worker, and tests failure, timeout, bounded
output, cancellation of an uncooperative process, and redacted run records.
These tests do not instantiate Kdenlive's timeline. See the native integration
guide for the full application test gate.

Protocol v1 deliberately supports only insertion. Mixing, subtitles, effects,
trim/move, linked AV groups, and replacement proposals require explicit contract
extensions plus native tests. A successful validation is not an editor apply
acknowledgment or an assessment of editorial quality.
