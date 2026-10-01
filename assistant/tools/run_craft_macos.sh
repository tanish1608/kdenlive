#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Montaz contributors
set -eo pipefail

montaz_craft_root="${MONTAZ_CRAFT_ROOT:-${KDEROOT:-$HOME/CraftRoot}}"
export CRAFT_PYTHON_BIN="${CRAFT_PYTHON_BIN:-${KDENLIVE_ASSISTANT_PYTHON:-$(command -v python3)}}"
source "$montaz_craft_root/craft/craftenv.sh"
cb kdenlive
set -u

# These are development SDK paths, not the eventual packaged app layout.
export DYLD_FALLBACK_LIBRARY_PATH="$montaz_craft_root/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
export MLT_PREFIX="$montaz_craft_root"
export MLT_DATA="$montaz_craft_root/share/mlt"
export MLT_PROFILES_PATH="$MLT_DATA/profiles"
export FREI0R_PATH="$montaz_craft_root/plugins/frei0r-1"
for montaz_plugins in plugins/mlt lib/mlt-7 lib/mlt; do
    if [[ -d "$montaz_craft_root/$montaz_plugins" ]]; then
        export MLT_REPOSITORY="$montaz_craft_root/$montaz_plugins"
        break
    fi
done
export KDENLIVE_ASSISTANT_PYTHON="${KDENLIVE_ASSISTANT_PYTHON:-$CRAFT_PYTHON_BIN}"
exec "$PWD/bin/kdenlive.app/Contents/MacOS/kdenlive" "$@"
