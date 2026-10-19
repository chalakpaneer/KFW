/*****************************************************************************
 * kfw_media_probe.c: Kishan Forensic Workbench — media adapter CLI probe
 *****************************************************************************
 * Copyright (C) 2026 Kishan G A
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * Usage:
 *   kfw_media_probe <media-path> [frame-time-ms] [out.png]
 *
 * Prints a JSON record describing the media and, when a frame time is given,
 * the provenance of the exported frame. Exits non-zero on failure.
 *****************************************************************************/

#include "kfw_media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_puts(const char *s)
{
    putchar('"');
    for (; s != NULL && *s != '\0'; ++s)
    {
        switch (*s)
        {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if ((unsigned char)*s < 0x20)
                    printf("\\u%04x", (unsigned char)*s);
                else
                    putchar(*s);
        }
    }
    putchar('"');
}

static const char *track_type_name(int t)
{
    switch (t)
    {
        case 0: return "audio";
        case 1: return "video";
        case 2: return "text";
        default: return "unknown";
    }
}

static void fourcc_str(uint32_t cc, char out[5])
{
    out[0] = (char)((cc >> 0) & 0xff);
    out[1] = (char)((cc >> 8) & 0xff);
    out[2] = (char)((cc >> 16) & 0xff);
    out[3] = (char)((cc >> 24) & 0xff);
    out[4] = '\0';
    for (int i = 0; i < 4; ++i)
        if (out[i] < 0x20 || out[i] > 0x7e)
            out[i] = '?';
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <media-path> [frame-time-ms] [out.png]\n",
                argv[0]);
        return 2;
    }

    const char *path = argv[1];
    long long frame_ms = (argc >= 3) ? strtoll(argv[2], NULL, 10) : -1;
    const char *out_png = (argc >= 4) ? argv[3] : "frame.png";

    printf("{\n");
    printf("  \"probe\": \"kfw_media_probe\",\n");
    printf("  \"engine_version\": ");
    json_puts(kfw_media_engine_version());
    printf(",\n");
    printf("  \"source_path\": ");
    json_puts(path);
    printf(",\n");

    kfw_media_adapter_t *a = kfw_media_open(path);
    if (a == NULL)
    {
        printf("  \"ok\": false,\n  \"error\": \"kfw_media_open failed\"\n}\n");
        return 1;
    }

    int prc = kfw_media_parse(a, 30000000);
    if (prc != 0)
    {
        printf("  \"ok\": false,\n  \"error\": \"kfw_media_parse failed\"\n}\n");
        kfw_media_close(a);
        return 1;
    }

    const kfw_media_info_t *info = kfw_media_get_info(a);
    printf("  \"ok\": true,\n");
    printf("  \"parsed\": %s,\n", info->is_parsed ? "true" : "false");
    printf("  \"duration_us\": %lld,\n", (long long)info->duration_us);
    printf("  \"media_type\": %d,\n", (int)info->type);
    printf("  \"mrl\": ");
    json_puts(info->mrl);
    printf(",\n");

    printf("  \"tracks\": [");
    for (size_t i = 0; i < info->n_tracks; ++i)
    {
        const kfw_track_t *t = &info->tracks[i];
        char cc[5], occ[5];
        fourcc_str(t->codec, cc);
        fourcc_str(t->original_fourcc, occ);

        printf("%s\n    {", i ? "," : "");
        printf(" \"type\": ");
        json_puts(track_type_name(t->track_type));
        printf(", \"codec\": ");
        json_puts(cc);
        printf(", \"original_fourcc\": ");
        json_puts(occ);
        printf(", \"bitrate\": %u", t->bitrate);
        printf(", \"language\": ");
        json_puts(t->language);
        printf(", \"description\": ");
        json_puts(t->description);
        if (t->track_type == 1)
        {
            printf(", \"width\": %u, \"height\": %u", t->width, t->height);
            printf(", \"sar_num\": %u, \"sar_den\": %u", t->sar_num, t->sar_den);
            printf(", \"fps_num\": %u, \"fps_den\": %u",
                   t->frame_rate_num, t->frame_rate_den);
        }
        else if (t->track_type == 0)
        {
            printf(", \"channels\": %u, \"sample_rate\": %u",
                   t->channels, t->sample_rate);
        }
        printf(" }");
    }
    printf("\n  ]");

    int rc = 0;
    if (frame_ms >= 0)
    {
        kfw_frame_meta_t meta;
        memset(&meta, 0, sizeof(meta));

        int erc = kfw_media_export_frame(a, (int64_t)frame_ms * 1000,
                                         out_png, &meta);
        printf(",\n  \"frame\": {");
        if (erc == 0)
        {
            printf(" \"ok\": true");
            printf(", \"requested_time_us\": %lld",
                   (long long)meta.requested_time_us);
            printf(", \"actual_time_us\": %lld",
                   (long long)meta.actual_time_us);
            printf(", \"delta_us\": %lld",
                   (long long)(meta.actual_time_us - meta.requested_time_us));
            printf(", \"width\": %u, \"height\": %u", meta.width, meta.height);
            printf(", \"export_path\": ");
            json_puts(meta.export_path);
            printf(", \"tool_version\": ");
            json_puts(meta.tool_version);
        }
        else
        {
            printf(" \"ok\": false, \"error\": \"export failed (%d)\"", erc);
            rc = 1;
        }
        printf(" }");
        kfw_frame_meta_clear(&meta);
    }

    printf("\n}\n");

    kfw_media_close(a);
    return rc;
}
