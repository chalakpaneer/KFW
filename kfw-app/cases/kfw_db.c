/*****************************************************************************
 * kfw_db.c: Kishan Forensic Workbench — SQLite case database implementation
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *****************************************************************************/

#include "kfw_db.h"
#include "../integrity/kfw_integrity.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Current schema version.  Increment on every incompatible schema change. */
#define KFW_SCHEMA_VERSION 1

/* ---- Internal structure -------------------------------------------------- */

struct kfw_db
{
    sqlite3    *sql;
    int         next_case_seq;   /* in-memory counter, refreshed from DB */
    int         next_ev_seq;
    int         next_evt_seq;
    char        errmsg_buf[256];
};

/**
 * Read a TEXT column as a NUL-terminated string.  SQLite may return a typed
 * NULL for columns without values; never feed those to printf("%s").
 */
static const char *col_text(sqlite3_stmt *st, int col)
{
    const unsigned char *s = sqlite3_column_text(st, col);
    return s ? (const char *)s : "";
}

/* ---- Schema DDL ---------------------------------------------------------- */

static const char *KFW_DDL =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"

    "CREATE TABLE IF NOT EXISTS kfw_meta ("
    "  key   TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL"
    ");"

    "CREATE TABLE IF NOT EXISTS cases ("
    "  case_id      TEXT PRIMARY KEY,"
    "  title        TEXT NOT NULL,"
    "  examiner     TEXT NOT NULL DEFAULT '',"
    "  status       TEXT NOT NULL DEFAULT 'Open',"
    "  created_utc  INTEGER NOT NULL,"
    "  modified_utc INTEGER NOT NULL,"
    "  notes        TEXT    NOT NULL DEFAULT ''"
    ");"

    "CREATE TABLE IF NOT EXISTS evidence ("
    "  evidence_id   TEXT PRIMARY KEY,"
    "  case_id       TEXT NOT NULL REFERENCES cases(case_id),"
    "  orig_filename TEXT NOT NULL,"
    "  orig_path     TEXT NOT NULL DEFAULT '',"
    "  size_bytes    INTEGER NOT NULL DEFAULT 0,"
    "  sha256        TEXT NOT NULL DEFAULT '',"
    "  stored_path   TEXT NOT NULL DEFAULT '',"
    "  imported_utc  INTEGER NOT NULL,"
    "  provenance    TEXT NOT NULL DEFAULT 'Original',"
    "  file_type     TEXT NOT NULL DEFAULT '',"
    "  verified_utc  INTEGER NOT NULL DEFAULT 0,"
    "  verify_status TEXT NOT NULL DEFAULT 'Not checked'"
    ");"

    "CREATE TABLE IF NOT EXISTS media_info ("
    "  evidence_id TEXT PRIMARY KEY REFERENCES evidence(evidence_id),"
    "  container   TEXT NOT NULL DEFAULT '',"
    "  duration_ms INTEGER NOT NULL DEFAULT -1,"
    "  video_codec TEXT NOT NULL DEFAULT '',"
    "  audio_codec TEXT NOT NULL DEFAULT '',"
    "  width       INTEGER NOT NULL DEFAULT 0,"
    "  height      INTEGER NOT NULL DEFAULT 0,"
    "  fps_num     INTEGER NOT NULL DEFAULT 0,"
    "  fps_den     INTEGER NOT NULL DEFAULT 1,"
    "  raw_json    TEXT NOT NULL DEFAULT '{}'"
    ");"

    "CREATE TABLE IF NOT EXISTS events ("
    "  event_id    TEXT PRIMARY KEY,"
    "  case_id     TEXT NOT NULL REFERENCES cases(case_id),"
    "  evidence_id TEXT NOT NULL DEFAULT '',"
    "  ts_ms       INTEGER NOT NULL DEFAULT 0,"
    "  kind        TEXT NOT NULL DEFAULT 'Note',"
    "  label       TEXT NOT NULL DEFAULT '',"
    "  body        TEXT NOT NULL DEFAULT '',"
    "  created_utc INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_events_case ON events(case_id, ts_ms);"

    "CREATE TABLE IF NOT EXISTS notes ("
    "  note_id     TEXT PRIMARY KEY,"
    "  case_id     TEXT NOT NULL REFERENCES cases(case_id),"
    "  evidence_id TEXT NOT NULL DEFAULT '',"
    "  author      TEXT NOT NULL DEFAULT '',"
    "  created_utc INTEGER NOT NULL,"
    "  body        TEXT NOT NULL DEFAULT ''"
    ");"

    "CREATE TABLE IF NOT EXISTS artifacts ("
    "  artifact_id TEXT PRIMARY KEY,"
    "  case_id     TEXT NOT NULL REFERENCES cases(case_id),"
    "  evidence_id TEXT NOT NULL DEFAULT '',"
    "  kind        TEXT NOT NULL DEFAULT '',"
    "  path        TEXT NOT NULL DEFAULT '',"
    "  sha256      TEXT NOT NULL DEFAULT '',"
    "  created_utc INTEGER NOT NULL"
    ");"

    /* Tamper-evident audit chain.
       entry_hash = SHA-256(prev_hash || utc || actor || action ||
                            target_type || target_id || detail_json)     */
    "CREATE TABLE IF NOT EXISTS audit ("
    "  seq         INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  utc         INTEGER NOT NULL,"
    "  actor       TEXT    NOT NULL DEFAULT 'system',"
    "  action      TEXT    NOT NULL,"
    "  target_type TEXT    NOT NULL DEFAULT '',"
    "  target_id   TEXT    NOT NULL DEFAULT '',"
    "  detail_json TEXT    NOT NULL DEFAULT '{}',"
    "  prev_hash   TEXT    NOT NULL DEFAULT '',"
    "  entry_hash  TEXT    NOT NULL DEFAULT ''"
    ");";

