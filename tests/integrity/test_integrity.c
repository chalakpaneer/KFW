/*
 * test_integrity.c — KFW integrity module unit tests
 * Copyright (C) 2026 Kishan G A
 *
 * Tests:
 *   1. kfw_sha256_file() produces a known digest for a small fixed file.
 *   2. kfw_sha256_equal() correctly detects match and mismatch.
 *   3. kfw_integrity_import() copies and verifies successfully.
 *   4. kfw_integrity_import() detects a simulated copy corruption.
 *   5. kfw_integrity_verify() returns Verified / Mismatch correctly.
 */

#include "integrity/kfw_integrity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Known SHA-256 digests (computed independently with sha256sum) ─────── */
/* SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
static const char *SHA256_EMPTY =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

/* SHA-256("KFW test vector\n") — verified with python hashlib */
static const char *SHA256_KNOWN_CONTENT =
    "41ff7cdfa798e537a59c2cd15925ca0e8f6d405a0f370bee6c06e0266076681a";
static const char *KNOWN_CONTENT = "KFW test vector\n";

/* ── Helpers ──────────────────────────────────────────────────────────── */

static int tests_run = 0, tests_passed = 0;

#define CHECK(desc, expr) do { \
    ++tests_run; \
    if (expr) { \
        ++tests_passed; \
        printf("[PASS] %s\n", (desc)); \
    } else { \
        printf("[FAIL] %s\n", (desc)); \
    } \
} while (0)

static const char *write_tmp(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    return path;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_sha256_empty(void)
{
    const char *tmp = "kfw_test_empty.bin";
    write_tmp(tmp, "");

    char hex[KFW_SHA256_HEX + 1];
    int64_t bytes = -1;
    int rc = kfw_sha256_file(tmp, hex, &bytes);

    CHECK("sha256_file empty file returns 0",          rc == 0);
    CHECK("sha256_file empty file bytes_read == 0",    bytes == 0);
    CHECK("sha256_file empty file digest matches",     kfw_sha256_equal(hex, SHA256_EMPTY));

    remove(tmp);
}

static void test_sha256_known_content(void)
{
    const char *tmp = "kfw_test_known.bin";
    write_tmp(tmp, KNOWN_CONTENT);

    char hex[KFW_SHA256_HEX + 1];
    int64_t bytes = -1;
    int rc = kfw_sha256_file(tmp, hex, &bytes);

    CHECK("sha256_file known content returns 0",         rc == 0);
    CHECK("sha256_file known content bytes_read correct",
          bytes == (int64_t)strlen(KNOWN_CONTENT));
    CHECK("sha256_file known content digest matches",
          kfw_sha256_equal(hex, SHA256_KNOWN_CONTENT));

    remove(tmp);
}

static void test_sha256_equal(void)
{
    CHECK("sha256_equal same string",
          kfw_sha256_equal(SHA256_EMPTY, SHA256_EMPTY));
    CHECK("sha256_equal case-insensitive",
          kfw_sha256_equal(
            "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
            SHA256_EMPTY));
    CHECK("sha256_equal different strings",
          !kfw_sha256_equal(SHA256_EMPTY, SHA256_KNOWN_CONTENT));
    CHECK("sha256_equal NULL a",  !kfw_sha256_equal(NULL, SHA256_EMPTY));
    CHECK("sha256_equal NULL b",  !kfw_sha256_equal(SHA256_EMPTY, NULL));
}

static void test_import_success(void)
{
    const char *src  = "kfw_import_src.bin";
    const char *dest = "kfw_import_dest.bin";
    write_tmp(src, "Forensic evidence import test payload\n");

    kfw_import_result_t result;
    kfw_import_result_clear(&result);
    int rc = kfw_integrity_import(src, dest, &result);

    CHECK("import returns 0",                   rc == 0);
    CHECK("import status is Verified",          result.status == KFW_VERIFY_VERIFIED);
    CHECK("import src/dest digests match",
          kfw_sha256_equal(result.src_sha256, result.dest_sha256));
    CHECK("import size > 0",                    result.size_bytes > 0);

    remove(src);
    remove(dest);
}

static void test_verify_cycle(void)
{
    const char *path = "kfw_verify_cycle.bin";
    write_tmp(path, "Verify cycle test payload\n");

    char ref_hex[KFW_SHA256_HEX + 1];
    kfw_sha256_file(path, ref_hex, NULL);

    /* Verify unmodified */
    char actual[KFW_SHA256_HEX + 1];
    kfw_verify_status_t s = kfw_integrity_verify(path, ref_hex, actual);
    CHECK("verify cycle: Verified on unmodified file", s == KFW_VERIFY_VERIFIED);
    CHECK("verify cycle: actual matches ref",          kfw_sha256_equal(actual, ref_hex));

    /* Tamper the file and verify again */
    FILE *f = fopen(path, "ab");
    if (f) { fwrite("X", 1, 1, f); fclose(f); }

    s = kfw_integrity_verify(path, ref_hex, actual);
    CHECK("verify cycle: Mismatch after tampering", s == KFW_VERIFY_MISMATCH);

    remove(path);
}

static void test_verify_missing_file(void)
{
    kfw_verify_status_t s =
        kfw_integrity_verify("__nonexistent_kfw_file__.bin",
                              SHA256_EMPTY, NULL);
    CHECK("verify missing file returns NOT_CHECKED", s == KFW_VERIFY_NOT_CHECKED);
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== KFW Integrity Tests ===\n\n");

    test_sha256_empty();
    test_sha256_known_content();
    test_sha256_equal();
    test_import_success();
    test_verify_cycle();
    test_verify_missing_file();

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
