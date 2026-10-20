/*****************************************************************************
 * kfw_db.h: Kishan Forensic Workbench — SQLite case database (public API)
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * KFW is an independent project and is not affiliated with or endorsed by
 * VideoLAN.
 *
 * Schema (architecture.md §3.3):
 *   cases, evidence, media_info, events, notes, artifacts, audit
 *****************************************************************************/

#ifndef KFW_DB_H
#define KFW_DB_H 1

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque database handle ---------------------------------------------- */
typedef struct kfw_db kfw_db_t;

/**
 * Open (or create) the KFW case database at @p path.
 * Runs CREATE TABLE IF NOT EXISTS for all schema tables and creates indices.
 *
 * @return handle on success, NULL on failure.
 */
kfw_db_t *kfw_db_open(const char *path);

/** Close the database. Safe with NULL. */
void kfw_db_close(kfw_db_t *db);

/** Return the last SQLite error message (static storage). */
const char *kfw_db_errmsg(kfw_db_t *db);

/* ---- Case record --------------------------------------------------------- */

typedef struct kfw_db_case
{
    char    case_id[32];         /**< "KFW-YYYY-NNN" */
    char    title[256];
    char    examiner[128];
    char    status[32];          /**< "Open" | "Closed" | "Pending Review" */
    int64_t created_utc;         /**< Unix timestamp (seconds) */
    int64_t modified_utc;
    char    notes[2048];
    /* ---- schema v2: case particulars ---- */
    char    case_number[64];     /**< agency / court reference number */
    char    agency[128];
    char    requester[128];
    char    incident_date[32];   /**< free text or ISO date */
    char    location[256];
    char    description[2048];
} kfw_db_case_t;

/**
 * Insert a new case.  case_id, created_utc, and modified_utc are auto-set.
 * @return 0 on success.
 */
int kfw_db_case_insert(kfw_db_t *db, kfw_db_case_t *c);

/**
 * Update the status and notes of an existing case.  modified_utc is set
 * to the current UTC time.
 */
int kfw_db_case_update(kfw_db_t *db, const char *case_id,
                        const char *status, const char *notes);

/** Load a case by ID.  Returns 0 if found, 1 if not found, negative on error. */
int kfw_db_case_get(kfw_db_t *db, const char *case_id, kfw_db_case_t *out);

/**
 * List all cases, newest first.
 *
 * @param out_arr   Receives a heap-allocated array of kfw_db_case_t.
 *                  Caller must free(*out_arr).
 * @param out_count Receives the array length.
 */
int kfw_db_case_list(kfw_db_t *db,
                     kfw_db_case_t **out_arr, int *out_count);

/* ---- Evidence record ----------------------------------------------------- */

typedef struct kfw_db_evidence
{
    char    evidence_id[32];     /**< "EV-NNNN" */
    char    case_id[32];
    char    orig_filename[512];
    char    orig_path[2048];
    int64_t size_bytes;
    char    sha256[65];          /**< 64 hex chars + NUL */
    char    stored_path[2048];
    int64_t imported_utc;
    char    provenance[32];      /**< "Original" | "Derived" */
    char    file_type[64];
    int64_t verified_utc;        /**< 0 = never verified */
    char    verify_status[16];   /**< "Verified" | "Mismatch" | "Not checked" */
    /* ---- schema v2 ---- */
    int64_t fs_modified_utc;     /**< source file's claimed mtime (0 = unknown) */
    char    duplicate_of[32];    /**< evidence_id with identical SHA-256, or "" */
} kfw_db_evidence_t;

/** Insert an evidence record.  evidence_id and imported_utc are auto-set. */
int kfw_db_evidence_insert(kfw_db_t *db, kfw_db_evidence_t *e);

/**
 * Patch an evidence row after the copy+hash completes: size, digest, stored
 * path and verification status.  The fields were unknown (or the digest was
 * still being computed) at insert time.
 */
int kfw_db_evidence_update_import(kfw_db_t *db, const char *evidence_id,
                                  int64_t size_bytes, const char *sha256,
                                  const char *stored_path,
                                  const char *status);

/** Update verification status and verified_utc timestamp. */
int kfw_db_evidence_set_verify(kfw_db_t *db, const char *evidence_id,
                                const char *status);

/** Load a single evidence row by ID.  0 found / 1 not found / negative err. */
int kfw_db_evidence_get(kfw_db_t *db, const char *evidence_id,
                         kfw_db_evidence_t *out);

/** List all evidence for a case, insertion order. */
int kfw_db_evidence_list(kfw_db_t *db, const char *case_id,
                          kfw_db_evidence_t **out_arr, int *out_count);

/* ---- Event record -------------------------------------------------------- */

typedef struct kfw_db_event
{
    char    event_id[40];
    char    case_id[32];
    char    evidence_id[32];     /**< may be empty if case-level */
    int64_t ts_ms;               /**< media timestamp (ms), or 0 */
    char    kind[32];            /**< "Marker" | "Bookmark" | "Note" | … */
    char    label[256];
    char    body[4096];
    int64_t created_utc;
} kfw_db_event_t;

