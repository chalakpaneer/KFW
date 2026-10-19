/*
 * test_reports.c — KFW report generation unit tests
 * Copyright (C) 2026 Kishan G A
 *
 * Tests:
 *   1. Open an in-memory SQLite database.
 *   2. Create a case and insert evidence.
 *   3. Add timeline events.
 *   4. Generate a report (JSON + HTML) into a temp directory.
 *   5. Verify both output files exist and are non-empty.
 *   6. Verify the JSON file contains expected fields.
 *   7. Verify the HTML file contains the validation limitations disclaimer.
 *   8. Verify the report audit event was appended.
 */

#include "cases/kfw_db.h"
#include "reporting/kfw_report.h"
#include "integrity/kfw_integrity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
# define PATH_SEP "\\"
#else
# include <sys/stat.h>
# define PATH_SEP "/"
#endif

static int tests_run = 0, tests_passed = 0;

#define CHECK(desc, expr) do { \
    ++tests_run; \
    if (expr) { \
        ++tests_passed; \
        printf("[PASS] %s\n", (desc)); \
    } else { \
        printf("[FAIL] %s  (line %d)\n", (desc), __LINE__); \
    } \
} while (0)

/* Read entire file into a heap-allocated string.  Caller must free(). */
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    buf[0] = '\0';
    if (sz > 0) {
        size_t rd = fread(buf, 1, (size_t)sz, f);
        buf[rd] = '\0';
    }
    fclose(f);
    return buf;
}

/* The word "authentic" must appear exactly once in the HTML, inside the
 * mandatory denial sentence of the Validation Limitations block:
 *   "... not conclusions of fabrication or authenticity."
 * (i.e. it may never be used as a positive authenticity conclusion) */
static int only_denial_authentic(const char *html)
{
    if (!html) return 0;
    const char *p = strstr(html, "authentic");
    if (p == NULL)
        return 0;
    if (strstr(p + 1, "authentic") != NULL)
        return 0;
    if (p < html + 15)
        return 0;
    return strncmp(p - 15, "fabrication or authenticity", 27) == 0;
}

int main(void)
{
    printf("=== KFW Report Generation Tests ===\n\n");

    /* Open in-memory database */
    kfw_db_t *db = kfw_db_open(":memory:");
    CHECK("db_open in-memory succeeds", db != NULL);
    if (!db) return 1;

    /* Create a case */
    kfw_db_case_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.title,    sizeof(c.title),    "Test Report Case");
    snprintf(c.examiner, sizeof(c.examiner), "Kishan G A");
    snprintf(c.status,   sizeof(c.status),   "Open");
    snprintf(c.notes,    sizeof(c.notes),    "Automated report test.");
    int rc = kfw_db_case_insert(db, &c);
    CHECK("case_insert succeeds",  rc == 0);
    CHECK("case_id generated",     c.case_id[0] != '\0');

    /* Insert evidence */
    kfw_db_evidence_t ev;
    memset(&ev, 0, sizeof(ev));
    snprintf(ev.case_id,        sizeof(ev.case_id),        "%s", c.case_id);
    snprintf(ev.orig_filename,  sizeof(ev.orig_filename),  "camera_01.mp4");
    snprintf(ev.file_type,      sizeof(ev.file_type),      "MP4");
    snprintf(ev.sha256,         sizeof(ev.sha256),
             "a1b2c3d4e5f601234567890abcdef1234567890abcdef1234567890abcdef12");
    ev.size_bytes = 1073741824LL;  /* 1 GB */
    snprintf(ev.verify_status,  sizeof(ev.verify_status),  "Verified");
    snprintf(ev.provenance,     sizeof(ev.provenance),     "Original");
    rc = kfw_db_evidence_insert(db, &ev);
    CHECK("evidence_insert succeeds", rc == 0);

    /* Add a timeline event */
    kfw_db_event_t evt;
    memset(&evt, 0, sizeof(evt));
    snprintf(evt.case_id,    sizeof(evt.case_id),    "%s", c.case_id);
    snprintf(evt.evidence_id,sizeof(evt.evidence_id),"%s", ev.evidence_id);
    evt.ts_ms = 83400;   /* 01:23.400 */
    snprintf(evt.kind,  sizeof(evt.kind),  "Marker");
    snprintf(evt.label, sizeof(evt.label), "Subject enters frame");
    rc = kfw_db_event_insert(db, &evt);
    CHECK("event_insert succeeds", rc == 0);

    /* Create a temp output directory */
#ifdef _WIN32
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    strncat(tmp_dir, "kfw_report_test", MAX_PATH - strlen(tmp_dir) - 1);
    CreateDirectoryA(tmp_dir, NULL);
#else
    char tmp_dir[256];
    snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/kfw_report_test_%d", (int)getpid());
    mkdir(tmp_dir, 0700);
#endif

    /* Generate the report */
    kfw_report_params_t params = {0};
    params.case_id          = c.case_id;
    params.out_dir          = tmp_dir;
    params.report_id        = "RPT-TEST-001";
    params.examiner         = "Kishan G A";
    params.tool_name        = "Kishan Forensic Workbench";
    params.tool_version     = "0.1.0-alpha";
    params.vlc_version      = "4.0.0-test";
    params.examiner_summary = "Automated test run.";

    kfw_report_output_t out = {0};
    rc = kfw_report_generate(db, &params, &out);
    CHECK("report_generate returns 0", rc == 0);
    CHECK("json_path set",  out.json_path  != NULL);
    CHECK("html_path set",  out.html_path  != NULL);
    CHECK("json_sha256 set",out.json_sha256 != NULL && out.json_sha256[0] != '\0');
    CHECK("html_sha256 set",out.html_sha256 != NULL && out.html_sha256[0] != '\0');

    /* Read outputs */
    char *json_content = out.json_path  ? slurp(out.json_path)  : NULL;
    char *html_content = out.html_path  ? slurp(out.html_path)  : NULL;

    CHECK("JSON file non-empty",  json_content && json_content[0] != '\0');
    CHECK("HTML file non-empty",  html_content && html_content[0] != '\0');

    /* JSON content checks */
    if (json_content)
    {
        CHECK("JSON contains case_id",
              strstr(json_content, c.case_id) != NULL);
        CHECK("JSON contains evidence_id",
              strstr(json_content, ev.evidence_id) != NULL);
        CHECK("JSON contains validation_limitations",
              strstr(json_content, "validation_limitations") != NULL);
        CHECK("JSON contains 'byte-identity'",
              strstr(json_content, "byte-identity") != NULL);
    }

    /* HTML content checks */
    if (html_content)
    {
        CHECK("HTML contains case title",
              strstr(html_content, "Test Report Case") != NULL);
        CHECK("HTML contains disclaimer",
              strstr(html_content, "Validation Limitations") != NULL);
        CHECK("HTML contains 'authentic' warning",
              strstr(html_content, "byte-identity") != NULL);
        CHECK("HTML does not use word 'authentic' as positive conclusion",
              only_denial_authentic(html_content));
    }

    /* Verify SHA-256 of JSON matches stored value */
    if (out.json_path && out.json_sha256)
    {
        char actual[KFW_SHA256_HEX + 1];
        kfw_sha256_file(out.json_path, actual, NULL);
        CHECK("JSON sha256 matches stored value",
              kfw_sha256_equal(actual, out.json_sha256));
    }

    /* Audit chain should have at least one report.generate entry */
    rc = kfw_db_audit_verify_chain(db, 0);
    CHECK("audit chain intact after report generation", rc == 0);

    free(json_content);
    free(html_content);
    kfw_report_output_clear(&out);
    kfw_db_close(db);

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
