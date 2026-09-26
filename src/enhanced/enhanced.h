#pragma once
/* Enhanced road view (ENHANCED.md). Not bound by the faithful-engine rules in ENGINE.md.
 *
 * The faithful engine keeps running every race frame at its own pace (road_frame: about 8.1 frames a second,
 * the town cruise too). This module records the car, opponent and view state of each of those frames, draws
 * the road view and the rear-view mirror again on the host at a higher resolution for every displayed frame,
 * with the motion carried smoothly between the game's frames, and lays the result over the EGA picture. The
 * hooks below are called from the engine at the places marked `ENH:`. */
#include "../types.h"

#define ENH_DEFAULT_RES_SCALE 4        /* output = 320x200 times this */
#define ENH_MAX_RES_SCALE     8
#define ENH_DEFAULT_AA        2        /* samples per output pixel along each axis */
#define ENH_MAX_AA            4
#define ENH_MAX_SAMPLES       16       /* res scale x aa at most (samples per original pixel, each axis) */
#define ENH_DEFAULT_MOTION_DELAY 100   /* percent of a game frame the smooth view runs behind the game: 100 = it
                                          only interpolates between frames it has */

/* main.c: enabled = false is --classic (the original's picture, scaled by res_scale). res_scale 1..8,
 * aa 1..4, motion_delay 0..100, town = the town cruise is enhanced too. */
void enh_init(bool enabled, int res_scale, int aa, int motion_delay, bool town);
bool enh_enabled(void);
int  enh_res_scale(void);

/* race_run (race_drive.c): the road view starts (before road_race_init) and ends (the frame loop left). */
void enh_race_begin(void);
void enh_race_end(void);

/* road_frame (race_road.c), after the projection: the frame's state becomes the newest snapshot. */
void enh_frame_built(void);
/* road_draw_frame (race_road.c): what the engine draws next into the back page is replaced by the enhanced
 * view (bg: sky, ground, road bands) or stays the original's picture (fg: road-side objects, the opponent,
 * the town walls, the mirror, the cockpit), until the enhanced renderer draws those too. begin = a new
 * frame into the back page (all its marks cleared, background). */
void enh_draw_begin(void);
void enh_draw_fg(bool fg);

/* road_page_flip (race_road.c): the frame just drawn is now the shown page. */
void enh_frame_presented(void);

/* platform/ega.c compose: the EGA picture has been scaled into xrgb (320*S x 200*S); lay the enhanced view
 * over it. rgb = the 16 palette registers as XRGB. Returns true while the enhanced view is shown (the
 * picture changes with time even when the game draws nothing). */
bool enh_compose(u32 *xrgb, const u32 rgb[16]);
