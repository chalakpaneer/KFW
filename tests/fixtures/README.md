# tests/fixtures/README.md — KFW test fixture corpus
# Copyright (C) 2026 Kishan G A

## Purpose

This directory holds known-good media files used by the playback adapter tests
(`tests/playback/test_playback.c`) and any future frame-export validation.

## Required files

| File | Description | Required SHA-256 |
|---|---|---|
| `test_video.mp4` | Short H.264/AAC clip (5–30 s) | _(record yours below after first generation)_ |

The playback tests skip gracefully if `test_video.mp4` is absent, so an empty
`fixtures/` directory does not break the build.  Add a fixture to run the full
suite.

## Generating a fixture with FFmpeg

```sh
# 10-second 720p colour-bar test signal, H.264 + AAC
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=25 \
       -f lavfi -i sine=frequency=1000:sample_rate=44100 \
       -t 10 -c:v libx264 -c:a aac \
       tests/fixtures/test_video.mp4
```

Record the digest immediately after generation so it can be used as the
reference value in integrity tests:

```sh
sha256sum tests/fixtures/test_video.mp4
```

> **Note:** Do not commit large media files to the repository.  The fixture
> should be generated locally or stored in a dedicated evidence-corpus store
> outside the source tree.
