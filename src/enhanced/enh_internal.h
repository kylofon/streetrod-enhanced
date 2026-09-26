#pragma once
/* Shared declarations of src/enhanced. */
#include <SDL3/SDL_stdinc.h>

#include "../types.h"

/* The road view on screen (race_road.c clip DS:5E4E..: rows 12h..65h), the split below it. */
#define ENH_VIEW_Y0 0x12
#define ENH_VIEW_Y1 0x66

typedef struct {
    bool enabled;
    int res_scale, aa, motion_delay;
    bool town;
} EnhOptions;
extern EnhOptions enh_opt;

/* One game frame's state, taken in road_frame after the projection. */
typedef struct {
    Uint64 t_ns;               /* host time when the frame was built */
    double pos;                /* segment + sub-step / 8 */
    s16 lat;                   /* camera x (DS:7D36) */
    s16 heading;               /* DS:7D3C */
    s16 rel_heading;           /* heading - YAW[segment]: the steering part, interpolated */
    s16 height;                /* DS:7D3E */
    double opp_pos;            /* opponent (or police) segment + fraction / 8 */
    s16 opp_lat;
    bool town;                 /* the town cruise (DS:8ACA = 0) */
} EnhSnap;

/* The smooth view at a moment: the road-curve part of the heading comes from YAW at pos. */
typedef struct {
    double pos, lat, rel_heading, opp_pos, opp_lat;
    s16 height;
    bool town;
} EnhView;

bool enh_view_at(Uint64 now_ns, EnhView *v);
