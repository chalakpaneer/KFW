/*****************************************************************************
 * kfw_timeline.c: Kishan Forensic Workbench — event/marker timeline
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *****************************************************************************/

#include "kfw_timeline.h"
#include <stdio.h>
#include <string.h>

int kfw_timeline_add(kfw_db_t       *db,
                      const char     *case_id,
                      const char     *evidence_id,
                      int64_t         ts_ms,
                      const char     *kind,
                      const char     *label,
                      const char     *body,
                      kfw_db_event_t *out_ev)
{
    if (!db || !case_id)
        return -1;

    kfw_db_event_t ev;
    memset(&ev, 0, sizeof(ev));
    snprintf(ev.case_id,     sizeof(ev.case_id),    "%s", case_id);
    snprintf(ev.evidence_id, sizeof(ev.evidence_id),"%s", evidence_id ? evidence_id : "");
    ev.ts_ms = ts_ms;
    snprintf(ev.kind,  sizeof(ev.kind),  "%s", kind  ? kind  : KFW_TIMELINE_NOTE);
    snprintf(ev.label, sizeof(ev.label), "%s", label ? label : "");
    snprintf(ev.body,  sizeof(ev.body),  "%s", body  ? body  : "");

    int rc = kfw_db_event_insert(db, &ev);
    if (rc == 0 && out_ev)
        *out_ev = ev;
    return rc;
}

int kfw_timeline_list(kfw_db_t        *db,
                       const char      *case_id,
                       kfw_db_event_t **out_arr,
                       int             *out_count)
{
    return kfw_db_event_list(db, case_id, out_arr, out_count);
}

void kfw_timeline_format_ts(int64_t ms, char *buf, size_t buf_size)
{
    if (!buf || buf_size < 13)
        return;
    int64_t h  = ms / 3600000;
    int64_t m  = (ms % 3600000) / 60000;
    int64_t s  = (ms % 60000) / 1000;
    int64_t ms3 = ms % 1000;
    snprintf(buf, buf_size, "%02lld:%02lld:%02lld.%03lld",
             (long long)h, (long long)m, (long long)s, (long long)ms3);
}
