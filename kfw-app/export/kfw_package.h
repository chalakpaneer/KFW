/*
 * kfw_package.h — evidence handover packages (create + verify)
 * Copyright (C) 2026 Kishan G A
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A handover package is a self-describing folder:
 *
 *   <CASE>_handover_<UTC stamp>/
 *     manifest.json          every other file: path, size, SHA-256
 *     manifest.sha256        SHA-256 of manifest.json
 *     README.txt             how to verify, and what the package does NOT prove
 *     case.json              case particulars
 *     evidence_inventory.csv one row per evidence item
 *     chain_of_custody.csv   audit events concerning this case (+ .json)
 *     timeline.csv           examiner/analyzer events
 *     evidence/              byte-for-byte copies of the evidence files
 *     reports/               freshly generated JSON + HTML report
 *
 * Limits, stated plainly: the manifest detects accidental damage and casual
 * edits.  Someone able to rewrite both manifest.json and manifest.sha256 can
 * forge a consistent package, so the manifest SHA-256 should be sent to the
 * recipient through a second channel (or signed - planned for 0.3).
 */
#ifndef KFW_PACKAGE_H
#define KFW_PACKAGE_H 1

#include "../cases/kfw_db.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kfw_package_params
{
    kfw_db_t   *db;
    const char *case_id;
    const char *examiner;
    const char *out_dir;           /**< parent folder; package folder is created inside */
    const char *tool_name;         /**< e.g. "Kishan Forensic Workbench" */
    const char *tool_version;      /**< e.g. "0.1.0-alpha" */
    int         include_evidence;  /**< 1 = copy evidence files, 0 = metadata only */
} kfw_package_params_t;

typedef struct kfw_package_result
{
    char    package_dir[2048];
    char    manifest_sha256[65];
    int     n_files;               /**< files listed in the manifest */
    int64_t total_bytes;
    int     n_evidence;            /**< evidence items in the case */
    int     n_evidence_copied;
    int     n_evidence_missing;    /**< stored file not found on disk */
    int     n_evidence_mismatch;   /**< stored file no longer matches recorded SHA-256 */
    int     report_ok;             /**< 1 if the report was generated */
} kfw_package_result_t;

/** @return 0 = complete and clean; 1 = created but with missing/mismatching
 *  evidence or no report (see result); negative = failure. */
int kfw_package_create(const kfw_package_params_t *p, kfw_package_result_t *out);

typedef struct kfw_package_verify_result
{
    int  manifest_digest;    /**< 1 ok, 0 mismatch, -1 manifest.sha256 absent */
    int  n_files;            /**< entries in the manifest */
    int  n_ok;
    int  n_mismatch;
    int  n_missing;
    int  n_extra;            /**< files present but not in the manifest */
    char manifest_sha256[65];
    char first_problem[512]; /**< first problem found, or "" */
} kfw_package_verify_result_t;

/** @return 0 if the package verifies completely; 1 if problems were found;
 *  negative if the package could not be read. */
int kfw_package_verify(const char *package_dir, kfw_package_verify_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KFW_PACKAGE_H */
