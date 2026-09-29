# Street Rod Enhanced — SDL3

An enhanced version of *Street Rod* (1989, California Dreams / Logical Design Works), running natively on SDL3.
The game itself is the faithful C reimplementation of `SR.EXE` from the Street Rod port
([street-rod-sdl3](https://github.com/kylofon/street-rod-sdl3); VGA 16 colours, PC speaker). On top of it, the
road view of the races and the drives through town is drawn again at a high resolution and moves smoothly at
your screen's refresh rate, from the original data only (see `ENHANCED.md`). It is not an emulator. The
original data is not redistributed; you can get the game for free from the copyright owner:
https://www.streetrodonline.com/downloads/.

## Status

0.1.0: the enhanced road view (stage 1 of `ENHANCED.md`).

* The road view of races and town drives at 1280 × 800 by default (up to 2560 × 1600) with anti-aliased edges,
  moving smoothly between the game's frames (about 8 a second): the road, the sky and the horizon, the
  road-side objects, signs and banners, the town's buildings, the opponent and the police car, and the rear-view
  mirror.
* The game itself is unchanged: the same physics, opponents, timing and screens. The cockpit, the start light,
  the dashboard and all other screens are the original's pictures, scaled.
* Planned (stage 2): a longer draw distance.

Options (the launcher's Picture box sets them): `--classic` shows the original picture; `--res-scale N` (2..8,
default 4) the picture size, 320 × 200 times N; `--aa N` (1..4, default 2) anti-aliasing; `--town off` leaves
the drives through town original.

## How to play (Windows)

You need the files of the original DOS *Street Rod* (`SR.EXE`, `LIB1`, `LIB2`, `HOT_DATA`), and for the data disk
cars *Street Rod SE* in a folder named `datadisk` inside it.

1. Put your original game files in a folder named `Game` (with `datadisk` inside it, if you have it).
2. Copy the program's files into the folder that holds `Game`, so that `Street Rod Enhanced.exe` sits next to
   `Game`:

   ```text
   Street Rod Enhanced\
   ├── Game\                        <- your original game files (SR.EXE, LIB1, LIB2, HOT_DATA, datadisk\ ...)
   ├── Street Rod Enhanced.exe      <- the launcher
   ├── streetrod-enhanced.exe       <- the game
   ├── SDL3.dll
   └── (the other DLLs)
   ```

3. Double-click `Street Rod Enhanced.exe`, choose the cars and options, and press **Play**.

Keep the folder somewhere you can save files, not Program Files: the game saves your games and the hall of fame
in the folder it runs in. Press Alt+Enter for full screen.

## Build

From the repository root, in Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds `streetrod-enhanced.exe` and the launcher `Street Rod Enhanced.exe` (wxWidgets 3.2; leave it out with
`-DSRE_LAUNCHER=OFF`) and copies the DLLs they need beside them.

## Files

* `src/` — the engine (`ENGINE.md`: where it comes from and its rules) and `src/enhanced/`, the enhanced road
  view.
* `launcher/` — the launcher (`launcher/README.md`).
* `ENHANCED.md` — the design of the enhancements. `CHANGELOG.md` — the changes.

## License

MIT (see `LICENSE`). *Street Rod* is © California Dreams / Logical Design Works. Its files are not part of this
repository or the releases.

## Support

https://buymeacoffee.com/krzysztofkania
