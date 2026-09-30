<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Assistant foundation

Python 3.11+, standard library only. This package validates proposals and models
run events. It does not render video, run an LLM, apply editor changes, or maintain
a second authoritative timeline.

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
tests when the QProcess worker is added.

Protocol v1 deliberately supports only insertion. Mixing, subtitles, effects,
trim/move, linked AV groups, and replacement proposals require explicit contract
extensions plus native tests. A successful validation is not an editor apply
acknowledgment or an assessment of editorial quality.
