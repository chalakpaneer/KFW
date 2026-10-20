/*****************************************************************************
 * kfw_report.c: Kishan Forensic Workbench — report generation
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *****************************************************************************/

#include "kfw_report.h"
#include "../integrity/kfw_integrity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define PATH_SEP "\\"
#else
# define PATH_SEP "/"
#endif

/* ---- Helpers ------------------------------------------------------------- */

static char *kfw_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/** ISO-8601 UTC timestamp string from Unix seconds. */
static void utc_iso8601(int64_t unix_sec, char *buf, size_t n)
{
    time_t t = (time_t)unix_sec;
    struct tm *tm = gmtime(&t);
    if (tm)
        strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", tm);
    else
        snprintf(buf, n, "0000-00-00T00:00:00Z");
}

/** Human-readable file size. */
static void human_size(int64_t bytes, char *buf, size_t n)
{
    if (bytes >= (int64_t)1073741824)
        snprintf(buf, n, "%.2f GB", (double)bytes / 1073741824.0);
    else if (bytes >= (int64_t)1048576)
        snprintf(buf, n, "%.2f MB", (double)bytes / 1048576.0);
    else if (bytes >= (int64_t)1024)
        snprintf(buf, n, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, n, "%lld B", (long long)bytes);
}

/** Escape a string for embedding in JSON. */
static void json_escape(const char *in, char *out, size_t out_n)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < out_n; ++i)
    {
        unsigned char c = (unsigned char)in[i];
        if (c == '"')  { out[j++] = '\\'; out[j++] = '"';  }
        else if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n';  }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r';  }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't';  }
        else                { out[j++] = (char)c; }
    }
    out[j] = '\0';
}

