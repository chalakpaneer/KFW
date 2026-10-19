/*****************************************************************************
 * kfw_evidence.c: Kishan Forensic Workbench — evidence lifecycle
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *****************************************************************************/

#include "kfw_evidence.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <direct.h>   /* _mkdir */
# define PATH_SEP "\\"
# define kfw_mkdir(p) _mkdir(p)
#else
# include <sys/stat.h>
# define PATH_SEP "/"
# define kfw_mkdir(p) mkdir(p, 0700)
#endif

/* ---- Internal helpers ---------------------------------------------------- */

/** Extract the basename from a full path (last path separator component). */
static const char *basename_of(const char *path)
{
    const char *s = path;
    const char *last = path;
    while (*s)
    {
        if (*s == '/' || *s == '\\')
            last = s + 1;
        ++s;
    }
    return last;
}

/** Detect file type from extension (very lightweight — not a magic-byte check). */
static const char *file_type_from_name(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return "Unknown";
    const char *ext = dot + 1;
    /* Normalise to upper-case for comparison */
    char buf[16] = {0};
    for (int i = 0; i < 15 && ext[i]; ++i)
        buf[i] = (char)(ext[i] >= 'a' && ext[i] <= 'z'
                        ? ext[i] - 32 : ext[i]);

    if (!strcmp(buf,"MP4"))  return "MP4";
    if (!strcmp(buf,"AVI"))  return "AVI";
    if (!strcmp(buf,"MKV"))  return "MKV";
    if (!strcmp(buf,"MOV"))  return "MOV";
    if (!strcmp(buf,"MTS"))  return "MTS";
    if (!strcmp(buf,"M2TS")) return "M2TS";
    if (!strcmp(buf,"TS"))   return "TS";
    if (!strcmp(buf,"MPG") || !strcmp(buf,"MPEG")) return "MPEG";
    if (!strcmp(buf,"WMV"))  return "WMV";
    if (!strcmp(buf,"FLV"))  return "FLV";
    if (!strcmp(buf,"WAV"))  return "WAV";
    if (!strcmp(buf,"MP3"))  return "MP3";
    return "Media";
}

/** Ensure that the directory path @p dir exists (create if needed). */
static int ensure_dir(const char *dir)
{
    if (!dir)
        return -EINVAL;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(dir);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    return CreateDirectoryA(dir, NULL) ? 0 : -EIO;
#else
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return mkdir(dir, 0700) == 0 ? 0 : -errno;
#endif
}

/* ---- Public API ----------------------------------------------------------- */

int kfw_evidence_stored_path(const char *store_root,
                              const char *case_id,
                              const char *evidence_id,
                              const char *orig_name,
                              char       *out_buf,
                              size_t      buf_size)
{
    if (!store_root || !case_id || !evidence_id || !orig_name ||
        !out_buf || buf_size == 0)
        return -1;

    int n = snprintf(out_buf, buf_size, "%s%s%s%s%s_%s",
                     store_root, PATH_SEP, case_id, PATH_SEP,
                     evidence_id, orig_name);
    return (n > 0 && (size_t)n < buf_size) ? 0 : -1;
}

