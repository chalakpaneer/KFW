/*
 * test_playback.c — KFW media adapter unit tests
 * Copyright (C) 2026 Kishan G A
 *
 * Run from tests/fixtures/ where a known test video is present.
 *
 * Tests:
 *   1. kfw_media_open() returns a handle for a valid file.
 *   2. kfw_media_open() returns NULL for a nonexistent file.
 *   3. kfw_media_parse() succeeds and populates media info.
 *   4. kfw_media_get_info() returns non-NULL after a successful parse.
 *   5. Duration is positive for a video file.
 *   6. At least one track is reported.
 *   7. kfw_media_engine_version() returns a non-empty string.
 *   8. kfw_media_close(NULL) does not crash.
 *
 * NOTE: Tests 3-6 are skipped (SKIP) if no fixture video is present.
 * The fixture is any file named "test_video.mp4" in the working directory.
 */

#include "media/kfw_media.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_skipped = 0;

#define CHECK(desc, expr) do { \
    ++tests_run; \
    if (expr) { \
        ++tests_passed; \
        printf("[PASS] %s\n", (desc)); \
    } else { \
        printf("[FAIL] %s\n", (desc)); \
    } \
} while (0)

#define SKIP(desc) do { \
    ++tests_skipped; \
    printf("[SKIP] %s — no fixture video\n", (desc)); \
} while (0)

/* Check if the fixture file exists */
static int fixture_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int main(void)
{
    printf("=== KFW Media Adapter Tests ===\n\n");

    /* Test 1: engine version */
    const char *ver = kfw_media_engine_version();
    CHECK("engine_version returns non-empty string",
          ver != NULL && ver[0] != '\0');

    /* Test 2: close(NULL) does not crash */
    kfw_media_close(NULL);
    CHECK("media_close(NULL) does not crash", 1);

    /* Test 3: open nonexistent file returns NULL */
    kfw_media_adapter_t *bad = kfw_media_open("__nonexistent__.mp4");
    CHECK("media_open nonexistent returns NULL", bad == NULL);

    /* Tests 4-8: require fixture */
    const char *fixture = "test_video.mp4";
    if (!fixture_exists(fixture))
    {
        SKIP("media_open valid file");
        SKIP("media_parse success");
        SKIP("media_get_info non-NULL");
        SKIP("duration positive");
        SKIP("at least one track");
        goto done;
    }

    kfw_media_adapter_t *a = kfw_media_open(fixture);
    CHECK("media_open valid file returns handle", a != NULL);
    if (!a) goto done;

    /* Parse with a 5-second timeout */
    int rc = kfw_media_parse(a, 5000000LL);
    CHECK("media_parse returns 0", rc == 0);

    const kfw_media_info_t *info = kfw_media_get_info(a);
    CHECK("media_get_info non-NULL after parse", info != NULL);

    if (info)
    {
        CHECK("duration_us positive", info->duration_us > 0);
        CHECK("at least one track",   info->n_tracks >= 1);

        printf("    MRL:          %s\n",      info->mrl ? info->mrl : "(null)");
        printf("    Duration:     %lld us\n", (long long)info->duration_us);
        printf("    Tracks:       %zu\n",     info->n_tracks);
        for (size_t i = 0; i < info->n_tracks; ++i)
        {
            const kfw_track_t *t = &info->tracks[i];
            const char *tkind =
                t->track_type == 0 ? "audio" :
                t->track_type == 1 ? "video" : "text";
            printf("      [%zu] %s  codec=0x%08x  %ux%u\n",
                   i, tkind, t->codec, t->width, t->height);
        }
    }

    kfw_media_close(a);

done:
    printf("\n%d / %d tests passed", tests_passed, tests_run);
    if (tests_skipped)
        printf("  (%d skipped — add tests/fixtures/test_video.mp4 to run them)",
               tests_skipped);
    printf("\n");
    return (tests_passed == tests_run) ? 0 : 1;
}
