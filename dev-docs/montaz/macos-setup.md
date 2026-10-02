<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- SPDX-FileCopyrightText: 2026 Montaz contributors -->

# Set up and run Montaz on macOS

This guide targets the existing Apple Silicon Mac and local Kdenlive checkout.
Xcode supplies the C++ compiler. KDE Craft installs the compatible Qt, KDE
Frameworks, MLT, and build tools required by this editor. The full installation
and editor build succeeded on this host on September 30, 2026.

## 1. Make room first

The host had approximately 31 GB available when rechecked on September 30, 2026.
Aim for roughly 30 GB of free working space, or use an external SSD with ample
space. This is a recommended working margin, not an exact published dependency
size; cache/build requirements vary. Use System Settings → General → Storage to
review space. Keep footage and projects.

## 2. Open Terminal and check existing tools

Open Terminal through Spotlight (Command + Space). Paste one block at a time and
wait for each command to finish. Lines ending in `\` continue onto the next line.

```sh
uname -m
xcode-select -p
clang++ --version
git --version
python3 --version
```

This Mac already has Xcode, Git, Homebrew, and Python 3.12. Architecture should be
`arm64`; Python must be 3.11 or newer for our assistant. If Apple developer tools
are missing on another Mac, run `xcode-select --install` and finish the installer.
If Python is missing, install a recent Python from python.org or through Homebrew.

## 3. Choose directories

Run these in the same Terminal window:

```sh
export MONTAZ_CRAFT_ROOT="$HOME/CraftRoot"
export MONTAZ_REPO="/Users/tanishvardhineni/Documents/montaz/kdenlive"
export KDENLIVE_ASSISTANT_PYTHON="/Library/Frameworks/Python.framework/Versions/3.12/bin/python3"
```

The last path is the interpreter already installed on this Mac. Use the home-folder
CraftRoot assignment above on this laptop. The chosen directory must initially
be empty or absent.

For an external SSD, first connect it and run `ls /Volumes` to find its actual
name. Then set the root to a directory on that mounted drive. Example drive names
are placeholders; a drive called `DevSSD` is not currently mounted on this host.
If the prompt shows `(base)`, run `conda deactivate` before setting up Craft to
leave Anaconda's active environment. Use the explicit interpreter below.

## 4. Install KDE Craft

For the Python.org interpreter on this Mac, initialize its HTTPS certificate
bundle once before running the bootstrap:

```sh
/bin/sh "/Applications/Python 3.12/Install Certificates.command"
```

This is Python's bundled certificate installer. It installs `certifi` and links
the root certificate bundle used by this interpreter's HTTPS connections.

Download the official KDE bootstrap script, then run it:

```sh
curl -fL https://raw.githubusercontent.com/KDE/craft/master/setup/CraftBootstrap.py \
  -o "$HOME/Downloads/CraftBootstrap.py"
"$KDENLIVE_ASSISTANT_PYTHON" "$HOME/Downloads/CraftBootstrap.py" \
  --prefix "$MONTAZ_CRAFT_ROOT"
```

Choose `arm64` when asked about architecture. Accept the default compiler/build
choices for macOS. The script installs Craft into the selected directory and
prints an activation command. This step downloads files and can take time.

## 5. Activate Craft and install editor dependencies

```sh
source "$MONTAZ_CRAFT_ROOT/craft/craftenv.sh"
craft --help
craft --options "[Compile]Jobs=2" --install-deps kdenlive
```

Keep this Terminal window open. Craft installs Kdenlive's dependency graph and
uses available KDE binary caches. This is the largest setup step. Two build jobs
limit simultaneous compilation on the laptop. This checkout requires Qt >= 6.10,
KDE Frameworks/ECM >= 6.21, MLT >= 7.38, and KDDockWidgets >= 2.4.

Craft provides an isolated development environment. Its dependency versions must
satisfy the checkout's CMake checks; if a version check fails, capture that error
before changing package versions or mixing in libraries from another installation.

## 6. Compile our checkout with the assistant enabled

```sh
craft --no-cache \
  --options "[Compile]Jobs=2" \
  --options "kdenlive.srcDir=$MONTAZ_REPO" \
  --options "kdenlive.buildTests=True" \
  --options "kdenlive.args=-DBUILD_MONTAZ_ASSISTANT=ON -DBUILD_TESTING=ON" \
  --compile --install --qmerge kdenlive
