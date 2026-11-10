/* Enhanced view: the left side mirror at the output resolution (see enh_internal.h, ENHANCED.md). */
#include "enhanced/enhanced.h"
#include "enhanced/enh_internal.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "game/ui.h"
#include "mem.h"

/* the last copy, kept while a dialog covers the rear-view mirror */
static u32 mirror_cache[32 * ENH_MAX_RES_SCALE * 19 * ENH_MAX_RES_SCALE];
static bool mirror_cache_valid = false;
static int mirror_cache_mw = 0;
static int mirror_cache_mh = 0;

void enh_side_mirror_reset(void)
{
    mirror_cache_valid = false;
}

typedef struct { int x0, y0, x1, y1; } EnhRect;

#define UI_DEPTH 0x8B8E                /* ui.h: the UI stack's depth (max 4) and its screens */
#define UI_STACK 0x8B86

/* ui_bounds (ui_menu.c) without its writes to DGROUP: the rectangles [x0, x1) x [y0, y1) of the screens on
 * the UI stack that have records (the dialogs and menus); the screens without (the "click anywhere" waits)
 * cover nothing. */
static int enh_dialogs(EnhRect out[4])
{
    int n = 0, depth = DSS(UI_DEPTH);
    for (int d = 0; d < depth && d < 4; d++) {
        u16 rec = ui_screen_rec(DSS((u16)(UI_STACK + 2 * d)));
        int x0 = 1000, y0 = 1000, x1 = 1, y1 = 1;
        for (int guard = 0; DSW((u16)(rec + UI_R_NEXT)) != 0 && guard < 256; guard++) {
            rec = UI_REC(DSW((u16)(rec + UI_R_NEXT)));
            x0 = SDL_min(x0, DSS((u16)(rec + UI_R_X0)));
            x1 = SDL_max(x1, DSS((u16)(rec + UI_R_X1)));
            y0 = SDL_min(y0, DSS((u16)(rec + UI_R_Y0)));
            y1 = SDL_max(y1, DSS((u16)(rec + UI_R_Y1)));
        }
        x0 = SDL_max(x0 - 3, 0);
        y0 = SDL_max(y0 - 3, 0);
        x1 = SDL_min(x1 + 3, 320);
        y1 = SDL_min(y1 + 3, 200);
        if (x1 > x0 && y1 > y0) out[n++] = (EnhRect){ x0, y0, x1, y1 };
    }
    return n;
}

static bool enh_in_rects(const EnhRect *r, int n, int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < n; i++)
        if (r[i].x0 < x1 && r[i].x1 > x0 && r[i].y0 < y1 && r[i].y1 > y0) return true;
    return false;
}

/* The side mirror (race_road.c: the original copies the rear-view mirror's (248, 22) 32 x 19 to (8, 103)
 * while it draws the road): the enhanced view's copy, or the last one while a dialog covers the rear-view
 * mirror. */
void enh_side_mirror(u32 *xrgb, int res_scale)
{
    int W = 320 * res_scale;
    EnhRect dlg[4];
    int ndlg = enh_dialogs(dlg);
    bool top_mirror_covered = enh_in_rects(dlg, ndlg, 248, 22, 280, 41);    /* the part the side mirror shows */
    if (enh_in_rects(dlg, ndlg, 8, 103, 40, 122)) return;                   /* a dialog over the side mirror */

    int sx0 = 248 * res_scale, sy0 = 22 * res_scale;
    int dx0 = 8 * res_scale,   dy0 = 103 * res_scale;
    int mw  = 32 * res_scale,  mh  = 19 * res_scale;
    if (!top_mirror_covered) {
        if (mw * mh <= (int)(sizeof(mirror_cache) / sizeof(mirror_cache[0]))) {
            for (int y = 0; y < mh; y++) {
                memcpy(mirror_cache + y * mw,
                       xrgb + (size_t)(sy0 + y) * W + sx0,
                       (size_t)mw * sizeof(u32));
                memcpy(xrgb + (size_t)(dy0 + y) * W + dx0,
                       xrgb + (size_t)(sy0 + y) * W + sx0,
                       (size_t)mw * sizeof(u32));
            }
            mirror_cache_valid = true;
            mirror_cache_mw = mw;
            mirror_cache_mh = mh;
        }
    } else if (mirror_cache_valid && mirror_cache_mw == mw && mirror_cache_mh == mh) {
        for (int y = 0; y < mh; y++) {
            memcpy(xrgb + (size_t)(dy0 + y) * W + dx0,
                   mirror_cache + y * mw,
                   (size_t)mw * sizeof(u32));
        }
    }
}
