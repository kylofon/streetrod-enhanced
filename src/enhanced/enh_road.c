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
#define SKY_SCROLL   0.125     /* the sky and horizon scroll this fraction of the original's (heading x2, x4) */

typedef struct {
    int base;                  /* segment the relative yaw / height are measured from */
    double pos, lat, hv;       /* position, camera x, view heading relative to YAW[base] */
    double hpos;               /* height at pos relative to HGT[base] */
} Cam;

static int segc(int i) { return i < 0 ? 0 : i > SEG_LAST ? SEG_LAST : i; }
static u16 trk(u16 arr, int i)
{
    if (DSS(DS_racing) != 0 && DSS(DS_race_type) == 0) {
        if (i > 0x17C) {
            if (arr == TRK_YAW || arr == TRK_HGT) return TRKW(arr, 0x17C);
            return 0;
        }
    }
    if (i > SEG_LAST) {
        if (arr == TRK_YAW || arr == TRK_HGT) return TRKW(arr, SEG_LAST);
        return 0;
    }
    return TRKW(arr, segc(i));
}

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

typedef struct { double z, dx, dy; bool mirror; } Row;

static Row row_at(const Cam *c, double s)
{
    Row r;
    r.z = 8.0 * (s - c->pos) + 4.0;
    r.dx = c->hv - interp(c, s, yaw_rel);
    r.dy = c->hpos - interp(c, s, hgt_rel);
    r.mirror = false;
    return r;
}

/* The mirror (road_proj_x_mirror / road_proj_y_mirror): the road behind, row k of the original at depth
 * substep + 8k, the signs of the heading and height differences reversed, centre 118h, horizon 24h, and
 * a quarter of the front view's scale. */
static Row row_at_mirror(const Cam *c, double s)
{
    Row r;
    r.z = 8.0 * (c->pos - s);
    r.dx = interp(c, s, yaw_rel) - c->hv;
    r.dy = interp(c, s, hgt_rel) - c->hpos;
    r.mirror = true;
    return r;
}

static double px(const Cam *c, const Row *r, double X)
{
    if (r->mirror) return 0x118 - (c->lat - (X + r->z * r->dx / 4.0)) / r->z;
    return 160.0 - 4.0 * (c->lat - (X + r->z * r->dx / 4.0)) / r->z;
}
static double py(const Row *r, double Y)
{
    if (r->mirror) return (Y + r->z * r->dy - 0x76C) / r->z + 0x24;
    return 4.0 * (Y + r->z * r->dy - 0x76C) / r->z + 60.0;
}
/* the depth that sizes a road-side object: the mirror's quarter scale is four times as far */
static double size_depth(const Row *r) { return r->mirror ? 4.0 * r->z : r->z; }

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

/* road_proj_y(F0h, 7D0h, h) in the original's 16-bit arithmetic */
static double horizon_y(s16 h)
{
    s16 y = (s16)(0x7D0 + (s16)(0xF0 * h));
    s16 t = (s16)(u16)((u16)(s16)(y - 0x76C) << 2);
    return 0x3C + t / 0xF0;
}

static void draw_sky(const Cam *c, bool town, double *ground_y)
{
    if (town) {                    /* fixed: sky 1Dh rows from 66h, skyline 10h rows from A3h at 2Fh */
        enh_draw_sky(true, 0, 0);
        *ground_y = 0x3F;
        return;
    }
    s16 h0 = (s16)trk(TRK_HGT, c->base);
    double hy = horizon_y(h0) + 4.0 * c->hpos;
    double heading = ((s16)trk(TRK_YAW, c->base) + c->hv) * SKY_SCROLL;
    enh_draw_sky(false, hy, heading);
    *ground_y = hy + 8;
}

/* ---------------------------------------------------------------- road */

static void quad(double xa0, double xa1, double ya, double xb0, double xb1, double yb, u8 col)
{
    double xs[4] = { xb0, xb1, xa1, xa0 }, ys[4] = { yb, yb, ya, ya };
    enh_fill_poly(xs, ys, 4, col);
}

