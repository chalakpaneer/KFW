/*****************************************************************************
 * kfw_media.c: Kishan Forensic Workbench ???????? media adapter (libVLC backend)
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
 * KFW is an independent project and is not affiliated with or endorsed by
 * VideoLAN.
 *
 * Target engine: libVLC 3.x (stable, LGPL-v2+ engine shippped with KFW).
 * Parsing and track discovery use libvlc_media_parse_with_options() plus
 * libvlc_media_tracks_get().  Frame export is handled by the Qt multimedia
 * exporter in the UI layer (kfw_shell.cpp), NOT by the engine ???????? the VLC 3.x
 * thumbnailer does not exist; see docs/architecture.md ????3.1.
 *****************************************************************************/

#include "kfw_media.h"

#include <vlc/vlc.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# define kfw_sleep_ms(ms)  Sleep((DWORD)(ms))
#else
# include <unistd.h>
# define kfw_sleep_ms(ms)  usleep((useconds_t)(ms) * 1000)
#endif

/** kfw_media_export_frame() always returns this under libVLC 3.x. */
#define KFW_ERR_NOT_SUPPORTED   (-100)

/* Optional engine diagnostics for runtime QA (env-gated):
 *   KFW_VLC_VERBOSE  -> raise libVLC verbosity to debug
 *   KFW_VLC_LOG      -> append engine messages to this file */
static void kfw_vlc_log_setup(libvlc_instance_t *inst)
{
    const char *logPath = getenv("KFW_VLC_LOG");
    if (logPath != NULL && logPath[0] != '\0')
    {
        FILE *f = fopen(logPath, "a");
        if (f != NULL)
            libvlc_log_set_file(inst, f);
        /* Intentionally left open: libVLC keeps the FILE* for the instance
         * lifetime, closed by libvlc on release. */
    }
}

#ifdef _WIN32
/* libvlc_media_new_path() rejects POSIX-style Windows paths ("/"-separated
 * drive paths) in the 3.0.23 Windows build ??? hand it native separators. */
static void kfw_native_path(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; in[n] != '\0' && n + 1 < cap; ++n)
        out[n] = (in[n] == '/') ? '\\' : in[n];
    out[n] = '\0';
}
#else
static void kfw_native_path(const char *in, char *out, size_t cap)
{
    snprintf(out, cap, "%s", in);
}
#endif

/* ---- Shared libVLC instances -------------------------------------------------
 * Creating and releasing a libVLC instance on every evidence switch is slow
 * and, on libVLC 3.x for Windows, racy: the probe worker thread called
 * libvlc_new() while the GUI thread was inside libvlc_release(), which crashed
 * the process when the examiner switched between evidence items.  Instances
 * are now created once (lazily, under a lock) and live until
 * kfw_media_shutdown().  Probing (headless, no audio) and live playback need
 * different options, so each has its own long-lived instance. */
static atomic_flag g_vlc_lock = ATOMIC_FLAG_INIT;
static libvlc_instance_t *g_probe_inst  = NULL;
static libvlc_instance_t *g_player_inst = NULL;

static void kfw_vlc_lock(void)
{
    while (atomic_flag_test_and_set(&g_vlc_lock))
        kfw_sleep_ms(1);
}

static void kfw_vlc_unlock(void)
{
    atomic_flag_clear(&g_vlc_lock);
}

static libvlc_instance_t *kfw_vlc_shared(libvlc_instance_t **slot,
                                         int argc, const char *const *argv)
{
    kfw_vlc_lock();
    if (*slot == NULL)
    {
        *slot = libvlc_new(argc, argv);
        if (*slot != NULL)
            kfw_vlc_log_setup(*slot);
    }
    libvlc_instance_t *inst = *slot;
    kfw_vlc_unlock();
    return inst;
}

void kfw_media_shutdown(void)
{
    kfw_vlc_lock();
    if (g_player_inst != NULL)
    {
        libvlc_release(g_player_inst);
        g_player_inst = NULL;
    }
    if (g_probe_inst != NULL)
    {
        libvlc_release(g_probe_inst);
        g_probe_inst = NULL;
    }
    kfw_vlc_unlock();
}

static libvlc_media_t *kfw_media_from_path(libvlc_instance_t *inst,
                                           const char *path)
{
    char native[4096];
    kfw_native_path(path, native, sizeof(native));
    return libvlc_media_new_path(inst, native);
}

struct kfw_media_adapter
{
    libvlc_instance_t *inst;
    libvlc_media_t    *media;
    kfw_media_info_t   info;
};

static char *kfw_strdup(const char *s)
{
    if (s == NULL)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d != NULL)
        memcpy(d, s, n);
    return d;
}

