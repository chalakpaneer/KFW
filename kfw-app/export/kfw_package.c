/*
 * kfw_package.c — evidence handover packages (create + verify)
 * Copyright (C) 2026 Kishan G A
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "kfw_package.h"

#include "../integrity/kfw_integrity.h"
#include "../reporting/kfw_report.h"
#include "../util/kfw_util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#else
# include <dirent.h>
# include <sys/stat.h>
#endif

/* ---- manifest entry list ---------------------------------------------------- */

typedef struct pkg_entry
{
    char    path[1024];      /* relative, '/' separated */
    int64_t size;
    char    sha256[KFW_SHA256_HEX + 1];
} pkg_entry_t;

typedef struct pkg_list
{
    pkg_entry_t *v;
    int          n, cap;
} pkg_list_t;

static int list_add(pkg_list_t *l, const char *rel, int64_t size, const char *sha)
{
    if (l->n >= l->cap)
    {
        int nc = l->cap ? l->cap * 2 : 32;
        pkg_entry_t *g = (pkg_entry_t *)realloc(l->v, (size_t)nc * sizeof(*g));
        if (!g)
            return -ENOMEM;
        l->v = g;
        l->cap = nc;
    }
    pkg_entry_t *e = &l->v[l->n++];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", rel);
    e->size = size;
    snprintf(e->sha256, sizeof(e->sha256), "%s", sha);
    return 0;
}

/* Hash a file inside the package and add it to the manifest list. */
static int list_add_file(pkg_list_t *l, const char *pkg_dir, const char *rel)
{
    char full[2400], sha[KFW_SHA256_HEX + 1];
    int64_t bytes = 0;
    if (kfw_path_join(full, sizeof(full), pkg_dir, rel) != 0)
        return -ENAMETOOLONG;
    int rc = kfw_sha256_file(full, sha, &bytes);
    if (rc != 0)
        return rc;
    return list_add(l, rel, bytes, sha);
}

/* Make a filename safe on Windows and POSIX. */
static void sanitize_name(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 1 < out_size; ++p)
    {
        unsigned char c = *p;
        if (c < 0x20 || c == '"' || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
            c = '_';
        out[n++] = (char)c;
    }
    out[n] = '\0';
    if (n == 0)
        snprintf(out, out_size, "unnamed");
}

/* ---- small writers ------------------------------------------------------------ */

static FILE *open_in_pkg(const char *pkg_dir, const char *rel)
{
    char full[2400];
    if (kfw_path_join(full, sizeof(full), pkg_dir, rel) != 0)
        return NULL;
    return fopen(full, "wb");
}

static void put_json_str(FILE *fp, const char *s)
{
    char esc[8192];
    kfw_json_escape(s ? s : "", esc, sizeof(esc));
    fputs(esc, fp);
}

static int write_case_json(const char *dir, const kfw_db_case_t *c,
                           const char *tool, const char *ver)
{
    FILE *fp = open_in_pkg(dir, "case.json");
    if (!fp)
        return -EIO;
    char t1[32], t2[32];
    kfw_utc_iso(c->created_utc, t1);
    kfw_utc_iso(c->modified_utc, t2);
    fputs("{\n", fp);
#define KV(k, v, last) do { fprintf(fp, "  \"%s\": \"", k); put_json_str(fp, v); fputs(last ? "\"\n" : "\",\n", fp); } while (0)
    KV("case_id", c->case_id, 0);
    KV("title", c->title, 0);
    KV("case_number", c->case_number, 0);
    KV("agency", c->agency, 0);
    KV("requester", c->requester, 0);
    KV("incident_date", c->incident_date, 0);
    KV("location", c->location, 0);
    KV("description", c->description, 0);
    KV("examiner", c->examiner, 0);
    KV("status", c->status, 0);
    KV("created_utc", t1, 0);
    KV("modified_utc", t2, 0);
    {
        char tv[256];
        snprintf(tv, sizeof(tv), "%s %s", tool ? tool : "KFW", ver ? ver : "");
        KV("generated_by", tv, 1);
    }
#undef KV
    fputs("}\n", fp);
    return fclose(fp) == 0 ? 0 : -EIO;
}

