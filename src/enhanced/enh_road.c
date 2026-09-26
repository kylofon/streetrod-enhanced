/* The road view drawn again from the segment table (race_render.md §1-§4): the original's projection
 * evaluated in floating point at the smooth position, the road cut into slices of 1/8 segment with the
 * heading and height interpolated between segments, the sky and the horizon scrolled continuously.
 *
 * Projection (road_proj_x / road_proj_y, race_road.c), a point at world x X, height Y, on the road at
 * continuous segment position s, seen from position pos:
 *     z  = 8 (s - pos) + 4                  (row k of the original: 8k + 4 - substep)
 *     dx = heading - YAW(s),  dy = height(pos) - HGT(s)
 *     sx = 160 - 4 (cam_x - (X + z dx / 4)) / z
 *     sy = 4 (Y + z dy - 76Ch) / z + 60
 * A band between two rows takes the flags of its far row (road_draw_band_front). */
#include "enhanced/enh_internal.h"

#include <math.h>

#include "game/race_int.h"
#include "platform/ega.h"

#define SEG_LAST     0x45F     /* the table's last segment; the road beyond is flat and straight */
#define SLICES       8         /* per segment */
#define FAR_ROWS     14        /* the original's farthest row */
#define DASH_HALF    12.0      /* half the centre dash's world width (dash_width_front: 7 px at z 12) */
#define COL_ROAD     1
#define COL_FINISH   15
#define COL_KERB     4
#define COL_GRASS    9

typedef struct {
    int base;                  /* segment the relative yaw / height are measured from */
    double pos, lat, hv;       /* position, camera x, view heading relative to YAW[base] */
    double hpos;               /* height at pos relative to HGT[base] */
} Cam;

static int segc(int i) { return i < 0 ? 0 : i > SEG_LAST ? SEG_LAST : i; }
static u16 trk(u16 arr, int i) { return TRKW(arr, segc(i)); }

static double yaw_rel(const Cam *c, int i) { return (s16)(trk(TRK_YAW, i) - trk(TRK_YAW, c->base)); }
static double hgt_rel(const Cam *c, int i) { return (s16)(trk(TRK_HGT, i) - trk(TRK_HGT, c->base)); }

static double interp(const Cam *c, double s, double (*f)(const Cam *, int))
{
    double fl = floor(s);
    int i = (int)fl;
    return f(c, i) + (f(c, i + 1) - f(c, i)) * (s - fl);
}

/* road edges for the centre flags (edges_of, race_road.c) */
static void edges(u16 cm, double *xl, double *xr)
{
    cm &= 0xE0;
    if (cm == 0)        { *xl = 0x5DC; *xr = 0xCE4; }
    else if (cm & 0x80) { *xl = 0x7D0; *xr = 0xAF0; }
    else if (cm & 0x20) { *xl = 0x7D0; *xr = 0xCE4; }
    else                { *xl = 0x5DC; *xr = 0xAF0; }
}

typedef struct { double z, dx, dy; } Row;

static Row row_at(const Cam *c, double s)
{
    Row r;
    r.z = 8.0 * (s - c->pos) + 4.0;
    r.dx = c->hv - interp(c, s, yaw_rel);
    r.dy = c->hpos - interp(c, s, hgt_rel);
    return r;
}
static double px(const Cam *c, const Row *r, double X) { return 160.0 - 4.0 * (c->lat - (X + r->z * r->dx / 4.0)) / r->z; }
static double py(const Row *r, double Y) { return 4.0 * (Y + r->z * r->dy - 0x76C) / r->z + 60.0; }

/* world x of a road line at s: the vertex at segment m has the edges of C[m], linear between */
static void edges_at(double s, double *xl, double *xr)
{
    double fl = floor(s), t = s - fl, al, ar, bl, br;
    edges(trk(TRK_C, (int)fl), &al, &ar);
    edges(trk(TRK_C, (int)fl + 1), &bl, &br);
    *xl = al + (bl - al) * t;
    *xr = ar + (br - ar) * t;
}

/* ---------------------------------------------------------------- sky and horizon */

static u8 src_rows[0xB8][320];     /* page 1 rows 66h..B7h: the sky and horizon sheets */

static void read_sheets(void)
{
    for (int y = 0x66; y < 0xB8; y++) {
        u16 o = (u16)(0x2000 + y * EGA_BYTES_PER_LINE);
        for (int x = 0; x < 320; x++) {
            u16 b = (u16)(o + (x >> 3));
            int bit = 7 - (x & 7), c = 0;
            for (int k = 0; k < 4; k++) c |= (ega_plane(k)[b] >> bit & 1) << k;
            src_rows[y][x] = (u8)c;
        }
    }
}

/* one sample row from sheet row sy, scrolled so that screen x shows sheet column x - shift */
static void sheet_row(int j, int sy, double shift)
{
    EnhBuf *b = &enh_buf;
    if (sy < 0x66) sy = 0x66;
    if (sy > 0xB7) sy = 0xB7;
    u8 *out = b->px + (size_t)j * b->w;
    for (int i = 0; i < b->w; i++) {
        double x = (i + 0.5) / b->sa - shift;
        int col = (int)floor(x) % 320;
        if (col < 0) col += 320;
        out[i] = src_rows[sy][col];
    }
}

/* road_proj_y(F0h, 7D0h, h) in the original's 16-bit arithmetic */
static double horizon_y(s16 h)
{
    s16 y = (s16)(0x7D0 + (s16)(0xF0 * h));
    s16 t = (s16)(u16)((u16)(s16)(y - 0x76C) << 2);
    return 0x3C + t / 0xF0;
}