/** Escape a string for embedding in HTML. */
static void html_escape(const char *in, char *out, size_t out_n)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 10 < out_n; ++i)
    {
        switch ((unsigned char)in[i])
        {
        case '&':  { const char *e = "&amp;";  while (*e && j < out_n) out[j++] = *e++; } break;
        case '<':  { const char *e = "&lt;";   while (*e && j < out_n) out[j++] = *e++; } break;
        case '>':  { const char *e = "&gt;";   while (*e && j < out_n) out[j++] = *e++; } break;
        case '"':  { const char *e = "&quot;"; while (*e && j < out_n) out[j++] = *e++; } break;
        default:   out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

void kfw_report_output_clear(kfw_report_output_t *out)
{
    if (!out) return;
    free(out->json_path);   out->json_path   = NULL;
    free(out->html_path);   out->html_path   = NULL;
    free(out->json_sha256); out->json_sha256 = NULL;
    free(out->html_sha256); out->html_sha256 = NULL;
}

/* ---- JSON manifest ------------------------------------------------------- */

static int write_json_manifest(FILE *f,
                                const kfw_report_params_t *p,
                                kfw_db_case_t             *c,
                                kfw_db_evidence_t         *evs,
                                int                        ev_count,
                                kfw_db_event_t            *evts,
                                int                        evt_count,
                                int64_t                    gen_utc)
{
    char ts[32], eb[2048];
    utc_iso8601(gen_utc, ts, sizeof(ts));

    fprintf(f, "{\n");
    fprintf(f, "  \"kfw_report_version\": 1,\n");
    fprintf(f, "  \"report_id\": \"%s\",\n", p->report_id ? p->report_id : "");
    fprintf(f, "  \"generated_utc\": \"%s\",\n", ts);
    fprintf(f, "  \"tool\": {\n");
    fprintf(f, "    \"name\": \"%s\",\n",    p->tool_name    ? p->tool_name    : "Kishan Forensic Workbench");
    fprintf(f, "    \"version\": \"%s\",\n", p->tool_version ? p->tool_version : "0.1.0-alpha");
    fprintf(f, "    \"vlc_version\": \"%s\"\n", p->vlc_version ? p->vlc_version : "");
    fprintf(f, "  },\n");
    fprintf(f, "  \"examiner\": \"%s\",\n", p->examiner ? p->examiner : "");

    /* Case */
    utc_iso8601(c->created_utc,  ts, sizeof(ts));
    char created_ts[32]; snprintf(created_ts, sizeof(created_ts), "%s", ts);
    utc_iso8601(c->modified_utc, ts, sizeof(ts));

    json_escape(c->notes, eb, sizeof(eb));
    fprintf(f, "  \"case\": {\n");
    fprintf(f, "    \"case_id\": \"%s\",\n",      c->case_id);
    fprintf(f, "    \"title\": \"%s\",\n",        c->title);
    fprintf(f, "    \"examiner\": \"%s\",\n",     c->examiner);
    fprintf(f, "    \"status\": \"%s\",\n",       c->status);
    fprintf(f, "    \"created_utc\": \"%s\",\n",  created_ts);
    fprintf(f, "    \"modified_utc\": \"%s\",\n", ts);
    fprintf(f, "    \"notes\": \"%s\"\n",         eb);
    fprintf(f, "  },\n");

    /* Evidence inventory */
    fprintf(f, "  \"evidence\": [\n");
    for (int i = 0; i < ev_count; ++i)
    {
        kfw_db_evidence_t *e = &evs[i];
        char imp_ts[32];
        utc_iso8601(e->imported_utc, imp_ts, sizeof(imp_ts));
        char ver_ts[32];
        if (e->verified_utc > 0)
            utc_iso8601(e->verified_utc, ver_ts, sizeof(ver_ts));
        else
            snprintf(ver_ts, sizeof(ver_ts), "never");
        char sz_str[32]; human_size(e->size_bytes, sz_str, sizeof(sz_str));

        json_escape(e->orig_filename, eb, sizeof(eb));
        fprintf(f, "    {\n");
        fprintf(f, "      \"evidence_id\": \"%s\",\n",    e->evidence_id);
        fprintf(f, "      \"orig_filename\": \"%s\",\n",  eb);
        fprintf(f, "      \"file_type\": \"%s\",\n",      e->file_type);
        fprintf(f, "      \"size_bytes\": %lld,\n",       (long long)e->size_bytes);
        fprintf(f, "      \"size_human\": \"%s\",\n",     sz_str);
        fprintf(f, "      \"sha256\": \"%s\",\n",         e->sha256);
        fprintf(f, "      \"provenance\": \"%s\",\n",     e->provenance);
        fprintf(f, "      \"imported_utc\": \"%s\",\n",   imp_ts);
        fprintf(f, "      \"verify_status\": \"%s\",\n",  e->verify_status);
        fprintf(f, "      \"verified_utc\": \"%s\"\n",    ver_ts);
        fprintf(f, "    }%s\n", i < ev_count - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* Timeline events */
    fprintf(f, "  \"timeline\": [\n");
    for (int i = 0; i < evt_count; ++i)
    {
        kfw_db_event_t *ev = &evts[i];
        char evt_ts[32]; utc_iso8601(ev->created_utc, evt_ts, sizeof(evt_ts));
        json_escape(ev->label, eb, sizeof(eb));
        char body_eb[4096]; json_escape(ev->body, body_eb, sizeof(body_eb));
        fprintf(f, "    {\n");
        fprintf(f, "      \"event_id\": \"%s\",\n",    ev->event_id);
        fprintf(f, "      \"ts_ms\": %lld,\n",         (long long)ev->ts_ms);
        fprintf(f, "      \"kind\": \"%s\",\n",        ev->kind);
        fprintf(f, "      \"label\": \"%s\",\n",       eb);
        fprintf(f, "      \"body\": \"%s\",\n",        body_eb);
        fprintf(f, "      \"created_utc\": \"%s\"\n",  evt_ts);
        fprintf(f, "    }%s\n", i < evt_count - 1 ? "," : "");
    }
    fprintf(f, "  ],\n");

    /* Validation limitations — mandatory per architecture.md §1.5 */
    fprintf(f, "  \"validation_limitations\": [\n");
    fprintf(f, "    \"Hash verification establishes byte-identity only. It does not establish that footage is genuine.\",\n");
    fprintf(f, "    \"Anomaly indicators are investigative leads requiring examination, not conclusions of fabrication.\",\n");
    fprintf(f, "    \"Findings must be independently validated before use in formal investigations.\"\n");
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    return 0;
}

/* ---- HTML report --------------------------------------------------------- */

static int write_html_report(FILE *f,
                              const kfw_report_params_t *p,
                              kfw_db_case_t             *c,
                              kfw_db_evidence_t         *evs,
                              int                        ev_count,
                              kfw_db_event_t            *evts,
                              int                        evt_count,
                              int64_t                    gen_utc,
                              const char                *json_path,
                              const char                *json_sha256)
{
    char ts[32];
    utc_iso8601(gen_utc, ts, sizeof(ts));
    char eb[2048];

    /* ── Head ── */
    fprintf(f,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "<meta charset=\"UTF-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        "<title>KFW Investigation Report — %s</title>\n"
        "<style>\n"
        ":root{"
        "--bg:#12161c;--panel:#1c232b;--card:#212933;--border:#2b3540;"
        "--teal:#4a83f0;--teal-dim:#2f63c6;--hi:#e5ebf2;--mid:#9aa7b5;"
        "--lo:#667689;--red:#e56b6b;--amber:#d9a13c;--green:#58c98b;"
        "}\n"
        "*{box-sizing:border-box;margin:0;padding:0;}\n"
        "body{background:var(--bg);color:var(--hi);"
        "font-family:'Segoe UI',system-ui,sans-serif;font-size:14px;line-height:1.55;}\n"
        "a{color:var(--teal);text-decoration:none;}\n"
        ".header{background:var(--panel);border-bottom:1px solid var(--border);"
        "padding:18px 32px;display:flex;align-items:center;gap:16px;}\n"
        ".logo{width:36px;height:36px;background:var(--teal-dim);"
        "border-radius:8px;display:flex;align-items:center;justify-content:center;"
        "font-size:20px;font-weight:bold;color:var(--teal);flex-shrink:0;}\n"
        ".product-name{font-size:18px;font-weight:700;color:var(--hi);}\n"
        ".product-sub{font-size:11px;color:var(--lo);letter-spacing:.7px;margin-top:2px;}\n"
        ".content{max-width:1100px;margin:0 auto;padding:32px 24px;}\n"
        "h1{font-size:22px;font-weight:700;margin-bottom:4px;}\n"
        "h2{font-size:15px;font-weight:700;color:var(--teal);letter-spacing:.5px;"
        "text-transform:uppercase;margin:28px 0 10px;border-bottom:1px solid var(--border);padding-bottom:6px;}\n"
        ".meta-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;margin-bottom:24px;}\n"
        ".meta-card{background:var(--card);border:1px solid var(--border);"
        "border-radius:10px;padding:14px 18px;}\n"
        ".meta-label{font-size:10px;font-weight:700;color:var(--lo);letter-spacing:.8px;"
        "text-transform:uppercase;margin-bottom:4px;}\n"
        ".meta-value{font-size:14px;color:var(--hi);}\n"
        "table{width:100%%;border-collapse:collapse;margin-bottom:24px;}\n"
        "th{background:var(--panel);color:var(--lo);font-size:10px;font-weight:700;"
        "letter-spacing:.8px;text-transform:uppercase;padding:8px 12px;text-align:left;"
        "border:1px solid var(--border);}\n"
        "td{padding:9px 12px;border:1px solid var(--border);vertical-align:top;}\n"
        "tr:hover td{background:rgba(74,131,240,.04);}\n"
        ".mono{font-family:'Consolas','Courier New',monospace;font-size:11px;color:var(--teal);word-break:break-all;}\n"
        ".chip{display:inline-block;padding:2px 10px;border-radius:10px;font-size:10px;"
        "font-weight:700;letter-spacing:.4px;}\n"
        ".chip-green{background:#0f2218;border:1px solid var(--green);color:var(--green);}\n"
        ".chip-amber{background:#2a2310;border:1px solid var(--amber);color:var(--amber);}\n"
        ".chip-red{background:#2a0f0f;border:1px solid var(--red);color:var(--red);}\n"
        ".chip-grey{background:var(--panel);border:1px solid var(--border);color:var(--lo);}\n"
        ".disclaimer{background:#1f1a0d;border:1px solid var(--amber);border-radius:8px;"
        "padding:14px 18px;margin:24px 0;}\n"
        ".disclaimer .d-title{color:var(--amber);font-weight:700;font-size:12px;"
        "text-transform:uppercase;letter-spacing:.7px;margin-bottom:8px;}\n"
        ".disclaimer ul{padding-left:20px;color:var(--mid);font-size:13px;}\n"
        ".disclaimer li{margin-bottom:4px;}\n"
        ".footer{margin-top:48px;padding-top:16px;border-top:1px solid var(--border);"
        "color:var(--lo);font-size:11px;display:flex;justify-content:space-between;}\n"
        "</style>\n"
        "</head>\n"
        "<body>\n",
        c->case_id);

    /* ── Header bar ── */
    fprintf(f,
        "<div class=\"header\">\n"
        "<div class=\"logo\">K</div>\n"
        "<div>\n"
        "<div class=\"product-name\">Kishan Forensic Workbench</div>\n"
        "<div class=\"product-sub\">KFW · DIGITAL MEDIA EVIDENCE EXAMINATION</div>\n"
        "</div>\n"
        "</div>\n"
        "<div class=\"content\">\n");

    /* ── Title ── */
    html_escape(c->title, eb, sizeof(eb));
    fprintf(f,
        "<h1>Investigation Report</h1>\n"
        "<p style=\"color:var(--mid);margin-bottom:24px;\">%s — %s</p>\n",
        c->case_id, eb);

    /* ── Case meta cards ── */
    char created_ts[32], modified_ts[32];
    utc_iso8601(c->created_utc,  created_ts,  sizeof(created_ts));
    utc_iso8601(c->modified_utc, modified_ts, sizeof(modified_ts));

    html_escape(c->examiner, eb, sizeof(eb));
    fprintf(f,
        "<div class=\"meta-grid\">\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Case ID</div>"
        "<div class=\"meta-value\"><span class=\"mono\">%s</span></div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Examiner</div>"
        "<div class=\"meta-value\">%s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Status</div>"
        "<div class=\"meta-value\">%s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Created</div>"
        "<div class=\"meta-value\">%s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Last modified</div>"
        "<div class=\"meta-value\">%s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Report generated</div>"
        "<div class=\"meta-value\">%s</div></div>\n"
        "</div>\n",
        c->case_id, eb, c->status,
        created_ts, modified_ts, ts);

    /* ── Evidence inventory ── */
    fprintf(f, "<h2>Evidence Inventory</h2>\n");
    fprintf(f,
        "<table>\n"
        "<thead><tr>"
        "<th>Evidence ID</th><th>Filename</th><th>Type</th>"
        "<th>Size</th><th>SHA-256</th><th>Provenance</th>"
        "<th>Imported</th><th>Verification</th>"
        "</tr></thead><tbody>\n");

    for (int i = 0; i < ev_count; ++i)
    {
        kfw_db_evidence_t *e = &evs[i];
        char imp_ts[32]; utc_iso8601(e->imported_utc, imp_ts, sizeof(imp_ts));
        char sz[32]; human_size(e->size_bytes, sz, sizeof(sz));
        html_escape(e->orig_filename, eb, sizeof(eb));

        const char *chip_class =
            strcmp(e->verify_status, "Verified") == 0 ? "chip-green" :
            strcmp(e->verify_status, "Mismatch") == 0 ? "chip-red"   : "chip-grey";

        fprintf(f,
            "<tr>\n"
            "<td><span class=\"mono\">%s</span></td>\n"
            "<td>%s</td>\n"
            "<td>%s</td>\n"
            "<td>%s</td>\n"
            "<td><span class=\"mono\" style=\"font-size:9px;\">%s</span></td>\n"
            "<td>%s</td>\n"
            "<td>%s</td>\n"
            "<td><span class=\"chip %s\">%s</span></td>\n"
            "</tr>\n",
            e->evidence_id, eb, e->file_type, sz,
            e->sha256, e->provenance, imp_ts,
            chip_class, e->verify_status);
    }
    fprintf(f, "</tbody></table>\n");

    /* ── Timeline ── */
    if (evt_count > 0)
    {
        fprintf(f, "<h2>Investigation Timeline</h2>\n");
        fprintf(f,
            "<table>\n"
            "<thead><tr>"
            "<th>Timestamp</th><th>Kind</th><th>Label</th><th>Notes</th>"
            "</tr></thead><tbody>\n");
        for (int i = 0; i < evt_count; ++i)
        {
            kfw_db_event_t *ev = &evts[i];
            char ts_str[48];
            int64_t ms = ev->ts_ms;
            snprintf(ts_str, sizeof(ts_str), "%02lld:%02lld:%02lld.%03lld",
                     (long long)(ms / 3600000),
                     (long long)((ms % 3600000) / 60000),
                     (long long)((ms % 60000) / 1000),
                     (long long)(ms % 1000));
            html_escape(ev->label, eb, sizeof(eb));
            char body_eb[4096]; html_escape(ev->body, body_eb, sizeof(body_eb));
            fprintf(f,
                "<tr>\n"
                "<td><span class=\"mono\">%s</span></td>\n"
                "<td>%s</td>\n"
                "<td>%s</td>\n"
                "<td>%s</td>\n"
                "</tr>\n",
                ts_str, ev->kind, eb, body_eb);
        }
        fprintf(f, "</tbody></table>\n");
    }

    /* ── Tool information ── */
    fprintf(f, "<h2>Tool Information</h2>\n");
    fprintf(f,
        "<div class=\"meta-grid\">\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Application</div>"
        "<div class=\"meta-value\">%s %s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">Media engine</div>"
        "<div class=\"meta-value\">libVLC %s</div></div>\n"
        "<div class=\"meta-card\"><div class=\"meta-label\">JSON manifest</div>"
        "<div class=\"meta-value\"><span class=\"mono\" style=\"font-size:10px;\">%s</span></div></div>\n"
        "</div>\n",
        p->tool_name ? p->tool_name : "KFW",
        p->tool_version ? p->tool_version : "0.1.0-alpha",
        p->vlc_version ? p->vlc_version : "",
        json_sha256 ? json_sha256 : "");

    /* ── Examiner summary ── */
    if (p->examiner_summary && p->examiner_summary[0])
    {
        html_escape(p->examiner_summary, eb, sizeof(eb));
        fprintf(f,
            "<h2>Examiner Summary</h2>\n"
            "<p style=\"color:var(--mid);\">%s</p>\n", eb);
    }

    /* ── Mandatory validation limitations ── */
    fprintf(f,
        "<div class=\"disclaimer\">\n"
        "<div class=\"d-title\">⚠ Validation Limitations</div>\n"
        "<ul>\n"
        "<li>Hash verification establishes <strong>byte-identity only</strong>. "
        "A matching hash does not establish that the footage is genuine.</li>\n"
        "<li>Anomaly indicators are <strong>investigative leads</strong> requiring "
        "further examination. They are not conclusions of fabrication or authenticity.</li>\n"
        "<li>Findings in this report must be <strong>independently validated</strong> "
        "before use in formal investigations.</li>\n"
        "</ul>\n"
        "</div>\n");

    /* ── Footer ── */
    fprintf(f,
        "<div class=\"footer\">\n"
        "<span>Kishan Forensic Workbench · Creator &amp; Lead Developer: Kishan G A · "
        "Not affiliated with or endorsed by VideoLAN</span>\n"
        "<span>%s</span>\n"
        "</div>\n"
        "</div>\n"  /* .content */
        "</body>\n"
        "</html>\n",
        ts);

    return 0;
}

/* ---- kfw_report_generate ------------------------------------------------- */

int kfw_report_generate(kfw_db_t                  *db,
                         const kfw_report_params_t *params,
                         kfw_report_output_t       *output)
{
    if (!db || !params || !params->case_id || !params->out_dir || !output)
        return -1;

    memset(output, 0, sizeof(*output));

    int64_t gen_utc = (int64_t)time(NULL);

    /* Load case */
    kfw_db_case_t casedata;
    if (kfw_db_case_get(db, params->case_id, &casedata) != 0)
        return -1;

    /* Load evidence */
    kfw_db_evidence_t *evs  = NULL; int ev_count  = 0;
    kfw_db_evidence_list(db, params->case_id, &evs,  &ev_count);

    /* Load timeline events */
    kfw_db_event_t *evts = NULL; int evt_count = 0;
    kfw_db_event_list(db, params->case_id, &evts, &evt_count);

    const char *rid = params->report_id ? params->report_id : "RPT-001";

    /* Build output file paths */
    char json_path[2048], html_path[2048];
    snprintf(json_path, sizeof(json_path), "%s%s%s_%s_report.json",
             params->out_dir, PATH_SEP, params->case_id, rid);
    snprintf(html_path, sizeof(html_path), "%s%s%s_%s_report.html",
             params->out_dir, PATH_SEP, params->case_id, rid);

    /* Write JSON */
    int rc = -1;
    {
        FILE *fj = fopen(json_path, "w");
        if (fj)
        {
            rc = write_json_manifest(fj, params, &casedata,
                                     evs, ev_count, evts, evt_count, gen_utc);
            fclose(fj);
        }
    }
    if (rc != 0)
        goto done;

    /* Compute JSON digest */
    char json_sha256[KFW_SHA256_HEX + 1] = {0};
    kfw_sha256_file(json_path, json_sha256, NULL);

    /* Write HTML */
    {
        FILE *fh = fopen(html_path, "w");
        if (fh)
        {
            rc = write_html_report(fh, params, &casedata,
                                   evs, ev_count, evts, evt_count,
                                   gen_utc, json_path, json_sha256);
            fclose(fh);
        }
        else rc = -1;
    }
    if (rc != 0)
        goto done;

    /* Compute HTML digest */
    char html_sha256[KFW_SHA256_HEX + 1] = {0};
    kfw_sha256_file(html_path, html_sha256, NULL);

    output->json_path   = kfw_strdup(json_path);
    output->html_path   = kfw_strdup(html_path);
    output->json_sha256 = kfw_strdup(json_sha256);
    output->html_sha256 = kfw_strdup(html_sha256);

    /* Audit the report generation */
    {
        char detail[512];
        snprintf(detail, sizeof(detail),
                 "{\"report_id\":\"%s\",\"json_sha256\":\"%s\","
                 "\"html_sha256\":\"%s\"}",
                 rid, json_sha256, html_sha256);
        kfw_db_audit_append(db,
                             params->examiner ? params->examiner : "system",
                             "report.generate",
                             "case", params->case_id, detail);
    }
    rc = 0;

done:
    free(evs);
    free(evts);
    return rc;
}
