# fheroes1 engine preview

A Windows test build of Heroes of Might and Magic I on the fheroes2 1.1.17 engine. This branch combines the official release with the Heroes I work from DMJC/fheroes. The upstream license and source notices are included. [fheroes2 1.1.17](https://github.com/ihhub/fheroes2/releases/tag/1.1.17) · [DMJC/fheroes](https://github.com/DMJC/fheroes/tree/7daae97f23917d5f959612c9e2ce0c58d3fb0c15)

## Build and run in Parallels

Use CMake 3.24 or later, an x64 Windows C++ compiler, SDL2, SDL2_mixer and zlib. Point CMake to the dependency prefixes if they are outside the compiler's search path.

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target fheroes
cmake --install build --config Release --prefix stage
```

Place the runtime SDL2 and SDL2_mixer DLLs alongside `stage/bin/fheroes1.exe` if you linked the shared libraries. Copy `DATA/HEROES.AGG` and `MAPS` from your original Heroes I install alongside the executable, keeping the directory structure.

This preview reads the original archive and imports terrain, scenery, towns, treasure chests, resource piles, wandering armies and resource-producing mines. Other interactive map objects, custom town ownership, events, starting armies and campaign compatibility are incomplete. It uses its own Windows profile directory `%APPDATA%/fheroes1`.

To import music from an extracted Heroes I `SOUND` directory, run the importer in PowerShell and select **External** music in the game:

```powershell
.\import-heroes1-music.ps1 -SoundDirectory 'C:\Games\Heroes1\SOUND' -MusicDirectory 'C:\Games\fheroes1\music'
```
