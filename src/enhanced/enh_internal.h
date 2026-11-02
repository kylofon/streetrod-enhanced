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

/* The newest frame's state that is not interpolated. */
typedef struct {
    s16 height;                /* DS:7D3E */
    s16 opp_sprite;            /* DS:7D7C after the frame was drawn: the opponent's view */
    bool town;                 /* the town cruise (DS:8ACA = 0) */
} EnhSnap;

/* The smooth view at a moment: the road-curve part of the heading comes from YAW at pos. */
typedef struct {
    double pos, lat, rel_heading, opp_pos, opp_lat;
    s16 height, opp_sprite;
    bool town;
} EnhView;

bool enh_view_at(Uint64 now_ns, EnhView *v);

/* enh_sprite.c: palette-index images, ENH_CLEAR = transparent. */
#define ENH_CLEAR 0xFF
typedef struct { int w, h; u8 *px; } EnhImage;
void enh_images_reset(void);
void enh_images_frame_begin(void);
const EnhImage *enh_image_bitmap(u16 seg, u16 off);
const EnhImage *enh_image_vram(u16 page, int x, int y, int w, int h);

/* enh_raster.c: samples of the view rows ENH_VIEW_Y0..Y1, sa = res scale x aa per original pixel. */
typedef struct {
    int w, h, sa;
    u8 *px;                    /* palette indices */
} EnhBuf;
extern EnhBuf enh_buf;
void enh_raster_init(void);
void enh_raster_shutdown(void);
void enh_raster_begin(void);
void enh_buf_setup(void);
/* Drawing is clipped to [x0, x1) x [y0, y1) of the original screen; enh_clip_view() = the whole view. */
void enh_clip(double x0, double y0, double x1, double y1);
void enh_clip_view(void);
void enh_draw_sky(bool town, double hy, double heading);
void enh_fill_rows(double y0, double y1, u8 c);                  /* original screen rows [y0, y1) */
void enh_fill_poly(const double *xs, const double *ys, int n, u8 c);
void enh_fill_rect(double x0, double y0, double x1, double y1, u8 c);
void enh_draw_image(const EnhImage *img, double x0, double y0, double x1, double y1);
/* The drawing calls above record commands; this runs them, in horizontal bands on the worker threads, then
 * writes output rows ENH_VIEW_Y0..y_end of xrgb (320*S wide): the samples resolved through pal, except where
 * the EGA page at plane offset `page` has written or foreground pixels. */
void enh_raster_dispatch_and_resolve(u32 *xrgb, const u32 *pal, int npal, u16 page, int y_end);

/* enh_road.c */
void enh_draw_road(const EnhView *v);