/* ---- Helper macros ------------------------------------------------------- */

#define EXEC(db, sql)  sqlite3_exec((db)->sql, (sql), NULL, NULL, NULL)
#define NOW_UTC()      ((int64_t)time(NULL))

static void kfw_db__set_err(kfw_db_t *db, const char *msg)
{
    if (db && msg)
        snprintf(db->errmsg_buf, sizeof(db->errmsg_buf), "%s", msg);
}

/* ---- kfw_db_open --------------------------------------------------------- */

kfw_db_t *kfw_db_open(const char *path)
{
    if (!path)
        return NULL;

    kfw_db_t *db = (kfw_db_t *)calloc(1, sizeof(*db));
    if (!db)
        return NULL;

    if (sqlite3_open(path, &db->sql) != SQLITE_OK)
    {
        free(db);
        return NULL;
    }

    /* Apply DDL */
    char *errmsg = NULL;
    if (sqlite3_exec(db->sql, KFW_DDL, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        kfw_db__set_err(db, errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(db->sql);
        free(db);
        return NULL;
    }

    /* Store schema version if first open */
    char ver_sql[128];
    snprintf(ver_sql, sizeof(ver_sql),
             "INSERT OR IGNORE INTO kfw_meta(key,value) "
             "VALUES('schema_version','%d');", KFW_SCHEMA_VERSION);
    sqlite3_exec(db->sql, ver_sql, NULL, NULL, NULL);

    /* Seed sequence counters from existing rows */
    {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db->sql,
                "SELECT COUNT(*) FROM cases", -1, &st, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(st) == SQLITE_ROW)
                db->next_case_seq = sqlite3_column_int(st, 0) + 1;
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(db->sql,
                "SELECT COUNT(*) FROM evidence", -1, &st, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(st) == SQLITE_ROW)
                db->next_ev_seq = sqlite3_column_int(st, 0) + 1;
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(db->sql,
                "SELECT COUNT(*) FROM events", -1, &st, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(st) == SQLITE_ROW)
                db->next_evt_seq = sqlite3_column_int(st, 0) + 1;
            sqlite3_finalize(st);
        }
    }

    return db;
}

void kfw_db_close(kfw_db_t *db)
{
    if (!db)
        return;
    sqlite3_close(db->sql);
    free(db);
}

const char *kfw_db_errmsg(kfw_db_t *db)
{
    return db ? db->errmsg_buf : "null database handle";
}

int kfw_db_schema_version(kfw_db_t *db)
{
    if (!db)
        return -1;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql,
            "SELECT value FROM kfw_meta WHERE key='schema_version'",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    int ver = -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        ver = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return ver;
}

sqlite3 *kfw_db_raw_handle(kfw_db_t *db)
{
    return db ? db->sql : NULL;
}

/* ---- Case CRUD ----------------------------------------------------------- */

