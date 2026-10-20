/*
 * test_v2.c — KFW 0.2 forensic-services tests
 * Copyright (C) 2026 Kishan G A
 *
 * Covers: schema migration from a real v1 file, case particulars, duplicate
 * detection, tags, search, chain-of-custody query, audit-JSON escaping, and
 * handover-package create / verify / tamper detection.
 */
#include "cases/kfw_db.h"
#include "evidence/kfw_evidence.h"
#include "export/kfw_package.h"
#include "integrity/kfw_integrity.h"
#include "util/kfw_util.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int run = 0, ok = 0;
#define CHECK(desc, expr) do { ++run; if (expr) { ++ok; printf("[PASS] %s\n", desc); } \
    else printf("[FAIL] %s  (line %d)\n", desc, __LINE__); } while (0)

static void write_file(const char *path, const char *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(data, 1, n, f); fclose(f); }
}

static int audit_has(kfw_db_audit_row_t *r, int n, const char *action)
{
    for (int i = 0; i < n; ++i)
        if (strcmp(r[i].action, action) == 0) return 1;
    return 0;
}

static int audit_count(kfw_db_audit_row_t *r, int n, const char *action)
{
    int c = 0;
    for (int i = 0; i < n; ++i)
        if (strcmp(r[i].action, action) == 0) ++c;
    return c;
}