static int write_inventory(const char *dir, const kfw_db_evidence_t *ev, int n,
                           const char *const *state)
{
    FILE *fp = open_in_pkg(dir, "evidence_inventory.csv");
    if (!fp)
        return -EIO;
    fputs("evidence_id,original_filename,file_type,size_bytes,sha256,"
          "verify_status,imported_utc,source_mtime_utc,provenance,"
          "duplicate_of,package_state\n", fp);
    for (int i = 0; i < n; ++i)
    {
        char t1[32], t2[32];
        kfw_utc_iso(ev[i].imported_utc, t1);
        if (ev[i].fs_modified_utc > 0)
            kfw_utc_iso(ev[i].fs_modified_utc, t2);
        else
            t2[0] = '\0';
        kfw_csv_field(fp, ev[i].evidence_id);   fputc(',', fp);
        kfw_csv_field(fp, ev[i].orig_filename); fputc(',', fp);
        kfw_csv_field(fp, ev[i].file_type);     fputc(',', fp);
        fprintf(fp, "%lld,", (long long)ev[i].size_bytes);
        kfw_csv_field(fp, ev[i].sha256);        fputc(',', fp);
        kfw_csv_field(fp, ev[i].verify_status); fputc(',', fp);
        kfw_csv_field(fp, t1);                  fputc(',', fp);
        kfw_csv_field(fp, t2);                  fputc(',', fp);
        kfw_csv_field(fp, ev[i].provenance);    fputc(',', fp);
        kfw_csv_field(fp, ev[i].duplicate_of);  fputc(',', fp);
        kfw_csv_field(fp, state[i]);            fputc('\n', fp);
    }
    return fclose(fp) == 0 ? 0 : -EIO;
}

static int write_custody(const char *dir, const kfw_db_audit_row_t *r, int n)
{
    FILE *fp = open_in_pkg(dir, "chain_of_custody.csv");
    if (!fp)
        return -EIO;
    fputs("seq,utc,actor,action,target_type,target_id,detail_json,entry_hash\n", fp);
    for (int i = 0; i < n; ++i)
    {
        char t[32];
        kfw_utc_iso(r[i].utc, t);
        fprintf(fp, "%lld,", (long long)r[i].seq);
        kfw_csv_field(fp, t);              fputc(',', fp);
        kfw_csv_field(fp, r[i].actor);     fputc(',', fp);
        kfw_csv_field(fp, r[i].action);    fputc(',', fp);
        kfw_csv_field(fp, r[i].target_type); fputc(',', fp);
        kfw_csv_field(fp, r[i].target_id); fputc(',', fp);
        kfw_csv_field(fp, r[i].detail_json); fputc(',', fp);
        kfw_csv_field(fp, r[i].entry_hash); fputc('\n', fp);
    }
    if (fclose(fp) != 0)
        return -EIO;

    fp = open_in_pkg(dir, "chain_of_custody.json");
    if (!fp)
        return -EIO;
    fputs("[\n", fp);
    for (int i = 0; i < n; ++i)
    {
        char t[32];
        kfw_utc_iso(r[i].utc, t);
        fprintf(fp, "  {\"seq\":%lld,\"utc\":\"%s\",\"actor\":\"", (long long)r[i].seq, t);
        put_json_str(fp, r[i].actor);
        fputs("\",\"action\":\"", fp);       put_json_str(fp, r[i].action);
        fputs("\",\"target_type\":\"", fp);  put_json_str(fp, r[i].target_type);
        fputs("\",\"target_id\":\"", fp);    put_json_str(fp, r[i].target_id);
        /* detail_json is itself JSON written by KFW; embed it as a string so a
         * malformed legacy entry can never break this file. */
        fputs("\",\"detail\":\"", fp);       put_json_str(fp, r[i].detail_json);
        fprintf(fp, "\",\"prev_hash\":\"%s\",\"entry_hash\":\"%s\"}%s\n",
                r[i].prev_hash, r[i].entry_hash, i + 1 < n ? "," : "");
    }
    fputs("]\n", fp);
    return fclose(fp) == 0 ? 0 : -EIO;
}

