# fheroes1 · Quick Split

Heroes of Might and Magic I with quality-of-life changes for Windows in Parallels. This branch contains the native game and scenario editor. Game data comes from your installed copy of Heroes I.

This is a fork of sushi-shi’s reconstructed Buka 2003 port. Changes here include castle controls, army splitting and transfer, hotkeys, pre-battle results with replay, and GOG audio support. The original port’s license is included. [Upstream source](https://github.com/sushi-shi/homm1-decomp/tree/port)

## Quick Split controls

- Drag the adventure map with the left mouse button. A click still selects an object or sets a route.
- In manual combat, click **Options** above **Auto**, or press **B**. Set animation speed, turn order, the hex grid, damage hints and automatic spellcasting there.
- **F4** toggles fullscreen. In fullscreen, **F10** shows the host menu. The **Display** menu offers scaling and fullscreen resolution.
- Castle dwellings open recruitment at the maximum affordable quantity. The meeting screen has separate arrows for armies and artifacts.
- The optional pre-battle result uses the battle engine and AI. **Restart** returns to manual combat from the initial state.

To add a scenario pack, put its Heroes I maps in `EXPANSIONS/<pack name>/MAPS` in your game folder. Restart and select the pack under **Map packs**. Keep the original 8.3 map filenames.

## Build for Windows

Install CMake 3.25 or later, Ninja, Python 3 and an x86-64 MinGW-w64 C/C++ toolchain. Run from a shell where the toolchain is available. If cross-compiling, pass the Windows toolchain file when configuring.

```powershell
cmake --preset windows
cmake --build --preset windows --target homm1 homm1_editor
```

The build produces `build/windows/heroes.exe` and `build/windows/heroes-editor.exe`.

## Run in Parallels

Pass the directory that contains your Heroes I `DATA` and `MAPS` folders. Use a separate copy of the directory for testing.

```powershell
.\build\windows\heroes.exe --data "C:\Games\heroes1_orig\heroes1-quick-split-test" --window /I0
```
