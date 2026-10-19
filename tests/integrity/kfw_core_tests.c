/*****************************************************************************
 * kfw_core_tests.c: KFW unit tests — integrity + case database + audit chain
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Run from the build directory; leaves no outside artifacts (all temp files
 * are created under the directory given by argv[1] and removed on exit).
 *****************************************************************************/

#include "../../kfw-app/integrity/kfw_integrity.h"
#include "../../kfw-app/cases/kfw_db.h"

#include <sqlite3.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#undef _WIN32_WINNT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int g_failures = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) { printf("PASS  %s\n", (name)); } \
        else      { printf("FAIL  %s\n", (name)); ++g_failures; } \
    } while (0)

/* ---- helpers ------------------------------------------------------------- */

static char g_dir[380];

static void make_test_file(const char *dir, const char *name,
                           const void *data, size_t len, char *out_path,
                           size_t out_cap)
{
    snprintf(out_path, out_cap, "%s\\%s", dir, name);
    FILE *f = fopen(out_path, "wb");
    if (!f)
    {
        fprintf(stderr, "cannot create %s\n", out_path);
        exit(1);
    }
    fwrite(data, 1, len, f);
    fclose(f);
}

/* Fill a pseudo-random-ish deterministic buffer (NOT cryptographic). */
static void fill_buf(unsigned char *b, size_t n)
{
    uint32_t x = 0x12345678u;
    for (size_t i = 0; i < n; ++i)
    {
        x = x * 1103515245u + 12345u;
        b[i] = (unsigned char)(x >> 16);
    }
}

/* ---- tests ---------------------------------------------------------------- */

static void test_sha256_vectors(const char *dir)
{
    char path[512], hex[65] = {0};
    int64_t n = 0;

    /* empty file → e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    make_test_file(dir, "empty.bin", "", 0, path, sizeof(path));
    CHECK(kfw_sha256_file(path, hex, &n) == 0, "sha256_file: empty file");
    CHECK(n == 0, "sha256_file: zero bytes reported");
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "sha256_file: empty file digest matches known vector");

    /* 1 MiB of patterned data → hash once via buffer, once via file */
    static unsigned char big[1024 * 1024];
    fill_buf(big, sizeof(big));
    make_test_file(dir, "big.bin", big, sizeof(big), path, sizeof(path));

    char hex_buf[65] = {0}, hex_file[65] = {0};
    int rc_buf  = kfw_sha256_buf(big, sizeof(big), hex_buf);
    int rc_file = kfw_sha256_file(path, hex_file, &n);
    CHECK(rc_buf == 0 && rc_file == 0, "sha256: buffer and file hashing succeed");
    CHECK(n == (int64_t)sizeof(big), "sha256_file: byte count");
    CHECK(strcmp(hex_buf, hex_file) == 0 && hex_buf[0] != '\0',
          "sha256: buffer digest equals file digest");

    /* case-insensitive comparison */
    CHECK(kfw_sha256_equal(hex_file, hex_buf) == true, "sha256_equal: identity");
    char upper[65];
    snprintf(upper, sizeof(upper), "%s", hex_file);
    for (int i = 0; i < 64; ++i)
        upper[i] = (char)toupper((unsigned char)upper[i]);
    CHECK(kfw_sha256_equal(hex_file, upper) == true, "sha256_equal: case-insensitive");
    upper[63] = (upper[63] == '0') ? '1' : '0';
    CHECK(kfw_sha256_equal(hex_file, upper) == false, "sha256_equal: detects difference");
    CHECK(kfw_sha256_equal(NULL, hex_file) == false, "sha256_equal: NULL input rejected");

    /* missing file is an error */
    CHECK(kfw_sha256_file("Z:\\nonexistent\\no.bin", hex, NULL) < 0,
          "sha256_file: missing file fails cleanly");

    unlink(path);
}