static int write_timeline(const char *dir, const kfw_db_event_t *ev, int n)
{
    FILE *fp = open_in_pkg(dir, "timeline.csv");
    if (!fp)
        return -EIO;
    fputs("evidence_id,ts_ms,kind,label,body,created_utc\n", fp);
    for (int i = 0; i < n; ++i)
    {
        char t[32];
        kfw_utc_iso(ev[i].created_utc, t);
        kfw_csv_field(fp, ev[i].evidence_id); fputc(',', fp);
        fprintf(fp, "%lld,", (long long)ev[i].ts_ms);
        kfw_csv_field(fp, ev[i].kind);        fputc(',', fp);
        kfw_csv_field(fp, ev[i].label);       fputc(',', fp);
        kfw_csv_field(fp, ev[i].body);        fputc(',', fp);
        kfw_csv_field(fp, t);                 fputc('\n', fp);
    }
    return fclose(fp) == 0 ? 0 : -EIO;
}

static int write_readme(const char *dir, const kfw_db_case_t *c)
{
    FILE *fp = open_in_pkg(dir, "README.txt");
    if (!fp)
        return -EIO;
    fprintf(fp,
        "KFW evidence handover package\n"
        "Case: %s - %s\n\n"
        "HOW TO VERIFY\n"
        "  Open this folder in KFW (Reports > Verify package), or recompute the\n"
        "  SHA-256 of every file listed in manifest.json and compare.\n"
        "  manifest.sha256 holds the SHA-256 of manifest.json itself.\n\n"
        "WHAT THIS PACKAGE PROVES\n"
        "  That the files you hold are byte-for-byte the files listed here, and\n"
        "  that the chain-of-custody records are those exported from the case\n"
        "  database at packaging time.\n\n"
        "WHAT IT DOES NOT PROVE\n"
        "  - That the footage is genuine or unedited. A matching hash shows the\n"
        "    bytes did not change after import, nothing about how they were made.\n"
        "  - That the package itself is untampered: anyone who can rewrite both\n"
        "    manifest.json and manifest.sha256 can produce a consistent forgery.\n"
        "    Send the manifest SHA-256 to the recipient by a separate channel.\n"
        "  - Indicators in the report are leads for examination, not findings.\n",
        c->case_id, c->title);
    return fclose(fp) == 0 ? 0 : -EIO;
}

static int write_manifest(const char *dir, const kfw_db_case_t *c,
                          const char *tool, const char *ver,
                          const char *examiner, const pkg_list_t *l,
                          char sha_out[KFW_SHA256_HEX + 1])
{
    FILE *fp = open_in_pkg(dir, "manifest.json");
    if (!fp)
        return -EIO;
    char now[32];
    kfw_utc_iso((long long)time(NULL), now);
    fputs("{\n  \"format\": \"kfw-handover-1\",\n  \"case_id\": \"", fp);
    put_json_str(fp, c->case_id);
    fprintf(fp, "\",\n  \"created_utc\": \"%s\",\n  \"generated_by\": \"", now);
    {
        char tv[256];
        snprintf(tv, sizeof(tv), "%s %s", tool ? tool : "KFW", ver ? ver : "");
        put_json_str(fp, tv);
    }
    fputs("\",\n  \"examiner\": \"", fp);
    put_json_str(fp, examiner ? examiner : "");
    fprintf(fp, "\",\n  \"file_count\": %d,\n  \"files\": [\n", l->n);
    for (int i = 0; i < l->n; ++i)
    {
        fputs("    {\"path\":\"", fp);
        put_json_str(fp, l->v[i].path);
        fprintf(fp, "\",\"size\":%lld,\"sha256\":\"%s\"}%s\n",
                (long long)l->v[i].size, l->v[i].sha256,
                i + 1 < l->n ? "," : "");
    }
    fputs("  ]\n}\n", fp);
    if (fclose(fp) != 0)
        return -EIO;

    char full[2400];
    kfw_path_join(full, sizeof(full), dir, "manifest.json");
    int rc = kfw_sha256_file(full, sha_out, NULL);
    if (rc != 0)
        return rc;

    fp = open_in_pkg(dir, "manifest.sha256");
    if (!fp)
        return -EIO;
    fprintf(fp, "%s  manifest.json\n", sha_out);
    return fclose(fp) == 0 ? 0 : -EIO;
}

/* ---- create -------------------------------------------------------------------- */

