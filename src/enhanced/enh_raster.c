/* Sample buffer of the enhanced road view: palette-index samples at (res scale x aa) per original pixel,
 * command recording during road assembly, multithreaded rasterization across horizontal worker bands,
 * and linear-light supersample resolve laid over the scaled EGA picture. */
#include "enhanced/enh_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/ega.h"

#define MAX_CMDS 65536
#define MAX_WORKERS 32

#define COL_ROAD    1
#define COL_GRASS   9

typedef enum {
    CMD_CLIP,
    CMD_SKY,
    CMD_FILL_ROWS,
    CMD_FILL_RECT,
    CMD_FILL_POLY,
    CMD_DRAW_IMAGE,
} CmdType;

typedef struct {
    CmdType type;
    union {
        struct {
            int cx0, cy0, cx1, cy1;
        } clip;
        struct {
            bool town;
            double hy;
            double heading;
        } sky;
        struct {
            int j0, j1;
            u8 color;
        } fill_rows;
        struct {
            int i0, i1, j0, j1;
            u8 color;
        } fill_rect;
        struct {
            int n;
            u8 color;
            int j0, j1;
            double sx[8];
            double sy[8];
        } fill_poly;
        struct {
            EnhImage img;
            bool flip;
            double sl, sr, st, sb;
            double du, dv;
            int j0, j1, i0, i1;
        } draw_image;
    };
} RenderCmd;

EnhBuf enh_buf;

static RenderCmd cmd_list[MAX_CMDS];
static int cmd_count = 0;

static int num_workers = 1;
static SDL_Thread *worker_threads[MAX_WORKERS];
static SDL_Semaphore *sem_start[MAX_WORKERS];
static SDL_Semaphore *sem_done[MAX_WORKERS];
static bool workers_running = false;

typedef struct {
    u32 *xrgb;
    u16 page;
    int y_end;
    float lr[256], lg[256], lb[256];
} FrameContext;

static FrameContext frame_ctx;

static u8 src_rows[0xB8][320];     /* page 1 rows 66h..B7h: the sky and horizon sheets */

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

static void enh_raster_read_sheets(void)
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
}

void enh_raster_begin(void)
{
    enh_images_frame_begin();
    enh_buf_setup();
    enh_raster_read_sheets();
    cmd_count = 0;
    enh_clip_view();
}

void enh_clip(double x0, double y0, double x1, double y1)
{
    EnhBuf *b = &enh_buf;
    int cx0 = SDL_clamp((int)ceil(x0 * b->sa - 0.5), 0, b->w);
    int cx1 = SDL_clamp((int)ceil(x1 * b->sa - 0.5), 0, b->w);
    int cy0 = SDL_clamp((int)ceil((y0 - ENH_VIEW_Y0) * b->sa - 0.5), 0, b->h);
    int cy1 = SDL_clamp((int)ceil((y1 - ENH_VIEW_Y0) * b->sa - 0.5), 0, b->h);

    if (cmd_count < MAX_CMDS) {
        RenderCmd *cmd = &cmd_list[cmd_count++];
        cmd->type = CMD_CLIP;
        cmd->clip.cx0 = cx0;
        cmd->clip.cy0 = cy0;
        cmd->clip.cx1 = cx1;
        cmd->clip.cy1 = cy1;
    }
}

void enh_clip_view(void)
{
    EnhBuf *b = &enh_buf;
    if (cmd_count < MAX_CMDS) {
        RenderCmd *cmd = &cmd_list[cmd_count++];
        cmd->type = CMD_CLIP;
        cmd->clip.cx0 = 0;
        cmd->clip.cy0 = 0;
        cmd->clip.cx1 = b->w;
        cmd->clip.cy1 = b->h;
    }
}

void enh_draw_sky(bool town, double hy, double heading)
{
    if (cmd_count < MAX_CMDS) {
        RenderCmd *cmd = &cmd_list[cmd_count++];
        cmd->type = CMD_SKY;
        cmd->sky.town = town;
        cmd->sky.hy = hy;
        cmd->sky.heading = heading;
    }
}