int kfw_db_case_insert(kfw_db_t *db, kfw_db_case_t *c)
{
    if (!db || !c)
        return -1;

    int64_t now = NOW_UTC();
    /* Auto-generate a year-aware sequential ID */
    struct tm *t = gmtime(&(time_t){now});
    snprintf(c->case_id, sizeof(c->case_id),
             "KFW-%04d-%03d", t->tm_year + 1900, db->next_case_seq);
    c->created_utc  = now;
    c->modified_utc = now;

    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO cases(case_id,title,examiner,status,"
        "                  created_utc,modified_utc,notes)"
        " VALUES(?,?,?,?,?,?,?)";

    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(st, 1, c->case_id,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, c->title,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, c->examiner,  -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, c->status,    -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, c->created_utc);
    sqlite3_bind_int64(st, 6, c->modified_utc);
    sqlite3_bind_text(st, 7, c->notes,     -1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);

    /* Only consume the sequence number when the insert actually happened,
       so a failed insert does not create gaps in the case IDs. */
    if (rc == 0)
    {
        db->next_case_seq++;
        kfw_db_audit_append(db, c->examiner[0] ? c->examiner : "system",
                             "case.create", "case", c->case_id, NULL);
    }
    return rc;
}

int kfw_db_case_update(kfw_db_t *db, const char *case_id,
                        const char *status, const char *notes)
{
    if (!db || !case_id)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "UPDATE cases SET status=?,notes=?,modified_utc=? WHERE case_id=?";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(st,  1, status  ? status  : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  2, notes   ? notes   : "", -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, NOW_UTC());
    sqlite3_bind_text(st,  4, case_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);

    /* No rows changed -> the case does not exist. */
    if (rc == 0 && sqlite3_changes(db->sql) == 0)
        return -1;

    if (rc == 0)
    {
        char detail[256];
        snprintf(detail, sizeof(detail), "{\"status\":\"%s\"}", status);
        kfw_db_audit_append(db, "system", "case.update",
                             "case", case_id, detail);
    }
    return rc;
}

int kfw_db_case_get(kfw_db_t *db, const char *case_id, kfw_db_case_t *out)
{
    if (!db || !case_id || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    sqlite3_stmt *st;
    const char *sql =
        "SELECT case_id,title,examiner,status,created_utc,modified_utc,notes"
        " FROM cases WHERE case_id=?";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, case_id, -1, SQLITE_STATIC);

    int rc = 1; /* not found */
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        snprintf(out->case_id,  sizeof(out->case_id),  "%s", col_text(st, 0));
        snprintf(out->title,    sizeof(out->title),    "%s", col_text(st, 1));
        snprintf(out->examiner, sizeof(out->examiner), "%s", col_text(st, 2));
        snprintf(out->status,   sizeof(out->status),   "%s", col_text(st, 3));
        out->created_utc  = sqlite3_column_int64(st, 4);
        out->modified_utc = sqlite3_column_int64(st, 5);
        snprintf(out->notes, sizeof(out->notes), "%s", col_text(st, 6));
        rc = 0;
    }
    sqlite3_finalize(st);
    return rc;
}

int kfw_db_case_list(kfw_db_t *db,
                     kfw_db_case_t **out_arr, int *out_count)
{
    if (!db || !out_arr || !out_count)
        return -1;
    *out_arr   = NULL;
    *out_count = 0;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT case_id,title,examiner,status,created_utc,modified_utc,notes"
        " FROM cases ORDER BY modified_utc DESC";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    int cap = 0, count = 0;
    kfw_db_case_t *arr = NULL;

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int new_cap = cap ? cap * 2 : 8;
            kfw_db_case_t *grown = (kfw_db_case_t *)realloc(arr,
                                    (size_t)new_cap * sizeof(*arr));
            if (!grown)
                break;
            arr = grown;
            cap = new_cap;
        }
        kfw_db_case_t *row = &arr[count++];
        memset(row, 0, sizeof(*row));
        snprintf(row->case_id,  sizeof(row->case_id),  "%s", col_text(st, 0));
        snprintf(row->title,    sizeof(row->title),    "%s", col_text(st, 1));
        snprintf(row->examiner, sizeof(row->examiner), "%s", col_text(st, 2));
        snprintf(row->status,   sizeof(row->status),   "%s", col_text(st, 3));
        row->created_utc  = sqlite3_column_int64(st, 4);
        row->modified_utc = sqlite3_column_int64(st, 5);
        snprintf(row->notes, sizeof(row->notes), "%s", col_text(st, 6));
    }
    sqlite3_finalize(st);

    *out_arr   = arr;
    *out_count = count;
    return 0;
}

