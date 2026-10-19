/*****************************************************************************
 * kfw_detect.c: Kishan Forensic Workbench — live frame analysis pipeline
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Four independent indicators, each rate-limited and length-gated so the
 * timeline stays low-noise:
 *
 *   BlackFrame  — luminance below the darkness bound for a sustained run
 *   Freeze      — frame-to-frame luminance grid difference ≈ 0, sustained
 *   Flicker     — abrupt mean-luminance jump (splice / interference hint)
 *   RapidCuts   — scene-change density in a sliding window (heavily edited)
 *
 * The luminance grid is sampled with stride reads, so per-frame cost is
 * O(grid * cell-samples) — cheap enough to run inline on the decoder thread.
 *****************************************************************************/

#include "kfw_detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Tunables --------------------------------------------------------------- */

#define KFWDETECT_GRID_W      32      /* luminance grid cells horizontally   */
#define KFWDETECT_GRID_H      18      /* grid cells vertically               */

/* Luma thresholds (0..255 scale). */
#define KFWDETECT_BLACK_MAX       16.0f  /* mean below = black                */
#define KFWDETECT_FREEZE_DIFF     0.12f  /* mean grid diff below = frozen     */
#define KFWDETECT_FLICKER_JUMP    46.0f  /* |Δmean| above = luminance jump    */
#define KFWDETECT_CUT_JUMP        30.0f  /* |Δmean| above with stable before  */
#define KFWDETECT_STABLE_DIFF     2.5f   /* pre-cut required stability        */

/* Sustained-run / window limits, driven by the fps hint. */
#define KFWDETECT_BLACK_MIN_SEC     0.60f
#define KFWDETECT_FREEZE_MIN_SEC    2.00f
#define KFWDETECT_FLC_COOLDOWN_SEC  1.50f
#define KFWDETECT_CUTS_WINDOW_SEC   5.0f
#define KFWDETECT_CUTS_WINDOW_MIN   7
#define KFWDETECT_CUTW_COOLDOWN_SEC 10.0f

/* Sample stride cap inside each grid cell (per-axis multiplier budget). */
#define KFWDETECT_CELL_SAMPLES    16

struct kfw_detect
{
    float  fps;
    double frame_interval_ms;

    /* Per-frame luminance statistics. */
    float grid[KFWDETECT_GRID_W * KFWDETECT_GRID_H];
    bool  has_prev;
    float prev_mean;
    float mean;
    float grid_diff;      /* mean |Δ| on the grid vs previous frame        */
    float mean_jump;      /* |Δmean| vs previous frame                     */

    /* Run / rate-limit state. */
    unsigned black_run;
    int64_t  black_start_ms;
    bool     black_fired;
    unsigned freeze_run;
    int64_t  freeze_start_ms;
    bool     freeze_fired;
    int64_t  last_flicker_ms;
    bool     prev_stable; /* previous frame was low-diff (cut gating)      */

    /* Rapid-cut sliding window (ring of cut timestamps). */
    int64_t  cut_times[64];
    unsigned cut_count;
    int64_t  last_cutflag_ms;

    kfw_detect_counts_t counts;
};

/* ---- Luminance grid --------------------------------------------------------- */

static float luma_of(uint8_t b, uint8_t g, uint8_t r)
{
    return 0.114f * (float)b + 0.587f * (float)g + 0.299f * (float)r;
}

static void luma_grid(kfw_detect_t *d, unsigned width, unsigned height,
                      const uint8_t *pix, size_t pitch)
{
    const unsigned gw = KFWDETECT_GRID_W, gh = KFWDETECT_GRID_H;
    float *grid = d->grid;
    double sum = 0.0;

    for (unsigned gy = 0; gy < gh; ++gy)
    {
        for (unsigned gx = 0; gx < gw; ++gx)
        {
            unsigned x0 = (unsigned)(((uint64_t)gx * width)  / gw);
            unsigned y0 = (unsigned)(((uint64_t)gy * height) / gh);
            unsigned x1 = (unsigned)(((uint64_t)(gx + 1) * width)  / gw);
            unsigned y1 = (unsigned)(((uint64_t)(gy + 1) * height) / gh);
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            if (x1 > width)  x1 = width;
            if (y1 > height) y1 = height;

            unsigned sx = (x1 - x0 >= 4) ? ((x1 - x0) / 4) : 1;
            unsigned sy = (y1 - y0 >= 8) ? ((y1 - y0) / 8) : 1;
            if (sx * sy > KFWDETECT_CELL_SAMPLES * 4)
                sx = (KFWDETECT_CELL_SAMPLES * 4) / (sy > 0 ? sy : 1);

            double acc = 0.0;
            unsigned n = 0;
            for (unsigned y = y0; y < y1; y += sy)
            {
                const uint8_t *row = pix + (size_t)y * pitch;
                for (unsigned x = x0; x < x1; x += sx, ++n)
                {
                    const uint8_t *p = row + (size_t)x * 4;
                    acc += luma_of(p[0], p[1], p[2]);
                }
            }
            float cell = 0.0f;
            if (n > 0)
                cell = (float)(acc / n);
            grid[gy * gw + gx] = cell;
            sum += cell;
        }
    }
    d->mean = (float)(sum / (double)(gw * gh));
}

