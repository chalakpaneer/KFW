/*
 * kfw_util.c — small portable helpers shared by the forensic services
 * Copyright (C) 2026 Kishan G A
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "kfw_util.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define PATH_SEP_CH '\\'
#else
# include <sys/stat.h>
# include <sys/types.h>
# define PATH_SEP_CH '/'
#endif

size_t kfw_json_escape(const char *in, char *out, size_t out_size)
{
    static const char hx[] = "0123456789abcdef";
    size_t need = 0;
    if (!in)
        in = "";

    for (const unsigned char *p = (const unsigned char *)in; *p; ++p)
    {
        char tmp[8];
        size_t n = 0;
        switch (*p)
        {
        case '"':  tmp[n++] = '\\'; tmp[n++] = '"';  break;
        case '\\': tmp[n++] = '\\'; tmp[n++] = '\\'; break;
        case '\n': tmp[n++] = '\\'; tmp[n++] = 'n';  break;
        case '\r': tmp[n++] = '\\'; tmp[n++] = 'r';  break;
        case '\t': tmp[n++] = '\\'; tmp[n++] = 't';  break;
        default:
            if (*p < 0x20)
            {
                tmp[n++] = '\\'; tmp[n++] = 'u'; tmp[n++] = '0'; tmp[n++] = '0';
                tmp[n++] = hx[*p >> 4]; tmp[n++] = hx[*p & 0x0f];
            }
            else
                tmp[n++] = (char)*p;
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (out_size > 0 && need + 1 < out_size)
                out[need] = tmp[i];
            ++need;
        }
    }
    if (out_size > 0)
        out[need < out_size ? need : out_size - 1] = '\0';
    return need;
}

void kfw_csv_field(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (const char *p = s ? s : ""; *p; ++p)
    {
        if (*p == '"')
            fputc('"', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

int kfw_path_exists(const char *path)
{
    if (!path)
        return 0;
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static int make_one_dir(const char *dir)
{
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL))
        return 0;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -EIO;
#else
    if (mkdir(dir, 0700) == 0)
        return 0;
    return errno == EEXIST ? 0 : -errno;
#endif
}

int kfw_mkdir_p(const char *path)
{
    if (!path || !*path)
        return -EINVAL;

    char buf[2048];
    if (strlen(path) >= sizeof(buf))
        return -ENAMETOOLONG;
    strcpy(buf, path);

    for (char *p = buf + 1; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            /* keep "C:\" style drive prefixes intact */
            if (p > buf && *(p - 1) == ':')
                continue;
            char c = *p;
            *p = '\0';
            int rc = make_one_dir(buf);
            *p = c;
            if (rc != 0)
                return rc;
        }
    }
    return make_one_dir(buf);
}

int kfw_path_join(char *out, size_t out_size, const char *a, const char *b)
{
    if (!out || out_size == 0 || !a || !b)
        return -EINVAL;
    size_t la = strlen(a);
    int need_sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\';
    int n = snprintf(out, out_size, "%s%s%s", a, need_sep ? (char[]){PATH_SEP_CH, 0} : "", b);
    return (n > 0 && (size_t)n < out_size) ? 0 : -ENAMETOOLONG;
}

void kfw_utc_iso(long long unix_seconds, char out[32])
{
    time_t t = (time_t)unix_seconds;
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}