int kfw_package_create(const kfw_package_params_t *p, kfw_package_result_t *out)
{
    if (!p || !p->db || !p->case_id || !p->out_dir || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));

    kfw_db_case_t cs;
    if (kfw_db_case_get(p->db, p->case_id, &cs) != 0)
        return -ENOENT;

    /* Package folder:  <out_dir>/<case>_handover_YYYYMMDD_HHMMSS */
    char stamp[32];
    {
        time_t t = time(NULL);
        struct tm tmv;
#ifdef _WIN32
        gmtime_s(&tmv, &t);
#else
        gmtime_r(&t, &tmv);
#endif
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    }
    char leaf[200], safe_case[64];
    sanitize_name(p->case_id, safe_case, sizeof(safe_case));
    snprintf(leaf, sizeof(leaf), "%s_handover_%s", safe_case, stamp);

    char dir[2048];
    if (kfw_path_join(dir, sizeof(dir), p->out_dir, leaf) != 0)
        return -ENAMETOOLONG;
    char sub[2200];
    int rc = kfw_mkdir_p(dir);
    if (rc != 0) return rc;
    kfw_path_join(sub, sizeof(sub), dir, "evidence");
    if ((rc = kfw_mkdir_p(sub)) != 0) return rc;
    kfw_path_join(sub, sizeof(sub), dir, "reports");
    if ((rc = kfw_mkdir_p(sub)) != 0) return rc;
    snprintf(out->package_dir, sizeof(out->package_dir), "%s", dir);

    pkg_list_t list = { NULL, 0, 0 };
    kfw_db_evidence_t *ev = NULL;
    int n_ev = 0;
    kfw_db_event_t *events = NULL;
    int n_events = 0;
    kfw_db_audit_row_t *audit = NULL;
    int n_audit = 0;
    const char **state = NULL;
    int result = 0;

    kfw_db_evidence_list(p->db, p->case_id, &ev, &n_ev);
    kfw_db_event_list(p->db, p->case_id, &events, &n_events);
    out->n_evidence = n_ev;

    state = (const char **)calloc((size_t)(n_ev > 0 ? n_ev : 1), sizeof(char *));
    if (!state) { rc = -ENOMEM; goto done; }

    /* 1. evidence files */
    for (int i = 0; i < n_ev; ++i)
    {
        if (!p->include_evidence)
        {
            state[i] = "not_included";
            continue;
        }
        char safe[512], rel[1024], full[2400];
        sanitize_name(ev[i].orig_filename, safe, sizeof(safe));
        snprintf(rel, sizeof(rel), "evidence/%s_%s", ev[i].evidence_id, safe);
        kfw_path_join(full, sizeof(full), dir, rel);

        kfw_import_result_t r;
        kfw_import_result_clear(&r);
        int crc = ev[i].stored_path[0]
                    ? kfw_integrity_import(ev[i].stored_path, full, &r)
                    : -ENOENT;
        if (crc != 0 && crc != KFW_ERR_DIGEST_MISMATCH)
        {
            state[i] = "MISSING";
            out->n_evidence_missing++;
            continue;
        }
        if (crc == KFW_ERR_DIGEST_MISMATCH)
        {
            state[i] = "COPY_ERROR";
            out->n_evidence_mismatch++;
            continue;
        }
        if (ev[i].sha256[0] && !kfw_sha256_equal(r.src_sha256, ev[i].sha256))
        {
            /* The stored copy no longer matches the digest recorded at import. */
            state[i] = "HASH_MISMATCH_WITH_RECORD";
            out->n_evidence_mismatch++;
        }
        else
            state[i] = "ok";
        list_add(&list, rel, r.size_bytes, r.dest_sha256);
        out->n_evidence_copied++;
    }

    /* 2. fresh report (audited, so it also appears in the custody export) */
    {
        char rdir[2200];
        kfw_path_join(rdir, sizeof(rdir), dir, "reports");
        char rid[64];
        snprintf(rid, sizeof(rid), "RPT-PKG-%s", stamp);
        kfw_report_params_t rp;
        memset(&rp, 0, sizeof(rp));
        rp.case_id = p->case_id;
        rp.out_dir = rdir;
        rp.report_id = rid;
        rp.examiner = p->examiner ? p->examiner : cs.examiner;
        rp.tool_name = p->tool_name ? p->tool_name : "Kishan Forensic Workbench";
        rp.tool_version = p->tool_version ? p->tool_version : "";
        rp.vlc_version = "";
        rp.examiner_summary = "";
        kfw_report_output_t ro;
        memset(&ro, 0, sizeof(ro));
        if (kfw_report_generate(p->db, &rp, &ro) == 0 && ro.json_path && ro.html_path)
        {
            const char *jb = strrchr(ro.json_path, '/');
            const char *jb2 = strrchr(ro.json_path, '\\');
            if (jb2 > jb) jb = jb2;
            const char *hb = strrchr(ro.html_path, '/');
            const char *hb2 = strrchr(ro.html_path, '\\');
            if (hb2 > hb) hb = hb2;
            char rel[1024];
            snprintf(rel, sizeof(rel), "reports/%s", jb ? jb + 1 : ro.json_path);
            list_add_file(&list, dir, rel);
            snprintf(rel, sizeof(rel), "reports/%s", hb ? hb + 1 : ro.html_path);
            list_add_file(&list, dir, rel);
            out->report_ok = 1;
        }
        kfw_report_output_clear(&ro);
    }

    /* 3. case records */
    kfw_db_audit_list_for_case(p->db, p->case_id, &audit, &n_audit);
    if ((rc = write_case_json(dir, &cs, p->tool_name, p->tool_version)) != 0) goto done;
    if ((rc = write_inventory(dir, ev, n_ev, state)) != 0) goto done;
    if ((rc = write_custody(dir, audit, n_audit)) != 0) goto done;
    if ((rc = write_timeline(dir, events, n_events)) != 0) goto done;
    if ((rc = write_readme(dir, &cs)) != 0) goto done;
    {
        static const char *const names[] = {
            "case.json", "evidence_inventory.csv", "chain_of_custody.csv",
            "chain_of_custody.json", "timeline.csv", "README.txt" };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if ((rc = list_add_file(&list, dir, names[i])) != 0)
                goto done;
    }

    /* 4. manifest (sorted for a stable, diff-friendly file) */
    for (int i = 1; i < list.n; ++i)
        for (int j = i; j > 0 && strcmp(list.v[j - 1].path, list.v[j].path) > 0; --j)
        {
            pkg_entry_t tmp = list.v[j];
            list.v[j] = list.v[j - 1];
            list.v[j - 1] = tmp;
        }
    if ((rc = write_manifest(dir, &cs, p->tool_name, p->tool_version,
                             p->examiner ? p->examiner : cs.examiner,
                             &list, out->manifest_sha256)) != 0)
        goto done;

    out->n_files = list.n;
    for (int i = 0; i < list.n; ++i)
        out->total_bytes += list.v[i].size;

    /* 5. audit the packaging itself (recorded after sealing; documented) */
    {
        char d[8], esc[4200], detail[4700];
        (void)d;
        kfw_json_escape(dir, esc, sizeof(esc));
        snprintf(detail, sizeof(detail),
                 "{\"package\":\"%s\",\"manifest_sha256\":\"%s\",\"files\":%d}",
                 esc, out->manifest_sha256, out->n_files);
        kfw_db_audit_append(p->db, p->examiner ? p->examiner : "system",
                            "package.create", "case", p->case_id, detail);
    }

    result = (out->n_evidence_missing || out->n_evidence_mismatch || !out->report_ok) ? 1 : 0;

