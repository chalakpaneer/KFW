/*****************************************************************************
 * kfw_evidence.h: Kishan Forensic Workbench — evidence lifecycle (public API)
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
 * This module owns the high-level evidence workflow (architecture.md §3.2):
 *
 *   kfw_evidence_import()
 *     1. Determine unique EV-NNNN ID
 *     2. Copy source file → evidence store directory
 *     3. SHA-256 both streams (kfw_integrity_import)
 *     4. Write kfw_db_evidence record
 *     5. Append audit event
 *
 *   kfw_evidence_reverify()
 *     Recompute stored-file digest; compare to DB reference; update status.
 *****************************************************************************/

#ifndef KFW_EVIDENCE_H
#define KFW_EVIDENCE_H 1

#include "../integrity/kfw_integrity.h"
#include "../cases/kfw_db.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Context required for all evidence operations.
 * Holds a reference to the open database and the root path of the evidence
 * store on disk (a directory where imported files are copied).
 */
typedef struct kfw_evidence_ctx
{
    kfw_db_t   *db;            /**< Open case database (owned by caller). */
    const char *store_root;    /**< Absolute path to the evidence store dir. */
    const char *examiner;      /**< Examiner name for audit records. */
} kfw_evidence_ctx_t;

/**
 * Import a source file into the evidence store.
 *
 * @param ctx         Evidence context (db, store_root, examiner).
 * @param case_id     Case this evidence belongs to.
 * @param src_path    Absolute path to the original source file.
 * @param provenance  "Original" or "Derived" — caller-supplied classification.
 * @param out_ev      On success, receives the populated evidence record.
 *                    evidence_id is auto-generated.
 *
 * @return  0                       Success — stored and verified.
 * @return  KFW_ERR_DIGEST_MISMATCH Copy digest differs from source digest.
 * @return  negative                I/O or allocation failure.
 */
int kfw_evidence_import(const kfw_evidence_ctx_t *ctx,
                        const char               *case_id,
                        const char               *src_path,
                        const char               *provenance,
                        kfw_db_evidence_t        *out_ev);

/**
 * Re-verify an already-imported evidence item.
 *
 * Computes SHA-256 of the stored file, compares to the reference digest in
 * the database, updates the verify_status and verified_utc fields, and appends
 * an audit event.
 *
 * @param ctx          Evidence context.
 * @param evidence_id  The "EV-NNNN" identifier to verify.
 * @param out_status   Receives the resulting kfw_verify_status_t.
 *
 * @return 0 on success (regardless of match/mismatch), negative on error.
 */
int kfw_evidence_reverify(const kfw_evidence_ctx_t *ctx,
                          const char               *evidence_id,
                          kfw_verify_status_t      *out_status);

/**
 * Derive the expected stored-file path for a given evidence_id.
 *
 * Naming convention:  <store_root>/<case_id>/<evidence_id>_<orig_filename>
 *
 * @param store_root   Evidence store root.
 * @param case_id      Case identifier.
 * @param evidence_id  Evidence identifier.
 * @param orig_name    Original filename (basename only; without path).
 * @param out_buf      Output buffer.
 * @param buf_size     Size of output buffer in bytes.
 *
 * @return 0 on success, -1 if the buffer is too small.
 */
int kfw_evidence_stored_path(const char *store_root,
                              const char *case_id,
                              const char *evidence_id,
                              const char *orig_name,
                              char       *out_buf,
                              size_t      buf_size);

#ifdef __cplusplus
}
#endif

#endif /* KFW_EVIDENCE_H */