void enh_fill_rows(double y0, double y1, u8 c)
{
    if (cmd_count >= MAX_CMDS) return;
    EnhBuf *b = &enh_buf;
    RenderCmd *cmd = &cmd_list[cmd_count++];
    cmd->type = CMD_FILL_ROWS;
    cmd->fill_rows.j0 = (int)ceil((y0 - ENH_VIEW_Y0) * b->sa - 0.5);
    cmd->fill_rows.j1 = (int)ceil((y1 - ENH_VIEW_Y0) * b->sa - 0.5);
    cmd->fill_rows.color = c;
}

/* [x0, x1) x [y0, y1); nothing when x1 <= x0 or y1 <= y0 (a far town wall less than a pixel tall comes out
 * upside down) */
void enh_fill_rect(double x0, double y0, double x1, double y1, u8 c)
{
    if (x1 <= x0 || y1 <= y0 || cmd_count >= MAX_CMDS) return;
    EnhBuf *b = &enh_buf;
    int i0 = (int)ceil(x0 * b->sa - 0.5);
    int i1 = (int)ceil(x1 * b->sa - 0.5);
    int j0 = (int)ceil((y0 - ENH_VIEW_Y0) * b->sa - 0.5);
    int j1 = (int)ceil((y1 - ENH_VIEW_Y0) * b->sa - 0.5);
    if (i0 >= i1 || j0 >= j1) return;

    RenderCmd *cmd = &cmd_list[cmd_count++];
    cmd->type = CMD_FILL_RECT;
    cmd->fill_rect.i0 = i0;
    cmd->fill_rect.i1 = i1;
    cmd->fill_rect.j0 = j0;
    cmd->fill_rect.j1 = j1;
    cmd->fill_rect.color = c;
}

/* Convex polygon, vertices in original screen coordinates (any winding). A sample is covered when its
 * centre is inside (left and top edges inclusive). */
void enh_fill_poly(const double *xs, const double *ys, int n, u8 c)
{
    if (n <= 2 || n > 8 || cmd_count >= MAX_CMDS) return;
    EnhBuf *b = &enh_buf;
    RenderCmd *cmd = &cmd_list[cmd_count++];
    cmd->type = CMD_FILL_POLY;
    cmd->fill_poly.n = n;
    cmd->fill_poly.color = c;

    double ymin = 1e30, ymax = -1e30;
    for (int i = 0; i < n; i++) {
        double sx = xs[i] * b->sa;
        double sy = (ys[i] - ENH_VIEW_Y0) * b->sa;
        cmd->fill_poly.sx[i] = sx;
        cmd->fill_poly.sy[i] = sy;
        if (sy < ymin) ymin = sy;
        if (sy > ymax) ymax = sy;
    }
    cmd->fill_poly.j0 = (int)ceil(ymin - 0.5);
    cmd->fill_poly.j1 = (int)ceil(ymax - 0.5);
}

/* The image stretched over [x0, x1) x [y0, y1) (original screen coordinates), nearest texel per sample;
 * mirrored when x1 < x0. */
void enh_draw_image(const EnhImage *img, double x0, double y0, double x1, double y1)
{
    if (!img || !img->px || y1 <= y0 || x1 == x0 || cmd_count >= MAX_CMDS) return;
    EnhBuf *b = &enh_buf;
    bool flip = x1 < x0;
    double l = flip ? x1 : x0, r = flip ? x0 : x1;
    double sl = l * b->sa, sr = r * b->sa, st = (y0 - ENH_VIEW_Y0) * b->sa, sb = (y1 - ENH_VIEW_Y0) * b->sa;

    RenderCmd *cmd = &cmd_list[cmd_count++];
    cmd->type = CMD_DRAW_IMAGE;
    cmd->draw_image.img = *img;
    cmd->draw_image.flip = flip;
    cmd->draw_image.sl = sl;
    cmd->draw_image.sr = sr;
    cmd->draw_image.st = st;
    cmd->draw_image.sb = sb;
    cmd->draw_image.du = img->w / (sr - sl);
    cmd->draw_image.dv = img->h / (sb - st);
    cmd->draw_image.i0 = (int)ceil(sl - 0.5);
    cmd->draw_image.i1 = (int)ceil(sr - 0.5);
    cmd->draw_image.j0 = (int)ceil(st - 0.5);
    cmd->draw_image.j1 = (int)ceil(sb - 0.5);
}