done:
    free(list.v);
    free(ev);
    free(events);
    free(audit);
    free(state);
    return rc != 0 ? rc : result;
}

/* ---- verify -------------------------------------------------------------------- */

typedef struct walk_ctx
{
    pkg_list_t found;     /* relative paths (size/sha unused) */
} walk_ctx_t;

static void walk_dir(const char *root, const char *rel_prefix, walk_ctx_t *w)
{
    char full[2400];
    if (rel_prefix[0])
        kfw_path_join(full, sizeof(full), root, rel_prefix);
    else
        snprintf(full, sizeof(full), "%s", root);

#ifdef _WIN32
    char pat[2500];
    snprintf(pat, sizeof(pat), "%s\\*", full);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        char rel[1024];
        snprintf(rel, sizeof(rel), "%s%s%s", rel_prefix,
                 rel_prefix[0] ? "/" : "", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            walk_dir(root, rel, w);
        else
            list_add(&w->found, rel, 0, "");
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(full);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        char rel[1024], child[2600];
        snprintf(rel, sizeof(rel), "%s%s%s", rel_prefix,
                 rel_prefix[0] ? "/" : "", de->d_name);
        snprintf(child, sizeof(child), "%s/%s", full, de->d_name);
        struct stat st;
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            walk_dir(root, rel, w);
        else
            list_add(&w->found, rel, 0, "");
    }
    closedir(d);
#endif
}