/* ---- Evidence CRUD ------------------------------------------------------- */

int kfw_db_evidence_insert(kfw_db_t *db, kfw_db_evidence_t *e)
{
    if (!db || !e)
        return -1;

    int64_t now = NOW_UTC();
    snprintf(e->evidence_id, sizeof(e->evidence_id),
             "EV-%04d", db->next_ev_seq);
    e->imported_utc = now;
    if (!e->verify_status[0])
        snprintf(e->verify_status, sizeof(e->verify_status), "Not checked");
    if (!e->provenance[0])
        snprintf(e->provenance, sizeof(e->provenance), "Original");

    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO evidence(evidence_id,case_id,orig_filename,orig_path,"
        "  size_bytes,sha256,stored_path,imported_utc,provenance,"
        "  file_type,verified_utc,verify_status)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(st,  1, e->evidence_id,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  2, e->case_id,        -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  3, e->orig_filename,  -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  4, e->orig_path,      -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, e->size_bytes);
    sqlite3_bind_text(st,  6, e->sha256,         -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  7, e->stored_path,    -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 8, e->imported_utc);
    sqlite3_bind_text(st,  9, e->provenance,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 10, e->file_type,      -1, SQLITE_STATIC);
    sqlite3_bind_int64(st,11, e->verified_utc);
    sqlite3_bind_text(st, 12, e->verify_status,  -1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);

    if (rc == 0)
    {
        db->next_ev_seq++;
        char detail[256];
        snprintf(detail, sizeof(detail),
                 "{\"sha256\":\"%s\",\"size\":%lld}",
                 e->sha256, (long long)e->size_bytes);
        kfw_db_audit_append(db, "system", "evidence.import",
                             "evidence", e->evidence_id, detail);
    }
    return rc;
}

int kfw_db_evidence_update_import(kfw_db_t *db, const char *evidence_id,
                                  int64_t size_bytes, const char *sha256,
                                  const char *stored_path,
                                  const char *status)
{
    if (!db || !evidence_id || !sha256 || !stored_path || !status)
        return -1;

    sqlite3_stmt *st = NULL;
    const char *sql =
        "UPDATE evidence SET size_bytes=?,sha256=?,stored_path=?,"
        "  verify_status=? WHERE evidence_id=?";

    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_int64(st, 1, size_bytes);
    sqlite3_bind_text(st,  2, sha256,     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  3, stored_path,-1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  4, status,     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st,  5, evidence_id,-1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int kfw_db_evidence_set_verify(kfw_db_t *db, const char *evidence_id,
                                const char *status)
{
    if (!db || !evidence_id || !status)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "UPDATE evidence SET verify_status=?,verified_utc=? "
        "WHERE evidence_id=?";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st,  1, status,      -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, NOW_UTC());
    sqlite3_bind_text(st,  3, evidence_id, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);

    if (rc == 0)
        kfw_db_audit_append(db, "system", "evidence.verify",
                             "evidence", evidence_id, NULL);
    return rc;
}

int kfw_db_evidence_get(kfw_db_t *db, const char *evidence_id,
                         kfw_db_evidence_t *out)
{
    if (!db || !evidence_id || !out)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT evidence_id,case_id,orig_filename,orig_path,size_bytes,"
        "  sha256,stored_path,imported_utc,provenance,file_type,"
        "  verified_utc,verify_status"
        " FROM evidence WHERE evidence_id=?";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, evidence_id, -1, SQLITE_STATIC);

    int rc = 1;   /* default: not found */
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        rc = 0;
        memset(out, 0, sizeof(*out));
#define COL_STR(ctx, dst, col) \
        snprintf(dst, sizeof(dst), "%s", col_text(ctx, col))
        COL_STR(st, out->evidence_id,  0);
        COL_STR(st, out->case_id,      1);
        COL_STR(st, out->orig_filename,2);
        COL_STR(st, out->orig_path,    3);
        out->size_bytes   = sqlite3_column_int64(st, 4);
        COL_STR(st, out->sha256,       5);
        COL_STR(st, out->stored_path,  6);
        out->imported_utc = sqlite3_column_int64(st, 7);
        COL_STR(st, out->provenance,   8);
        COL_STR(st, out->file_type,    9);
        out->verified_utc = sqlite3_column_int64(st,10);
        COL_STR(st, out->verify_status,11);
#undef COL_STR
    }
    sqlite3_finalize(st);
    return rc;
}