/* ---------------------------------------------------------------- execution in bands */

/* one sample row from sheet row sy, scrolled so that screen x shows sheet column x - shift */
static inline void sheet_row(int j, int sy, double shift)
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

/* Sample row j from sheet row sy scrolled by shift. The res scale x aa sample rows of one original row are
 * the same: a row equal to the one above (in this band) is copied, not sampled. */
static inline void sky_sample_row(int j, int j_start, int sy, double shift, int *last_sy, double *last_shift)
{
    EnhBuf *b = &enh_buf;
    if (j > j_start && sy == *last_sy && shift == *last_shift) {
        memcpy(b->px + (size_t)j * b->w, b->px + (size_t)(j - 1) * b->w, (size_t)b->w);
        return;
    }
    sheet_row(j, sy, shift);
    *last_sy = sy;
    *last_shift = shift;
}

static inline void render_sky_band(const RenderCmd *cmd, int j_start, int j_end)
{
    EnhBuf *b = &enh_buf;
    int last_sy = -1;
    double last_shift = 0;
    if (cmd->sky.town) {           /* fixed: sky 1Dh rows from 66h, skyline 10h rows from A3h at 2Fh */
        for (int j = j_start; j < j_end; j++) {
            double y = ENH_VIEW_Y0 + (j + 0.5) / b->sa;
            if (y < 0x2F)
                sky_sample_row(j, j_start, 0x66 + (int)floor(y - 0x12), 0, &last_sy, &last_shift);
            else if (y < 0x3F)
                sky_sample_row(j, j_start, 0xA3 + (int)floor(y - 0x2F), 0, &last_sy, &last_shift);
            else {
                memset(b->px + (size_t)j * b->w, COL_ROAD, (size_t)b->w);
                last_sy = -1;
            }
        }
    } else {
        double hy = cmd->sky.hy;
        double heading = cmd->sky.heading;
        /* the original moves the sky x2 and the horizon x4 the heading; turning, both are at infinity and move
         * alike, so with the reduced rotation the sky goes with the horizon */
        double sky_k = enh_opt.sky_scroll < 100 ? 4.0 : 2.0;
        for (int j = j_start; j < j_end; j++) {
            double y = ENH_VIEW_Y0 + (j + 0.5) / b->sa;
            if (y < hy - 12)
                sky_sample_row(j, j_start, (int)floor(y + 0xA6 - hy), sky_k * heading,
                               &last_sy, &last_shift);
            else if (y < hy + 8)
                sky_sample_row(j, j_start, 0xA3 + (int)floor(y - (hy - 12)), 4.0 * heading,          /* x4 */
                               &last_sy, &last_shift);
            else {
                memset(b->px + (size_t)j * b->w, COL_GRASS, (size_t)b->w);
                last_sy = -1;
            }
        }
    }
}

static inline void render_fill_rows_band(const RenderCmd *cmd, int j_start, int j_end,
                                         int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    int j0 = SDL_max(SDL_max(cmd->fill_rows.j0, clip_y0), j_start);
    int j1 = SDL_min(SDL_min(cmd->fill_rows.j1, clip_y1), j_end);
    if (j0 >= j1 || clip_x1 <= clip_x0) return;

    EnhBuf *b = &enh_buf;
    u8 c = cmd->fill_rows.color;
    size_t span_len = (size_t)(clip_x1 - clip_x0);

    for (int j = j0; j < j1; j++) memset(b->px + (size_t)j * b->w + clip_x0, c, span_len);
}

static inline void render_fill_rect_band(const RenderCmd *cmd, int j_start, int j_end,
                                         int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    int j0 = SDL_max(SDL_max(cmd->fill_rect.j0, clip_y0), j_start);
    int j1 = SDL_min(SDL_min(cmd->fill_rect.j1, clip_y1), j_end);
    if (j0 >= j1) return;

    int i0 = SDL_max(cmd->fill_rect.i0, clip_x0);
    int i1 = SDL_min(cmd->fill_rect.i1, clip_x1);
    if (i0 >= i1) return;

    EnhBuf *b = &enh_buf;
    u8 c = cmd->fill_rect.color;
    size_t span_len = (size_t)(i1 - i0);

    for (int j = j0; j < j1; j++) memset(b->px + (size_t)j * b->w + i0, c, span_len);
}

