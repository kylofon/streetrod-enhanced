# Street Rod Enhanced — SDL3

An enhanced native port of California Dreams' *Street Rod* (1989). Not an emulator. The original game files are
not included; the game is free from the copyright owner: https://www.streetrodonline.com/downloads/.

## Features

* Smooth road view in races and drives through town, at your display's refresh rate.
* High resolution (up to 2560 × 1600) with anti-aliasing.
* Extended road draw distance in races (up to 120 segments, 8× the original's).
* Target frame rate control (`--fps`) and vertical synchronisation (`--vsync`).
* Separate music and sound effects volumes.
* The game is otherwise the original: garage, opponents, physics, police and screens.
* The Street Rod SE data disk's 25 cars.
* A launcher for cars, switches, picture and sound options.

## How to play (Windows)

1. Put your original game files in a folder named `Game` (for the data disk cars, Street Rod SE in `Game\datadisk`).
2. Place the program files next to it:

   ```text
   Street Rod Enhanced\
   ├── Game\                        <- your game files (SR.EXE, LIB1, LIB2, HOT_DATA, datadisk\ ...)
   ├── Street Rod Enhanced.exe      <- the launcher
   ├── streetrod-enhanced.exe       <- the game
   └── SDL3.dll and the other DLLs
   ```

3. Run `Street Rod Enhanced.exe`, pick your options and press **Play**.

* The folder must be writable (saved games and the hall of fame are saved in it).
* Alt+Enter: full screen.
* Launcher settings: `%APPDATA%\Street Rod Enhanced\settings.ini`.

## Requirements

* Game files: `SR.EXE` (or `SRSE.EXE`), `LIB1`, `LIB2`, `HOT_DATA`.
* The road view is drawn on all CPU cores (at the default, about 1.2 ms a frame on 6 cores / 12 threads, 4.2 ms
  on one); lower the resolution, anti-aliasing or draw distance on slow machines.
* To build: CMake 3.24+, C11, SDL 3; the launcher needs C++17 and wxWidgets 3.2.

## Build

In Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

* `-DSRE_LAUNCHER=OFF` builds the game only.
* The build copies the DLLs both programs need beside them.

## Run

```bash
./build/streetrod-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Game files folder (default `Game`) |
| `--scale N` | Window size, × 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--res-scale N` | Resolution, × 320×200 (default 4, 2–8) |
| `--aa N` | Anti-aliasing, N×N samples (default 2, 1 = off) |
| `--draw-distance N` | Road draw distance in races, segments (default 60, 15–120; the original's is 15) |
| `--fps N\|auto` | Frame rate cap, 0–1000 (default auto: the display's refresh; 0 = uncapped; with VSync only a cap below the refresh counts) |
| `--vsync on\|off` | Wait for vertical sync (default on) |
| `--motion-delay P` | Smooth view delay, % of a game frame (default 100) |
| `--town on\|off` | Enhanced drives through town (default on) |
| `--reduced-sky-rotation on\|off` | The horizon turns a quarter as much as in the original, and the sky with it (default on; off = as the original) |
| `--music-volume P` | Music volume, % (default 100) |
| `--sfx-volume P` | Sound effects volume, % (default 100) |
| `--classic` | Original picture only |
| `--check` | Check that `SR.EXE` loads, then exit |
| `demo`, `auto`, `nomouse` | The original's switches: self-playing demo, races drive themselves, no mouse |

## Controls

* Mouse, or arrows / keypad to move the pointer; Space, Ins or keypad 0 to click.
* Races: Up gas, Down brake, Left / Right steer, Space / Ins shift, A / Z gear up / down.
* Esc skips the drive through town and ends the demo. M music, Ctrl sound on / off.

## Layout

* `src/` — the game (`ENGINE.md`).
* `src/enhanced/` — the enhanced road view (`ENHANCED.md`).
* `launcher/` — the launcher (`launcher/README.md`).

## License

MIT (`LICENSE`). *Street Rod* is © California Dreams / Logical Design Works; its files are not included.

## Support

https://buymeacoffee.com/krzysztofkania
