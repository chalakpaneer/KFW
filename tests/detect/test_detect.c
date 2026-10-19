/*****************************************************************************
 * test_detect.c: KFW live-analysis detector suite
 * Copyright (C) 2026 Kishan G A
 *
 * Synthetic BGRA frames at a fixed 25 fps clock drive the four detectors
 * (BlackFrame / Freeze / Flicker / RapidCuts) through their length gates,
 * cooldowns, and reset behaviour. Exit 0 = all assertions passed.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *****************************************************************************/

#include "detect/kfw_detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_W 96
#define FRAME_H 54

/* ---- Event collector -------------------------------------------------------- */

typedef struct
{
    int64_t ts;
    char kind[24];
    char detail[64];
} test_event_t;

static test_event_t events[64];
static unsigned event_count;

static void event_sink(void *user, int64_t ts, const char *kind,
                       const char *detail)
{
    (void)user;
    if (event_count >= 64)
        return;
    events[event_count].ts = ts;
    snprintf(events[event_count].kind, sizeof(events[0].kind), "%s", kind);
    snprintf(events[event_count].detail, sizeof(events[0].detail), "%s",
             detail != NULL ? detail : "");
    ++event_count;
}

static void events_clear(void)
{
    memset(events, 0, sizeof(events));
    event_count = 0;
}

static unsigned events_count_kind(const char *kind)
{
    unsigned n = 0;
    for (unsigned i = 0; i < event_count; ++i)
        if (strcmp(events[i].kind, kind) == 0)
            ++n;
    return n;
}

/* ---- Synthetic frame helpers --------------------------------------------------- */

static uint8_t frame_data_a[FRAME_H * FRAME_W * 4];
static uint8_t frame_data_b[FRAME_H * FRAME_W * 4];

static void fill_flat(uint8_t *buf, unsigned v)
{
    for (unsigned i = 0; i < FRAME_H * FRAME_W; ++i)
    {
        buf[i * 4 + 0] = v;   /* B */
        buf[i * 4 + 1] = v;   /* G */
        buf[i * 4 + 2] = v;   /* R */
        buf[i * 4 + 3] = 0xff;
    }
}

/* Black content with tiny per-frame variance so the freeze detector does not
 * co-fire (grid diff must exceed 0.12). */
static void fill_dark_noisy(uint8_t *buf, unsigned idx)
{
    unsigned v = 5u + (idx % 3u);
    for (unsigned y = 0; y < FRAME_H; ++y)
        for (unsigned x = 0; x < FRAME_W; ++x)
        {
            uint8_t *p = buf + (y * FRAME_W + x) * 4;
            p[0] = (uint8_t)v;
            p[1] = (uint8_t)v;
            p[2] = (uint8_t)v;
            p[3] = 0xff;
        }
}

/* ---- Tests ------------------------------------------------------------------------ */

static int expect(unsigned cond, const char *what)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        return 0;
    }
    printf("ok: %s\n", what);
    return 1;
}

