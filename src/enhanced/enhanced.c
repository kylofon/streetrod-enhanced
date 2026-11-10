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

/* A smoothly interpolated series of samples: [0] = the newest, [1] and [2] the ones before. */
#define SER_N 5
typedef struct {
    Uint64 t[3];
    double v[3][SER_N];
    int n;
} Series;

static Series frames;          /* per game frame: lat, rel_heading, pos, opp_pos, opp_lat */
static Series phys;            /* per physics step in a race: pos, opp_pos */
static EnhSnap snap;           /* the newest frame's other state */
static bool active;            /* between enh_race_begin and enh_race_end, and enhancing this drive */
static u16 shown_start;        /* plane offset of the page the last road frame was presented in */
static bool presented;         /* a road frame is shown and nothing has replaced the page since */
static FILE *trace;            /* SR_ENH_TRACE=file: one line per composed frame */

static void ser_push(Series *s, Uint64 t, const double *v)
{
    memmove(&s->t[1], &s->t[0], sizeof s->t[0] * 2);
    memmove(&s->v[1], &s->v[0], sizeof s->v[0] * 2);
    s->t[0] = t;
    memcpy(s->v[0], v, sizeof s->v[0]);
    if (s->n < 3) s->n++;
}

static double lerp(double a, double b, double x) { return a + (b - a) * x; }

/* The series at now: the newest segment (samples 1 -> 0) running motion_delay behind, blended from the
 * previous segment (2 -> 1) with a smoothstep over one sample period, so the motion stays continuous in
 * speed when a new sample arrives. nominal_ns = the expected period. */
static void ser_at(const Series *s, Uint64 now, double nominal_ns, double *out)
{
    if (s->n == 1) { memcpy(out, s->v[0], sizeof s->v[0]); return; }
    double period = (double)(s->t[0] - s->t[1]);
    if (period < nominal_ns * 0.5 || period > nominal_ns * 2.0) period = nominal_ns;
    double x = ((double)now - (double)s->t[0]) / period + 1.0 - enh_opt.motion_delay / 100.0;
    x = SDL_clamp(x, 0.0, 1.5);
    for (int k = 0; k < SER_N; k++) out[k] = lerp(s->v[1][k], s->v[0][k], x);
    if (s->n == 3 && x < 1.0) {
        double w = x * x * (3 - 2 * x), xo = SDL_min(x + 1.0, 1.5);
        for (int k = 0; k < SER_N; k++) out[k] = lerp(lerp(s->v[2][k], s->v[1][k], xo), out[k], w);
    }
}

#define TICK_NS ((double)PIT_DIV_GAME * 1e9 / PIT_HZ)

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
    frames.n = phys.n = 0;
    presented = false;
    enh_images_reset();
    enh_side_mirror_reset();
    active = enh_opt.enabled && (DSB(DS_racing) != 0 || enh_opt.town);
}

void enh_race_end(void)
{
    ega_set_layer(EGA_LAYER_OFF);
    active = false;
    presented = false;
    frames.n = phys.n = 0;
}

static double car_pos(void) { return DSS(DS_seg) + DSS(DS_substep) / 8.0; }
static double opp_pos(void) { return DSS(DS_opp_seg) + DSS(DS_opp_seg_frac) / 8.0; }

void enh_frame_built(void)
{
    if (!active) return;
    s16 seg = DSS(DS_seg);
    double v[SER_N] = { DSS(DS_cam_x), (s16)(DSS(DS_heading) - (s16)TRKW(TRK_YAW, seg)), car_pos(), opp_pos(),
                       DSS(DS_opp_lat) };
    /* A jump (race start, reset, a drive-to entry) starts the motion anew. */
    if (frames.n > 0 && (SDL_fabs(v[2] - frames.v[0][2]) >= 2.0 || SDL_fabs(v[1] - frames.v[0][1]) >= 0x40))
        frames.n = phys.n = 0;
    /* the heading difference is unwrapped against the last frame */
    if (frames.n > 0) v[1] = frames.v[0][1] + (s16)((s16)v[1] - (s16)frames.v[0][1]);
    ser_push(&frames, host_tick_time_ns(), v);
    snap.height = DSS(DS_cur_height);
    snap.town = DSB(DS_racing) == 0;
}

void enh_phys_step(void)
{
    if (!active) return;
    double v[SER_N] = { car_pos(), opp_pos(), 0, 0, 0 };
    if (phys.n > 0 && SDL_fabs(v[0] - phys.v[0][0]) >= 2.0) phys.n = 0;
    ser_push(&phys, host_tick_time_ns(), v);
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
    snap.opp_sprite = DSS(DS_opp_sprite);   /* the view of the opponent road_draw_frame chose */
}


bool enh_view_at(Uint64 now_ns, EnhView *v)
{
    if (frames.n == 0) return false;
    double f[SER_N];
    ser_at(&frames, now_ns, RACE_TICKS_PER_FRAME * TICK_NS, f);
    v->lat = f[0];
    v->rel_heading = f[1];
    v->pos = f[2];
    v->opp_pos = f[3];
    if (!snap.town && phys.n > 0) {        /* in a race the cars move with the physics, every 12th tick */
        double p[SER_N];
        ser_at(&phys, now_ns, 12 * TICK_NS, p);
        v->pos = p[0];
        v->opp_pos = p[1];
    }
    v->opp_lat = f[4];
    v->opp_sprite = snap.opp_sprite;
    v->town = snap.town;
    v->height = snap.height;
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
    enh_side_mirror(xrgb, enh_opt.res_scale);
    if (trace) {
        static double last_pos;
        fprintf(trace, "%llu pos %.4f d %.4f lat %.1f rh %.2f opp %.3f ms %.2f\n",
                (unsigned long long)t0, v.pos, v.pos - last_pos, v.lat, v.rel_heading, v.opp_pos,
                (SDL_GetTicksNS() - t0) / 1e6);
        last_pos = v.pos;
    }
    return true;
}
