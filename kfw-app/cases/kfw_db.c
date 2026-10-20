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
#include "../util/kfw_util.h"
#include "../integrity/kfw_integrity.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Current schema version.  Increment on every incompatible schema change. */
#define KFW_SCHEMA_VERSION KFW_DB_SCHEMA_VERSION

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
    "  notes        TEXT    NOT NULL DEFAULT '',"
    "  case_number  TEXT    NOT NULL DEFAULT '',"
    "  agency       TEXT    NOT NULL DEFAULT '',"
    "  requester    TEXT    NOT NULL DEFAULT '',"
    "  incident_date TEXT   NOT NULL DEFAULT '',"
    "  location     TEXT    NOT NULL DEFAULT '',"
    "  description  TEXT    NOT NULL DEFAULT ''"
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
    "  verify_status TEXT NOT NULL DEFAULT 'Not checked',"
    "  fs_modified_utc INTEGER NOT NULL DEFAULT 0,"
    "  duplicate_of  TEXT NOT NULL DEFAULT ''"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_evidence_sha ON evidence(sha256);"

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

    "CREATE TABLE IF NOT EXISTS tags ("
    "  tag_id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name   TEXT NOT NULL UNIQUE COLLATE NOCASE"
    ");"
    "CREATE TABLE IF NOT EXISTS item_tags ("
    "  tag_id    INTEGER NOT NULL REFERENCES tags(tag_id),"
    "  item_type TEXT NOT NULL,"
    "  item_id   TEXT NOT NULL,"
    "  PRIMARY KEY (tag_id, item_type, item_id)"
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

    /* Schema migration.  CREATE TABLE IF NOT EXISTS above leaves tables from
     * an older database untouched, so add any v2 columns they lack.  ALTERs
     * on a fresh database fail with "duplicate column" and are ignored. */
    const int old_version = kfw_db_schema_version(db);   /* -1 = new database */
    {
        static const char *const alters[] = {
            "ALTER TABLE cases ADD COLUMN case_number TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE cases ADD COLUMN agency TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE cases ADD COLUMN requester TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE cases ADD COLUMN incident_date TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE cases ADD COLUMN location TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE cases ADD COLUMN description TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE evidence ADD COLUMN fs_modified_utc INTEGER NOT NULL DEFAULT 0",
            "ALTER TABLE evidence ADD COLUMN duplicate_of TEXT NOT NULL DEFAULT ''",
        };
        if (old_version >= 1 && old_version < KFW_SCHEMA_VERSION)
            for (size_t i = 0; i < sizeof(alters) / sizeof(alters[0]); ++i)
                sqlite3_exec(db->sql, alters[i], NULL, NULL, NULL);

        char ver_sql[160];
        snprintf(ver_sql, sizeof(ver_sql),
                 "INSERT OR REPLACE INTO kfw_meta(key,value) "
                 "VALUES('schema_version','%d');", KFW_SCHEMA_VERSION);
        sqlite3_exec(db->sql, ver_sql, NULL, NULL, NULL);

        if (old_version >= 1 && old_version < KFW_SCHEMA_VERSION)
        {
            char detail[96];
            snprintf(detail, sizeof(detail), "{\"from\":%d,\"to\":%d}",
                     old_version, KFW_SCHEMA_VERSION);
            kfw_db_audit_append(db, "system", "db.migrate", "database",
                                "kfw.db", detail);
        }
    }

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
        "                  created_utc,modified_utc,notes,"
        "                  case_number,agency,requester,incident_date,"
        "                  location,description)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";

    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(st, 1, c->case_id,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, c->title,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, c->examiner,  -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, c->status,    -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, c->created_utc);
    sqlite3_bind_int64(st, 6, c->modified_utc);
    sqlite3_bind_text(st, 7, c->notes,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 8, c->case_number,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 9, c->agency,        -1, SQLITE_STATIC);
    sqlite3_bind_text(st,10, c->requester,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st,11, c->incident_date, -1, SQLITE_STATIC);
    sqlite3_bind_text(st,12, c->location,      -1, SQLITE_STATIC);
    sqlite3_bind_text(st,13, c->description,   -1, SQLITE_STATIC);

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
        char esc[128], detail[192];
        kfw_json_escape(status ? status : "", esc, sizeof(esc));
        snprintf(detail, sizeof(detail), "{\"status\":\"%s\"}", esc);
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
        "SELECT case_id,title,examiner,status,created_utc,modified_utc,notes,"
        "  case_number,agency,requester,incident_date,location,description"
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
        snprintf(out->case_number,   sizeof(out->case_number),   "%s", col_text(st, 7));
        snprintf(out->agency,        sizeof(out->agency),        "%s", col_text(st, 8));
        snprintf(out->requester,     sizeof(out->requester),     "%s", col_text(st, 9));
        snprintf(out->incident_date, sizeof(out->incident_date), "%s", col_text(st,10));
        snprintf(out->location,      sizeof(out->location),      "%s", col_text(st,11));
        snprintf(out->description,   sizeof(out->description),   "%s", col_text(st,12));
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
        "SELECT case_id,title,examiner,status,created_utc,modified_utc,notes,"
        "  case_number,agency,requester,incident_date,location,description"
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
        snprintf(row->case_number,   sizeof(row->case_number),   "%s", col_text(st, 7));
        snprintf(row->agency,        sizeof(row->agency),        "%s", col_text(st, 8));
        snprintf(row->requester,     sizeof(row->requester),     "%s", col_text(st, 9));
        snprintf(row->incident_date, sizeof(row->incident_date), "%s", col_text(st,10));
        snprintf(row->location,      sizeof(row->location),      "%s", col_text(st,11));
        snprintf(row->description,   sizeof(row->description),   "%s", col_text(st,12));
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
        "  file_type,verified_utc,verify_status,fs_modified_utc,duplicate_of)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
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
    sqlite3_bind_int64(st,13, e->fs_modified_utc);
    sqlite3_bind_text(st, 14, e->duplicate_of,   -1, SQLITE_STATIC);

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
        "  verified_utc,verify_status,fs_modified_utc,duplicate_of"
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
        out->fs_modified_utc = sqlite3_column_int64(st, 12);
        COL_STR(st, out->duplicate_of, 13);
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
        "  verified_utc,verify_status,fs_modified_utc,duplicate_of"
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
        row->fs_modified_utc = sqlite3_column_int64(st, 12);
        COL_STR(st, row->duplicate_of, 13);
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

