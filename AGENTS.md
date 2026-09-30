# Repository Guidelines

## Project Direction

This is the Montaz AI-assistant fork of Kdenlive, based on upstream `v26.08.1`.
Read `dev-docs/montaz/README.md`, `architecture.md`, and `context.md` before making
architectural changes. They distinguish implemented work from planned work.
The earlier Electron editor is a separate, preserved repository; do not copy its
timeline, player, CRDT bridge, or rendering pipeline into this tree.

## Structure and Ownership

- `src/`: Kdenlive's C++/Qt application; `timeline2/` owns timeline operations.
- `data/`, `renderer/`, `tests/`: upstream resources, rendering entry point, and
  native regression tests. Preserve upstream layout and license notices.
- `assistant/src/montaz_assistant/`: independent Python contracts for proposals.
- `assistant/tests/`: offline tests; no credentials or provider calls required.
- `dev-docs/montaz/`: architecture, migration evidence, and implementation gates.

Kdenlive owns project state, time, playback, undo, subtitles, and export. The AI
returns proposals against an immutable project/sequence revision. Apply them
through native undoable commands only; never overwrite a live project file.

## Development and Validation

Run the dependency-free assistant suite from the repository root:

```sh
PYTHONPATH=assistant/src python3 -m unittest discover -s assistant/tests -v
python3 assistant/tools/run_native_tests.py --qmake /path/to/Qt/bin/qmake
```

For native work follow `dev-docs/build.md`, using KDE Craft on macOS. Once
dependencies are installed, configure with `cmake -S . -B build -DBUILD_TESTING=ON`,
build with `cmake --build build`, and run `ctest --test-dir build --output-on-failure`.
Enable the experimental panel with `-DBUILD_MONTAZ_ASSISTANT=ON`; see
`dev-docs/montaz/native-integration.md` for the remaining integration gates.
Do not describe native integration as verified without compiling and exercising it.

## Style and Testing

Follow `.clang-format` and neighboring Qt code; localize user-facing strings.
Use four-space Python indentation, type hints, and standard-library tests.
Test stale revisions, duplicate requests, source bounds, exact frame timing,
cancellation, and rollback. Add native integration tests for actual mutations.

## Commits and Boundaries

Use `codex/` branches and focused imperative commits. Keep upstream changes small
and separate from assistant logic. Explain validation and remaining limitations.
Never commit keys, footage, generated audio, analysis caches, or user projects.
Use synthetic fixtures. Packaging and public releases are deferred.
