# Enhanced version — design (proposal, nothing implemented yet)

The faithful engine (see `ENGINE.md`) runs unchanged: game flow, garage, the race logic and physics, the
original road renderer, the dashboard and the screens, at the game's own pace. `src/enhanced/` would record
what every race frame is built from and draw the road view and the rear-view mirror again on the host, for
every displayed frame, laid over the EGA picture. The approach is the one of Test Drive II and III Enhanced;
Street Rod's renderer makes it simpler, because its world is already stored whole (below).

This file is the plan for stage 1. The choices marked **open** are to be decided before the work starts.

## What the original gives us

* **The whole road is in memory.** The track is a list of 1120 segments (`../port/spec/race_render.md` §1.1),
  built at a new game and kept in segment `389b`: per segment the heading (`YAW`), the height (`HGT`), the centre
  flags (width, dashes, crossings, banners, signs) and the left/right road-side flags (buildings, palms, trees,
  posts, poles, signs). Segments 0–179 are the town, 180–1119 the race road.
* **The projection is simple and has no rotation:** a row at depth `z` is shifted sideways by the heading
  difference and up/down by the height difference (`race_render.md` §1.2). It can be evaluated at any depth, not
  only at the original's 15 rows (one per segment).
* **The car state per frame** (DGROUP): segment `DS:7D30`, sub-step `DS:7D32` (8 per segment), step per frame
  `DS:7D34`, lateral position `DS:7D36`, heading `DS:7D3C`; the opponent's segment and lateral position; the
  sky / horizon scroll follows the heading.
* **Pacing:** a race frame every 9 timer ticks (about 8.1 a second), the car's physics every 12th tick in the
  timer interrupt, and the car moves along the road once per frame (`race.md` §7). The game logic stays at this
  pace; only the picture becomes smooth.

## Stage 1 — smooth, high-resolution road view (proposal)

* **Smooth motion.** Snapshot the car state after each game frame with its tick time; for every displayed frame
  interpolate the continuous road position `seg + substep/8`, the lateral position, the heading and the
  opponent's position between the last two frames (as in TD3 Enhanced, running a fraction of a frame behind
  instead of guessing ahead). Crossing a segment must not shift the view: road depth is measured from the
  continuous position.
* **High resolution.** Evaluate the original projection in floating point at 1280 × 800 (or higher), with the
  road drawn as polygons between many interpolated depths instead of 15 flat bands, anti-aliased edges, the
  centre dashes, kerbs, sidewalks, crossings and town walls from the same flags.
* **Sprites.** The road-side objects, the opponent and the police car from their native LIB2 pictures, scaled
  continuously with distance, instead of the original's pre-shrunk sizes (`race_render.md` §4.16).
* **Sky and horizon.** The night sky (LIB2 #5) and the horizon panorama (#267) scrolled continuously with the
  interpolated heading.
* **Mirror** redrawn the same way (8 rows behind in the original).
* **Unchanged:** the dashboard (below the split screen), the cockpit overlays (A-pillar LIB2 #6, hood #7) drawn
  over the enhanced view, the menus, the garage and all other screens (still pictures).
* **Draw distance:** the original's 15 segments. **Open:** a longer distance is possible (the road ahead exists),
  but the original sizes its scenery for 15; to be tried after stage 1.

## Hooks (planned, marked `ENH:` in the engine)

| Hook | Where | Purpose |
|---|---|---|
| `enh_init()` | `main.c` | options (`--classic` = the original picture only) |
| `enh_race_begin()` / `enh_race_end()` | `race_run` (`game/race_drive.c`, 0f38:7b22) entering / leaving the frame loop | sprite images, buffers; overlay on / off |
| `enh_frame_built()` | `road_frame` (`game/race_road.c`, 2645:1e9e) after the projection | snapshot of the car, opponent and view state with the tick time |
| `enh_frame_presented()` | `road_page_flip` (2645:2288) | which page is shown, the window rows |
| `enh_compose()` | `platform/ega.c` compose | draw the enhanced view and lay it over the scaled EGA picture above the split line |

The host composes a 320 × 200 frame today (`HOST_FRAME_MAX_W/H`); stage 1 needs a larger output frame, as TD3
Enhanced added.

## Open decisions

1. Resolution and anti-aliasing defaults (TD3 Enhanced: 1280 × 800, 2 × 2).
2. How far the smooth view runs behind the game (TD3 Enhanced: one game frame, interpolation only).
3. Whether the town cruise is enhanced too (same renderer, so it would come almost for free).
4. Whether the picture keeps strictly to the game's 16 palette colours (as TD2 Enhanced did) or allows smoother
   shading where the original's colours step (as TD3 Enhanced did for its sky gradient).
