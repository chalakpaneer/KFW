/*
 * kfw_util.h — small portable helpers shared by the forensic services
 * Copyright (C) 2026 Kishan G A
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * No Qt, no libVLC, no SQLite: plain C so every module (and the test-suite)
 * can use it.
 */
#ifndef KFW_UTIL_H
#define KFW_UTIL_H 1

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Escape @p in for use inside a JSON string literal (quotes, backslashes,
 * control characters).  UTF-8 bytes >= 0x80 pass through unchanged.
 * Always NUL-terminates when out_size > 0.
 * @return length the full escaped string needs (excluding NUL); if the return
 *         value >= out_size the output was truncated.
 */
size_t kfw_json_escape(const char *in, char *out, size_t out_size);

/** Write @p s as an RFC 4180 CSV field (always quoted) to @p fp. */
void kfw_csv_field(FILE *fp, const char *s);

/** Create @p path and every missing parent directory.  0 on success. */
int kfw_mkdir_p(const char *path);

/** 1 if @p path exists (file or directory), else 0. */
int kfw_path_exists(const char *path);

/** Join @p a and @p b with the platform separator.  0 on success. */
int kfw_path_join(char *out, size_t out_size, const char *a, const char *b);

/** Current UTC time as "YYYY-MM-DDTHH:MM:SSZ". */
void kfw_utc_iso(long long unix_seconds, char out[32]);

#ifdef __cplusplus
}
#endif

#endif /* KFW_UTIL_H */