/* ===========================================================================
 * Schema v2 additions: case particulars, duplicates, tags, search, custody
 * ======================================================================== */

/* Map one row of the standard evidence column list onto @p row. */
static void evidence_from_row(sqlite3_stmt *st, kfw_db_evidence_t *row)
{
    memset(row, 0, sizeof(*row));
#define COL_STR(dst, col) snprintf(dst, sizeof(dst), "%s", col_text(st, col))
    COL_STR(row->evidence_id,   0);
    COL_STR(row->case_id,       1);
    COL_STR(row->orig_filename, 2);
    COL_STR(row->orig_path,     3);
    row->size_bytes = sqlite3_column_int64(st, 4);
    COL_STR(row->sha256,        5);
    COL_STR(row->stored_path,   6);
    row->imported_utc = sqlite3_column_int64(st, 7);
    COL_STR(row->provenance,    8);
    COL_STR(row->file_type,     9);
    row->verified_utc = sqlite3_column_int64(st, 10);
    COL_STR(row->verify_status, 11);
    row->fs_modified_utc = sqlite3_column_int64(st, 12);
    COL_STR(row->duplicate_of,  13);
#undef COL_STR
}

int kfw_db_case_update_meta(kfw_db_t *db, const char *case_id,
                            const kfw_db_case_t *m, const char *actor)
{
    if (!db || !case_id || !m)
        return -1;

    sqlite3_stmt *st;
    const char *sql =
        "UPDATE cases SET case_number=?,agency=?,requester=?,incident_date=?,"
        "  location=?,description=?,modified_utc=? WHERE case_id=?";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, m->case_number,   -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, m->agency,        -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, m->requester,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, m->incident_date, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, m->location,      -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, m->description,   -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 7, NOW_UTC());
    sqlite3_bind_text(st, 8, case_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc == 0 && sqlite3_changes(db->sql) == 0)
        return -1;                      /* no such case */

    if (rc == 0)
    {
        char num[160], ag[288], detail[512];
        kfw_json_escape(m->case_number, num, sizeof(num));
        kfw_json_escape(m->agency, ag, sizeof(ag));
        snprintf(detail, sizeof(detail),
                 "{\"case_number\":\"%s\",\"agency\":\"%s\"}", num, ag);
        kfw_db_audit_append(db, actor && *actor ? actor : "system",
                            "case.meta", "case", case_id, detail);
    }
    return rc;
}