/* A slice of road between sa (near) and sb (far) with the screen rows of its near and far edges. A slice
 * faces the eye when its far edge is higher on the screen than its near edge; one that does not lies beyond
 * a crest and is hidden by the ground in front of it. */
typedef struct { double sa, sb, ya, yb, occ; bool front, ground; } Slice;
#define MAX_SLICES 1024

/* The ground of the facing slices, laid down before any road: the rows of a slice belong to the ground
 * across the whole width (the road has no side slope), the sky's own ground only begins below its horizon.
 * occ[i] is the top row of everything nearer than slice i: what lies farther is clipped to the rows above
 * it, so that a crest hides the road and the objects behind it without painting over a quad. */
static void slices_ground(Slice *sl, int ns, double y_open, u8 col)
{
    double occ = y_open;
    for (int i = ns - 1; i >= 0; i--) {                     /* near to far */
        sl[i].occ = occ;
        if (!sl[i].front) continue;
        if (sl[i].ground) enh_fill_rows(sl[i].yb, fmin(sl[i].ya, occ), col);
        if (sl[i].yb < occ) occ = sl[i].yb;
    }
}

/* the rows above occ within box (x0, y0, x1, y1), set only when it changes */
static void clip_occ(const double box[4], double occ, double *cur)
{
    occ = fmin(occ, box[3]);
    if (occ == *cur) return;
    *cur = occ;
    enh_clip(box[0], box[1], box[2], occ);
}

/* what is drawn just before slice s lies behind it: the slice itself hides it too */
static double occ_before(const Slice *s) { return s->front ? fmin(s->occ, s->yb) : s->occ; }