static void test_import_verify(const char *dir)
{
    char src[512], dst[512];
    static unsigned char data[300 * 1024 + 7]; /* cross the IO buffer size */
    fill_buf(data, sizeof(data));
    make_test_file(dir, "evidence_src.bin", data, sizeof(data), src, sizeof(src));
    snprintf(dst, sizeof(dst), "%s\\imported.bin", dir);

    kfw_import_result_t r;
    kfw_import_result_clear(&r);

    CHECK(kfw_integrity_import(src, dst, &r) == 0, "import: clean copy succeeds");
    CHECK(r.status == KFW_VERIFY_VERIFIED, "import: status Verified");
    CHECK(r.size_bytes == (int64_t)sizeof(data), "import: size preserved");
    CHECK(r.src_sha256[0] != '\0' && strcmp(r.src_sha256, r.dest_sha256) == 0,
          "import: source and stored digests identical");
    unlink(src);

    /* Re-verify the stored copy: must be Verified */
    char actual[65] = {0};
    CHECK(kfw_integrity_verify(dst, r.dest_sha256, actual) == KFW_VERIFY_VERIFIED,
          "verify: intact copy reports Verified");

    /* Tamper with the stored copy: must be Mismatch, and the actual digest
       must differ from the reference */
    FILE *f = fopen(dst, "r+b");
    if (f) { fseek(f, 1024, SEEK_SET); fputc(0xFF, f); fclose(f); }
    memset(actual, 0, sizeof(actual));
    CHECK(kfw_integrity_verify(dst, r.dest_sha256, actual) == KFW_VERIFY_MISMATCH,
          "verify: tampered file reports Mismatch");
    CHECK(actual[0] != '\0' && strcmp(actual, r.dest_sha256) != 0,
          "verify: tampered digest differs from reference");

    /* Reference must never be empty-string-matching */
    CHECK(kfw_integrity_verify(dst, "", actual) == KFW_VERIFY_MISMATCH,
          "verify: empty reference digest is a mismatch");

    unlink(dst);
}