int kfw_db_evidence_find_by_sha256(kfw_db_t *db, const char *sha256,
                                   kfw_db_evidence_t **out_arr, int *out_count)
{
    if (!db || !sha256 || !*sha256 || !out_arr || !out_count)
        return -1;
    *out_arr = NULL;
    *out_count = 0;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT evidence_id,case_id,orig_filename,orig_path,size_bytes,"
        "  sha256,stored_path,imported_utc,provenance,file_type,"
        "  verified_utc,verify_status,fs_modified_utc,duplicate_of"
        " FROM evidence WHERE sha256=? ORDER BY imported_utc, evidence_id";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, sha256, -1, SQLITE_STATIC);

    int cap = 0, count = 0;
    kfw_db_evidence_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int nc = cap ? cap * 2 : 4;
            kfw_db_evidence_t *g = (kfw_db_evidence_t *)realloc(arr, (size_t)nc * sizeof(*arr));
            if (!g)
                break;
            arr = g;
            cap = nc;
        }
        evidence_from_row(st, &arr[count++]);
    }
    sqlite3_finalize(st);
    *out_arr = arr;
    *out_count = count;
    return 0;
}

int kfw_db_evidence_set_import_extras(kfw_db_t *db, const char *evidence_id,
                                      int64_t fs_modified_utc,
                                      const char *duplicate_of)
{
    if (!db || !evidence_id)
        return -1;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql,
            "UPDATE evidence SET fs_modified_utc=?,duplicate_of=? WHERE evidence_id=?",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(st, 1, fs_modified_utc);
    sqlite3_bind_text(st, 2, duplicate_of ? duplicate_of : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, evidence_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc == 0 && sqlite3_changes(db->sql) == 0)
        return -1;
    return rc;
}

/* ---- Tags ---------------------------------------------------------------- */

static int tag_item_type_ok(const char *t)
{
    return t && (strcmp(t, "evidence") == 0 || strcmp(t, "event") == 0);
}

/* Trim and copy a tag name; returns length (0 = invalid). */
static size_t tag_clean(const char *in, char *out, size_t out_size)
{
    if (!in)
        return 0;
    while (*in == ' ' || *in == '\t')
        ++in;
    size_t n = strlen(in);
    while (n > 0 && (in[n - 1] == ' ' || in[n - 1] == '\t' ||
                     in[n - 1] == '\n' || in[n - 1] == '\r'))
        --n;
    if (n == 0 || n >= out_size)
        return 0;
    memcpy(out, in, n);
    out[n] = '\0';
    return n;
}

static int tag_lookup_id(kfw_db_t *db, const char *name)
{
    sqlite3_stmt *st;
    int id = -1;
    if (sqlite3_prepare_v2(db->sql, "SELECT tag_id FROM tags WHERE name=?",
                           -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW)
        id = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return id;
}

static void tag_audit(kfw_db_t *db, const char *actor, const char *action,
                      const char *item_type, const char *item_id,
                      const char *tag)
{
    char esc[160], detail[220];
    kfw_json_escape(tag, esc, sizeof(esc));
    snprintf(detail, sizeof(detail), "{\"tag\":\"%s\"}", esc);
    kfw_db_audit_append(db, actor && *actor ? actor : "system", action,
                        item_type, item_id, detail);
}

int kfw_db_tag_apply(kfw_db_t *db, const char *actor, const char *item_type,
                     const char *item_id, const char *tag_name)
{
    char name[64];
    if (!db || !tag_item_type_ok(item_type) || !item_id || !*item_id ||
        tag_clean(tag_name, name, sizeof(name)) == 0)
        return -1;

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql, "INSERT OR IGNORE INTO tags(name) VALUES(?)",
                           -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(st);
    sqlite3_finalize(st);

    int tag_id = tag_lookup_id(db, name);
    if (tag_id < 0)
        return -1;

    if (sqlite3_prepare_v2(db->sql,
            "INSERT OR IGNORE INTO item_tags(tag_id,item_type,item_id) VALUES(?,?,?)",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(st, 1, tag_id);
    sqlite3_bind_text(st, 2, item_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, item_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);

    if (rc == 0 && sqlite3_changes(db->sql) > 0)
        tag_audit(db, actor, "tag.apply", item_type, item_id, name);
    return rc;
}

int kfw_db_tag_remove(kfw_db_t *db, const char *actor, const char *item_type,
                      const char *item_id, const char *tag_name)
{
    char name[64];
    if (!db || !tag_item_type_ok(item_type) || !item_id ||
        tag_clean(tag_name, name, sizeof(name)) == 0)
        return -1;
    int tag_id = tag_lookup_id(db, name);
    if (tag_id < 0)
        return 1;                       /* tag never existed: nothing to do */

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql,
            "DELETE FROM item_tags WHERE tag_id=? AND item_type=? AND item_id=?",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(st, 1, tag_id);
    sqlite3_bind_text(st, 2, item_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, item_id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc == 0 && sqlite3_changes(db->sql) > 0)
        tag_audit(db, actor, "tag.remove", item_type, item_id, name);
    return rc;
}

static int tag_collect(sqlite3_stmt *st, int with_count,
                       kfw_db_tag_t **out_arr, int *out_count)
{
    int cap = 0, count = 0;
    kfw_db_tag_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int nc = cap ? cap * 2 : 8;
            kfw_db_tag_t *g = (kfw_db_tag_t *)realloc(arr, (size_t)nc * sizeof(*arr));
            if (!g)
                break;
            arr = g;
            cap = nc;
        }
        kfw_db_tag_t *t = &arr[count++];
        memset(t, 0, sizeof(*t));
        t->tag_id = sqlite3_column_int(st, 0);
        snprintf(t->name, sizeof(t->name), "%s", col_text(st, 1));
        if (with_count)
            t->use_count = sqlite3_column_int(st, 2);
    }
    *out_arr = arr;
    *out_count = count;
    return 0;
}

int kfw_db_tag_list_for(kfw_db_t *db, const char *item_type, const char *item_id,
                        kfw_db_tag_t **out_arr, int *out_count)
{
    if (!db || !tag_item_type_ok(item_type) || !item_id || !out_arr || !out_count)
        return -1;
    *out_arr = NULL;
    *out_count = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql,
            "SELECT t.tag_id,t.name FROM tags t JOIN item_tags i ON i.tag_id=t.tag_id"
            " WHERE i.item_type=? AND i.item_id=? ORDER BY t.name",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, item_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, item_id, -1, SQLITE_STATIC);
    int rc = tag_collect(st, 0, out_arr, out_count);
    sqlite3_finalize(st);
    return rc;
}

