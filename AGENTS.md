# fheroes1

This repository targets the Windows build for Parallels. See the README for the MinGW build and game data layout.

- Keep game data, commercial binaries, and player saves out of the repository.
- Keep the two Windows executables standalone.
- Preserve the upstream source attribution and license.
- `src/SOURCE`, `src/BASE` and `src/EDITOR` contain the game and editor.
- `src/PORT` and `src/PLATFORM` contain the native compatibility and SDL3 layers.
- `HOMM1_INPUT_REPLAY=FILE` runs scripted input, with timed `click`, `key`, `shot`, and `exit` actions.