void kfw_media_info_clear(kfw_media_info_t *info)
{
    if (info == NULL)
        return;
    for (size_t i = 0; i < info->n_tracks; ++i)
    {
        free(info->tracks[i].language);
        free(info->tracks[i].description);
    }
    free(info->tracks);
    free(info->mrl);
    memset(info, 0, sizeof(*info));
    info->duration_us = -1;
}

void kfw_frame_meta_clear(kfw_frame_meta_t *meta)
{
    if (meta == NULL)
        return;
    free(meta->evidence_id);
    free(meta->source_sha256);
    free(meta->export_path);
    free(meta->tool_version);
    memset(meta, 0, sizeof(*meta));
}

const char *kfw_media_engine_version(void)
{
    return libvlc_get_version();
}

/* ---- Track extraction ----------------------------------------------------- */

static bool fill_track(kfw_track_t *kt, const libvlc_media_track_t *src)
{
    memset(kt, 0, sizeof(*kt));
    kt->track_type      = (int)src->i_type;
    kt->codec           = src->i_codec;
    kt->original_fourcc = src->i_original_fourcc;
    kt->bitrate         = src->i_bitrate;
    kt->language        = kfw_strdup(src->psz_language);
    kt->description     = kfw_strdup(src->psz_description);

    if (src->i_type == libvlc_track_video && src->video != NULL)
    {
        kt->width          = src->video->i_width;
        kt->height         = src->video->i_height;
        kt->sar_num        = src->video->i_sar_num;
        kt->sar_den        = src->video->i_sar_den;
        kt->frame_rate_num = src->video->i_frame_rate_num;
        kt->frame_rate_den = src->video->i_frame_rate_den;
    }
    else if (src->i_type == libvlc_track_audio && src->audio != NULL)
    {
        kt->channels    = src->audio->i_channels;
        kt->sample_rate = src->audio->i_rate;
    }
    return true;
}

static int build_info(kfw_media_adapter_t *a)
{
    kfw_media_info_clear(&a->info);

    /* Engine-reported URI: the returned string is allocated by the libVLC
     * engine, whose DLL links msvcrt — KFW is UCRT, and freeing across CRT
     * heaps corrupts ours. Copy into adapter-owned storage and leave the
     * engine's small allocation in place (documented leak, bounded by the
     * number of probes per process). */
    char *mrl = libvlc_media_get_mrl(a->media);
    if (mrl != NULL)
    {
        a->info.mrl = kfw_strdup(mrl);
        /* Do NOT free(mrl) here — see the comment above. */
    }

    enum libvlc_media_type_t t = libvlc_media_get_type(a->media);
    a->info.type = (kfw_media_type_t)t;

    int64_t dur_ms = (int64_t)libvlc_media_get_duration(a->media);
    a->info.duration_us = dur_ms > 0 ? dur_ms * 1000 : -1;

    libvlc_media_parsed_status_t ps = libvlc_media_get_parsed_status(a->media);
    a->info.is_parsed = (ps == libvlc_media_parsed_status_done);

    libvlc_media_track_t **tracks = NULL;
    unsigned n = libvlc_media_tracks_get(a->media, &tracks);
    if (n == 0 || tracks == NULL)
        return 0;   /* parsed but no tracks ???????? degenerate media, not fatal */

    kfw_track_t *acc = (kfw_track_t *)calloc(n, sizeof(kfw_track_t));
    if (acc == NULL)
        return -1;

    size_t used = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        if (tracks[i] == NULL)
            continue;
        fill_track(&acc[used], tracks[i]);
        used++;
    }
    libvlc_media_tracks_release(tracks, n);
    tracks = NULL;

    a->info.tracks   = acc;
    a->info.n_tracks = used;
    return 0;
}

/* ---- Lifecycle ------------------------------------------------------------ */

