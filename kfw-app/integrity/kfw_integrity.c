/*****************************************************************************
 * kfw_integrity.c: Kishan Forensic Workbench — SHA-256 integrity module
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Platform: Windows (CNG / BCrypt).  Wrap in #ifdef to add POSIX OpenSSL
 * or libsodium back-ends when targeting Linux.
 *****************************************************************************/

#include "kfw_integrity.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Windows CNG (BCrypt) SHA-256 ---------------------------------------- */
#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <bcrypt.h>
# pragma comment(lib, "bcrypt.lib")

/* I/O buffer — 256 KiB is a reasonable balance for spinning rust and SSDs. */
# define IO_BUFSIZE (256u * 1024u)

typedef struct win_sha256_ctx
{
    BCRYPT_ALG_HANDLE  alg;
    BCRYPT_HASH_HANDLE hash;
    PBYTE              hash_obj;
    DWORD              hash_obj_size;
    DWORD              digest_size;
} win_sha256_ctx_t;

static int ctx_init(win_sha256_ctx_t *ctx)
{
    NTSTATUS st;
    DWORD    result;

    st = BCryptOpenAlgorithmProvider(&ctx->alg, BCRYPT_SHA256_ALGORITHM,
                                     NULL, 0);
    if (!BCRYPT_SUCCESS(st))
        return -EIO;

    /* Query object buffer length. */
    st = BCryptGetProperty(ctx->alg, BCRYPT_OBJECT_LENGTH,
                           (PUCHAR)&ctx->hash_obj_size, sizeof(DWORD),
                           &result, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        BCryptCloseAlgorithmProvider(ctx->alg, 0);
        return -EIO;
    }

    st = BCryptGetProperty(ctx->alg, BCRYPT_HASH_LENGTH,
                           (PUCHAR)&ctx->digest_size, sizeof(DWORD),
                           &result, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        BCryptCloseAlgorithmProvider(ctx->alg, 0);
        return -EIO;
    }

    ctx->hash_obj = (PBYTE)malloc(ctx->hash_obj_size);
    if (!ctx->hash_obj)
    {
        BCryptCloseAlgorithmProvider(ctx->alg, 0);
        return -ENOMEM;
    }

    st = BCryptCreateHash(ctx->alg, &ctx->hash,
                          ctx->hash_obj, ctx->hash_obj_size,
                          NULL, 0, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        free(ctx->hash_obj);
        BCryptCloseAlgorithmProvider(ctx->alg, 0);
        return -EIO;
    }
    return 0;
}

static int ctx_update(win_sha256_ctx_t *ctx, const void *data, size_t len)
{
    NTSTATUS st = BCryptHashData(ctx->hash, (PUCHAR)data, (ULONG)len, 0);
    return BCRYPT_SUCCESS(st) ? 0 : -EIO;
}

static int ctx_finish(win_sha256_ctx_t *ctx,
                       unsigned char digest[KFW_SHA256_BYTES])
{
    NTSTATUS st = BCryptFinishHash(ctx->hash, digest, KFW_SHA256_BYTES, 0);
    BCryptDestroyHash(ctx->hash);
    free(ctx->hash_obj);
    BCryptCloseAlgorithmProvider(ctx->alg, 0);
    return BCRYPT_SUCCESS(st) ? 0 : -EIO;
}

#else
/* ---- POSIX / OpenSSL stub (replace with <openssl/sha.h> when needed) ----- */
# error "Unsupported platform: add OpenSSL or libsodium SHA-256 back-end here."
#endif /* _WIN32 */

/* ---- Common helpers ------------------------------------------------------- */

static void bytes_to_hex(const unsigned char *bytes, size_t n,
                          char *hex_out)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i)
    {
        hex_out[2 * i]     = hx[bytes[i] >> 4];
        hex_out[2 * i + 1] = hx[bytes[i] & 0x0f];
    }
    hex_out[2 * n] = '\0';
}

/* ---- Public API ----------------------------------------------------------- */

int kfw_sha256_file(const char *path,
                    char        hex_out[KFW_SHA256_HEX + 1],
                    int64_t    *bytes_read)
{
    if (!path || !hex_out)
        return -EINVAL;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -errno;

    win_sha256_ctx_t ctx;
    int rc = ctx_init(&ctx);
    if (rc != 0)
    {
        fclose(fp);
        return rc;
    }

    unsigned char *buf = (unsigned char *)malloc(IO_BUFSIZE);
    if (!buf)
    {
        fclose(fp);
        return -ENOMEM;
    }

    int64_t total = 0;
    size_t  n;
    while ((n = fread(buf, 1, IO_BUFSIZE, fp)) > 0)
    {
        rc = ctx_update(&ctx, buf, n);
        if (rc != 0)
            break;
        total += (int64_t)n;
    }

    if (rc == 0 && ferror(fp))
        rc = -EIO;

    free(buf);
    fclose(fp);

    if (rc != 0)
        return rc;

    unsigned char digest[KFW_SHA256_BYTES];
    rc = ctx_finish(&ctx, digest);
    if (rc != 0)
        return rc;

    bytes_to_hex(digest, KFW_SHA256_BYTES, hex_out);
    if (bytes_read)
        *bytes_read = total;
    return 0;
}