/** Insert an event.  event_id and created_utc are auto-set. */
int kfw_db_event_insert(kfw_db_t *db, kfw_db_event_t *ev);

/** List events for a case, ordered by ts_ms then created_utc. */
int kfw_db_event_list(kfw_db_t *db, const char *case_id,
                      kfw_db_event_t **out_arr, int *out_count);

/* ---- Audit record -------------------------------------------------------- */

/**
 * Append an audit event.  The audit table implements a hash chain:
 *   entry_hash = SHA-256(prev_hash || canonical_row)
 *
 * This is tamper-evident, not tamper-proof — see docs/threat-model.md.
 *
 * @param actor        Examiner name or "system".
 * @param action       Verb: "case.create", "evidence.import",
 *                     "evidence.verify", "frame.export", "report.generate" …
 * @param target_type  "case" | "evidence" | "artifact" | "report"
 * @param target_id    The case_id, evidence_id, etc.
 * @param detail_json  Optional JSON string with operation-specific context.
 */
int kfw_db_audit_append(kfw_db_t   *db,
                         const char *actor,
                         const char *action,
                         const char *target_type,
                         const char *target_id,
                         const char *detail_json);

/** Verify the audit chain from seq @p from_seq to end.
 *  Returns 0 if intact, 1 if broken, negative on error. */
int kfw_db_audit_verify_chain(kfw_db_t *db, int64_t from_seq);


/* ---- Schema v2 additions -------------------------------------------------- */

#define KFW_DB_SCHEMA_VERSION 2

/** Update the case particulars (number, agency, requester, incident date,
 *  location, description).  Audited as "case.meta". */
int kfw_db_case_update_meta(kfw_db_t *db, const char *case_id,
                            const kfw_db_case_t *meta, const char *actor);

/** All evidence rows (any case) whose SHA-256 equals @p sha256.
 *  Caller frees *out_arr with free(). */
int kfw_db_evidence_find_by_sha256(kfw_db_t *db, const char *sha256,
                                   kfw_db_evidence_t **out_arr, int *out_count);

/** Record that @p evidence_id is byte-identical to @p duplicate_of and the
 *  source file's modification time. */
int kfw_db_evidence_set_import_extras(kfw_db_t *db, const char *evidence_id,
                                      int64_t fs_modified_utc,
                                      const char *duplicate_of);

/* ---- Tags ------------------------------------------------------------------ */

typedef struct kfw_db_tag
{
    int  tag_id;
    char name[64];
    int  use_count;              /**< only filled by kfw_db_tag_list_all */
} kfw_db_tag_t;

/** item_type is "evidence" or "event".  The tag is created on first use.
 *  Applying an already-applied tag succeeds without a second audit entry. */
int kfw_db_tag_apply(kfw_db_t *db, const char *actor, const char *item_type,
                     const char *item_id, const char *tag_name);
int kfw_db_tag_remove(kfw_db_t *db, const char *actor, const char *item_type,
                      const char *item_id, const char *tag_name);
int kfw_db_tag_list_for(kfw_db_t *db, const char *item_type, const char *item_id,
                        kfw_db_tag_t **out_arr, int *out_count);
int kfw_db_tag_list_all(kfw_db_t *db, kfw_db_tag_t **out_arr, int *out_count);

/* ---- Search ---------------------------------------------------------------- */

typedef struct kfw_db_hit
{
    char    kind[16];            /**< "evidence" | "event" | "note" | "tag" */
    char    evidence_id[32];
    char    event_id[40];
    int64_t ts_ms;               /**< event time, or 0 */
    char    title[256];
    char    snippet[256];
} kfw_db_hit_t;

/** Case-insensitive substring search over evidence names/hashes/types, event
 *  labels and bodies, notes, and tag names.  An empty query returns 0 hits. */
int kfw_db_search(kfw_db_t *db, const char *case_id, const char *query,
                  kfw_db_hit_t **out_arr, int *out_count);

/* ---- Chain of custody ------------------------------------------------------ */

typedef struct kfw_db_audit_row
{
    int64_t seq;
    int64_t utc;
    char    actor[128];
    char    action[64];
    char    target_type[32];
    char    target_id[64];
    char    detail_json[2048];
    char    prev_hash[65];
    char    entry_hash[65];
} kfw_db_audit_row_t;

/** Audit rows that concern @p case_id: the case itself, its evidence, events
 *  and artifacts.  Ordered by seq.  Caller frees *out_arr with free(). */
int kfw_db_audit_list_for_case(kfw_db_t *db, const char *case_id,
                               kfw_db_audit_row_t **out_arr, int *out_count);

/* ---- Schema version and raw access --------------------------------------- */

typedef struct sqlite3 sqlite3; /* sqlite3.h, opaque here */

/** Return the schema version stored in the database, or -1 on error. */
int kfw_db_schema_version(kfw_db_t *db);

/**
 * Return the raw sqlite3 handle.  Intended for the test-suite and for
 * successive migration tooling only — application code must go through the
 * typed API above so that every mutation stays auditable.
 */
sqlite3 *kfw_db_raw_handle(kfw_db_t *db);

#ifdef __cplusplus
}
#endif

#endif /* KFW_DB_H */
