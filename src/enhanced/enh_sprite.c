/* Pictures of the road view for the enhanced renderer: the game's planar bitmaps (the opponent's and the
 * police car's views) and rectangles of the off-screen sheets in video memory (signs, billboards), decoded
 * to palette-index images, and drawn scaled into the sample buffer. */
#include "enhanced/enh_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "mem.h"
#include "platform/ega.h"
#include "platform/video.h"

#define CACHE 48

static struct { u32 key; EnhImage img; } cache[CACHE];
static int ncache;

void enh_images_reset(void)
{
    for (int i = 0; i < ncache; i++) SDL_free(cache[i].img.px);
    ncache = 0;
}

static EnhImage *cache_slot(u32 key, int w, int h, bool *fresh)
{
    for (int i = 0; i < ncache; i++)
        if (cache[i].key == key && cache[i].img.w == w && cache[i].img.h == h) { *fresh = false; return &cache[i].img; }
    if (ncache == CACHE) enh_images_reset();
    EnhImage *img = &cache[ncache].img;
    cache[ncache++].key = key;
    img->w = w;
    img->h = h;
    img->px = SDL_malloc((size_t)w * h);
    *fresh = true;
    return img;
}

/* A RAM bitmap (desc: DESC_* layout, video.h): 4 planes of h rows of `stride` bytes, the mask plane's set
 * bits transparent. */
const EnhImage *enh_image_bitmap(u16 seg, u16 off)
{
    FarPtr d = { off, seg };
    if (far_is_null(d)) return NULL;
    int w = desc_w(d), h = desc_h(d), stride = desc_stride(d);
    if (w <= 0 || h <= 0 || w > 640 || h > 400) return NULL;
    bool fresh;
    EnhImage *img = cache_slot((u32)seg << 16 | off, w, h, &fresh);
    if (!fresh) return img;
    FarPtr pl = desc_planes(d), mk = desc_mask(d);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u16 o = (u16)(y * stride + (x >> 3));
            int bit = 7 - (x & 7), c = 0;
            if (!far_is_null(mk) && (rd8(mk.seg, (u16)(mk.off + o)) >> bit & 1)) { img->px[y * w + x] = ENH_CLEAR; continue; }
            for (int k = 0; k < 4; k++)
                c |= (rd8(pl.seg, (u16)(pl.off + k * stride * h + o)) >> bit & 1) << k;
            img->px[y * w + x] = (u8)c;
        }
    return img;
}

/* A w x h rectangle at (x, y) of the page at plane offset `page` (opaque). */
const EnhImage *enh_image_vram(u16 page, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    bool fresh;
    EnhImage *img = cache_slot(0xFFFF0000u ^ (u32)(page + y * 320 + x), w, h, &fresh);
    if (!fresh) return img;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            u16 o = (u16)(page + (y + j) * EGA_BYTES_PER_LINE + ((x + i) >> 3));
            int bit = 7 - ((x + i) & 7), c = 0;
            for (int k = 0; k < 4; k++) c |= (ega_plane(k)[o] >> bit & 1) << k;
            img->px[j * w + i] = (u8)c;
        }
    return img;
}

/* The image stretched over [x0, x1) x [y0, y1) (original screen coordinates), nearest texel per sample;
 * mirrored when x1 < x0. */
void enh_draw_image(const EnhImage *img, double x0, double y0, double x1, double y1)
{
    if (!img || y1 <= y0 || x1 == x0) return;
    EnhBuf *b = &enh_buf;
    bool flip = x1 < x0;
    double l = flip ? x1 : x0, r = flip ? x0 : x1;
    double sl = l * b->sa, sr = r * b->sa, st = (y0 - ENH_VIEW_Y0) * b->sa, sb = (y1 - ENH_VIEW_Y0) * b->sa;
    int i0 = SDL_max(0, (int)ceil(sl - 0.5)), i1 = SDL_min(b->w, (int)ceil(sr - 0.5));
    int j0 = SDL_max(0, (int)ceil(st - 0.5)), j1 = SDL_min(b->h, (int)ceil(sb - 0.5));
    double du = img->w / (sr - sl), dv = img->h / (sb - st);
    for (int j = j0; j < j1; j++) {
        int v = (int)((j + 0.5 - st) * dv);
        if (v < 0 || v >= img->h) continue;
        const u8 *src = img->px + (size_t)v * img->w;
        u8 *out = b->px + (size_t)j * b->w;
        for (int i = i0; i < i1; i++) {
            int u = (int)((i + 0.5 - sl) * du);
            if (u < 0 || u >= img->w) continue;
            u8 c = src[flip ? img->w - 1 - u : u];
            if (c != ENH_CLEAR) out[i] = c;
        }
    }
}

void enh_fill_rect(double x0, double y0, double x1, double y1, u8 c)
{
    double xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    enh_fill_poly(xs, ys, 4, c);
}
