<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Local Qt Cocoa accessibility fix

This development-only build backports the ownership guards from
[Qt change 765434](https://codereview.qt-project.org/c/qt/qtbase/+/765434)
(patch commit `c7fd3f34b997bb363be15650647665b3b6b8a5f4`, Caleb Meadows), for
[QTBUG-149612](https://bugreports.qt.io/browse/QTBUG-149612). The upstream change
is under review; do not describe it as an official Qt release fix.

Synthesized Cocoa table elements share their parent's accessibility ID.
Releasing one must not delete the parent interface. Qt 6.11.1's original plugin
crashes or loses accessible rows when selected items are inspected. A small
standard Qt tree/list application reproduces it independently of Kdenlive.
The two guards in `table-ownership.patch` preserve parent ownership without
disabling accessibility or hiding selected children.

The builder downloads only the Cocoa plugin sources from pinned Qt 6.11.1 commit
`59c81a3c2247b821b9b84b4eb8d939b77e07e276`, checks their Git blob digests, applies
the patch and builds against the installed Craft SDK. Source files retain Qt's
license notices. The wrapper uses Qt's public plugin CMake helpers and that
version's private Core/Gui headers. This is deliberately version-specific.

From a Craft shell with CMake and Ninja available:

```sh
python3 assistant/tools/build_cocoa_accessibility_fix.py \
  --root "$HOME/Library/Caches/montaz/qt-cocoa-6.11.1"
export MONTAZ_COCOA_PLUGIN_ROOT="$HOME/Library/Caches/montaz/qt-cocoa-6.11.1"
assistant/tools/run_craft_macos.sh /path/to/project.kdenlive
```

Pass `--craft-root /path/to/CraftRoot` for a different SDK. The launcher validates
the Qt version, architecture, SDK location and binary hashes, patch and plugin
hashes before prepending this root to its own `QT_PLUGIN_PATH`. A different or
modified SDK requires rebuilding. Craft's original plugin stays intact; unset
`MONTAZ_COCOA_PLUGIN_ROOT` to use it. This does not install or package anything.

Acceptance requires repeated native selected-row queries, list replacement,
and real editor Apply/Undo/save/reopen/render checks. Unit tests alone cannot
establish Cocoa accessibility behavior. Replace this workaround with a released
upstream dependency fix once that version is verified.