static void set_problem(kfw_package_verify_result_t *r, const char *fmt, const char *a)
{
    if (r->first_problem[0] == '\0')
        snprintf(r->first_problem, sizeof(r->first_problem), fmt, a);
}

/* Extract the string value following  "key":"  on a manifest line. */
static int json_line_str(const char *line, const char *key, char *out, size_t out_size)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(line, needle);
    if (!p)
        return 0;
    p += strlen(needle);
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_size)
    {
        if (*p == '\\' && p[1])
            ++p;                       /* paths KFW writes need only \\ and \" */
        out[n++] = *p++;
    }
    out[n] = '\0';
    return 1;
}

int kfw_package_verify(const char *package_dir, kfw_package_verify_result_t *out)
{
    if (!package_dir || !out)
        return -EINVAL;
    memset(out, 0, sizeof(*out));
    out->manifest_digest = -1;

    char mpath[2400];
    kfw_path_join(mpath, sizeof(mpath), package_dir, "manifest.json");
    FILE *fp = fopen(mpath, "rb");
    if (!fp)
        return -ENOENT;

    /* manifest.json against manifest.sha256 */
    if (kfw_sha256_file(mpath, out->manifest_sha256, NULL) != 0)
    {
        fclose(fp);
        return -EIO;
    }
    {
        char spath[2400];
        kfw_path_join(spath, sizeof(spath), package_dir, "manifest.sha256");
        FILE *sf = fopen(spath, "rb");
        if (sf)
        {
            char line[256] = {0};
            if (fgets(line, sizeof(line), sf))
            {
                char want[KFW_SHA256_HEX + 1] = {0};
                strncpy(want, line, KFW_SHA256_HEX);
                out->manifest_digest = kfw_sha256_equal(want, out->manifest_sha256) ? 1 : 0;
            }
            else
                out->manifest_digest = 0;
            fclose(sf);
        }
        if (out->manifest_digest == 0)
            set_problem(out, "manifest.json does not match manifest.sha256%s", "");
    }

    /* Check every listed file. */
    pkg_list_t listed = { NULL, 0, 0 };
    char line[4096];
    while (fgets(line, sizeof(line), fp))
    {
        char rel[1024], sha[KFW_SHA256_HEX + 8];
        if (!json_line_str(line, "path", rel, sizeof(rel)) ||
            !json_line_str(line, "sha256", sha, sizeof(sha)))
            continue;
        list_add(&listed, rel, 0, sha);
        out->n_files++;

        char full[2400], actual[KFW_SHA256_HEX + 1];
        kfw_path_join(full, sizeof(full), package_dir, rel);
        if (!kfw_path_exists(full))
        {
            out->n_missing++;
            set_problem(out, "missing file: %s", rel);
        }
        else if (kfw_sha256_file(full, actual, NULL) != 0 ||
                 !kfw_sha256_equal(actual, sha))
        {
            out->n_mismatch++;
            set_problem(out, "SHA-256 mismatch: %s", rel);
        }
        else
            out->n_ok++;
    }
    fclose(fp);

    /* Files present but not listed (manifest files themselves excluded). */
    walk_ctx_t w = { { NULL, 0, 0 } };
    walk_dir(package_dir, "", &w);
    for (int i = 0; i < w.found.n; ++i)
    {
        const char *f = w.found.v[i].path;
        if (!strcmp(f, "manifest.json") || !strcmp(f, "manifest.sha256"))
            continue;
        int known = 0;
        for (int j = 0; j < listed.n && !known; ++j)
            known = strcmp(listed.v[j].path, f) == 0;
        if (!known)
        {
            out->n_extra++;
            set_problem(out, "file not in manifest: %s", f);
        }
    }
    free(w.found.v);
    free(listed.v);

    return (out->n_mismatch || out->n_missing || out->n_extra ||
            out->manifest_digest == 0 || out->n_files == 0) ? 1 : 0;
}