kfw_media_adapter_t *kfw_media_open(const char *path)
{
    if (path == NULL)
        return NULL;
    if (getenv("KFW_MEDIA_DEBUG"))
        fprintf(stderr, "kfw_media_open: path=%s\n", path);

    /* libVLC 3.x creates media objects for nonexistent paths without
     * complaint; reject missing files up-front so callers can rely on
     * open() failing fast. */
    FILE *probe = fopen(path, "rb");
    if (probe == NULL)
    {
        fprintf(stderr, "kfw_media_open: fopen failed errno=%d\n", errno);
        return NULL;
    }
    fclose(probe);

    kfw_media_adapter_t *a =
        (kfw_media_adapter_t *)calloc(1, sizeof(*a));
    if (a == NULL)
        return NULL;
    a->info.duration_us = -1;

    /* Headless, no embedded video window, no audio outputs. */
    const char *argv[] = { "--no-audio", "--vout=dummy", "--network-caching=0" };
    a->inst = kfw_vlc_shared(&g_probe_inst,
                             (int)(sizeof(argv) / sizeof(argv[0])), argv);
    if (a->inst == NULL)
    {
        fprintf(stderr, "kfw_media_open: libvlc_new failed\n");
        free(a);
        return NULL;
    }

    a->media = kfw_media_from_path(a->inst, path);
    if (a->media == NULL)
    {
        fprintf(stderr, "kfw_media_open: media_new_path failed\n");
        free(a);
        return NULL;
    }
    if (getenv("KFW_MEDIA_DEBUG"))
        fprintf(stderr, "kfw_media_open: ok\n");

    return a;
}

void kfw_media_close(kfw_media_adapter_t *a)
{
    if (a == NULL)
        return;
    if (a->media != NULL)
        libvlc_media_release(a->media);
    /* a->inst is the shared probe instance - released in kfw_media_shutdown(). */
    kfw_media_info_clear(&a->info);
    free(a);
}

/* ---- Parsing --------------------------------------------------------------- */

int kfw_media_parse(kfw_media_adapter_t *a, int64_t timeout_us)
{
    if (a == NULL || a->media == NULL)
        return -1;

    /* libVLC 3.x parse is asynchronous: request, then poll the status. */
    int timeout_ms = timeout_us > 0 ? (int)(timeout_us / 1000) : 10000;
    if (timeout_ms <= 0)
        timeout_ms = 10000;

    int rc = libvlc_media_parse_with_options(
        a->media,
        (libvlc_media_parse_flag_t)(libvlc_media_parse_local
                                    | libvlc_media_fetch_local),
        timeout_ms);
    if (rc != 0)
        return -1;

    const int poll_margin_ms = 500;   /* allow VLC's own timeout to fire first */
    int elapsed = 0;
    for (;;)
    {
        libvlc_media_parsed_status_t ps =
            libvlc_media_get_parsed_status(a->media);

        if (ps == libvlc_media_parsed_status_done)
            break;
        if (ps == libvlc_media_parsed_status_failed)
            return -1;
        if (ps == libvlc_media_parsed_status_timeout)
            return -1;

        if (elapsed >= timeout_ms + poll_margin_ms)
            return -1;   /* did not finish in time */

        kfw_sleep_ms(10);
        elapsed += 10;
    }

    return build_info(a);
}

const kfw_media_info_t *kfw_media_get_info(const kfw_media_adapter_t *a)
{
    return a == NULL ? NULL : &a->info;
}

int kfw_media_export_frame(kfw_media_adapter_t *a, int64_t time_us,
                           const char *out_path, kfw_frame_meta_t *meta)
{
    if (meta != NULL)
        memset(meta, 0, sizeof(*meta));
    if (a == NULL || out_path == NULL)
        return -1;

    /* libVLC 3.x has no headless thumbnailer; frame capture is performed by
       the application layer (live VLC frame tap in kfw_shell.cpp), which
       keeps the workbench sovereign from the engine version. */
    errno = ENOSYS;
    return KFW_ERR_NOT_SUPPORTED;
}

/* ---- Live playback engine --------------------------------------------------- */

struct kfw_player
{
    libvlc_instance_t      *inst;
    libvlc_media_t         *media;
    libvlc_media_player_t  *mp;

    kfw_player_frame_fn     frame_fn;
    void                   *frame_user;

    /* Double ring allocated by the format callback. lock()/display() run on
     * the engine's video output thread, so ring state touches one thread. */
    uint8_t                *buf[2];
    size_t                  buf_size;
    unsigned                cur;
    unsigned                w, h;
    size_t                  pitch;
};

static unsigned kfw_vmem_format(void **opaque, char *chroma, unsigned *width,
                                unsigned *height, unsigned *pitches,
                                unsigned *lines)
{
    kfw_player_t *p = (kfw_player_t *)(*opaque);
    if (p == NULL || *width == 0 || *height == 0)
        return 0;

    memcpy(chroma, "RV32", 4);          /* packed BGRA, exactly 4 chars */
    *pitches = (*width * 4u + 31u) & ~31u;   /* 32-byte aligned scanlines */
    *lines   = *height;

    const size_t need = (size_t)*pitches * (size_t)*lines;
    for (unsigned i = 0; i < 2; ++i)
    {
        if (p->buf[i] != NULL && p->buf_size >= need)
            continue;   /* reuse when the previous allocation still fits */
        free(p->buf[i]);
        p->buf[i] = (uint8_t *)malloc(need);
        if (p->buf[i] == NULL)
        {
            p->buf_size = 0;
            return 0;
        }
        memset(p->buf[i], 0, need);
    }
    p->buf_size = need;
    p->w = *width;
    p->h = *height;
    p->pitch = *pitches;
    return 1;   /* one buffer per frame, managed by the ring */
}

