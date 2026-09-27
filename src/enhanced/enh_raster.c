/* Sample buffer of the enhanced road view: palette-index samples at (res scale x aa) per original pixel,
 * convex polygon fill at sample centres, and the resolve (linear-light average of each output pixel's
 * samples through the current palette) laid over the scaled EGA picture where the coverage allows. */
#include "enhanced/enh_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "platform/ega.h"

EnhBuf enh_buf;

void enh_buf_setup(void)
{
    int sa = enh_opt.res_scale * enh_opt.aa;
    int w = 320 * sa, h = (ENH_VIEW_Y1 - ENH_VIEW_Y0) * sa;
    if (enh_buf.px && enh_buf.sa == sa) return;
    SDL_free(enh_buf.px);
    enh_buf.px = SDL_malloc((size_t)w * h);
    enh_buf.w = w;
    enh_buf.h = h;
    enh_buf.sa = sa;
    enh_clip_view();
}

void enh_clip(double x0, double y0, double x1, double y1)
{
    EnhBuf *b = &enh_buf;
    b->cx0 = SDL_clamp((int)ceil(x0 * b->sa - 0.5), 0, b->w);
    b->cx1 = SDL_clamp((int)ceil(x1 * b->sa - 0.5), 0, b->w);
    b->cy0 = SDL_clamp((int)ceil((y0 - ENH_VIEW_Y0) * b->sa - 0.5), 0, b->h);
    b->cy1 = SDL_clamp((int)ceil((y1 - ENH_VIEW_Y0) * b->sa - 0.5), 0, b->h);
}

void enh_clip_view(void)
{
    EnhBuf *b = &enh_buf;
    b->cx0 = 0; b->cy0 = 0; b->cx1 = b->w; b->cy1 = b->h;
}

void enh_fill_rows(double y0, double y1, u8 c)
{
    EnhBuf *b = &enh_buf;
    int j0 = (int)ceil((y0 - ENH_VIEW_Y0) * b->sa - 0.5), j1 = (int)ceil((y1 - ENH_VIEW_Y0) * b->sa - 0.5);
    if (j0 < b->cy0) j0 = b->cy0;
    if (j1 > b->cy1) j1 = b->cy1;
    if (b->cx1 <= b->cx0) return;
    for (int j = j0; j < j1; j++) memset(b->px + (size_t)j * b->w + b->cx0, c, (size_t)(b->cx1 - b->cx0));
}

/* Convex polygon, vertices in original screen coordinates (any winding). A sample is covered when its
 * centre is inside (left and top edges inclusive). */
void enh_fill_poly(const double *xs, const double *ys, int n, u8 c)
{
    EnhBuf *b = &enh_buf;
    double sx[8], sy[8], ymin = 1e30, ymax = -1e30;
    if (n > 8) return;
    for (int i = 0; i < n; i++) {
        sx[i] = xs[i] * b->sa;
        sy[i] = (ys[i] - ENH_VIEW_Y0) * b->sa;
        if (sy[i] < ymin) ymin = sy[i];
        if (sy[i] > ymax) ymax = sy[i];
    }
    int j0 = (int)ceil(ymin - 0.5), j1 = (int)ceil(ymax - 0.5);
    if (j0 < b->cy0) j0 = b->cy0;
    if (j1 > b->cy1) j1 = b->cy1;
    for (int j = j0; j < j1; j++) {
        double yc = j + 0.5, xl = 1e30, xr = -1e30;
        for (int i = 0; i < n; i++) {
            int k = (i + 1) % n;
            double ya = sy[i], yb = sy[k];
            if ((yc < ya) == (yc < yb)) continue;                   /* edge does not cross this row */
            double x = sx[i] + (sx[k] - sx[i]) * (yc - ya) / (yb - ya);
            if (x < xl) xl = x;
            if (x > xr) xr = x;
        }
        if (xl > xr) continue;
        int i0 = (int)ceil(xl - 0.5), i1 = (int)ceil(xr - 0.5);
        if (i0 < b->cx0) i0 = b->cx0;
        if (i1 > b->cx1) i1 = b->cx1;
        if (i1 > i0) memset(b->px + (size_t)j * b->w + i0, c, (size_t)(i1 - i0));
    }
}

/* sRGB <-> linear light tables */
static float to_lin[256];
static u8 to_srgb[4096];

static void tables(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < 256; i++) {
        double v = i / 255.0;
        to_lin[i] = (float)(v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4));
    }
    for (int i = 0; i < 4096; i++) {
        double v = i / 4095.0;
        double s = v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055;
        to_srgb[i] = (u8)SDL_clamp((int)(s * 255 + 0.5), 0, 255);
    }
}

void enh_resolve(u32 *xrgb, const u32 *pal, int npal, u16 page, int y_end)
{
    tables();
    EnhBuf *b = &enh_buf;
    int S = enh_opt.res_scale, aa = enh_opt.aa, W = 320 * S;
    float lr[256], lg[256], lb[256];
    for (int i = 0; i < npal && i < 256; i++) {
        lr[i] = to_lin[pal[i] >> 16 & 0xFF];
        lg[i] = to_lin[pal[i] >> 8 & 0xFF];
        lb[i] = to_lin[pal[i] & 0xFF];
    }
    const u8 *wr = ega_written(), *fg = ega_fg();
    float inv = 4095.0f / (float)(aa * aa);
    for (int y = ENH_VIEW_Y0; y < y_end; y++) {
        u16 row = (u16)(page + y * EGA_BYTES_PER_LINE);
        for (int oy = 0; oy < S; oy++) {
            int Y = y * S + oy;
            u32 *out = xrgb + (size_t)Y * W;
            const u8 *src = b->px + (size_t)((y - ENH_VIEW_Y0) * S + oy) * aa * b->w;
            for (int x = 0; x < 320; x++) {
                u16 o = (u16)(row + (x >> 3));
                u8 bit = (u8)(0x80 >> (x & 7));
                if ((wr[o] | fg[o]) & bit) continue;                /* the original's pixel */
                for (int ox = 0; ox < S; ox++) {
                    int X = x * S + ox;
                    float r = 0, g = 0, bl = 0;
                    for (int j = 0; j < aa; j++) {
                        const u8 *s = src + (size_t)j * b->w + X * aa;
                        for (int i = 0; i < aa; i++) { r += lr[s[i]]; g += lg[s[i]]; bl += lb[s[i]]; }
                    }
                    out[X] = (u32)to_srgb[(int)(r * inv)] << 16 | (u32)to_srgb[(int)(g * inv)] << 8 |
                             to_srgb[(int)(bl * inv)];
                }
            }
        }
    }
}
