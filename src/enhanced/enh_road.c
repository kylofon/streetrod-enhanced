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
#include <stdlib.h>

#include "game/race_int.h"
#include "mem.h"
#include "platform/ega.h"
#include "platform/video.h"

#define SEG_LAST     0x45F     /* the table's last segment; the road beyond is flat and straight */
#define SLICES       8         /* per segment */
#define FAR_ROWS     14        /* the original's farthest row */
#define DASH_HALF    12.0      /* half the centre dash's world width (dash_width_front: 7 px at z 12) */
#define COL_ROAD     1
#define COL_FINISH   15
#define COL_KERB     4
#define COL_GRASS    9
#define COL_WALL_A   2
#define COL_WALL_B   3
#define COL_STOREY   4
#define COL_BANNER   0x0A
#define Y_ROAD       0x7D0     /* world heights: the road, the eye (the top of a façade) */
#define Y_EYE        0x76C
#define OBJ_ROWS     10        /* road-side objects: the original's rows 1..10 */

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
    if (town && !(C & 4)) {                                 /* façades and upper storeys (town_walls_front) */
        double yea = py(&ra, Y_EYE), yeb = py(&rb, Y_EYE);
        for (int side = 0; side < 2; side++) {
            u16 F = side ? R : L;
            double X = side ? 0xE74 : 0x44C, xa = px(c, &ra, X), xb = px(c, &rb, X);
            double xs[4] = { xa, xb, xb, xa }, ys[4] = { ya, yb, yeb, yea };
            enh_fill_poly(xs, ys, 4, (F & 0x10) ? COL_WALL_A : COL_WALL_B);
            if (F & 4) {
                double Yt = (F & 8) ? 2 * Y_EYE - Y_ROAD : Y_EYE - (Y_ROAD - Y_EYE) / 2;
                double ts[4] = { yea, yeb, py(&rb, Yt), py(&ra, Yt) };
                enh_fill_poly(xs, ts, 4, COL_STOREY);
            }
        }
    }
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

/* ---------------------------------------------------------------- road-side objects */

/* A row of the original at segment n: the projected road lines (the race_road.c row tables). */
typedef struct { Row r; double xl, xr, xol, xor_, y, ye; int n; } ObjRow;

static ObjRow obj_row(const Cam *c, int n)
{
    ObjRow o;
    o.n = n;
    o.r = row_at(c, n);
    double l, r;
    edges(trk(TRK_C, n), &l, &r);
    o.xl = px(c, &o.r, l);
    o.xr = px(c, &o.r, r);
    o.xol = px(c, &o.r, 0x44C);
    o.xor_ = px(c, &o.r, 0xE74);
    o.y = py(&o.r, Y_ROAD);
    o.ye = py(&o.r, Y_EYE);
    return o;
}

/* The original draws the size made for its row (size 9 - row, the largest for rows 1..3); the enhanced
 * view takes the size made for the nearest row at or in front of the object and scales it by depth. */
static int size_for(double z, int nsizes, double *f)
{
    int k = 9 - (int)floor(z / 8.0);
    if (k > nsizes - 1) k = nsizes - 1;
    if (k < 0) k = 0;
    *f = 8.0 * (9 - k) / z;
    return k;
}

/* shape_draw: an hline sprite of the shape table, type `type`, anchored at (x, y) */
static void shape(double x, double y, double z, int side, int type)
{
    u16 set = DSW((u16)(DS_shape_table + 2 * type));
    int nsizes = DSS(set);
    if (nsizes <= 0 || z <= 0.5) return;
    double f;
    int k = size_for(z, nsizes, &f);
    u16 e = (u16)(DSW((u16)(set + 2)) + 6 * k);
    int n = DSS(e);
    FarPtr ln = ds_far((u16)(e + 2));
    for (; n > 0; n--, ln.off = (u16)(ln.off + 4)) {
        int ldx = (s8)rd8(ln.seg, ln.off), ldy = (s8)rd8(ln.seg, (u16)(ln.off + 1));
        int lw = (s8)rd8(ln.seg, (u16)(ln.off + 2)), lc = rd8(ln.seg, (u16)(ln.off + 3));
        double x0, x1;
        if (side == -1) { x0 = x - f * (ldx + lw); x1 = x - f * (ldx - 1); }
        else            { x0 = x + f * ldx;        x1 = x + f * (ldx + lw + 1); }
        enh_fill_rect(x0, y + f * ldy, x1, y + f * (ldy + 1), (u8)lc);
    }
}