```

`srcDir` selects our modified checkout. `--no-cache` forces this editor build to
use that source. `--compile` configures and compiles it; `--install --qmerge`
installs and registers it inside CraftRoot, including required application data.
The installation is a local development build, not a distributable release.

The panel and editor adapter compiled against the complete KDE/MLT SDK. Preserve
the first compiler error and its surrounding lines if a later build fails.

## 7. Run the integration checks and launch

```sh
cb kdenlive
ctest -R '^assistanttest$' --output-on-failure
"$MONTAZ_REPO/assistant/tools/run_craft_macos.sh"
```

`cb` changes into Craft's Kdenlive build directory. The launcher activates Craft
and runs `bin/kdenlive.app/Contents/MacOS/kdenlive` with the development SDK's
library, MLT plugin/profile, and Frei0r paths. It retains the Python setting.
Before launch it stages upstream effects, workspace layouts, conversion/render
presets and the built render helper into the development app bundle. This is
required because a build alone does not install runtime resources on macOS; it
does not install an SDK or change user settings. If the helper is missing, build
`kdenlive_render` alongside `kdenlive`. The standalone dependency-free Python
tests can also be run from the source tree:

```sh
cd "$MONTAZ_REPO"
PYTHONPATH=assistant/src "$KDENLIVE_ASSISTANT_PYTHON" \
  -m unittest discover -s assistant/tests -v
```

In the editor, open **View → AI Assistant**, import a few small videos, open a
sequence, open **AI**, choose a travel preset and duration, then send a chat brief.
Inspect the list before **Apply edit**. Gemini uses a private settings file or
`GEMINI_API_KEY`; see [assistant configuration](../../assistant/README.md).
The visual planner creates video-only cuts. Music/effects and original-audio
captions use separate optional reviewed tools; narration remains pending.
Set `MONTAZ_ASSISTANT_DEMO=1` for the offline diagnostic.
Verify one Undo removes the whole edit and Redo restores it. Full playback,
save/reopen, seeking and a native Goa export were exercised locally. Additional
native failure checks remain required; see
[native integration](native-integration.md).

## 8. Reopen the development environment later

In each new Terminal window, repeat the three directory/interpreter assignments
from step 3 and the `source` command from step 5. Then run `cb kdenlive` and the
executable from step 7. After source changes, repeat the build command in step 6.

## Troubleshooting

| Symptom | Next action |
| --- | --- |
| `craft: command not found` | Activate `craftenv.sh` in this same Terminal. |
| Permission denied for `/Volumes/DevSSD` | That example drive is absent. Reset `MONTAZ_CRAFT_ROOT` to `$HOME/CraftRoot`. |
| `SSL: CERTIFICATE_VERIFY_FAILED` | Run Python's certificate installer in step 4, then retry. If another certificate error persists, retain it for diagnosis. |
| ECM/KF6/Qt/MLT not found | Confirm Craft is activated and dependency installation finished. |
| Compiler/SDK or Xcode license error | Open Xcode once and complete its setup prompts. |
| `No tests were found` | Craft activation changes directories. Run `cb kdenlive` before CTest and check both build options were ON. |
| `libvpx.12.dylib` not loaded | Rebuild after the test configuration fix; use the launcher for the editor. The library is installed but its bare install name needs Craft's library search path. |
| Empty Load Layout, conversion or render presets | Launch with the helper; it stages source runtime data into `Contents/Resources`. |
| Missing MLT modules | Use the launcher. The SDK keeps modules in `plugins/mlt`; an unpackaged app cannot find them through bundle discovery. |
| Assistant panel missing | Confirm the local source path and `BUILD_MONTAZ_ASSISTANT=ON` build option. |
| Assistant worker cannot start | Check the explicit Python path exists and reports Python 3.11+. |
| Download or compilation fails | Stop at that step; retain the failing command and first error. |

Assistant run records live under the app's cache directory at
`montaz/runs/*.jsonl`; see the native integration guide for their contents.

## Official references

- [Kdenlive build guide](https://github.com/KDE/kdenlive/blob/v26.08.1/dev-docs/build.md).
- [KDE Craft overview and environment activation](https://community.kde.org/Craft).
- [Official bootstrap script](https://github.com/KDE/craft/blob/master/setup/CraftBootstrap.py).
- [Python.org macOS certificate setup](https://docs.python.org/3/using/mac.html).
- [Current Craft CLI](https://github.com/KDE/craft/blob/master/bin/craft.py) and
  [local source option](https://github.com/KDE/craft/blob/master/bin/Source/DirSource.py).
