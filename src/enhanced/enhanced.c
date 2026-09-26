/* Enhanced road view: options, hooks, snapshots and the smooth view state (see enhanced.h, ENHANCED.md). */
#include "enhanced/enhanced.h"
#include "enhanced/enh_internal.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "game/race.h"
#include "game/race_int.h"
#include "host.h"
#include "platform/ega.h"

EnhOptions enh_opt = { false, 1, ENH_DEFAULT_AA, ENH_DEFAULT_MOTION_DELAY, true };

/* Snapshots: [0] = the newest game frame, [1] and [2] the ones before. */
static EnhSnap snaps[3];
static int nsnaps;
static bool active;            /* between enh_race_begin and enh_race_end, and enhancing this drive */
static u16 shown_start;        /* plane offset of the page the last road frame was presented in */
static bool presented;         /* a road frame is shown and nothing has replaced the page since */
static FILE *trace;            /* SR_ENH_TRACE=file: one line per composed frame */

void enh_init(bool enabled, int res_scale, int aa, int motion_delay, bool town)
{
    enh_opt.enabled = enabled;
    enh_opt.res_scale = SDL_clamp(res_scale, 1, ENH_MAX_RES_SCALE);
    enh_opt.aa = SDL_clamp(aa, 1, ENH_MAX_AA);
    while (enh_opt.res_scale * enh_opt.aa > ENH_MAX_SAMPLES) enh_opt.aa--;
    enh_opt.motion_delay = SDL_clamp(motion_delay, 0, 100);
    enh_opt.town = town;
    const char *t = SDL_getenv("SR_ENH_TRACE");
    if (enabled && t && *t && (trace = fopen(t, "w")) != NULL) setvbuf(trace, NULL, _IOLBF, 4096);
}

bool enh_enabled(void) { return enh_opt.enabled; }
int  enh_res_scale(void) { return enh_opt.res_scale; }

void enh_race_begin(void)
{
    nsnaps = 0;
    presented = false;
    active = enh_opt.enabled && (DSB(DS_racing) != 0 || enh_opt.town);
}

void enh_race_end(void)
{
    ega_set_layer(EGA_LAYER_OFF);
    active = false;
    presented = false;
    nsnaps = 0;
}

void enh_frame_built(void)
{
    if (!active) return;
    s16 seg = DSS(DS_seg);
    EnhSnap s;
    s.t_ns = SDL_GetTicksNS();
    s.pos = (double)seg + DSS(DS_substep) / 8.0;
    s.lat = DSS(DS_cam_x);
    s.heading = DSS(DS_heading);
    s.rel_heading = (s16)(DSS(DS_heading) - (s16)TRKW(TRK_YAW, seg));
    s.height = DSS(DS_cur_height);
    s.opp_pos = (double)DSS(DS_opp_seg) + DSS(DS_opp_seg_frac) / 8.0;
    s.opp_lat = DSS(DS_opp_lat);
    s.town = DSB(DS_racing) == 0;
    /* A jump (race start, reset, a drive-to entry) starts the motion anew. */
    if (nsnaps > 0 && (SDL_fabs(s.pos - snaps[0].pos) >= 2.0 ||
                       SDL_abs((s16)(s.rel_heading - snaps[0].rel_heading)) >= 0x40))
        nsnaps = 0;
    memmove(&snaps[1], &snaps[0], sizeof snaps[0] * 2);
    snaps[0] = s;
    if (nsnaps < 3) nsnaps++;
}

void enh_draw_begin(void)
{
    if (!active) return;
    u16 page = DSS(DS_back_page) == 1 ? 0x2000 : 0x4000;
    ega_clear_fg(page, ENH_VIEW_Y1 * EGA_BYTES_PER_LINE);
    ega_set_layer(EGA_LAYER_BG);
}

void enh_draw_fg(bool fg)
{
    if (active) ega_set_layer(fg ? EGA_LAYER_FG : EGA_LAYER_BG);
}

void enh_frame_presented(void)
{
    if (!active) return;
    ega_set_layer(EGA_LAYER_OFF);
    /* The page drawn by road_draw_frame is shown: its view rows are the road frame. Whatever is written
     * there from now on (messages, the start light, the results) stays the original's picture. */
    shown_start = (u16)(DSS(DS_shown_page) * 0x2000);
    ega_clear_written(shown_start, ENH_VIEW_Y1 * EGA_BYTES_PER_LINE);
    presented = true;
}

static double lerp(double a, double b, double x) { return a + (b - a) * x; }

/* Frame n's trajectory at x (0 = snapshot n-1, 1 = snapshot n). */
static void traj(const EnhSnap *a, const EnhSnap *b, double x, EnhView *v)
{
    v->pos = lerp(a->pos, b->pos, x);
    v->lat = lerp(a->lat, b->lat, x);
    v->rel_heading = lerp(a->rel_heading, (double)a->rel_heading + (s16)(b->rel_heading - a->rel_heading), x);
    v->opp_pos = lerp(a->opp_pos, b->opp_pos, x);
    v->opp_lat = lerp(a->opp_lat, b->opp_lat, x);
}

/* The smooth view at time now_ns: the newest trajectory (snapshots 1 -> 0) running motion_delay behind,
 * blended from the previous trajectory (2 -> 1) with a smoothstep over one game frame, so the motion
 * stays continuous in speed when a new frame arrives. */
bool enh_view_at(Uint64 now_ns, EnhView *v)
{
    if (nsnaps == 0) return false;
    const EnhSnap *s0 = &snaps[0];
    if (nsnaps == 1) {
        traj(s0, s0, 0, v);
    } else {
        double period = (double)(s0->t_ns - snaps[1].t_ns);
        double nominal = RACE_TICKS_PER_FRAME * (double)PIT_DIV_GAME * 1e9 / PIT_HZ;
        if (period < nominal * 0.5 || period > nominal * 2.0) period = nominal;
        double x = (double)(now_ns - s0->t_ns) / period + 1.0 - enh_opt.motion_delay / 100.0;
        x = SDL_clamp(x, 0.0, 1.5);
        traj(&snaps[1], s0, x, v);
        if (nsnaps == 3 && x < 1.0) {
            EnhView old;
            traj(&snaps[2], &snaps[1], SDL_min(x + 1.0, 1.5), &old);
            double w = x * x * (3 - 2 * x);
            v->pos = lerp(old.pos, v->pos, w);
            v->lat = lerp(old.lat, v->lat, w);
            v->rel_heading = lerp(old.rel_heading, v->rel_heading, w);
            v->opp_pos = lerp(old.opp_pos, v->opp_pos, w);
            v->opp_lat = lerp(old.opp_lat, v->opp_lat, w);
        }
    }
    v->town = s0->town;
    v->height = s0->height;
    return true;
}

bool enh_compose(u32 *xrgb, const u32 rgb[16])
{
    if (!active || !presented || ega_start() != shown_start) return false;
    EnhView v;
    if (!enh_view_at(SDL_GetTicksNS(), &v)) return false;
    Uint64 t0 = SDL_GetTicksNS();
    enh_draw_road(&v);
    int y_end = SDL_min(ENH_VIEW_Y1, ega_line_compare());
    enh_resolve(xrgb, rgb, 16, shown_start, y_end);
    if (trace) {
        static double last_pos;
        fprintf(trace, "%llu pos %.4f d %.4f lat %.1f rh %.2f opp %.3f ms %.2f\n",
                (unsigned long long)t0, v.pos, v.pos - last_pos, v.lat, v.rel_heading, v.opp_pos,
                (SDL_GetTicksNS() - t0) / 1e6);
        last_pos = v.pos;
    }
    return true;
}