/* scenery_draw_sign_picture: the largest of the 7 pre-scaled pictures, scaled; kind 1 = page 2 row 66h
 * (the sign sheet), 2 / 3 = page 0 rows 64h / 8Fh (the town billboards) */
static void sign_picture(double x, double y, double z, int align, int kind)
{
    if (z <= 0.5) return;
    u16 e = (u16)(DS_sign_sizes + 6 * 6);
    int sx = DSS(e), w = DSS((u16)(e + 2)), h = DSS((u16)(e + 4));
    const EnhImage *img = kind == 1 ? enh_image_vram(0x4000, sx, 0x66, w, h)
                                    : enh_image_vram(0x0000, sx, kind == 2 ? 0x64 : 0x8F, w, h);
    double f = 24.0 / z, W = w * f, H = h * f, x0, y0;
    if (align == 1)      { x0 = x - W;     y0 = y - H; }
    else if (align == 0) { x0 = x - W / 2; y0 = y - H / 2; }
    else                 { x0 = x;         y0 = y - H; }
    enh_draw_image(img, x0, y0, x0 + W, y0 + H);
}

/* scenery_draw_banner: two posts, the cross bar, then a text shape or a picture */
static void banner(const ObjRow *o, u16 flags, u16 C)
{
    double z = o->r.z, t = 1.0 + 48.0 / z;
    double ytop = o->y - 4.0 * (o->y - o->ye);
    enh_fill_rect(o->xl - t, ytop, o->xl, o->y, COL_BANNER);
    enh_fill_rect(o->xr + 1, ytop, o->xr + 1 + t, o->y, COL_BANNER);
    enh_fill_rect(o->xl, ytop, o->xr + 1, ytop + t, COL_BANNER);
    double mid = (o->xl + o->xr) / 2;
    if (flags & 0x1000)      shape(mid, ytop, z, 1, 0x0D);
    else if (flags & 0x2000) sign_picture(mid, ytop, z, 0, 1);
    else if (C & 1)          sign_picture(mid, ytop, z, 0, (flags & 0x4000) ? 2 : 3);
    else                     shape(mid, ytop, z, 1, (flags & 0x4000) ? 0x0C : 0x0E);
}