static void draw_sky(const Cam *c, bool town, double *ground_y)
{
    EnhBuf *b = &enh_buf;
    read_sheets();
    if (town) {                    /* fixed: sky 1Dh rows from 66h, skyline 10h rows from A3h at 2Fh */
        for (int j = 0; j < b->h; j++) {
            double y = ENH_VIEW_Y0 + (j + 0.5) / b->sa;
            if (y < 0x2F) sheet_row(j, 0x66 + (int)floor(y - 0x12), 0);
            else if (y < 0x3F) sheet_row(j, 0xA3 + (int)floor(y - 0x2F), 0);
            else { enh_fill_rows(y - 0.5 / b->sa, ENH_VIEW_Y1, COL_ROAD); break; }
        }
        *ground_y = 0x3F;
        return;
    }
    s16 h0 = (s16)trk(TRK_HGT, c->base);
    double hy = horizon_y(h0) + 4.0 * c->hpos;
    double heading = (s16)trk(TRK_YAW, c->base) + c->hv;
    for (int j = 0; j < b->h; j++) {
        double y = ENH_VIEW_Y0 + (j + 0.5) / b->sa;
        if (y < hy - 12) sheet_row(j, (int)floor(y + 0xA6 - hy), 2.0 * heading);
        else if (y < hy + 8) sheet_row(j, 0xA3 + (int)floor(y - (hy - 12)), 4.0 * heading);
        else { enh_fill_rows(y - 0.5 / b->sa, ENH_VIEW_Y1, COL_GRASS); break; }
    }
    *ground_y = hy + 8;
}

/* ---------------------------------------------------------------- road */

static void quad(double xa0, double xa1, double ya, double xb0, double xb1, double yb, u8 col)
{
    double xs[4] = { xb0, xb1, xa1, xa0 }, ys[4] = { yb, yb, ya, ya };
    enh_fill_poly(xs, ys, 4, col);
}

/* the slice between sa (near) and sb (far) */
static void draw_slice(const Cam *c, double sa, double sb, bool town)
{
    Row ra = row_at(c, sa), rb = row_at(c, sb);
    double ya = py(&ra, 0x7D0), yb = py(&rb, 0x7D0);
    if (yb >= ya) return;                                   /* back-facing (beyond a crest) */
    int n = segc((int)ceil(sb - 1e-9));
    u16 C = trk(TRK_C, n), L = trk(TRK_L, n), R = trk(TRK_R, n);
    bool cross = C & 1;
    enh_fill_rows(yb, ya, town ? COL_ROAD : COL_GRASS);    /* the ground of this slice */

    double la, ra_, lb, rb_;
    edges_at(sa, &la, &ra_);
    edges_at(sb, &lb, &rb_);
    double xla = px(c, &ra, la), xra = px(c, &ra, ra_), xlb = px(c, &rb, lb), xrb = px(c, &rb, rb_);
    if (!cross) {
        quad(xla, xra, ya, xlb, xrb, yb, (C & 0x10) ? COL_FINISH : COL_ROAD);
    } else {
        if (C & 4) return;                                  /* intersection */
        double xola = px(c, &ra, 0x44C), xolb = px(c, &rb, 0x44C);
        double xora = px(c, &ra, 0xE74), xorb = px(c, &rb, 0xE74);
        if (L & 0x80) {
            double ma = (xola + xla) / 2, mb = (xolb + xlb) / 2;
            quad(xola, ma, ya, xolb, mb, yb, COL_KERB);
            quad(ma, xla, ya, mb, xlb, yb, COL_GRASS);
        } else
            quad(xola, xla, ya, xolb, xlb, yb, COL_KERB);
        if (R & 0x80) {
            double ma = (xora + xra) / 2, mb = (xorb + xrb) / 2;
            quad(xra, ma, ya, xrb, mb, yb, COL_GRASS);
            quad(ma, xora, ya, mb, xorb, yb, COL_KERB);
        } else
            quad(xra, xora, ya, xrb, xorb, yb, COL_KERB);
    }
    if (C & 8) {                                            /* centre dash */
        double ma = (la + ra_) / 2, mb = (lb + rb_) / 2;
        quad(px(c, &ra, ma - DASH_HALF), px(c, &ra, ma + DASH_HALF), ya,
             px(c, &rb, mb - DASH_HALF), px(c, &rb, mb + DASH_HALF), yb, COL_KERB);
    }
}

void enh_draw_road(const EnhView *v)
{
    enh_buf_setup();
    Cam c;
    c.base = segc((int)floor(v->pos));
    c.pos = v->pos;
    c.lat = v->lat;
    c.hpos = interp(&c, v->pos, hgt_rel);
    c.hv = interp(&c, v->pos, yaw_rel) + v->rel_heading;

    double ground_y;
    draw_sky(&c, v->town, &ground_y);
    (void)ground_y;

    /* slices far to near on a fixed grid of 1/8 segment, from the original's farthest row to just in
     * front of the eye (z = 2) */
    double s_far = v->pos + FAR_ROWS, s_near = v->pos - 0.25;
    double sb = s_far;
    double s = floor(s_far * SLICES) / SLICES;
    if (s >= sb) s -= 1.0 / SLICES;
    while (sb > s_near) {
        double sa = s > s_near ? s : s_near;
        draw_slice(&c, sa, sb, v->town);
        sb = sa;
        s -= 1.0 / SLICES;
    }
}
