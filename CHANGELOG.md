# Changelog

## 0.1.0 (2026-09-29)

- **Enhanced road view** (`src/enhanced/`, `ENHANCED.md`): the races and the drives through town drawn again at
  1280 × 800 (320 × 200 up to 8 times) with 2 × 2 anti-aliasing, moving smoothly at the screen's refresh rate
  while the game keeps its own pace: the road, kerbs, sidewalks, dashes and finish, the sky and the horizon, the
  road-side objects, signs and banners (the largest sizes, scaled by depth), the town's walls, the opponent and
  the police car, and the rear-view mirror.
- Options `--classic` (the original picture), `--res-scale`, `--aa`, `--motion-delay`, `--town on|off`;
  `SR_ENH_TRACE=file` for the smooth view's positions and render times.
- Launcher: a Picture box (graphics enhanced / original, resolution, anti-aliasing, town drives); the Program
  field refuses a DOS program such as `SR.EXE`.

- Project from the faithful Street Rod port (`../StreetRod`, `srport/` at 0e5598e, release v0.1.0): the game, its
  launcher (renamed "Street Rod Enhanced", settings in `%APPDATA%\Street Rod Enhanced`, built by default) and the
  build. The sound check tool `srsnd-dump` stays in the faithful repository.
- `ENHANCED.md`: the design of the enhanced road view (proposal).