int kfw_evidence_import(const kfw_evidence_ctx_t *ctx,
                        const char               *case_id,
                        const char               *src_path,
                        const char               *provenance,
                        kfw_db_evidence_t        *out_ev)
{
    if (!ctx || !ctx->db || !ctx->store_root ||
        !case_id || !src_path || !out_ev)
        return -EINVAL;

    memset(out_ev, 0, sizeof(*out_ev));
    snprintf(out_ev->case_id, sizeof(out_ev->case_id), "%s", case_id);
    snprintf(out_ev->provenance, sizeof(out_ev->provenance), "%s",
             provenance ? provenance : "Original");

    const char *orig_name = basename_of(src_path);
    snprintf(out_ev->orig_filename, sizeof(out_ev->orig_filename),
             "%s", orig_name);
    snprintf(out_ev->orig_path, sizeof(out_ev->orig_path), "%s", src_path);
    snprintf(out_ev->file_type, sizeof(out_ev->file_type),
             "%s", file_type_from_name(orig_name));
    snprintf(out_ev->verify_status, sizeof(out_ev->verify_status),
             "Not checked");

    /* Pre-insert to obtain the auto-generated evidence_id */
    int rc = kfw_db_evidence_insert(ctx->db, out_ev);
    if (rc != 0)
        return rc;

    /* Build case-level sub-directory in the evidence store */
    char case_dir[2048];
    snprintf(case_dir, sizeof(case_dir), "%s%s%s",
             ctx->store_root, PATH_SEP, case_id);
    ensure_dir(ctx->store_root);
    ensure_dir(case_dir);

    /* Determine destination path */
    char dest[2048];
    rc = kfw_evidence_stored_path(ctx->store_root, case_id,
                                   out_ev->evidence_id, orig_name,
                                   dest, sizeof(dest));
    if (rc != 0)
        return rc;

    /* Copy with dual-stream SHA-256 */
    kfw_import_result_t result;
    kfw_import_result_clear(&result);
    rc = kfw_integrity_import(src_path, dest, &result);

    /* Update the DB record with size, hash, stored path, and verify status */
    out_ev->size_bytes = result.size_bytes;
    snprintf(out_ev->sha256,       sizeof(out_ev->sha256),
             "%s", result.src_sha256);
    snprintf(out_ev->stored_path,  sizeof(out_ev->stored_path),  "%s", dest);
    snprintf(out_ev->verify_status, sizeof(out_ev->verify_status),
             "%s", kfw_verify_status_str(result.status));

    /* Persist the post-copy fields */
    kfw_db_evidence_update_import(ctx->db, out_ev->evidence_id,
                                   out_ev->size_bytes, out_ev->sha256,
                                   out_ev->stored_path,
                                   out_ev->verify_status);

    /* Append import audit event with provenance detail */
    {
        char detail[512];
        snprintf(detail, sizeof(detail),
                 "{\"src\":\"%s\",\"dest\":\"%s\","
                 "\"src_sha256\":\"%s\",\"dest_sha256\":\"%s\","
                 "\"size\":%lld,\"verify\":\"%s\"}",
                 src_path, dest,
                 result.src_sha256, result.dest_sha256,
                 (long long)result.size_bytes,
                 kfw_verify_status_str(result.status));
        kfw_db_audit_append(ctx->db,
                             ctx->examiner ? ctx->examiner : "system",
                             "evidence.import",
                             "evidence", out_ev->evidence_id, detail);
    }

    return rc;   /* 0 = ok, KFW_ERR_DIGEST_MISMATCH if copy was bad */
}

int kfw_evidence_reverify(const kfw_evidence_ctx_t *ctx,
                           const char               *evidence_id,
                           kfw_verify_status_t      *out_status)
{
    if (!ctx || !ctx->db || !evidence_id || !out_status)
        return -EINVAL;

    *out_status = KFW_VERIFY_NOT_CHECKED;

    /* 1. Load the stored evidence record */
    kfw_db_evidence_t ev;
    if (kfw_db_evidence_get(ctx->db, evidence_id, &ev) != 0)
        return -ENOENT;

    if (!ev.stored_path[0] || !ev.sha256[0])
        return -ENOENT;

    /* 2. Re-hash the stored file and compare to the reference digest */
    char actual_hex[KFW_SHA256_HEX + 1] = {0};
    kfw_verify_status_t status =
        kfw_integrity_verify(ev.stored_path, ev.sha256, actual_hex);

    *out_status = status;

    /* 3. Record the result and append an audit event */
    kfw_db_evidence_set_verify(ctx->db, evidence_id,
                                kfw_verify_status_str(status));

    {
        char detail[768];
        snprintf(detail, sizeof(detail),
                 "{\"evidence_id\":\"%s\",\"stored\":\"%s\","
                 "\"reference_sha256\":\"%s\",\"computed_sha256\":\"%s\","
                 "\"result\":\"%s\"}",
                 evidence_id, ev.stored_path,
                 ev.sha256,
                 status == KFW_VERIFY_NOT_CHECKED ? "" : actual_hex,
                 kfw_verify_status_str(status));
        kfw_db_audit_append(ctx->db,
                             ctx->examiner ? ctx->examiner : "system",
                             "evidence.reverify",
                             "evidence", evidence_id, detail);
    }

    return 0;
}