/* draw_row_objects_front (race_road.c) at segment n */
static void row_objects(const ObjRow *o)
{
    int n = o->n;
    u16 c = trk(TRK_C, n), L = trk(TRK_L, n), R = trk(TRK_R, n);
    bool cross = c & 1;
    double z = o->r.z, y = o->y;
    if (L & 0x20) shape(cross ? o->xl : o->xol, y, z, -1, 0);
    if (L & 0x100) {
        if (cross)          shape(o->xl, y, z, -1, 4);
        else if (L & 0x200) shape(2 * o->xol - o->xl, y, z, -1, 4);
        else                shape(o->xol, y, z, -1, 4);
    }
    if (L & 0x800)  shape(o->xl, y, z, 1, 3);
    if (L & 0x1000) shape(o->xl, y, z, 1, 6);
    if (L & 1) {
        if (cross) sign_picture(o->xl, (o->ye + y) / 2, z, 1, 1);
        else       shape(o->xl, y, z, 1, 1);
    }
    if (cross) {
        if (L & 0x40) shape(o->xl, y, z, 1, 2);             /* hydrants */
        if (R & 0x40) shape(o->xr, y, z, -1, 2);
    } else {
        if (L & 4) shape(o->xl, y, z, -1, 7);
        if (L & 8) shape(o->xl, y, z, -1, 8);
        if (L & 0x40) shape(-((o->xl - o->xol) / 2 - o->xol), y, z, -1, 0x0A);
        if (L & 0x80) shape(o->xol, y, z, -1, 0x0B);
        if (R & 4) shape(o->xr, y, z, 1, 7);
        if (R & 8) shape(o->xr, y, z, 1, 8);
        if (R & 0x40) shape(o->xor_, y, z, 1, 0x0A);
        if (R & 0x80) shape((o->xor_ - o->xr) / 2 + o->xor_, y, z, 1, 0x0B);
        if (c & 2) shape(o->xr, y, z, 1, 9);
    }
    if (R & 0x20) shape(cross ? o->xr : o->xor_, y, z, 1, 0);
    if (R & 0x100) {
        if (cross)          shape(o->xr, y, z, 1, 4);
        else if (R & 0x200) shape(2 * o->xor_ - o->xr, y, z, 1, 4);
        else                shape(o->xor_, y, z, 1, 4);
    }
    if (R & 0x2000) shape(o->xr, y, z, 1, 0x12);
    if (R & 0x4000) shape(o->xr, y, z, 1, 0x13);
    if (R & 0x800)  shape(o->xr, y, z, 1, 3);
    if (R & 0x1000) shape(o->xr, y, z, -1, 6);
    if (R & 1) {
        if (cross) sign_picture(o->xr, (o->ye + y) / 2, z, -1, 1);
        else       shape(o->xr, y, z, 1, 1);
    }
    if (c & 0xF000) banner(o, c & 0xF000, c);
    if (c & 0x100) shape(o->xr, y, z, 1, 5);
    if (c & 0x800) shape(o->xr, y, z, 1, 0x0F);
    if (c & 0x200) shape(o->xr, y, z, 1, 0x10);
    if (c & 0x400) shape(o->xr, y, z, 1, 0x11);
}

/* The end faces of a town block at the near end of segment n (town_walls_front): the side wall along a
 * cross street, and the side face where an upper storey ends. */
static void town_block_ends(const Cam *c, int n)
{
    if (trk(TRK_C, n) & 4) return;
    ObjRow a = obj_row(c, n - 1), b = obj_row(c, n);
    if (a.r.z <= 0.5) return;
    bool street = trk(TRK_C, n - 1) & 4;
    for (int side = 0; side < 2; side++) {
        u16 F = trk(side ? TRK_R : TRK_L, n), Fq = trk(side ? TRK_R : TRK_L, n - 1);
        double xa = side ? a.xor_ : a.xol, xb = side ? b.xor_ : b.xol;
        if (street) {
            double x0 = side ? xa : 0, x1 = side ? 320 : xa;
            enh_fill_rect(x0, a.ye, x1, a.y - 1, COL_WALL_A);
            enh_fill_rect(x0, a.y - 1, x1, a.y, COL_STOREY);
        }
        if ((F & 4) && !(Fq & 4)) {
            double Yt = (F & 8) ? 2 * Y_EYE - Y_ROAD : Y_EYE - (Y_ROAD - Y_EYE) / 2;
            double top = py(&a.r, Yt), ext = xa + (xa - xb);   /* as wide again as the block's last segment */
            enh_fill_rect(SDL_min(xa, ext), top, SDL_max(xa, ext), a.ye, COL_WALL_A);
        }
    }
}

/* ---------------------------------------------------------------- the opponent (opponent_draw) */