/* the slice between sa (near) and sb (far): road, kerbs, dash and town façades, no ground */
static void draw_slice(const Cam *c, const Slice *s, bool town)
{
    double sa = s->sa, sb = s->sb, ya = s->ya, yb = s->yb;
    if (!s->front) return;
    Row ra = row_at(c, sa), rb = row_at(c, sb);
    int n = segc((int)ceil(sb - 1e-9));
    u16 C = trk(TRK_C, n), L = trk(TRK_L, n), R = trk(TRK_R, n);
    bool cross = C & 1;

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

static ObjRow obj_row(const Cam *c, int n, bool mirror)
{
    ObjRow o;
    o.n = n;
    o.r = mirror ? row_at_mirror(c, n) : row_at(c, n);
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
 * view takes the largest (most detailed) size whose lines are still a sample tall, scaled by depth. Farther
 * away it steps down through the original's smaller sizes (mip levels) instead of thinning the largest
 * one's lines below a sample, where they would come and go from frame to frame. */
static int size_for(double z, int nsizes, double *f)
{
    double min_f = 1.0 / enh_buf.sa;
    int k = SDL_min(nsizes, 9) - 1;
    while (k > 0 && 8.0 * (9 - k) / z < min_f) k--;
    *f = 8.0 * (9 - k) / z;
    return k;
}

/* shape_draw: an hline sprite of the shape table, type `type`, anchored at (x, y); each line covers at
 * least one sample each way, so a far shape stays a steady speck instead of flickering */
static void shape(double x, double y, double z, int side, int type)
{
    u16 set = DSW((u16)(DS_shape_table + 2 * type));
    int nsizes = DSS(set);
    if (nsizes <= 0 || z <= 0.5) return;
    double f, m = 1.0 / enh_buf.sa;
    int k = size_for(z, nsizes, &f);
    u16 e = (u16)(DSW((u16)(set + 2)) + 6 * k);
    int n = DSS(e);
    FarPtr ln = ds_far((u16)(e + 2));
    for (; n > 0; n--, ln.off = (u16)(ln.off + 4)) {
        int ldx = (s8)rd8(ln.seg, ln.off), ldy = (s8)rd8(ln.seg, (u16)(ln.off + 1));
        int lw = (s8)rd8(ln.seg, (u16)(ln.off + 2)), lc = rd8(ln.seg, (u16)(ln.off + 3));
        double x0, x1, y0 = y + f * ldy;
        if (side == -1) { x0 = x - f * (ldx + lw); x1 = x - f * (ldx - 1); }
        else            { x0 = x + f * ldx;        x1 = x + f * (ldx + lw + 1); }
        enh_fill_rect(x0, y0, SDL_max(x1, x0 + m), SDL_max(y0 + f, y0 + m), (u8)lc);
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
    W = SDL_max(W, 1.0 / enh_buf.sa);                       /* at least a sample: no flicker far away */
    H = SDL_max(H, 1.0 / enh_buf.sa);
    if (align == 1)      { x0 = x - W;     y0 = y - H; }
    else if (align == 0) { x0 = x - W / 2; y0 = y - H / 2; }
    else                 { x0 = x;         y0 = y - H; }
    enh_draw_image(img, x0, y0, x0 + W, y0 + H);
}

/* scenery_draw_banner: two posts, the cross bar, then a text shape or a picture */
static void banner(const ObjRow *o, u16 flags, u16 C)
{
    double z = size_depth(&o->r), t = 1.0 + 48.0 / z;
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
    double z = size_depth(&o->r), y = o->y;
    bool mirror = o->r.mirror;                               /* the mirror has no sign pictures, no banners */
    if (L & 0x20) shape(cross ? o->xl : o->xol, y, z, -1, 0);
    if (L & 0x100) {
        if (cross)          shape(o->xl, y, z, -1, 4);
        else if (L & 0x200) shape(2 * o->xol - o->xl, y, z, -1, 4);
        else                shape(o->xol, y, z, -1, 4);
    }
    if (L & 0x800)  shape(o->xl, y, z, 1, 3);
    if (L & 0x1000) shape(o->xl, y, z, 1, 6);
    if (L & 1) {
        if (cross) { if (!mirror) sign_picture(o->xl, (o->ye + y) / 2, z, 1, 1); }
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
        if (cross) { if (!mirror) sign_picture(o->xr, (o->ye + y) / 2, z, -1, 1); }
        else       shape(o->xr, y, z, 1, 1);
    }
    if ((c & 0xF000) && !mirror) banner(o, c & 0xF000, c);
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
    ObjRow a = obj_row(c, n - 1, false), b = obj_row(c, n, false);
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
    if (d < -8 || d > enh_opt.draw_distance * 8.0) return;
    if (d >= 0x78) s = 2;                                    /* far rear view */
    if (s < 0 || s > 8) return;
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
        if (w < 0.5 || h < 0.5) return;
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
        ObjRow o = obj_row(c, e->n, false);
        row_objects(&o);
    } else if (e->kind == EV_BLOCK_END) {
        town_block_ends(c, e->n);
    } else {
        opponent(c, v);
    }
}

/* ---------------------------------------------------------------- the rear-view mirror (road_draw_mirror) */

#define MIR_X0 0xF2            /* clip_set_mirror: (F2h, 16h) .. (13Dh, 2Ah) */
#define MIR_Y0 0x16
#define MIR_X1 0x13E
#define MIR_Y1 0x2B
#define MIR_ROWS 7             /* the original's rows 7..1 behind the car */

/* road_draw_band_mirror + town_walls_mirror: the slice between sa (farther back) and sb, no ground. The
 * Slice keeps its near edge in sa / ya as in the front view. */
static void mirror_slice(const Cam *c, const Slice *s, bool town)
{
    if (!s->front) return;
    double sa = s->sb, sb = s->sa, ya = s->yb, yb = s->ya;
    Row ra = row_at_mirror(c, sa), rb = row_at_mirror(c, sb);
    int n = segc((int)ceil(sb - 1e-9));
    u16 C = trk(TRK_C, n), L = trk(TRK_L, n), R = trk(TRK_R, n);
    bool cross = C & 1;
    if (town && cross && !(C & 4)) {                        /* façades; upper storeys: left half, right double */
        double yea = py(&ra, Y_EYE), yeb = py(&rb, Y_EYE);
        for (int side = 0; side < 2; side++) {
            u16 F = side ? R : L;
            double X = side ? 0xE74 : 0x44C, xa = px(c, &ra, X), xb = px(c, &rb, X);
            double xs[4] = { xa, xb, xb, xa }, ys[4] = { ya, yb, yeb, yea };
            enh_fill_poly(xs, ys, 4, (F & 0x10) ? COL_WALL_A : COL_WALL_B);
            if (F & 4) {
                double Yt = side ? 2 * Y_EYE - Y_ROAD : Y_EYE - (Y_ROAD - Y_EYE) / 2;
                double ts[4] = { yea, yeb, py(&rb, Yt), py(&ra, Yt) };
                enh_fill_poly(xs, ts, 4, COL_STOREY);
            }
        }
    }
    double la, ra_, lb, rb_;
    edges_at(sa, &la, &ra_);
    edges_at(sb, &lb, &rb_);
    double xla = px(c, &ra, la), xra = px(c, &ra, ra_), xlb = px(c, &rb, lb), xrb = px(c, &rb, rb_);
    if (!cross) {
        quad(xlb, xrb, yb, xla, xra, ya, COL_ROAD);
    } else {
        if (C & 4) return;
        double xola = px(c, &ra, 0x44C), xolb = px(c, &rb, 0x44C);
        double xora = px(c, &ra, 0xE74), xorb = px(c, &rb, 0xE74);
        if (L & 0x80) {
            double ma = (xola + xla) / 2, mb = (xolb + xlb) / 2;
            quad(xolb, mb, yb, xola, ma, ya, COL_KERB);
            quad(mb, xlb, yb, ma, xla, ya, COL_GRASS);
        } else
            quad(xolb, xlb, yb, xola, xla, ya, COL_KERB);
        if (R & 0x80) {
            double ma = (xora + xra) / 2, mb = (xorb + xrb) / 2;
            quad(xrb, mb, yb, xra, ma, ya, COL_GRASS);
            quad(mb, xorb, yb, ma, xora, ya, COL_KERB);
        } else
            quad(xrb, xorb, yb, xra, xora, ya, COL_KERB);
    }
    if (C & 8) {
        double ma = (la + ra_) / 2, mb = (lb + rb_) / 2;
        quad(px(c, &rb, mb - DASH_HALF), px(c, &rb, mb + DASH_HALF), yb,
             px(c, &ra, ma - DASH_HALF), px(c, &ra, ma + DASH_HALF), ya, COL_KERB);
    }
}

/* The front views 10..12 are the native front view (9) shrunk for the depths below. */
static double front_scale_k(void)
{
    static const double zc[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 40, 56 };
    FarPtr n = ds_far(DS_opp_sprites + 4 * 9);
    if (far_is_null(n) || desc_w(n) == 0) return 12;
    double sum = 0;
    int cnt = 0;
    for (int s = 10; s <= 12; s++) {
        FarPtr p = ds_far((u16)(DS_opp_sprites + 4 * s));
        if (far_is_null(p)) continue;
        sum += (double)desc_w(p) / desc_w(n) * zc[s];
        cnt++;
    }
    return cnt ? sum / cnt : 12;
}

static void mirror_opponent(const Cam *c, const EnhView *v)
{
    if (v->opp_sprite < 9 || v->opp_sprite > 12) return;
    Row r = row_at_mirror(c, v->opp_pos);
    if (r.z < 1) return;
    FarPtr p = ds_far(DS_opp_sprites + 4 * 9);
    const EnhImage *img = enh_image_bitmap(p.seg, p.off);
    if (!img) return;
    double f = front_scale_k() / r.z;
    if (f > 1.3) f = 1.3;
    double sx = px(c, &r, v->opp_lat), sy = py(&r, Y_ROAD), w = img->w * f, h = img->h * f;
    enh_draw_image(img, sx - w / 2, sy - h, sx + w / 2, sy);
}

static void draw_mirror(const Cam *c, const EnhView *v)
{
    FarPtr p = ds_far(DS_pic_mirror);                        /* mirror_draw_background */
    const EnhImage *bg = enh_image_bitmap(p.seg, p.off);
    if (bg) enh_draw_image(bg, 0xF0, 0x12, 0xF0 + bg->w, 0x12 + bg->h);
    double mir_rows = SDL_min(enh_opt.draw_distance / 2.0, 30.0);
    if (mir_rows < 7.0) mir_rows = 7.0;
    double s_far = v->pos - mir_rows, s_near = v->pos - 1;
    Row rf = row_at_mirror(c, s_far);
    double y7 = py(&rf, Y_ROAD);
    p = ds_far(DS_pic_mirror_sky);
    const EnhImage *sky = enh_image_bitmap(p.seg, p.off);
    if (sky && sky->h >= 2) {
        EnhImage strip = { sky->w, 2, sky->px };              /* its first 2 rows over the horizon */
        enh_draw_image(&strip, 0xF0, y7 - 2, 0xF0 + sky->w, y7);
    }
    enh_clip(MIR_X0, MIR_Y0, MIR_X1, MIR_Y1);
    if (!v->town) enh_fill_rows(y7, MIR_Y1, COL_GRASS);    /* the ground below the mirror's horizon */

    /* slices of 1/8 segment, far to near (the mirror's far rows are the ones further behind) */
    Slice sl[MAX_SLICES];
    int ns = 0;
    double sa = s_far;
    double s = ceil(s_far * SLICES) / SLICES;
    if (s <= sa) s += 1.0 / SLICES;
    while (sa < s_near && ns < MAX_SLICES) {
        double sb = s < s_near ? s : s_near;
        Slice *q = &sl[ns++];
        Row rn = row_at_mirror(c, sb), rf_ = row_at_mirror(c, sa);
        q->sa = sb;
        q->sb = sa;
        q->ya = py(&rn, Y_ROAD);
        q->yb = py(&rf_, Y_ROAD);
        q->front = rn.z > 0.5 && rf_.z > 0.5 && q->yb < q->ya - 1e-6;
        q->ground = !(trk(TRK_C, segc((int)ceil(sb - 1e-9))) & 1);
        sa = sb;
        s += 1.0 / SLICES;
    }
    slices_ground(sl, ns, MIR_Y1, COL_GRASS);

    /* a row's objects and the opponent after the band behind them, clipped to the rows above the road in
     * front of them */
    Event ev[128];
    int nev = 0;
    for (int n = (int)ceil(s_far); n < s_near && nev < 120; n++) {
        if (v->pos - n >= 1 && v->pos - n < mir_rows && !(trk(TRK_C, n) & 4))
            ev[nev++] = (Event){ n + 1.0, EV_OBJECTS, n };
    }
    if (!v->town && (v->pos - v->opp_pos) < mir_rows) ev[nev++] = (Event){ v->opp_pos + 1.0, EV_OPPONENT, 0 };
    int e = 0;
    for (int i = 1; i < nev; i++)                            /* ascending keys */
        for (int k = i; k > 0 && ev[k].key < ev[k - 1].key; k--) { Event t_ = ev[k]; ev[k] = ev[k - 1]; ev[k - 1] = t_; }
    const double box[4] = { MIR_X0, MIR_Y0, MIR_X1, MIR_Y1 };
    double clip_cur = MIR_Y1;
    for (int i = 0; i < ns; i++) {
        if (e < nev && ev[e].key <= sl[i].sb) {
            clip_occ(box, occ_before(&sl[i]), &clip_cur);
            for (; e < nev && ev[e].key <= sl[i].sb; e++) {
                if (ev[e].kind == EV_OBJECTS) { ObjRow o = obj_row(c, ev[e].n, true); row_objects(&o); }
                else mirror_opponent(c, v);
            }
        }
        if (!sl[i].front) continue;
        clip_occ(box, sl[i].occ, &clip_cur);
        mirror_slice(c, &sl[i], v->town);
    }
    clip_occ(box, MIR_Y1, &clip_cur);
    for (; e < nev; e++) {
        if (ev[e].kind == EV_OBJECTS) { ObjRow o = obj_row(c, ev[e].n, true); row_objects(&o); }
        else mirror_opponent(c, v);
    }
    enh_clip_view();
    enh_fill_rect(0xF0, 0x17, 0xF1, 0x2B, 2);                /* the frame */
    enh_fill_rect(0xF1, 0x17, 0xF2, 0x2B, 4);
    enh_fill_rect(0x13E, 0x17, 0x13F, 0x2B, 4);
    enh_fill_rect(0x13F, 0x17, 0x140, 0x2B, 2);
}

void enh_draw_road(const EnhView *v)
{
    enh_raster_begin();
    Cam c;
    c.base = segc((int)floor(v->pos));
    c.pos = v->pos;
    c.lat = v->lat;
    c.hpos = interp(&c, v->pos, hgt_rel);
    c.hv = interp(&c, v->pos, yaw_rel) + v->rel_heading;

    double ground_y;
    draw_sky(&c, v->town, &ground_y);
    (void)ground_y;

    double s_far = v->pos + enh_opt.draw_distance, s_near = v->pos - 0.25;

    /* Clamp view distance at boundaries */
    if (v->town && s_far > 179.0) s_far = 179.0;
    if (!v->town && DSS(DS_race_type) == 0 && s_far > 0x17C + 30) s_far = 0x17C + 30;
    if (s_far > SEG_LAST + 30) s_far = SEG_LAST + 30;

    /* What stands on the road, each drawn once the road up to one segment in front of it is drawn */
    Event ev[512];
    int nev = 0;
    int max_seg = v->town ? 179 : (DSS(DS_race_type) == 0 ? 0x17C : SEG_LAST);
    int obj_dist = enh_opt.draw_distance;

    for (int n = (int)floor(s_far); n > s_near && nev < 500; n--) {
        if (n <= max_seg && n - v->pos > 0 && n - v->pos <= obj_dist && !(trk(TRK_C, n) & 4))
            ev[nev++] = (Event){ n - 1.0, EV_OBJECTS, n };
        if (v->town && n <= max_seg) ev[nev++] = (Event){ n - 1.0 + 1e-6, EV_BLOCK_END, n };
    }
    if (!v->town && (v->opp_pos - v->pos) <= enh_opt.draw_distance && (v->opp_pos - v->pos) >= -1.0) {
        if (nev < 512) ev[nev++] = (Event){ v->opp_pos - 1.0, EV_OPPONENT, 0 };
    }
    qsort(ev, (size_t)nev, sizeof ev[0], event_cmp);

    /* slices far to near on an adaptive grid (always on the 1/8 grid, so that the events fall on slice
     * edges), from the draw distance to just in front of the eye; consecutive slices share their edges */
    Slice sl[MAX_SLICES];
    int ns = 0;
    double sb = s_far;
    while (sb > s_near && ns < MAX_SLICES) {
        double dist = sb - v->pos;
        double step = dist > 30.0 ? 1.0 : (dist > 15.0 ? 0.5 : (1.0 / SLICES));
        double s = floor(sb / step) * step;
        if (s >= sb - 1e-6) s -= step;
        double sa = s > s_near ? s : s_near;
        Slice *q = &sl[ns++];
        Row ra = row_at(&c, sa), rb = row_at(&c, sb);
        q->sa = sa;
        q->sb = sb;
        q->ya = py(&ra, Y_ROAD);
        q->yb = py(&rb, Y_ROAD);
        if (sa <= c.pos - 0.24 && q->ya < 105.0) q->ya = 105.0;   /* the nearest reaches under the hood */
        q->front = q->yb < q->ya - 1e-6;
        q->ground = true;
        sb = sa;
    }
    slices_ground(sl, ns, ENH_VIEW_Y1, v->town ? COL_ROAD : COL_GRASS);

    const double box[4] = { 0, ENH_VIEW_Y0, 320, ENH_VIEW_Y1 };
    double clip_cur = ENH_VIEW_Y1;
    int e = 0;
    for (int i = 0; i < ns; i++) {
        if (e < nev && ev[e].key >= sl[i].sb) {
            clip_occ(box, occ_before(&sl[i]), &clip_cur);
            while (e < nev && ev[e].key >= sl[i].sb) draw_event(&c, v, &ev[e++]);
        }
        if (!sl[i].front) continue;
        clip_occ(box, sl[i].occ < sl[i].ya - 1e-6 ? sl[i].occ : ENH_VIEW_Y1, &clip_cur);
        draw_slice(&c, &sl[i], v->town);
    }
    clip_occ(box, ENH_VIEW_Y1, &clip_cur);
    while (e < nev) draw_event(&c, v, &ev[e++]);
    draw_mirror(&c, v);
}