static inline void render_fill_poly_band(const RenderCmd *cmd, int j_start, int j_end,
                                         int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    int j0 = SDL_max(SDL_max(cmd->fill_poly.j0, clip_y0), j_start);
    int j1 = SDL_min(SDL_min(cmd->fill_poly.j1, clip_y1), j_end);
    if (j0 >= j1) return;

    EnhBuf *b = &enh_buf;
    int n = cmd->fill_poly.n;
    u8 c = cmd->fill_poly.color;
    const double *sx = cmd->fill_poly.sx;
    const double *sy = cmd->fill_poly.sy;

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
        if (i0 < clip_x0) i0 = clip_x0;
        if (i1 > clip_x1) i1 = clip_x1;
        if (i1 > i0) memset(b->px + (size_t)j * b->w + i0, c, (size_t)(i1 - i0));
    }
}

static inline void render_draw_image_band(const RenderCmd *cmd, int j_start, int j_end,
                                          int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    int j0 = SDL_max(SDL_max(cmd->draw_image.j0, clip_y0), j_start);
    int j1 = SDL_min(SDL_min(cmd->draw_image.j1, clip_y1), j_end);
    if (j0 >= j1) return;

    int i0 = SDL_max(cmd->draw_image.i0, clip_x0);
    int i1 = SDL_min(cmd->draw_image.i1, clip_x1);
    if (i0 >= i1) return;

    EnhBuf *b = &enh_buf;
    const EnhImage *img = &cmd->draw_image.img;
    bool flip = cmd->draw_image.flip;
    double sl = cmd->draw_image.sl;
    double st = cmd->draw_image.st;
    double du = cmd->draw_image.du;
    double dv = cmd->draw_image.dv;

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

/* Output rows y_from..y_to: the samples resolved through the frame's palette, except where the EGA page has
 * written or foreground pixels. */
static void resolve_band(int y_from, int y_to)
{
    EnhBuf *b = &enh_buf;
    int S = enh_opt.res_scale, aa = enh_opt.aa, W = 320 * S;
    const u8 *wr = ega_written(), *fg = ega_fg();
    float inv = 4095.0f / (float)(aa * aa);
    u16 page = frame_ctx.page;
    u32 *xrgb = frame_ctx.xrgb;
    const float *lr = frame_ctx.lr, *lg = frame_ctx.lg, *lb = frame_ctx.lb;
    for (int y = y_from; y < y_to; y++) {
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

/* Worker worker_id: the frame's commands clipped to its band of view rows, then the resolve of the band. */
static void worker_exec(int worker_id)
{
    EnhBuf *b = &enh_buf;
    int total_y = ENH_VIEW_Y1 - ENH_VIEW_Y0;
    int y_from = ENH_VIEW_Y0 + (worker_id * total_y) / num_workers;
    int y_to   = ENH_VIEW_Y0 + ((worker_id + 1) * total_y) / num_workers;
    int j_start = (y_from - ENH_VIEW_Y0) * b->sa;
    int j_end   = (y_to   - ENH_VIEW_Y0) * b->sa;

    int clip_x0 = 0, clip_y0 = 0, clip_x1 = b->w, clip_y1 = b->h;

    for (int c = 0; c < cmd_count; c++) {
        const RenderCmd *cmd = &cmd_list[c];
        switch (cmd->type) {
        case CMD_CLIP:
            clip_x0 = cmd->clip.cx0;
            clip_y0 = cmd->clip.cy0;
            clip_x1 = cmd->clip.cx1;
            clip_y1 = cmd->clip.cy1;
            break;
        case CMD_SKY:
            render_sky_band(cmd, j_start, j_end);
            break;
        case CMD_FILL_ROWS:
            render_fill_rows_band(cmd, j_start, j_end, clip_x0, clip_y0, clip_x1, clip_y1);
            break;
        case CMD_FILL_RECT:
            render_fill_rect_band(cmd, j_start, j_end, clip_x0, clip_y0, clip_x1, clip_y1);
            break;
        case CMD_FILL_POLY:
            render_fill_poly_band(cmd, j_start, j_end, clip_x0, clip_y0, clip_x1, clip_y1);
            break;
        case CMD_DRAW_IMAGE:
            render_draw_image_band(cmd, j_start, j_end, clip_x0, clip_y0, clip_x1, clip_y1);
            break;
        }
    }

    int res_y_end = SDL_min(y_to, frame_ctx.y_end);
    if (res_y_end > y_from) {
        resolve_band(y_from, res_y_end);
    }
}

static int SDLCALL worker_thread_fn(void *data)
{
    int worker_id = (int)(intptr_t)data;
    for (;;) {
        SDL_WaitSemaphore(sem_start[worker_id]);
        if (!workers_running) break;                 /* set before the semaphore was signalled */
        worker_exec(worker_id);
        SDL_SignalSemaphore(sem_done[worker_id]);
    }
    return 0;
}

void enh_raster_init(void)
{
    static bool inited = false;
    if (inited) return;
    inited = true;

    tables();

    int cores = SDL_GetNumLogicalCPUCores();
    if (cores < 1) cores = 1;
    num_workers = SDL_clamp(cores, 1, MAX_WORKERS);

    workers_running = true;
    int n = num_workers;
    for (int i = 1; i < n; i++) {
        sem_start[i] = SDL_CreateSemaphore(0);
        sem_done[i] = SDL_CreateSemaphore(0);
        char name[32];
        SDL_snprintf(name, sizeof(name), "enh_worker_%d", i);
        if (sem_start[i] && sem_done[i])
            worker_threads[i] = SDL_CreateThread(worker_thread_fn, name, (void *)(intptr_t)i);
        if (!worker_threads[i]) {                     /* fewer workers: the bands are split among those */
            fprintf(stderr, "enhanced view: worker thread %d failed: %s\n", i, SDL_GetError());
            if (sem_start[i]) SDL_DestroySemaphore(sem_start[i]);
            if (sem_done[i]) SDL_DestroySemaphore(sem_done[i]);
            sem_start[i] = sem_done[i] = NULL;
            num_workers = i;
            break;
        }
    }
    host_set_shutdown_handler(enh_raster_shutdown);   /* the workers end before SDL_Quit */
    atexit(enh_raster_shutdown);                     /* and on any other way out (it runs once) */
}

void enh_raster_shutdown(void)
{
    if (!workers_running) return;
    workers_running = false;
    for (int i = 1; i < num_workers; i++) {
        if (sem_start[i]) SDL_SignalSemaphore(sem_start[i]);
    }
    for (int i = 1; i < num_workers; i++) {
        if (worker_threads[i]) {
            SDL_WaitThread(worker_threads[i], NULL);
            worker_threads[i] = NULL;
        }
        if (sem_start[i]) {
            SDL_DestroySemaphore(sem_start[i]);
            sem_start[i] = NULL;
        }
        if (sem_done[i]) {
            SDL_DestroySemaphore(sem_done[i]);
            sem_done[i] = NULL;
        }
    }
    num_workers = 1;
}

void enh_raster_dispatch_and_resolve(u32 *xrgb, const u32 *pal, int npal, u16 page, int y_end)
{
    tables();
    static bool warned;
    if (cmd_count >= MAX_CMDS && !warned) {
        warned = true;
        fprintf(stderr, "enhanced view: a frame filled the %d drawing commands, any more are dropped\n", MAX_CMDS);
    }

    for (int i = 0; i < npal && i < 256; i++) {
        frame_ctx.lr[i] = to_lin[(pal[i] >> 16) & 0xFF];
        frame_ctx.lg[i] = to_lin[(pal[i] >> 8) & 0xFF];
        frame_ctx.lb[i] = to_lin[pal[i] & 0xFF];
    }
    frame_ctx.xrgb = xrgb;
    frame_ctx.page = page;
    frame_ctx.y_end = y_end;

    for (int i = 1; i < num_workers; i++) {
        SDL_SignalSemaphore(sem_start[i]);
    }

    worker_exec(0);

    for (int i = 1; i < num_workers; i++) {
        SDL_WaitSemaphore(sem_done[i]);
    }
}