static void kfw_vmem_cleanup(void *opaque)
{
    kfw_player_t *p = (kfw_player_t *)opaque;
    for (unsigned i = 0; i < 2; ++i)
    {
        free(p->buf[i]);
        p->buf[i] = NULL;
    }
    p->buf_size = 0;
    p->w = 0;
    p->h = 0;
    p->pitch = 0;
}

static void *kfw_vmem_lock(void *opaque, void **planes)
{
    kfw_player_t *p = (kfw_player_t *)opaque;
    p->cur = (p->cur + 1u) % 2u;
    planes[0] = p->buf[p->cur];
    return planes[0];
}

static void kfw_vmem_unlock(void *opaque, void *picture, void *const *planes)
{
    (void)opaque;
    (void)picture;
    (void)planes;
}

static void kfw_vmem_display(void *opaque, void *picture)
{
    kfw_player_t *p = (kfw_player_t *)opaque;
    if (p->frame_fn == NULL || p->w == 0 || p->h == 0 || p->pitch == 0)
        return;
    /* ts_ms is the engine's input clock ???????? approximate, good for indicators
     * and provenance co-recording (VLC 3.x vmem carries no per-frame pts). */
    const int64_t ts_ms = (int64_t)libvlc_media_player_get_time(p->mp);
    p->frame_fn(p->frame_user, ts_ms, p->w, p->h,
                (const uint8_t *)picture, p->pitch, (size_t)p->h);
}

static kfw_player_state_t map_state(libvlc_state_t s)
{
    switch (s)
    {
        case libvlc_Opening:
        case libvlc_Buffering:  return KFW_PLAYER_OPENING;
        case libvlc_Playing:    return KFW_PLAYER_PLAYING;
        case libvlc_Paused:     return KFW_PLAYER_PAUSED;
        case libvlc_Stopped:    return KFW_PLAYER_STOPPED;
        case libvlc_Ended:      return KFW_PLAYER_ENDED;
        case libvlc_Error:      return KFW_PLAYER_ERROR;
        default:                return KFW_PLAYER_IDLE;
    }
}

kfw_player_t *kfw_player_create(const char *path,
                                kfw_player_frame_fn cb, void *user)
{
    if (path == NULL || cb == NULL)
        return NULL;

    /* Same fast-fail probe as kfw_media_open(): libVLC happily constructs
     * players for nonexistent paths. */
    FILE *probe = fopen(path, "rb");
    if (probe == NULL)
        return NULL;
    fclose(probe);

    kfw_player_t *p = (kfw_player_t *)calloc(1, sizeof(*p));
    if (p == NULL)
        return NULL;
    p->frame_fn   = cb;
    p->frame_user = user;

    /* Real-time engine: audio ON, no forced vout (the callback tap replaces
     * the display), snappy seeks for local evidence files. KFW_VLC_VERBOSE=1
     * mirrors the engine's diagnostics to stderr for runtime QA. */
    const char *argv[3];
    int argc = 0;
    argv[argc++] = "--network-caching=0";
    if (getenv("KFW_VLC_VERBOSE") != NULL)
        argv[argc++] = "--verbose=2";
    p->inst = kfw_vlc_shared(&g_player_inst, argc, argv);
    if (p->inst == NULL)
    {
        free(p);
        return NULL;
    }

    p->media = kfw_media_from_path(p->inst, path);
    if (p->media == NULL)
    {
        free(p);
        return NULL;
    }

    p->mp = libvlc_media_player_new_from_media(p->media);
    if (p->mp == NULL)
    {
        libvlc_media_release(p->media);
        p->media = NULL;
        free(p);
        return NULL;
    }

    libvlc_video_set_callbacks(p->mp, kfw_vmem_lock, kfw_vmem_unlock,
                               kfw_vmem_display, p);
    libvlc_video_set_format_callbacks(p->mp, kfw_vmem_format, kfw_vmem_cleanup);
    libvlc_media_player_set_video_title_display(p->mp, libvlc_position_disable, 0);
    return p;
}