static float grid_mean_diff(const float *a, const float *b, unsigned n)
{
    double acc = 0.0;
    for (unsigned i = 0; i < n; ++i)
        acc += (double)((a[i] > b[i]) ? (a[i] - b[i]) : (b[i] - a[i]));
    return (float)(acc / n);
}

/* ---- Rapid-cut window ------------------------------------------------------- */

static void cuts_push(kfw_detect_t *d, int64_t ts)
{
    const unsigned cap = (unsigned)(sizeof(d->cut_times) / sizeof(d->cut_times[0]));
    if (d->cut_count < cap)
        d->cut_times[d->cut_count++] = ts;
    else
    {
        memmove(d->cut_times, d->cut_times + 1, (cap - 1) * sizeof(d->cut_times[0]));
        d->cut_times[cap - 1] = ts;
    }
}

static unsigned cuts_in_window(kfw_detect_t *d, int64_t now, double span_ms)
{
    unsigned c = 0;
    for (unsigned i = 0; i < d->cut_count; ++i)
        if ((double)(now - d->cut_times[i]) <= span_ms)
            ++c;
    return c;
}

/* ---- Lifecycle ---------------------------------------------------------------- */

kfw_detect_t *kfw_detect_create(float fps_hint)
{
    kfw_detect_t *d = (kfw_detect_t *)calloc(1, sizeof(*d));
    if (d == NULL)
        return NULL;
    d->fps = (fps_hint > 1.0f && fps_hint <= 240.0f) ? fps_hint : 25.0f;
    d->frame_interval_ms = 1000.0 / (double)d->fps;
    d->last_flicker_ms = -1;   /* "-1" = never fired (epoch 0 is real time) */
    d->last_cutflag_ms = -1;
    return d;
}

void kfw_detect_destroy(kfw_detect_t *d)
{
    free(d);
}

void kfw_detect_reset(kfw_detect_t *d)
{
    if (d == NULL)
        return;
    float  fps      = d->fps;
    double interval = d->frame_interval_ms;
    kfw_detect_counts_t counts = d->counts;
    memset(d, 0, sizeof(*d));
    d->fps = fps;
    d->frame_interval_ms = interval;
    d->counts = counts;
    d->last_flicker_ms = -1;
    d->last_cutflag_ms = -1;
}

void kfw_detect_counts(const kfw_detect_t *d, kfw_detect_counts_t *out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    if (d != NULL)
        *out = d->counts;
}

/* ---- Frame entry point ----------------------------------------------------------- */

