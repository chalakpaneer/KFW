/*****************************************************************************
 * kfw_integrity.h: Kishan Forensic Workbench — SHA-256 integrity module
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
 * Design rules (see docs/architecture.md §3.2):
 *   - SHA-256 is computed via the platform's vetted cryptographic library.
 *     KFW never implements its own hash.
 *   - On Windows: Windows CNG (BCrypt).
 *   - Verification status vocabulary: "Verified" | "Mismatch" | "Not checked".
 *   - A matching hash establishes byte-identity only; never use the word
 *     "authentic" in UI or reports.
 *****************************************************************************/

#ifndef KFW_INTEGRITY_H
#define KFW_INTEGRITY_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Length of a SHA-256 digest in bytes. */
#define KFW_SHA256_BYTES  32
/** Length of the hex-encoded digest string (no NUL). */
#define KFW_SHA256_HEX    64

/**
 * Verification status — exactly three values.
 * See architecture.md §3.2 for semantics.
 */
typedef enum kfw_verify_status
{
    KFW_VERIFY_NOT_CHECKED = 0, /**< No verification has been performed. */
    KFW_VERIFY_VERIFIED,        /**< Bytes hash to the reference digest.  */
    KFW_VERIFY_MISMATCH,        /**< Bytes do NOT hash to the reference.  */
} kfw_verify_status_t;

/** Human-readable status strings matching the vocabulary above. */
static inline const char *kfw_verify_status_str(kfw_verify_status_t s)
{
    switch (s)
    {
    case KFW_VERIFY_VERIFIED:    return "Verified";
    case KFW_VERIFY_MISMATCH:    return "Mismatch";
    case KFW_VERIFY_NOT_CHECKED: /* fall-through */
    default:                     return "Not checked";
    }
}

/**
 * Compute the SHA-256 digest of the file at @p path.
 *
 * @param path        Filesystem path to the file (UTF-8 on POSIX;
 *                    converted to UTF-16 on Windows internally).
 * @param hex_out     Caller-supplied buffer of at least KFW_SHA256_HEX + 1
 *                    bytes. On success, contains a lowercase hex string.
 * @param bytes_read  If non-NULL, receives the number of bytes hashed.
 *
 * @return 0 on success, negative errno-style value on failure.
 */
int kfw_sha256_file(const char *path,
                    char        hex_out[KFW_SHA256_HEX + 1],
                    int64_t    *bytes_read);

/**
 * Compare two hex digest strings, case-insensitively.
 * Both strings must be exactly KFW_SHA256_HEX characters long.
 *
 * @return true if they are identical, false otherwise.
 */
bool kfw_sha256_equal(const char *a, const char *b);

/**
 * Compute the SHA-256 digest of an in-memory buffer.
 *
 * Used for short data such as audit-chain pre-images; never has to go
 * through the filesystem.
 *
 * @param data       Buffer to hash (may be NULL only if len == 0).
 * @param len        Buffer length in bytes.
 * @param hex_out    Caller-supplied buffer of at least KFW_SHA256_HEX + 1
 *                   bytes. On success, contains a lowercase hex string.
 *
 * @return 0 on success, negative errno-style value on failure.
 */
int kfw_sha256_buf(const void *data, size_t len,
                   char         hex_out[KFW_SHA256_HEX + 1]);

/** Result record produced by kfw_integrity_import(). Defined below. */
struct kfw_import_result;

/**
 * Import a source file into the evidence store with integrity verification.
 *
 * Steps (architecture.md §3.2):
 *   1. Copy @p src_path → @p dest_path.
 *   2. Compute SHA-256 of the source while copying.
 *   3. Compute SHA-256 of the stored copy; compare to the source digest.
 *   4. Fill @p result with both digests, file size, and status.
 *
 * @param src_path      Absolute path to the original file.
 * @param dest_path     Absolute path where the evidence copy will be written.
 *                      The directory must already exist.
 * @param result        Output structure; see kfw_import_result_t.
 *
 * @return 0 if both digests match (file copied and verified),
 *         KFW_ERR_DIGEST_MISMATCH if the copy digest differs from source,
 *         negative errno-style value on I/O or allocation failure.
 */
int kfw_integrity_import(const char *src_path,
                         const char *dest_path,
                         struct kfw_import_result *result);

/** Sentinel return value for a copy-vs-source digest mismatch. */
#define KFW_ERR_DIGEST_MISMATCH (-0x4B465701)

/**
 * Result record produced by kfw_integrity_import().
 *
 * All string fields are heap-allocated and owned by the caller.
 * Call kfw_import_result_clear() to release them.
 */
typedef struct kfw_import_result
{
    char               src_sha256[KFW_SHA256_HEX + 1];  /**< Source digest.  */
    char               dest_sha256[KFW_SHA256_HEX + 1]; /**< Stored digest.  */
    int64_t            size_bytes;                        /**< Bytes copied.   */
    kfw_verify_status_t status;                           /**< Comparison.     */
} kfw_import_result_t;

/** Zero-fill a result record (digests are in-struct arrays, no free needed). */
static inline void kfw_import_result_clear(kfw_import_result_t *r)
{
    if (r)
    {
        r->src_sha256[0]  = '\0';
        r->dest_sha256[0] = '\0';
        r->size_bytes     = 0;
        r->status         = KFW_VERIFY_NOT_CHECKED;
    }
}

/**
 * Re-verify a stored evidence file against its recorded reference digest.
 *
 * @param stored_path   Path to the file in the evidence store.
 * @param ref_sha256    The reference SHA-256 hex digest (64 hex chars + NUL).
 * @param actual_hex    If non-NULL and large enough (KFW_SHA256_HEX+1),
 *                      receives the freshly computed digest.
 *
 * @return KFW_VERIFY_VERIFIED, KFW_VERIFY_MISMATCH, or KFW_VERIFY_NOT_CHECKED
 *         (if hashing fails).
 */
kfw_verify_status_t kfw_integrity_verify(const char *stored_path,
                                         const char *ref_sha256,
                                         char        actual_hex[KFW_SHA256_HEX + 1]);

#ifdef __cplusplus
}
#endif

#endif /* KFW_INTEGRITY_H */