static void test_db_and_audit(const char *dir)
{
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s\\kfw_unit.db", dir);
    unlink(db_path);
    snprintf(db_path, sizeof(db_path), "%s\\kfw_unit.db", dir);

    kfw_db_t *db = kfw_db_open(db_path);
    CHECK(db != NULL, "db: open creates database");
    CHECK(kfw_db_schema_version(db) == 1, "db: schema version 1");

    /* --- cases --- */
    kfw_db_case_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.title,   sizeof(c.title),   "%s", "Unit test case");
    snprintf(c.examiner,sizeof(c.examiner),"Kishan G A");
    CHECK(kfw_db_case_insert(db, &c) == 0, "db: case insert");
    CHECK(strstr(c.case_id, "KFW-") == c.case_id, "case id format");
    CHECK(strstr(c.case_id, "001") != NULL, "case id sequence is 001");

    kfw_db_case_t got;
    CHECK(kfw_db_case_get(db, c.case_id, &got) == 0, "db: case roundtrip");
    CHECK(strcmp(got.title, "Unit test case") == 0 &&
          strcmp(got.examiner, "Kishan G A") == 0 &&
          got.created_utc != 0, "db: case fields stored");

    CHECK(kfw_db_case_get(db, "KFW-2099-999", &got) == 1, "db: unknown case -> not found");

    CHECK(kfw_db_case_update(db, c.case_id, "Closed", "final notes") == 0,
          "db: case update");
    CHECK(kfw_db_case_get(db, c.case_id, &got) == 0 &&
          strcmp(got.status, "Closed") == 0 &&
          strcmp(got.notes, "final notes") == 0, "db: update stored");
    CHECK(kfw_db_case_update(db, "KFW-2099-999", "Open", "x") != 0,
          "db: update unknown case fails");

    /* audit: case.create + case.update (+ none deleted) */
    CHECK(kfw_db_audit_verify_chain(db, 0) == 0, "audit: chain intact after writes");

    /* --- evidence --- */
    kfw_db_evidence_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.case_id,       sizeof(e.case_id),       "%s", c.case_id);
    snprintf(e.orig_filename, sizeof(e.orig_filename), "%s", "camera_01.mp4");
    snprintf(e.file_type,     sizeof(e.file_type),     "%s", "MP4");
    e.size_bytes = 123456;
    CHECK(kfw_db_evidence_insert(db, &e) == 0, "db: evidence insert");
    CHECK(strstr(e.evidence_id, "EV-0001") == e.evidence_id, "evidence id format");
    CHECK(strcmp(e.verify_status, "Not checked") == 0, "evidence default status");

    kfw_db_evidence_t *e_arr = NULL; int e_n = 0;
    CHECK(kfw_db_evidence_list(db, c.case_id, &e_arr, &e_n) == 0, "db: evidence list");
    CHECK(e_n == 1, "evidence list count");
    free(e_arr);

    CHECK(kfw_db_evidence_set_verify(db, e.evidence_id, "Verified") == 0,
          "db: evidence set verify");
    kfw_db_evidence_set_verify(db, e.evidence_id, "Mismatch");

    /* --- events --- */
    kfw_db_event_t ev;
    memset(&ev, 0, sizeof(ev));
    snprintf(ev.case_id,      sizeof(ev.case_id),      "%s", c.case_id);
    snprintf(ev.evidence_id,  sizeof(ev.evidence_id),  "%s", e.evidence_id);
    snprintf(ev.kind,         sizeof(ev.kind),         "%s", "Marker");
    snprintf(ev.label,        sizeof(ev.label),        "%s", "subject enters frame");
    ev.ts_ms = 83400;
    CHECK(kfw_db_event_insert(db, &ev) == 0, "db: event insert");

    kfw_db_event_t *v_arr = NULL; int v_n = 0;
    CHECK(kfw_db_event_list(db, c.case_id, &v_arr, &v_n) == 0 && v_n == 1,
          "db: event list");
    free(v_arr);

    /* --- audit chain tamper-evidence ----------------------------------- */
    CHECK(kfw_db_audit_verify_chain(db, 0) == 0, "audit: chain intact before tamper");

    /* Tamper: rewrite one detail_json — chain must break */
    char *err = NULL;
    if (sqlite3_exec(kfw_db_raw_handle(db),
            "UPDATE audit SET detail_json='{}' WHERE seq = "
            "(SELECT MIN(seq) FROM audit)+1;", NULL, NULL, &err) != SQLITE_OK)
    {
        fprintf(stderr, "tamper exec failed: %s\n", err ? err : "");
        sqlite3_free(err);
    }
    CHECK(kfw_db_audit_verify_chain(db, 0) == 1, "audit: edited row detected");

    /* Restore consistency by rewriting from that row... not possible;
       we must rebuild: delete all later rows and re-add a consistent one —
       instead just confirm verification from a fresh DB still passes. */
    kfw_db_close(db);
    unlink(db_path);
    db = kfw_db_open(db_path);

    /* Head deletion: create rows, then drop the first one */
    memset(&c, 0, sizeof(c));
    snprintf(c.title, sizeof(c.title), "%s", "second case");
    kfw_db_case_insert(db, &c);
    kfw_db_case_update(db, c.case_id, "Open", "note");

    CHECK(kfw_db_audit_verify_chain(db, 0) == 0, "audit: fresh chain intact");

    if (sqlite3_exec(kfw_db_raw_handle(db),
            "DELETE FROM audit WHERE seq = (SELECT MIN(seq) FROM audit);",
            NULL, NULL, NULL) == SQLITE_OK)
        CHECK(kfw_db_audit_verify_chain(db, 0) == 1, "audit: deleted head detected");

    kfw_db_close(db);
    unlink(db_path);
}

int main(int argc, char **argv)
{
    if (argc >= 2)
        snprintf(g_dir, sizeof(g_dir), "%s", argv[1]);
    else
        snprintf(g_dir, sizeof(g_dir), ".");
    const char *dir = g_dir;

    /* clean pre-existing unit artifacts */
    char p[512];
    snprintf(p, sizeof(p), "%s\\kfw_unit.db", dir);   unlink(p);
    snprintf(p, sizeof(p), "%s\\kfw_unit.db-wal", dir); unlink(p);
    snprintf(p, sizeof(p), "%s\\kfw_unit.db-shm", dir); unlink(p);

    printf("KFW core unit tests — workdir '%s'\n", dir);
    test_sha256_vectors(dir);
    test_import_verify(dir);
    test_db_and_audit(dir);

    printf("\n%s\n", g_failures ? "RESULT: FAILURES PRESENT" : "RESULT: ALL PASSED");
    return g_failures ? 1 : 0;
}