/* The rear views 3..8 are the native rear view (2) shrunk for the depths below: k / z fits their sizes. */
static double rear_scale_k(void)
{
    static const double zc[9] = { 0, 0, 0, 24, 36, 52, 68, 84, 108 };
    FarPtr n = ds_far(DS_opp_sprites + 4 * 2);
    if (far_is_null(n) || desc_w(n) == 0) return 20;
    double sum = 0;
    int cnt = 0;
    for (int s = 3; s <= 8; s++) {
        FarPtr p = ds_far((u16)(DS_opp_sprites + 4 * s));
        if (far_is_null(p)) continue;
        sum += (double)desc_w(p) / desc_w(n) * zc[s];
        cnt++;
    }
    return cnt ? sum / cnt : 20;
}

static void opponent(const Cam *c, const EnhView *v)
{
    int s = v->opp_sprite;
    double d = (v->opp_pos - v->pos) * 8.0;
    if (s < 0 || s > 8 || d >= 0x78 || d < -8) return;
    Row r = row_at(c, v->opp_pos);
    if (s == 0) r.z += 0x10;
    if (r.z < 1) return;
    double sx = px(c, &r, v->opp_lat), sy = py(&r, Y_ROAD);
    int pic = s >= 2 ? 2 : s;                                /* the rear views: the native picture, scaled */
    FarPtr p = ds_far((u16)(DS_opp_sprites + 4 * pic));
    const EnhImage *img = enh_image_bitmap(p.seg, p.off);
    if (!img) return;
    if (s == 0) {                                            /* alongside on the left, cut at the screen edge */
        double x0 = sx - img->w - 0x40;
        enh_draw_image(img, x0, sy, x0 + img->w, sy + img->h);
    } else if (s == 1) {
        enh_draw_image(img, sx - img->w, sy - img->h + 0x11, sx, sy + 0x11);
    } else {
        double f = rear_scale_k() / r.z;
        if (f > 1.3) f = 1.3;
        double w = img->w * f, h = img->h * f, bottom = sy;
        if (s == 2 && bottom > 0x65) bottom = 0x65;
        enh_draw_image(img, sx - w / 2, bottom - h, sx + w / 2, bottom);
    }
}

/* ---------------------------------------------------------------- the frame */

typedef struct { double key; int kind, n; } Event;
enum { EV_OBJECTS, EV_BLOCK_END, EV_OPPONENT };

static int event_cmp(const void *a, const void *b)
{
    double ka = ((const Event *)a)->key, kb = ((const Event *)b)->key;
    return ka < kb ? 1 : ka > kb ? -1 : 0;
}

static void draw_event(const Cam *c, const EnhView *v, const Event *e)
{
    if (e->kind == EV_OBJECTS) {
        ObjRow o = obj_row(c, e->n);
        row_objects(&o);
    } else if (e->kind == EV_BLOCK_END) {
        town_block_ends(c, e->n);
    } else {
        opponent(c, v);
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

    /* What stands on the road, each drawn once the road up to one segment in front of it is drawn (the
     * original draws a row's objects and the opponent after the band in front of the row). */
    Event ev[64];
    int nev = 0;
    for (int n = (int)floor(s_far); n > s_near && nev < 60; n--) {
        if (n - v->pos > 0 && n - v->pos <= OBJ_ROWS && !(trk(TRK_C, n) & 4))
            ev[nev++] = (Event){ n - 1.0, EV_OBJECTS, n };
        if (v->town) ev[nev++] = (Event){ n - 1.0 + 1e-6, EV_BLOCK_END, n };
    }
    if (!v->town) ev[nev++] = (Event){ v->opp_pos - 1.0, EV_OPPONENT, 0 };
    qsort(ev, (size_t)nev, sizeof ev[0], event_cmp);
    int e = 0;

    double sb = s_far;
    double s = floor(s_far * SLICES) / SLICES;
    if (s >= sb) s -= 1.0 / SLICES;
    while (sb > s_near) {
        while (e < nev && ev[e].key >= sb) draw_event(&c, v, &ev[e++]);
        double sa = s > s_near ? s : s_near;
        draw_slice(&c, sa, sb, v->town);
        sb = sa;
        s -= 1.0 / SLICES;
    }
    while (e < nev) draw_event(&c, v, &ev[e++]);
}