int kfw_db_tag_list_all(kfw_db_t *db, kfw_db_tag_t **out_arr, int *out_count)
{
    if (!db || !out_arr || !out_count)
        return -1;
    *out_arr = NULL;
    *out_count = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql,
            "SELECT t.tag_id,t.name,COUNT(i.item_id) FROM tags t"
            " LEFT JOIN item_tags i ON i.tag_id=t.tag_id"
            " GROUP BY t.tag_id ORDER BY t.name",
            -1, &st, NULL) != SQLITE_OK)
        return -1;
    int rc = tag_collect(st, 1, out_arr, out_count);
    sqlite3_finalize(st);
    return rc;
}

/* ---- Search -------------------------------------------------------------- */

int kfw_db_search(kfw_db_t *db, const char *case_id, const char *query,
                  kfw_db_hit_t **out_arr, int *out_count)
{
    if (!db || !case_id || !query || !out_arr || !out_count)
        return -1;
    *out_arr = NULL;
    *out_count = 0;

    /* Trim, then build  %query%  with LIKE wildcards in the query escaped. */
    while (*query == ' ' || *query == '\t')
        ++query;
    size_t qlen = strlen(query);
    while (qlen > 0 && (query[qlen - 1] == ' ' || query[qlen - 1] == '\t'))
        --qlen;
    if (qlen == 0)
        return 0;
    if (qlen > 200)
        qlen = 200;

    char pat[2 * 200 + 4];
    size_t n = 0;
    pat[n++] = '%';
    for (size_t i = 0; i < qlen; ++i)
    {
        if (query[i] == '%' || query[i] == '_' || query[i] == '\\')
            pat[n++] = '\\';
        pat[n++] = query[i];
    }
    pat[n++] = '%';
    pat[n] = '\0';

    static const char *SQL =
        "SELECT 'evidence', evidence_id, '', 0, orig_filename,"
        "       file_type || ' / ' || substr(sha256,1,16)"
        "  FROM evidence WHERE case_id=?1 AND (orig_filename LIKE ?2 ESCAPE '\\'"
        "   OR sha256 LIKE ?2 ESCAPE '\\' OR file_type LIKE ?2 ESCAPE '\\'"
        "   OR evidence_id LIKE ?2 ESCAPE '\\')"
        " UNION ALL "
        "SELECT 'event', evidence_id, event_id, ts_ms, kind || ': ' || label,"
        "       substr(body,1,200)"
        "  FROM events WHERE case_id=?1 AND (label LIKE ?2 ESCAPE '\\'"
        "   OR body LIKE ?2 ESCAPE '\\' OR kind LIKE ?2 ESCAPE '\\')"
        " UNION ALL "
        "SELECT 'note', evidence_id, '', 0, 'Note by ' || author, substr(body,1,200)"
        "  FROM notes WHERE case_id=?1 AND body LIKE ?2 ESCAPE '\\'"
        " UNION ALL "
        "SELECT 'tag', e.evidence_id, '', 0, e.orig_filename, 'tag: ' || t.name"
        "  FROM item_tags i JOIN tags t ON t.tag_id=i.tag_id"
        "  JOIN evidence e ON e.evidence_id=i.item_id"
        " WHERE i.item_type='evidence' AND e.case_id=?1 AND t.name LIKE ?2 ESCAPE '\\'"
        " UNION ALL "
        "SELECT 'tag', v.evidence_id, v.event_id, v.ts_ms, v.kind || ': ' || v.label,"
        "       'tag: ' || t.name"
        "  FROM item_tags i JOIN tags t ON t.tag_id=i.tag_id"
        "  JOIN events v ON v.event_id=i.item_id"
        " WHERE i.item_type='event' AND v.case_id=?1 AND t.name LIKE ?2 ESCAPE '\\'"
        " ORDER BY 1, 4, 5 LIMIT 500";

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db->sql, SQL, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, case_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pat, -1, SQLITE_STATIC);

    int cap = 0, count = 0;
    kfw_db_hit_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int nc = cap ? cap * 2 : 16;
            kfw_db_hit_t *g = (kfw_db_hit_t *)realloc(arr, (size_t)nc * sizeof(*arr));
            if (!g)
                break;
            arr = g;
            cap = nc;
        }
        kfw_db_hit_t *h = &arr[count++];
        memset(h, 0, sizeof(*h));
        snprintf(h->kind,        sizeof(h->kind),        "%s", col_text(st, 0));
        snprintf(h->evidence_id, sizeof(h->evidence_id), "%s", col_text(st, 1));
        snprintf(h->event_id,    sizeof(h->event_id),    "%s", col_text(st, 2));
        h->ts_ms = sqlite3_column_int64(st, 3);
        snprintf(h->title,       sizeof(h->title),       "%s", col_text(st, 4));
        snprintf(h->snippet,     sizeof(h->snippet),     "%s", col_text(st, 5));
    }
    sqlite3_finalize(st);
    *out_arr = arr;
    *out_count = count;
    return 0;
}