int main(void)
{
    kfw_detect_t *d = kfw_detect_create(25.0f);
    if (!expect(d != NULL, "detector context created"))
        return 1;

    kfw_detect_counts_t cc;
    memset(&cc, 0, sizeof(cc));
    kfw_detect_counts(d, &cc);
    if (!expect(cc.frames_analyzed == 0 && cc.black_events == 0,
                "counts start at zero"))
        return 1;

    /* ---- T1: black run fires exactly one BlackFrame ------------------------- */
    events_clear();
    const unsigned black_frames = 30;                    /* ≈ 1.2 s */
    for (unsigned i = 0; i < black_frames; ++i)
    {
        fill_dark_noisy(frame_data_a, i);
        kfw_detect_frame(d, (int64_t)i * 40, FRAME_W, FRAME_H,
                         frame_data_a, FRAME_W * 4, FRAME_H,
                         event_sink, NULL);
    }
    if (!expect(events_count_kind(KFW_DETECT_BLACKFRAME) == 1,
                "black run fires one BlackFrame"))
        return 1;
    if (!expect(events[event_count - 1].ts >= 500, "BlackFrame ts ≈ run start"))
        return 1;

    /* ---- T2: freeze run (continues into 40 hyper-black frames) -------------- */
    fill_flat(frame_data_b, 120);
    for (unsigned i = black_frames; i < 86; ++i)
    {
        kfw_detect_frame(d, (int64_t)i * 40, FRAME_W, FRAME_H,
                         frame_data_b, FRAME_W * 4, FRAME_H,
                         event_sink, NULL);
    }
    if (!expect(events_count_kind(KFW_DETECT_FREEZE) == 1,
                "freeze run fires one Freeze"))
        return 1;
    if (!expect(events_count_kind(KFW_DETECT_BLACKFRAME) == 1,
                "flat content breaks the black run (no more events)"))
        return 1;

    /* ---- T3: luminance jump fires Flicker once, honours cooldown ------------- */
    fill_flat(frame_data_a, 60);
    kfw_detect_frame(d, 4000, FRAME_W, FRAME_H, frame_data_a,
                     FRAME_W * 4, FRAME_H, event_sink, NULL);
    kfw_detect_frame(d, 4040, FRAME_W, FRAME_H, frame_data_a,
                     FRAME_W * 4, FRAME_H, event_sink, NULL);
    fill_flat(frame_data_b, 160);
    kfw_detect_frame(d, 4080, FRAME_W, FRAME_H, frame_data_b,
                     FRAME_W * 4, FRAME_H, event_sink, NULL);
    const unsigned flicker_at_jump = events_count_kind(KFW_DETECT_FLICKER);
    if (!expect(flicker_at_jump == 1, "luminance jump fires Flicker"))
        return 1;

    fill_flat(frame_data_a, 60);
    kfw_detect_frame(d, 4120, FRAME_W, FRAME_H, frame_data_a,
                     FRAME_W * 4, FRAME_H, event_sink, NULL);
    if (!expect(events_count_kind(KFW_DETECT_FLICKER) == flicker_at_jump,
                "flicker cooldown blocks echo events"))
        return 1;

    /* ---- T4: rapid-cut cluster fires RapidCuts --------------------------------
     * Fresh context: detector clocks must stay monotonic (T3's cooldown state
     * lives on a later wallclock than this scenario restarts from). */
    events_clear();
    kfw_detect_t *d2 = kfw_detect_create(25.0f);
    unsigned idx = 0;
    for (unsigned pair = 0; pair < 40; ++pair)
    {
        fill_flat(frame_data_a, 60);
        kfw_detect_frame(d2, (int64_t)idx * 40, FRAME_W, FRAME_H,
                         frame_data_a, FRAME_W * 4, FRAME_H, event_sink, NULL);
        ++idx;
        kfw_detect_frame(d2, (int64_t)idx * 40, FRAME_W, FRAME_H,
                         frame_data_a, FRAME_W * 4, FRAME_H, event_sink, NULL);
        ++idx;
        fill_flat(frame_data_b, 190);
        kfw_detect_frame(d2, (int64_t)idx * 40, FRAME_W, FRAME_H,
                         frame_data_b, FRAME_W * 4, FRAME_H, event_sink, NULL);
        ++idx;
        kfw_detect_frame(d2, (int64_t)idx * 40, FRAME_W, FRAME_H,
                         frame_data_b, FRAME_W * 4, FRAME_H, event_sink, NULL);
        ++idx;
    }
    kfw_detect_counts(d2, &cc);
    if (!expect(cc.rapidcut_events >= 1, "rapid-cut window fires RapidCuts"))
        return 1;
    if (!expect(cc.flicker_events >= 1,
                "cut jumps recorded as luminance events too"))
        return 1;
    if (!expect(cc.frames_analyzed == idx,
                "frames_analyzed tracks every submitted frame"))
        return 1;

    /* ---- T5: reset drops runs, keeps counters ---------------------------------- */
    events_clear();
    kfw_detect_reset(d2);
    fill_flat(frame_data_a, 120);
    for (unsigned i = 0; i < 10; ++i)   /* 10 frozen frames < 2 s run gate */
        kfw_detect_frame(d2, 60000 + (int64_t)i * 40, FRAME_W, FRAME_H,
                         frame_data_a, FRAME_W * 4, FRAME_H, event_sink, NULL);
    kfw_detect_reset(d2);                 /* seek discontinuity */
    for (unsigned i = 0; i < 10; ++i)   /* run restarts, still below gate */
        kfw_detect_frame(d2, 90000 + (int64_t)i * 40, FRAME_W, FRAME_H,
                         frame_data_a, FRAME_W * 4, FRAME_H, event_sink, NULL);
    kfw_detect_counts(d2, &cc);
    if (!expect(cc.freeze_events == 0, "reset prevents sub-threshold freeze"))
        return 1;
    if (!expect(cc.rapidcut_events == 1, "counters survive reset"))
        return 1;

    /* ---- T6: degenerate input is safe -------------------------------------------- */
    kfw_detect_frame(d, 120000, 0, 0, NULL, 0, 0, event_sink, NULL);
    kfw_detect_frame(NULL, 0, 1, 1, frame_data_a, 4, 1, event_sink, NULL);
    kfw_detect_destroy(d);
    kfw_detect_destroy(d2);
    kfw_detect_destroy(NULL);
    if (!expect(1, "degenerate input handled without abort"))
        return 1;

    printf("\nAll detector tests passed.\n");
    return 0;
}