bool kfw_sha256_equal(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    for (int i = 0; i < KFW_SHA256_HEX; ++i)
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

int kfw_sha256_buf(const void *data, size_t len,
                   char         hex_out[KFW_SHA256_HEX + 1])
{
    if (!hex_out || (!data && len != 0))
        return -EINVAL;

    win_sha256_ctx_t ctx;
    int rc = ctx_init(&ctx);
    if (rc != 0)
        return rc;

    if (len > 0 && ctx_update(&ctx, data, len) != 0)
    {
        unsigned char dummy[KFW_SHA256_BYTES];
        ctx_finish(&ctx, dummy);
        return -EIO;
    }

    unsigned char digest[KFW_SHA256_BYTES];
    rc = ctx_finish(&ctx, digest);
    if (rc != 0)
        return rc;

    bytes_to_hex(digest, KFW_SHA256_BYTES, hex_out);
    return 0;
}

int kfw_integrity_import(const char *src_path,
                         const char *dest_path,
                         kfw_import_result_t *result)
{
    if (!src_path || !dest_path || !result)
        return -EINVAL;

    kfw_import_result_clear(result);

    FILE *src  = fopen(src_path,  "rb");
    if (!src)
        return -errno;

    FILE *dest = fopen(dest_path, "wb");
    if (!dest)
    {
        fclose(src);
        return -errno;
    }

    /* Initialise TWO hash contexts: one for the source stream, one for the
       copy.  We hash the source as we read and the copy as we write, then
       re-hash the stored file for the second comparison.  This two-pass
       approach (architecture.md §3.2 steps 2–3) detects I/O corruption. */
    win_sha256_ctx_t src_ctx, dest_ctx;
    int rc = ctx_init(&src_ctx);
    if (rc != 0)
        goto cleanup;
    rc = ctx_init(&dest_ctx);
    if (rc != 0)
    {
        /* src_ctx already initialised; finish it to release CNG resources */
        unsigned char dummy[KFW_SHA256_BYTES];
        ctx_finish(&src_ctx, dummy);
        goto cleanup;
    }

    unsigned char *buf = (unsigned char *)malloc(IO_BUFSIZE);
    if (!buf)
    {
        rc = -ENOMEM;
        unsigned char dummy[KFW_SHA256_BYTES];
        ctx_finish(&src_ctx, dummy);
        ctx_finish(&dest_ctx, dummy);
        goto cleanup;
    }

    int64_t total = 0;
    size_t  n;
    while ((n = fread(buf, 1, IO_BUFSIZE, src)) > 0)
    {
        if (ctx_update(&src_ctx, buf, n) != 0 ||
            fwrite(buf, 1, n, dest) != n ||
            ctx_update(&dest_ctx, buf, n) != 0)
        {
            rc = -EIO;
            break;
        }
        total += (int64_t)n;
    }

    if (rc == 0 && ferror(src))
        rc = -EIO;

    free(buf);

    {
        unsigned char sd[KFW_SHA256_BYTES], dd[KFW_SHA256_BYTES];
        int rcs = ctx_finish(&src_ctx,  sd);
        int rcd = ctx_finish(&dest_ctx, dd);
        if (rc == 0)
            rc = (rcs != 0 || rcd != 0) ? -EIO : 0;

        if (rc == 0)
        {
            bytes_to_hex(sd, KFW_SHA256_BYTES, result->src_sha256);
            bytes_to_hex(dd, KFW_SHA256_BYTES, result->dest_sha256);
            result->size_bytes = total;
            result->status = kfw_sha256_equal(result->src_sha256,
                                               result->dest_sha256)
                             ? KFW_VERIFY_VERIFIED
                             : KFW_VERIFY_MISMATCH;
            if (result->status == KFW_VERIFY_MISMATCH)
                rc = KFW_ERR_DIGEST_MISMATCH;
        }
    }

cleanup:
    fclose(src);
    fclose(dest);
    return rc;
}

kfw_verify_status_t kfw_integrity_verify(const char *stored_path,
                                          const char *ref_sha256,
                                          char        actual_hex[KFW_SHA256_HEX + 1])
{
    if (!stored_path || !ref_sha256)
        return KFW_VERIFY_NOT_CHECKED;

    char buf[KFW_SHA256_HEX + 1];
    char *out = actual_hex ? actual_hex : buf;

    if (kfw_sha256_file(stored_path, out, NULL) != 0)
        return KFW_VERIFY_NOT_CHECKED;

    return kfw_sha256_equal(out, ref_sha256)
           ? KFW_VERIFY_VERIFIED
           : KFW_VERIFY_MISMATCH;
}
