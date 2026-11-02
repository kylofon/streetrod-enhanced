# Street Rod Enhanced (the launcher)

`Street Rod Enhanced.exe` starts `streetrod-enhanced`, Street Rod (California Dreams, 1989) on SDL3 with the
enhancements, with the options chosen in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls,
like the Street Rod and Test Drive III launchers it is modelled on.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `streetrod-enhanced.exe` (default: beside the launcher).
  * The line below says whether the folder holds Street Rod or Street Rod SE and how many saved games it has,
    or which file is missing (`SR.EXE` or `SRSE.EXE`, `LIB1`, `LIB2`, `HOT_DATA`). **Play** stays greyed out
    until the folder and the program are there. A DOS program (the original `SR.EXE`) is refused as the
    program: Windows can't run it.
* **Start**
  * **Cars**: the original cars, or the 25 cars of the Street Rod car data disks. The second needs a
    `datadisk` folder with Street Rod SE (`SRSE.EXE` and its `LIB1`, `LIB2`, `HOT_DATA`) inside the game folder,
    as in the usual GOG/eXoDOS copies; the game then runs in that folder and keeps its own saved games there.
  * The original's own command-line switches: **Demo** (`demo`: the game plays itself, Esc ends it), **Races
    drive themselves** (`auto`) and **No mouse** (`nomouse`).
* **Options**: **Window size** (`--scale`), **Frame rate** (the display's refresh rate with VSync, the
  default, or uncapped: `--fps 0 --vsync off`) and **Start in full screen** (`--fullscreen`; Alt+Enter
  switches).
* **Keys in the game**: a reminder of the game's keys, including the port's A / Z gear keys.
* **Picture**: **Graphics** enhanced (the smooth, high-resolution road view, the default) or original
  (`--classic`); **Resolution** of the picture, 640 × 400 to 2560 × 1600 (`--res-scale`, default 1280 × 800);
  **Anti-aliasing** off, 2 × 2 (default), 3 × 3 or 4 × 4 (`--aa`); **Draw distance** the original's, 4 × (default)
  or 8 × (`--draw-distance` 15, 60, 120); **Drives through town enhanced too** (`--town`). The last four are
  greyed out with the original graphics.
* **Play** starts the game; the launcher stays open.

## Menus

* **File**: **Preferences** (**Always on top** keeps the launcher above other windows) and **Exit**.
* **Game settings > Sounds**: **Music** (`--music-volume`) and **Sound effects** (`--sfx-volume`), 0 - 100 %,
  each a slider and a number field. **Default**, **Apply**, **Cancel**.
* **About**: version, author and links.

Everything is remembered in `%APPDATA%\Street Rod Enhanced\settings.ini` (`~/.config/street-rod-enhanced` on Linux), the volumes
under `[Sound]`. A folder or program left at its default is stored empty, so it follows the launcher if the whole
folder moves. Delete the file to go back to the defaults.

## Building

Built with the game by default (`SRE_LAUNCHER`, see the README at the repository root); it needs CMake 3.24, a
C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`; Debian and Ubuntu:
`libwxgtk3.2-dev`). It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the
program in the window or copy the launcher next to it.

On Windows the build copies every DLL the launcher and the game need beside them (`copy_dlls.cmake`): the two
wxWidgets DLLs and the MSYS2 libraries they load, and `libiconv-2.dll` for `SDL3.dll`. The C++ runtime of the
launcher itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window, its menus, the Sounds page and the About box.
* `game.h`, `game.cpp`: the file checks, the data disk, saved games and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, Street Rod's night road drawn in code. `make_icon.py` (Pillow) writes the
  same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy (also used by the game's build).
* `CMakeLists.txt`: the build, standalone or from the main project.
