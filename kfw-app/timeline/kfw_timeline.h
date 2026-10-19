/*****************************************************************************
 * kfw_timeline.h: Kishan Forensic Workbench — event/marker timeline
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *****************************************************************************/

#ifndef KFW_TIMELINE_H
#define KFW_TIMELINE_H 1

#include "../cases/kfw_db.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event kinds used by the examination workspace and timeline strip.
 * These map to the kfw_db_event_t.kind field.
 */
#define KFW_TIMELINE_MARKER    "Marker"
#define KFW_TIMELINE_BOOKMARK  "Bookmark"
#define KFW_TIMELINE_NOTE      "Note"
#define KFW_TIMELINE_EXPORT    "FrameExport"
#define KFW_TIMELINE_VERIFY    "IntegrityCheck"

/**
 * Add a timestamped marker to the timeline.
 *
 * @param db           Open case database.
 * @param case_id      Case this marker belongs to.
 * @param evidence_id  Associated evidence item ("" for case-level).
 * @param ts_ms        Media timestamp in milliseconds.
 * @param kind         One of KFW_TIMELINE_* constants.
 * @param label        Short label shown in the timeline strip (≤ 128 chars).
 * @param body         Longer examiner note (may be empty).
 * @param out_ev       If non-NULL, receives the populated event record.
 *
 * @return 0 on success, negative on error.
 */
int kfw_timeline_add(kfw_db_t          *db,
                      const char        *case_id,
                      const char        *evidence_id,
                      int64_t            ts_ms,
                      const char        *kind,
                      const char        *label,
                      const char        *body,
                      kfw_db_event_t    *out_ev);

/**
 * Retrieve all timeline events for a case, ordered by media timestamp then
 * creation time.
 *
 * @param db           Open case database.
 * @param case_id      Case to query.
 * @param out_arr      Receives a heap-allocated array. Caller must free().
 * @param out_count    Receives the element count.
 *
 * @return 0 on success, negative on error.
 */
int kfw_timeline_list(kfw_db_t          *db,
                       const char        *case_id,
                       kfw_db_event_t   **out_arr,
                       int               *out_count);

/**
 * Format a millisecond timestamp as "HH:MM:SS.mmm".
 *
 * @param ms       Timestamp in milliseconds.
 * @param buf      Output buffer.
 * @param buf_size Buffer size in bytes (at least 13 bytes recommended).
 */
void kfw_timeline_format_ts(int64_t ms, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* KFW_TIMELINE_H */