void kfw_player_destroy(kfw_player_t *p)
{
    if (p == NULL)
        return;
    if (p->mp != NULL)
    {
        libvlc_media_player_stop(p->mp);
        libvlc_media_player_release(p->mp);
    }
    if (p->media != NULL)
        libvlc_media_release(p->media);
    /* p->inst is the shared player instance - released in kfw_media_shutdown(). */
    for (unsigned i = 0; i < 2; ++i)
        free(p->buf[i]);
    free(p);
}

int kfw_player_play(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return libvlc_media_player_play(p->mp) == 0 ? 0 : -1;
}

void kfw_player_pause(kfw_player_t *p)
{
    if (p != NULL && p->mp != NULL)
        libvlc_media_player_set_pause(p->mp, 1);
}

void kfw_player_stop(kfw_player_t *p)
{
    if (p != NULL && p->mp != NULL)
        libvlc_media_player_stop(p->mp);
}

kfw_player_state_t kfw_player_state(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return KFW_PLAYER_IDLE;
    return map_state(libvlc_media_player_get_state(p->mp));
}

int64_t kfw_player_time_ms(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return (int64_t)libvlc_media_player_get_time(p->mp);
}

int64_t kfw_player_length_ms(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    int64_t len = (int64_t)libvlc_media_player_get_length(p->mp);
    return len > 0 ? len : -1;
}

void kfw_player_seek_ms(kfw_player_t *p, int64_t ms)
{
    if (p == NULL || p->mp == NULL || ms < 0)
        return;
    libvlc_media_player_set_time(p->mp, (libvlc_time_t)ms);
}

void kfw_player_set_rate(kfw_player_t *p, float rate)
{
    if (p == NULL || p->mp == NULL)
        return;
    if (rate < 0.1f) rate = 0.1f;
    if (rate > 10.0f) rate = 10.0f;
    libvlc_media_player_set_rate(p->mp, rate);
}

float kfw_player_rate(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return 1.0f;
    return libvlc_media_player_get_rate(p->mp);
}

unsigned kfw_player_width(const kfw_player_t *p)
{
    return p == NULL ? 0 : p->w;
}

unsigned kfw_player_height(const kfw_player_t *p)
{
    return p == NULL ? 0 : p->h;
}

/* ---- Audio / subtitle tracks and volume ------------------------------------ */

static int copy_track_list(libvlc_track_description_t *head,
                           kfw_track_desc_t *out, int max)
{
    int n = 0;
    for (libvlc_track_description_t *d = head; d != NULL && n < max;
         d = d->p_next)
    {
        out[n].id = d->i_id;
        snprintf(out[n].name, sizeof(out[n].name), "%s",
                 d->psz_name != NULL ? d->psz_name : "");
        n++;
    }
    if (head != NULL)
        libvlc_track_description_list_release(head);
    return n;
}

int kfw_player_audio_tracks(kfw_player_t *p, kfw_track_desc_t *out, int max)
{
    if (p == NULL || p->mp == NULL || out == NULL || max <= 0)
        return 0;
    return copy_track_list(libvlc_audio_get_track_description(p->mp),
                           out, max);
}

int kfw_player_audio_track(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return libvlc_audio_get_track(p->mp);
}

int kfw_player_set_audio_track(kfw_player_t *p, int id)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return libvlc_audio_set_track(p->mp, id) == 0 ? 0 : -1;
}

int kfw_player_spu_tracks(kfw_player_t *p, kfw_track_desc_t *out, int max)
{
    if (p == NULL || p->mp == NULL || out == NULL || max <= 0)
        return 0;
    return copy_track_list(libvlc_video_get_spu_description(p->mp),
                           out, max);
}

int kfw_player_spu_track(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return libvlc_video_get_spu(p->mp);
}

int kfw_player_set_spu_track(kfw_player_t *p, int id)
{
    if (p == NULL || p->mp == NULL)
        return -1;
    return libvlc_video_set_spu(p->mp, id) == 0 ? 0 : -1;
}

void kfw_player_set_volume(kfw_player_t *p, int percent)
{
    if (p == NULL || p->mp == NULL)
        return;
    if (percent < 0)   percent = 0;
    if (percent > 200) percent = 200;
    libvlc_audio_set_volume(p->mp, percent);
}

int kfw_player_volume(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return 0;
    int v = libvlc_audio_get_volume(p->mp);
    return v < 0 ? 0 : v;
}

void kfw_player_set_mute(kfw_player_t *p, int mute)
{
    if (p != NULL && p->mp != NULL)
        libvlc_audio_set_mute(p->mp, mute ? 1 : 0);
}

int kfw_player_muted(kfw_player_t *p)
{
    if (p == NULL || p->mp == NULL)
        return 0;
    return libvlc_audio_get_mute(p->mp) > 0 ? 1 : 0;
}
