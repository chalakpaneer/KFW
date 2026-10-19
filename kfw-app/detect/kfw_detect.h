/*****************************************************************************
 * kfw_detect.h: Kishan Forensic Workbench — live frame analysis pipeline
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Indicators, not verdicts: every record this module emits is an observation
 * requiring examiner review. It never asserts authenticity or forgery
 * (docs/architecture.md §1.5).
 *****************************************************************************/

#ifndef KFW_DETECT_H
#define KFW_DETECT_H 1

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Live indicator kinds written to the investigation timeline. */
#define KFW_DETECT_BLACKFRAME "BlackFrame"
#define KFW_DETECT_FREEZE     "Freeze"
#define KFW_DETECT_FLICKER    "Flicker"
#define KFW_DETECT_RAPIDCUTS  "RapidCuts"

/** Aggregated indicator counts for a run (per kind). */
typedef struct kfw_detect_counts
{
    unsigned black_events;      /**< black runs crossed the length threshold */
    unsigned freeze_events;     /**< freeze runs crossed the length threshold */
    unsigned flicker_events;    /**< luminance spikes recorded              */
    unsigned rapidcut_events;   /**< rapid-cut windows recorded              */
    unsigned frames_analyzed;
} kfw_detect_counts_t;

/** Opaque live-analysis context. Create once per playback session. */
typedef struct kfw_detect kfw_detect_t;

/** Emitted for every live indicator: \p kind is one of KFW_DETECT_*. */
typedef void (*kfw_detect_sink)(void *user, int64_t ts_ms,
                                 const char *kind, const char *detail);

/**
 * Create a live detector context.
 *
 * \param fps_hint  Nominal frame rate (used for run-length thresholds);
 *                  <= 0 falls back to 25 fps.
 */
kfw_detect_t *kfw_detect_create(float fps_hint);

/** Release a detector context. Safe with NULL. */
void kfw_detect_destroy(kfw_detect_t *d);

/** Reset run state (e.g. on seek / discontinuity); counters are kept. */
void kfw_detect_reset(kfw_detect_t *d);

/** Current aggregate counts. */
void kfw_detect_counts(const kfw_detect_t *d, kfw_detect_counts_t *out);

/**
 * Submit a decoded frame (BGRA, 8-bit, as produced by libVLC "RV32").
 *
 * Runs the detector set inline (cheap luminance-grid statistics — designed to
 * be safe on the decoder thread) and invokes \p sink synchronously for each
 * triggered indicator. \p valid false (null/empty frame) resets run states.
 */
void kfw_detect_frame(kfw_detect_t *d, int64_t ts_ms,
                      unsigned width, unsigned height,
                      const uint8_t *bgra, size_t pitch, size_t lines,
                      kfw_detect_sink sink, void *user);

#ifdef __cplusplus
}
#endif

#endif /* KFW_DETECT_H */