int kfw_db_evidence_list(kfw_db_t *db, const char *case_id,
                          kfw_db_evidence_t **out_arr, int *out_count)
{
    if (!db || !case_id || !out_arr || !out_count)
        return -1;
    *out_arr   = NULL;
    *out_count = 0;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT evidence_id,case_id,orig_filename,orig_path,size_bytes,"
        "  sha256,stored_path,imported_utc,provenance,file_type,"
        "  verified_utc,verify_status"
        " FROM evidence WHERE case_id=? ORDER BY imported_utc";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, case_id, -1, SQLITE_STATIC);

    int cap = 0, count = 0;
    kfw_db_evidence_t *arr = NULL;

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int new_cap = cap ? cap * 2 : 8;
            kfw_db_evidence_t *grown = (kfw_db_evidence_t *)realloc(arr,
                                (size_t)new_cap * sizeof(*arr));
            if (!grown)
                break;
            arr = grown;
            cap = new_cap;
        }
        kfw_db_evidence_t *row = &arr[count++];
        memset(row, 0, sizeof(*row));
#define COL_STR(ctx, dst, col) \
        snprintf(dst, sizeof(dst), "%s", col_text(ctx, col))
        COL_STR(st, row->evidence_id,  0);
        COL_STR(st, row->case_id,      1);
        COL_STR(st, row->orig_filename,2);
        COL_STR(st, row->orig_path,    3);
        row->size_bytes   = sqlite3_column_int64(st, 4);
        COL_STR(st, row->sha256,       5);
        COL_STR(st, row->stored_path,  6);
        row->imported_utc = sqlite3_column_int64(st, 7);
        COL_STR(st, row->provenance,   8);
        COL_STR(st, row->file_type,    9);
        row->verified_utc = sqlite3_column_int64(st,10);
        COL_STR(st, row->verify_status,11);
#undef COL_STR
    }
    sqlite3_finalize(st);

    *out_arr   = arr;
    *out_count = count;
    return 0;
}

/* ---- Event record -------------------------------------------------------- */

int kfw_db_event_insert(kfw_db_t *db, kfw_db_event_t *ev)
{
    if (!db || !ev)
        return -1;

    int64_t now = NOW_UTC();
    /* Time + per-DB sequence gives a unique, sortable event ID. */
    snprintf(ev->event_id, sizeof(ev->event_id),
             "EVT-%lld-%04d", (long long)now, db->next_evt_seq);
    ev->created_utc = now;

    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO events(event_id,case_id,evidence_id,ts_ms,kind,"
        "  label,body,created_utc)"
        " VALUES(?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(st,  1, ev->event_id,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  2, ev->case_id,    -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  3, ev->evidence_id,-1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, ev->ts_ms);
    sqlite3_bind_text(st,  5, ev->kind,       -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  6, ev->label,      -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  7, ev->body,       -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 8, ev->created_utc);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc == 0)
        db->next_evt_seq++;
    return rc;
}

int kfw_db_event_list(kfw_db_t *db, const char *case_id,
                      kfw_db_event_t **out_arr, int *out_count)
{
    if (!db || !case_id || !out_arr || !out_count)
        return -1;
    *out_arr   = NULL;
    *out_count = 0;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT event_id,case_id,evidence_id,ts_ms,kind,label,body,created_utc"
        " FROM events WHERE case_id=? ORDER BY ts_ms,created_utc";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, case_id, -1, SQLITE_STATIC);

    int cap = 0, count = 0;
    kfw_db_event_t *arr = NULL;

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int new_cap = cap ? cap * 2 : 8;
            kfw_db_event_t *grown = (kfw_db_event_t *)realloc(arr,
                                (size_t)new_cap * sizeof(*arr));
            if (!grown)
                break;
            arr = grown;
            cap = new_cap;
        }
        kfw_db_event_t *row = &arr[count++];
        memset(row, 0, sizeof(*row));
#define COL_STR(ctx, dst, col) \
        snprintf(dst, sizeof(dst), "%s", col_text(ctx, col))
        COL_STR(st, row->event_id,   0);
        COL_STR(st, row->case_id,    1);
        COL_STR(st, row->evidence_id,2);
        row->ts_ms       = sqlite3_column_int64(st, 3);
        COL_STR(st, row->kind,       4);
        COL_STR(st, row->label,      5);
        COL_STR(st, row->body,       6);
        row->created_utc = sqlite3_column_int64(st, 7);