int main(int argc, char **argv)
{
    const char *work = "/tmp/kfw_v2_test";
    const char *v1db = argc > 1 ? argv[1] : NULL;     /* optional pre-built v1 db */
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", work, work);
    if (system(cmd) != 0) return 2;

    /* ---- util ---------------------------------------------------------- */
    {
        char esc[64];
        kfw_json_escape("C:\\Users\\x\"y\n", esc, sizeof esc);
        CHECK("json_escape: backslash, quote, newline",
              strcmp(esc, "C:\\\\Users\\\\x\\\"y\\n") == 0);
        char tiny[4];
        size_t need = kfw_json_escape("abcdefgh", tiny, sizeof tiny);
        CHECK("json_escape: truncation reported and NUL-terminated",
              need == 8 && tiny[3] == '\0');
        char ctl[16];
        kfw_json_escape("a\x01" "b", ctl, sizeof ctl);
        CHECK("json_escape: control char as \\u0001", strcmp(ctl, "a\\u0001b") == 0);
    }

    /* ---- migration from a genuine v1 file ----------------------------- */
    if (v1db)
    {
        kfw_db_t *old = kfw_db_open(v1db);
        CHECK("v1 db opens with v2 code", old != NULL);
        if (old)
        {
            CHECK("schema upgraded to 2", kfw_db_schema_version(old) == 2);
            kfw_db_case_t *cases = NULL; int n = 0;
            kfw_db_case_list(old, &cases, &n);
            CHECK("legacy case still readable", n == 1 && strcmp(cases[0].title, "Legacy case") == 0);
            CHECK("new case fields default to empty", n == 1 && cases[0].case_number[0] == '\0');
            kfw_db_evidence_t *ev = NULL; int ne = 0;
            kfw_db_evidence_list(old, cases[0].case_id, &ev, &ne);
            CHECK("legacy evidence readable, duplicate_of empty", ne == 1 && ev[0].duplicate_of[0] == '\0');
            kfw_db_audit_row_t *au = NULL; int na = 0;
            kfw_db_audit_list_for_case(old, cases[0].case_id, &au, &na);
            kfw_db_audit_row_t *all = NULL; int nall = 0;
            /* db.migrate targets the database, not the case: check the chain instead */
            CHECK("audit chain intact after migration", kfw_db_audit_verify_chain(old, 0) == 0);
            sqlite3 *raw = kfw_db_raw_handle(old); (void)raw; (void)all; (void)nall;
            CHECK("legacy audit rows still listed for case", audit_has(au, na, "case.create"));
            free(au); free(ev); free(cases);
            kfw_db_close(old);
            old = kfw_db_open(v1db);
            CHECK("re-opening upgraded db is idempotent", old && kfw_db_schema_version(old) == 2
                  && kfw_db_audit_verify_chain(old, 0) == 0);
            if (old) kfw_db_close(old);
        }
    }

    /* ---- fresh database ------------------------------------------------ */
    char dbpath[256], store[256], pkgs[256];
    snprintf(dbpath, sizeof dbpath, "%s/case.db", work);
    snprintf(store, sizeof store, "%s/store", work);
    snprintf(pkgs, sizeof pkgs, "%s/packages", work);
    kfw_db_t *db = kfw_db_open(dbpath);
    CHECK("fresh db opens at schema 2", db && kfw_db_schema_version(db) == 2);
    if (!db) return 1;

    kfw_db_case_t c; memset(&c, 0, sizeof c);
    snprintf(c.title, sizeof c.title, "Warehouse theft");
    snprintf(c.examiner, sizeof c.examiner, "K. Examiner");
    CHECK("case insert", kfw_db_case_insert(db, &c) == 0);

    kfw_db_case_t c2; memset(&c2, 0, sizeof c2);
    snprintf(c2.title, sizeof c2.title, "Second case");
    snprintf(c2.examiner, sizeof c2.examiner, "K. Examiner");
    CHECK("second case insert", kfw_db_case_insert(db, &c2) == 0);

    /* case particulars */
    {
        kfw_db_case_t m; memset(&m, 0, sizeof m);
        snprintf(m.case_number, sizeof m.case_number, "FIR/2026/0412");
        snprintf(m.agency, sizeof m.agency, "Cyber Crime \"Unit\"");
        snprintf(m.requester, sizeof m.requester, "Insp. R. Rao");
        snprintf(m.incident_date, sizeof m.incident_date, "2026-08-07");
        snprintf(m.location, sizeof m.location, "Bengaluru");
        snprintf(m.description, sizeof m.description, "Loading bay CCTV, night of 7 Aug");
        CHECK("case meta update", kfw_db_case_update_meta(db, c.case_id, &m, "K. Examiner") == 0);
        kfw_db_case_t g;
        CHECK("case meta read back", kfw_db_case_get(db, c.case_id, &g) == 0 &&
              strcmp(g.case_number, "FIR/2026/0412") == 0 &&
              strcmp(g.agency, "Cyber Crime \"Unit\"") == 0 &&
              strcmp(g.location, "Bengaluru") == 0);
        CHECK("case meta on unknown case fails", kfw_db_case_update_meta(db, "NOPE", &m, "x") != 0);
    }

    /* ---- evidence + duplicates ----------------------------------------- */
    char src1[256], src2[256], src3[512];
    snprintf(src1, sizeof src1, "%s/cam1.mp4", work);
    snprintf(src2, sizeof src2, "%s/cam1_copy.mp4", work);
    snprintf(src3, sizeof src3, "%s/odd \"name\"\\x.mp4", work);
    char payload[100000];
    for (size_t i = 0; i < sizeof payload; ++i) payload[i] = (char)((i * 31 + 7) & 0xff);
    write_file(src1, payload, sizeof payload);
    write_file(src2, payload, sizeof payload);          /* identical bytes */
    payload[500] ^= 0x55;
    write_file(src3, payload, sizeof payload);          /* different bytes */

    kfw_evidence_ctx_t ctx = { db, store, "K. Examiner" };
    kfw_db_evidence_t e1, e2, e3, e4;
    CHECK("import first file", kfw_evidence_import(&ctx, c.case_id, src1, "Original", &e1) == 0);
    CHECK("first file is not a duplicate", e1.duplicate_of[0] == '\0');
    CHECK("source mtime captured", e1.fs_modified_utc > 0);
    CHECK("import identical file, same case", kfw_evidence_import(&ctx, c.case_id, src2, "Original", &e2) == 0);
    CHECK("same-case duplicate links to first", strcmp(e2.duplicate_of, e1.evidence_id) == 0);
    CHECK("import identical file, other case", kfw_evidence_import(&ctx, c2.case_id, src1, "Original", &e4) == 0);
    CHECK("cross-case duplicate is linked", e4.duplicate_of[0] != '\0');
    CHECK("import odd-named file (quote + backslash)", kfw_evidence_import(&ctx, c.case_id, src3, "Original", &e3) == 0);
    CHECK("different bytes not flagged", e3.duplicate_of[0] == '\0');
    {
        kfw_db_evidence_t *rows = NULL; int n = 0;
        CHECK("find_by_sha256 returns all copies", kfw_db_evidence_find_by_sha256(db, e1.sha256, &rows, &n) == 0 && n == 3);
        free(rows);
        kfw_db_evidence_t g;
        CHECK("duplicate_of persisted", kfw_db_evidence_get(db, e2.evidence_id, &g) == 0 && strcmp(g.duplicate_of, e1.evidence_id) == 0);
    }

    /* ---- events, notes, tags, search ----------------------------------- */
    kfw_db_event_t ev; memset(&ev, 0, sizeof ev);
    snprintf(ev.case_id, sizeof ev.case_id, "%s", c.case_id);
    snprintf(ev.evidence_id, sizeof ev.evidence_id, "%s", e1.evidence_id);
    ev.ts_ms = 61000;
    snprintf(ev.kind, sizeof ev.kind, "Marker");
    snprintf(ev.label, sizeof ev.label, "Door opens");
    snprintf(ev.body, sizeof ev.body, "Person in red jacket enters at 100%% speed");
    CHECK("event insert", kfw_db_event_insert(db, &ev) == 0);
    {
        char sql[512];
        snprintf(sql, sizeof sql,
            "INSERT INTO notes(note_id,case_id,evidence_id,author,created_utc,body)"
            " VALUES('N1','%s','%s','K. Examiner',1,'Timestamp overlay looks 3 minutes fast')",
            c.case_id, e1.evidence_id);
        sqlite3_exec(kfw_db_raw_handle(db), sql, NULL, NULL, NULL);
    }

    CHECK("tag apply (evidence)", kfw_db_tag_apply(db, "K. Examiner", "evidence", e1.evidence_id, "  Key Footage ") == 0);
    CHECK("tag apply (event)", kfw_db_tag_apply(db, "K. Examiner", "event", ev.event_id, "suspect") == 0);
    CHECK("tag apply is idempotent / case-insensitive",
          kfw_db_tag_apply(db, "K. Examiner", "evidence", e1.evidence_id, "key footage") == 0);
    CHECK("tag apply rejects bad item type", kfw_db_tag_apply(db, "x", "case", "c", "t") != 0);
    CHECK("tag apply rejects empty name", kfw_db_tag_apply(db, "x", "evidence", e1.evidence_id, "   ") != 0);
    {
        kfw_db_tag_t *t = NULL; int n = 0;
        CHECK("tag_list_for", kfw_db_tag_list_for(db, "evidence", e1.evidence_id, &t, &n) == 0 && n == 1 && strcmp(t[0].name, "Key Footage") == 0);
        free(t);
        CHECK("tag_list_all counts uses", kfw_db_tag_list_all(db, &t, &n) == 0 && n == 2);
        free(t);
    }

    {
        kfw_db_hit_t *h = NULL; int n = 0;
        CHECK("search by filename", kfw_db_search(db, c.case_id, "cam1.MP4", &h, &n) == 0 && n >= 1);
        free(h);
        CHECK("search event label", kfw_db_search(db, c.case_id, "door opens", &h, &n) == 0 && n == 1 && strcmp(h[0].kind, "event") == 0 && h[0].ts_ms == 61000);
        free(h);
        CHECK("search event body", kfw_db_search(db, c.case_id, "red jacket", &h, &n) == 0 && n == 1);
        free(h);
        CHECK("search note body", kfw_db_search(db, c.case_id, "3 minutes fast", &h, &n) == 0 && n == 1 && strcmp(h[0].kind, "note") == 0);
        free(h);
        CHECK("search by tag finds evidence", kfw_db_search(db, c.case_id, "key footage", &h, &n) == 0 && n == 1 && strcmp(h[0].kind, "tag") == 0);
        free(h);
        CHECK("search by tag finds event", kfw_db_search(db, c.case_id, "suspect", &h, &n) == 0 && n == 1 && h[0].ts_ms == 61000);
        free(h);
        CHECK("search is scoped to the case", kfw_db_search(db, c2.case_id, "door opens", &h, &n) == 0 && n == 0);
        free(h);
        CHECK("search: '%' is literal, not a wildcard", kfw_db_search(db, c.case_id, "100%", &h, &n) == 0 && n == 1);
        free(h);
        CHECK("search: '_' is literal", kfw_db_search(db, c.case_id, "_", &h, &n) == 0 && n >= 1); /* cam1_copy.mp4 */
        free(h);
        CHECK("search: lone '%' matches only literal percent", kfw_db_search(db, c.case_id, "%", &h, &n) == 0 && n == 1);
        free(h);
        CHECK("search: empty query returns nothing", kfw_db_search(db, c.case_id, "   ", &h, &n) == 0 && n == 0);
        free(h);
        CHECK("search: quote characters are safe", kfw_db_search(db, c.case_id, "' OR 1=1 --", &h, &n) == 0 && n == 0);
        free(h);
    }
    CHECK("tag remove", kfw_db_tag_remove(db, "K. Examiner", "evidence", e1.evidence_id, "KEY FOOTAGE") == 0);
    {
        kfw_db_tag_t *t = NULL; int n = 0;
        kfw_db_tag_list_for(db, "evidence", e1.evidence_id, &t, &n);
        CHECK("tag really removed", n == 0);
        free(t);
    }

    /* ---- custody query + audit-JSON escaping ---------------------------- */
    {
        kfw_db_audit_row_t *a = NULL; int n = 0;
        CHECK("custody list", kfw_db_audit_list_for_case(db, c.case_id, &a, &n) == 0 && n > 0);
        CHECK("custody has case.create / evidence.import / case.meta / tag.apply / evidence.duplicate",
              audit_has(a, n, "case.create") && audit_has(a, n, "evidence.import") &&
              audit_has(a, n, "case.meta") && audit_has(a, n, "tag.apply") &&
              audit_has(a, n, "evidence.duplicate"));
        CHECK("tag.apply audited once despite repeat", audit_count(a, n, "tag.apply") == 2); /* evidence + event */
        int seq_ok = 1;
        for (int i = 1; i < n; ++i) if (a[i].seq <= a[i-1].seq) seq_ok = 0;
        CHECK("custody ordered by seq", seq_ok);
        int esc_ok = 0;
        for (int i = 0; i < n; ++i)
            if (strcmp(a[i].action, "evidence.import") == 0 && strstr(a[i].detail_json, "odd \\\"name\\\"\\\\x.mp4"))
                esc_ok = 1;
        CHECK("import audit JSON escapes quotes and backslashes", esc_ok);
        free(a);
        CHECK("audit chain intact", kfw_db_audit_verify_chain(db, 0) == 0);
    }

    /* ---- handover package ---------------------------------------------- */
    kfw_package_params_t pp; memset(&pp, 0, sizeof pp);
    pp.db = db; pp.case_id = c.case_id; pp.examiner = "K. Examiner";
    pp.out_dir = pkgs; pp.tool_name = "Kishan Forensic Workbench";
    pp.tool_version = "0.2.0-test"; pp.include_evidence = 1;
    kfw_package_result_t pr;
    int prc = kfw_package_create(&pp, &pr);
    CHECK("package create (clean)", prc == 0);
    CHECK("package copied all 3 evidence files", pr.n_evidence == 3 && pr.n_evidence_copied == 3);
    CHECK("package has a report", pr.report_ok == 1);
    CHECK("package lists files and has manifest digest", pr.n_files >= 9 && strlen(pr.manifest_sha256) == 64);

    kfw_package_verify_result_t vr;
    CHECK("verify: pristine package passes", kfw_package_verify(pr.package_dir, &vr) == 0 &&
          vr.manifest_digest == 1 && vr.n_ok == vr.n_files && vr.n_files == pr.n_files);
    CHECK("verify reports same manifest digest", strcmp(vr.manifest_sha256, pr.manifest_sha256) == 0);

    /* tamper 1: flip a byte in an evidence copy */
    char victim[2600];
    snprintf(victim, sizeof victim, "%s/evidence/%s_cam1.mp4", pr.package_dir, e1.evidence_id);
    {
        FILE *f = fopen(victim, "r+b");
        if (f) { fseek(f, 1000, SEEK_SET); fputc(0xAA, f); fclose(f); }
    }
    CHECK("verify: modified evidence copy detected", kfw_package_verify(pr.package_dir, &vr) == 1 &&
          vr.n_mismatch == 1 && strstr(vr.first_problem, "mismatch") != NULL);

    /* tamper 2: delete a file */
    remove(victim);
    CHECK("verify: missing file detected", kfw_package_verify(pr.package_dir, &vr) == 1 && vr.n_missing == 1);

    /* tamper 3: extra file */
    {
        char extra[2600];
        snprintf(extra, sizeof extra, "%s/evidence/planted.mp4", pr.package_dir);
        write_file(extra, "x", 1);
        CHECK("verify: unlisted file detected", kfw_package_verify(pr.package_dir, &vr) == 1 && vr.n_extra == 1);
    }

    /* tamper 4: edit manifest */
    {
        char mp[2600];
        snprintf(mp, sizeof mp, "%s/manifest.json", pr.package_dir);
        FILE *f = fopen(mp, "ab"); if (f) { fputs(" ", f); fclose(f); }
        CHECK("verify: edited manifest detected", kfw_package_verify(pr.package_dir, &vr) == 1 && vr.manifest_digest == 0);
    }
    CHECK("verify: non-package folder is an error", kfw_package_verify(work, &vr) < 0);

    /* a package built after the stored evidence file is damaged flags it */
    {
        FILE *f = fopen(e3.stored_path, "r+b");
        if (f) { fseek(f, 10, SEEK_SET); fputc(0x00, f); fputc(0x01, f); fclose(f); }
        kfw_package_result_t pr2;
        sleep(1);                                  /* distinct folder stamp */
        int rc2 = kfw_package_create(&pp, &pr2);
        CHECK("package flags evidence that no longer matches its record", rc2 == 1 && pr2.n_evidence_mismatch == 1);
        CHECK("package with damaged evidence still verifies structurally",
              kfw_package_verify(pr2.package_dir, &vr) == 0);
    }
    {
        pp.include_evidence = 0;
        kfw_package_result_t pr3;
        sleep(1);
        CHECK("metadata-only package", kfw_package_create(&pp, &pr3) == 0 && pr3.n_evidence_copied == 0 &&
              kfw_package_verify(pr3.package_dir, &vr) == 0);
    }
    {
        kfw_db_audit_row_t *a = NULL; int n = 0;
        kfw_db_audit_list_for_case(db, c.case_id, &a, &n);
        CHECK("package.create audited", audit_has(a, n, "package.create"));
        CHECK("report.generate audited via package", audit_has(a, n, "report.generate"));
        free(a);
        CHECK("audit chain intact at end", kfw_db_audit_verify_chain(db, 0) == 0);
    }

    kfw_db_close(db);
    printf("\n%d / %d tests passed\n", ok, run);
    return ok == run ? 0 : 1;
}