/* ---- Chain of custody ---------------------------------------------------- */

int kfw_db_audit_list_for_case(kfw_db_t *db, const char *case_id,
                               kfw_db_audit_row_t **out_arr, int *out_count)
{
    if (!db || !case_id || !out_arr || !out_count)
        return -1;
    *out_arr = NULL;
    *out_count = 0;

    sqlite3_stmt *st;
    const char *sql =
        "SELECT seq,utc,actor,action,target_type,target_id,detail_json,"
        "       prev_hash,entry_hash FROM audit"
        " WHERE target_id=?1"
        "    OR target_id IN (SELECT evidence_id FROM evidence WHERE case_id=?1)"
        "    OR target_id IN (SELECT event_id    FROM events   WHERE case_id=?1)"
        "    OR target_id IN (SELECT artifact_id FROM artifacts WHERE case_id=?1)"
        " ORDER BY seq";
    if (sqlite3_prepare_v2(db->sql, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, case_id, -1, SQLITE_STATIC);

    int cap = 0, count = 0;
    kfw_db_audit_row_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        if (count >= cap)
        {
            int nc = cap ? cap * 2 : 32;
            kfw_db_audit_row_t *g = (kfw_db_audit_row_t *)realloc(arr, (size_t)nc * sizeof(*arr));
            if (!g)
                break;
            arr = g;
            cap = nc;
        }
        kfw_db_audit_row_t *r = &arr[count++];
        memset(r, 0, sizeof(*r));
        r->seq = sqlite3_column_int64(st, 0);
        r->utc = sqlite3_column_int64(st, 1);
        snprintf(r->actor,       sizeof(r->actor),       "%s", col_text(st, 2));
        snprintf(r->action,      sizeof(r->action),      "%s", col_text(st, 3));
        snprintf(r->target_type, sizeof(r->target_type), "%s", col_text(st, 4));
        snprintf(r->target_id,   sizeof(r->target_id),   "%s", col_text(st, 5));
        snprintf(r->detail_json, sizeof(r->detail_json), "%s", col_text(st, 6));
        snprintf(r->prev_hash,   sizeof(r->prev_hash),   "%s", col_text(st, 7));
        snprintf(r->entry_hash,  sizeof(r->entry_hash),  "%s", col_text(st, 8));
    }
    sqlite3_finalize(st);
    *out_arr = arr;
    *out_count = count;
    return 0;
}