void kfw_detect_frame(kfw_detect_t *d, int64_t ts_ms,
                      unsigned width, unsigned height,
                      const uint8_t *bgra, size_t pitch, size_t lines,
                      kfw_detect_sink sink, void *user)
{
    (void)lines;
    if (d == NULL)
        return;

    if (width == 0 || height == 0 || bgra == NULL || pitch < 4u)
    {
        kfw_detect_reset(d);
        return;
    }
    d->counts.frames_analyzed++;

    float prev_grid[KFWDETECT_GRID_W * KFWDETECT_GRID_H];
    const bool have_prev = d->has_prev;
    if (have_prev)
    {
        memcpy(prev_grid, d->grid, sizeof(prev_grid));
        d->prev_mean = d->mean;
    }

    luma_grid(d, width, height, bgra, pitch);

    d->grid_diff = 0.0f;
    d->mean_jump = 0.0f;
    if (have_prev)
    {
        d->grid_diff = grid_mean_diff(prev_grid, d->grid,
                                      KFWDETECT_GRID_W * KFWDETECT_GRID_H);
        d->mean_jump = d->mean > d->prev_mean ? d->mean - d->prev_mean
                                              : d->prev_mean - d->mean;
    }

    const unsigned black_run_max  = (unsigned)(KFWDETECT_BLACK_MIN_SEC  * d->fps + 0.5f);
    const unsigned freeze_run_max = (unsigned)(KFWDETECT_FREEZE_MIN_SEC * d->fps + 0.5f);
    const double   win_ms         = KFWDETECT_CUTS_WINDOW_SEC  * 1000.0;
    const double   flc_cool       = KFWDETECT_FLC_COOLDOWN_SEC * 1000.0;
    const double   cutw_cool      = KFWDETECT_CUTW_COOLDOWN_SEC * 1000.0;

    char detail[128];

    /* ---- BlackFrame ------------------------------------------------------ */
    if (d->mean <= KFWDETECT_BLACK_MAX)
    {
        if (++d->black_run == black_run_max && !d->black_fired)
        {
            d->black_fired = true;
            d->black_start_ms = ts_ms;
            d->counts.black_events++;
            if (sink != NULL)
            {
                snprintf(detail, sizeof(detail),
                         "{\"start_ms\":%lld,\"threshold\":%.0f}",
                         (long long)ts_ms, (double)KFWDETECT_BLACK_MAX);
                sink(user, ts_ms, KFW_DETECT_BLACKFRAME, detail);
            }
        }
    }
    else
    {
        d->black_run = 0;
        d->black_fired = false;
    }

    /* ---- Freeze ----------------------------------------------------------- */
    if (d->grid_diff <= KFWDETECT_FREEZE_DIFF)
    {
        if (++d->freeze_run == freeze_run_max && !d->freeze_fired)
        {
            d->freeze_fired = true;
            d->freeze_start_ms = ts_ms;
            d->counts.freeze_events++;
            if (sink != NULL)
            {
                snprintf(detail, sizeof(detail),
                         "{\"start_ms\":%lld,\"run_sec\":%.2f}",
                         (long long)ts_ms, (double)KFWDETECT_FREEZE_MIN_SEC);
                sink(user, ts_ms, KFW_DETECT_FREEZE, detail);
            }
        }
    }
    else
    {
        d->freeze_run = 0;
        d->freeze_fired = false;
    }

    /* ---- Flicker (luminance jump; both frames non-black) ------------------- */
    if (have_prev && d->mean_jump >= KFWDETECT_FLICKER_JUMP
        && d->mean > KFWDETECT_BLACK_MAX && d->prev_mean > KFWDETECT_BLACK_MAX
        && (d->last_flicker_ms < 0
            || (double)(ts_ms - d->last_flicker_ms) >= flc_cool))
    {
        d->last_flicker_ms = ts_ms;
        d->counts.flicker_events++;
        if (sink != NULL)
        {
            snprintf(detail, sizeof(detail),
                     "{\"jump\":%.0f}", (double)d->mean_jump);
            sink(user, ts_ms, KFW_DETECT_FLICKER, detail);
        }
    }

    /* ---- RapidCuts (scene-change density) ---------------------------------- */
    if (have_prev && d->mean_jump >= KFWDETECT_CUT_JUMP && d->prev_stable)
    {
        cuts_push(d, ts_ms);
        d->prev_stable = false;
        if ((d->last_cutflag_ms < 0
             || (double)(ts_ms - d->last_cutflag_ms) >= cutw_cool)
            && cuts_in_window(d, ts_ms, win_ms) >= KFWDETECT_CUTS_WINDOW_MIN)
        {
            d->last_cutflag_ms = ts_ms;
            d->counts.rapidcut_events++;
            if (sink != NULL)
            {
                snprintf(detail, sizeof(detail),
                         "{\"window_sec\":%.1f,\"min_cuts\":%d}",
                         KFWDETECT_CUTS_WINDOW_SEC, KFWDETECT_CUTS_WINDOW_MIN);
                sink(user, ts_ms, KFW_DETECT_RAPIDCUTS, detail);
            }
        }
    }
    else if (d->grid_diff <= KFWDETECT_STABLE_DIFF)
    {
        d->prev_stable = true;   /* calm content re-arms the cut detector */
    }

    d->has_prev = true;
}