#undef COL_STR
    }
    sqlite3_finalize(st);
    *out_arr   = arr;
    *out_count = count;
    return 0;
}

/* ---- Audit chain --------------------------------------------------------- */

int kfw_db_audit_append(kfw_db_t   *db,
                         const char *actor,
                         const char *action,
                         const char *target_type,
                         const char *target_id,
                         const char *detail_json)
{
    if (!db)
        return -1;

    /* Fetch the last entry_hash to form the chain link */
    char prev_hash[KFW_SHA256_HEX + 1] = {0};
    {
        sqlite3_stmt *st;
        if (sqlite3_prepare_v2(db->sql,
                "SELECT entry_hash FROM audit ORDER BY seq DESC LIMIT 1",
                -1, &st, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(st) == SQLITE_ROW)
                snprintf(prev_hash, sizeof(prev_hash), "%s", col_text(st, 0));
            sqlite3_finalize(st);
        }
    }

    int64_t     now      = NOW_UTC();
    const char *dj       = detail_json ? detail_json : "{}";
    const char *tt       = target_type ? target_type : "";
    const char *tid      = target_id   ? target_id   : "";
    const char *act      = actor       ? actor        : "system";
    const char *actn     = action      ? action       : "";

    /* Compute the chain hash: SHA-256 of the canonical pre-image string.
       The buffer digest keeps this heap-only and avoids temp-file I/O
       on every audit write. */
    char pre_image[4096];
    int  pre_len = snprintf(pre_image, sizeof(pre_image),
                            "%s|%lld|%s|%s|%s|%s|%s",
                            prev_hash, (long long)now, act, actn,
                            tt, tid, dj);
    if (pre_len < 0 || (size_t)pre_len >= sizeof(pre_image))
        return -1;

    char entry_hash[KFW_SHA256_HEX + 1] = {0};
    if (kfw_sha256_buf(pre_image, (size_t)pre_len, entry_hash) != 0)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "INSERT INTO audit(utc,actor,action,target_type,target_id,"
        "  detail_json,prev_hash,entry_hash)"
        " VALUES(?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_int64(st, 1, now);
    sqlite3_bind_text(st,  2, act,        -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  3, actn,       -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  4, tt,         -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  5, tid,        -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  6, dj,         -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  7, prev_hash,  -1, SQLITE_STATIC);
    sqlite3_bind_text(st,  8, entry_hash, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    return rc;
}

int kfw_db_audit_verify_chain(kfw_db_t *db, int64_t from_seq)
{
    if (!db)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT seq,utc,actor,action,target_type,target_id,"
        "  detail_json,prev_hash,entry_hash"
        " FROM audit WHERE seq >= ? ORDER BY seq";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(st, 1, from_seq);

    char running_prev[KFW_SHA256_HEX + 1] = {0};
    int  first  = 1;
    int  broken = 0;

    while (sqlite3_step(st) == SQLITE_ROW)
    {
        const char *stored_prev  = col_text(st, 7);
        const char *stored_entry = col_text(st, 8);
        int64_t     utc          = sqlite3_column_int64(st, 1);
        const char *actor        = col_text(st, 2);
        const char *action       = col_text(st, 3);
        const char *tt           = col_text(st, 4);
        const char *tid          = col_text(st, 5);
        const char *dj           = col_text(st, 6);

        /* The chain starts with an empty prev_hash; a "first" row that
           claims a different predecessor is a forgery or a deleted head. */
        if ((!first && !kfw_sha256_equal(stored_prev, running_prev)) ||
            ( first && stored_prev[0] != '\0'))
        {
            broken = 1;
            break;
        }

        char pre_image[4096];
        int  pre_len = snprintf(pre_image, sizeof(pre_image),
                                "%s|%lld|%s|%s|%s|%s|%s",
                                stored_prev, (long long)utc, actor, action,
                                tt, tid, dj);
        if (pre_len < 0 || (size_t)pre_len >= sizeof(pre_image))
        {
            broken = 1;
            break;
        }

        char computed[KFW_SHA256_HEX + 1] = {0};
        if (kfw_sha256_buf(pre_image, (size_t)pre_len, computed) != 0)
        {
            broken = 1;
            break;
        }

        if (!kfw_sha256_equal(computed, stored_entry))
        {
            broken = 1;
            break;
        }

        snprintf(running_prev, sizeof(running_prev), "%s", stored_entry);
        first = 0;
    }
    sqlite3_finalize(st);
    return broken;
}
