/*****************************************************************************
 * kfw_media.h: Kishan Forensic Workbench — media adapter (public contract)
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
 * VideoLAN. This adapter is the ONLY KFW component permitted to include the
 * libVLC public headers (see docs/architecture.md, section 2.1).
 *****************************************************************************/

#ifndef KFW_MEDIA_H
#define KFW_MEDIA_H 1

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Media kind, as classified by the engine. */
typedef enum kfw_media_type
{
    KFW_MEDIA_UNKNOWN = 0,
    KFW_MEDIA_FILE,
    KFW_MEDIA_DIRECTORY,
    KFW_MEDIA_DISC,
    KFW_MEDIA_STREAM,
    KFW_MEDIA_PLAYLIST,
} kfw_media_type_t;

/** A single track, reduced to the fields KFW records for evidence. */
typedef struct kfw_track
{
    kfw_media_type_t type;       /**< actually libvlc_track_type_t, kept opaque */
    int              track_type; /**< 0=audio 1=video 2=text */
    uint32_t         codec;      /**< fourcc */
    uint32_t         original_fourcc;
    unsigned         bitrate;
    char            *language;   /**< owned by this struct, may be NULL */
    char            *description;/**< owned by this struct, may be NULL */

    /* video only */
    unsigned width;
    unsigned height;
    unsigned sar_num;
    unsigned sar_den;
    unsigned frame_rate_num;
    unsigned frame_rate_den;

    /* audio only */
    unsigned channels;
    unsigned sample_rate;
} kfw_track_t;

/** Aggregate technical information about a media item. */
typedef struct kfw_media_info
{
    char            *mrl;             /**< engine-reported resource locator */
    kfw_media_type_t type;
    int64_t          duration_us;     /**< -1 if unknown */
    bool             is_parsed;

    kfw_track_t     *tracks;          /**< array of n_tracks */
    size_t           n_tracks;
} kfw_media_info_t;

/** Provenance record attached to every exported frame (architecture.md §3.1). */
typedef struct kfw_frame_meta
{
    char        *evidence_id;      /**< KFW evidence identifier, caller-set */
    char        *source_sha256;    /**< digest of the source file, caller-set */
    int64_t      requested_time_us;/**< timestamp the examiner asked for     */
    int64_t      actual_time_us;   /**< timestamp the engine actually used   */
    unsigned     width;            /**< exported image width                 */
    unsigned     height;           /**< exported image height                */
    char        *export_path;      /**< where the frame was written          */
    char        *tool_version;     /**< libVLC version used for the export   */
} kfw_frame_meta_t;

/** Opaque adapter handle. */
typedef struct kfw_media_adapter kfw_media_adapter_t;

/* ---- Live playback engine -------------------------------------------------- */

/** Opaque live playback handle (its own libVLC instance with audio/video). */
typedef struct kfw_player kfw_player_t;

/** Reduced playback state, mapped from the engine's own state machine. */
typedef enum kfw_player_state
{
    KFW_PLAYER_IDLE = 0,
    KFW_PLAYER_OPENING,
    KFW_PLAYER_PLAYING,
    KFW_PLAYER_PAUSED,
    KFW_PLAYER_STOPPED,
    KFW_PLAYER_ENDED,
    KFW_PLAYER_ERROR,
} kfw_player_state_t;

/**
 * Called on the decoder thread for every rendered frame.
 *
 * \param bgra    Packed 32-bit BGRA (libVLC "RV32") frame buffer.
 * \param pitch   Bytes per scanline (may exceed width*4 — always honor it).
 *
 * The callback runs synchronously inside the engine's display step: the buffer
 * is stable for the duration of the call and must not be retained afterwards.
 * Copy what you need.
 */
typedef void (*kfw_player_frame_fn)(void *user, int64_t ts_ms,
                                    unsigned width, unsigned height,
                                    const uint8_t *bgra, size_t pitch,
                                    size_t lines);

/**
 * Create a live player for \p path (real-time playback, audio included).
 *
 * \param cb   Frame tap; invoked from the engine's decoder thread.
 * \param user Context passed back to \p cb.
 *
 * \return a handle on success, NULL on failure (missing file, engine failure).
 */
kfw_player_t *kfw_player_create(const char *path,
                                kfw_player_frame_fn cb, void *user);

/** Release the player (stops playback first). Safe with NULL. */
void kfw_player_destroy(kfw_player_t *p);

/** \return 0 on success (or already playing), negative on failure. */
int   kfw_player_play(kfw_player_t *p);

void  kfw_player_pause(kfw_player_t *p);
void  kfw_player_stop(kfw_player_t *p);

kfw_player_state_t kfw_player_state(kfw_player_t *p);

/** Current media time (ms, approximate — decoder-thread derived). */
int64_t kfw_player_time_ms(kfw_player_t *p);

/** Total media duration (ms; -1 if unknown). */
int64_t kfw_player_length_ms(kfw_player_t *p);

/** Seek to \p ms (absolute media time). Works while playing or paused. */
void  kfw_player_seek_ms(kfw_player_t *p, int64_t ms);

/** Playback rate; 1.0 = normal. Values outside 0.1..10 are clamped. */
void  kfw_player_set_rate(kfw_player_t *p, float rate);
float kfw_player_rate(kfw_player_t *p);

/** Live video dimensions as delivered to the frame tap (0 until started). */
unsigned kfw_player_width(const kfw_player_t *p);
unsigned kfw_player_height(const kfw_player_t *p);

/**
 * Create an adapter and open \p path.
 *
 * \return a handle on success, NULL on failure.
 * Initialize the engine once per process; this opens the media but does not
 * start playback.
 */
kfw_media_adapter_t *kfw_media_open(const char *path);

/** Release the adapter. Safe with NULL. */
void kfw_media_close(kfw_media_adapter_t *a);

/** Human-readable engine version string (static storage). */
const char *kfw_media_engine_version(void);

/**
 * Parse the media and populate technical information.
 *
 * Blocks until parsing completes or times out.
 * \return 0 on success, negative on error.
 */
int kfw_media_parse(kfw_media_adapter_t *a, int64_t timeout_us);

/** Get technical information. NULL if not parsed. Caller does NOT own it. */
const kfw_media_info_t *kfw_media_get_info(const kfw_media_adapter_t *a);

/**
 * Export a single frame at \p time_us to \p out_path (PNG).
 *
 * \deprecated Under the libVLC 3.x runtime this interface is not supported and
 * always returns -100 (KFW_ERR_NOT_SUPPORTED).  Frame capture with full
 * provenance is performed by the application-side Qt multimedia exporter
 * (kfw_shell.cpp), which records the actual frame timestamp, dimensions and
 * engine version.  A future engine refresh that ships the VLC 4.x thumbnailer
 * can re-enable the native path without changing this contract.
 *
 * \return 0 on success, -100 under libVLC 3.x, negative on error.
 */
int kfw_media_export_frame(kfw_media_adapter_t *a, int64_t time_us,
                           const char *out_path, kfw_frame_meta_t *meta);

/** Free a frame metadata record filled by kfw_media_export_frame(). */
void kfw_frame_meta_clear(kfw_frame_meta_t *meta);

/** Free a media info record returned indirectly (used internally/testing). */
void kfw_media_info_clear(kfw_media_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* KFW_MEDIA_H */
