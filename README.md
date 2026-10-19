# Kish Forensic Workbench

**KFW · Digital Media Evidence Examination**

*Inspect. Correlate. Verify. Report.*

KFW is an independent, open-source desktop application for examining digital
media evidence: case management, forensic video review, hash-based integrity
verification, timeline analysis, and investigation reporting.

It is built around the VLC multimedia engine and other appropriately attributed
open-source components.

---

## About the application

| | |
|---|---|
| **Product name** | Kishan Forensic Workbench (KFW) |
| **Version** | 0.1.0-alpha |
| **Created by** | Kishan G A |
| **Role** | Creator & Lead Developer |
| **License (KFW code)** | GPLv2-or-later — see `LICENSE` |
| **Status** | Independent open-source digital forensics project |

KFW is an independent project and is **not** affiliated with or endorsed by
VideoLAN. VLC and applicable third-party components remain subject to their
respective licenses and notices — see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

> **Disclaimer:** This application is a developing forensic tool. Findings must
> be independently validated before use in formal investigations.

---

## Repository layout

```
KFW/
├── kfw-app/                Forensic application code (KFW-owned)
│   ├── cases/              Case lifecycle and case metadata
│   ├── evidence/           Evidence records, IDs, provenance
│   ├── integrity/          SHA-256 hashing and verification
│   ├── media/              Media adapter over libVLC
│   ├── timeline/           Event markers and timestamped observations
│   ├── audit/              Audit event records
│   ├── reporting/          JSON manifests and HTML/PDF reports
│   ├── detect/             Anomaly detectors (black/freeze/flicker/cut)
│   ├── ui/                 Qt Quick (QML) forensic workspace UI
│   └── tools/              Icon generator, QA assets
├── tests/                  Test suites (integrity, playback, reports, detect)
├── docs/                   Architecture, threat model, validation plan
├── packaging/              Inno Setup installer script
├── third_party/            Vendored libVLC 3.0.23 SDK (gitignored — see below)
├── .gitignore
├── THIRD_PARTY_NOTICES.md
└── README.md
```

KFW is a standalone Qt application that uses the **libVLC engine as an
external dependency** (vendored SDK in `third_party/`, gitignored). KFW adds a
forensic product *around* VLC rather than modifying it.

---

## Architecture at a glance

```
┌──────────────────────────────────────────────────────────┐
│              Forensic Application Layer (KFW)            │
│  Case manager · Evidence integrity · Media analysis      │
│  Audit & reporting · Timeline · UI                       │
├──────────────────────────────────────────────────────────┤
│                    Media Adapter                         │
│      Playback · Seeking · Frame capture · Info           │
├──────────────────────────────────────────────────────────┤
│                 VLC / libVLC Engine (upstream)           │
│      Demuxing · Decoding · Media input · Plugins         │
└──────────────────────────────────────────────────────────┘
        │                    │                    │
   Case database        Evidence store       Audit records
     (SQLite)        (originals/exports)   (events/verification)
```

Full details: [`docs/architecture.md`](docs/architecture.md).

---

## Release roadmap

### Release 1 — Core forensic MVP

- [x] Create, open, and close investigation cases
- [x] Import evidence while preserving the source file
- [x] Calculate and reverify SHA-256 hashes
- [x] Maintain evidence IDs and original-file metadata
- [x] Play, pause, seek, and step through video frames
- [x] Export selected frames with provenance information
- [x] Inspect container, codec, duration, and stream metadata
- [x] Record examiner notes and timestamped event markers
- [x] Generate a case manifest and initial investigation report
- [x] Log evidence imports, exports, and verification events

### Release 2 — Investigation tools

- [ ] Multi-camera timeline correlation
- [ ] Contact sheets and rapid frame review
- [ ] Metadata discrepancy and timestamp discontinuity detection
- [ ] Audio track inspection and transcript integration
- [ ] Searchable notes, tags, and event markers
- [ ] Side-by-side comparison of evidence items
- [ ] Evidence export verification
- [ ] Signed reports and tamper-evident audit logs

Progress beyond Release 1: automatic anomaly detection (black, freeze, flicker,
and cut indicators with cooldown-based event recording) is implemented and
covered by the `detect` test suite.

### Release 3 — Advanced analysis

- [ ] Optional object detection and event search
- [ ] Video integrity anomaly indicators
- [ ] Local speech transcription and searchable audio
- [ ] Batch processing of authorized evidence collections
- [ ] Plugin interface for additional forensic analyzers
- [ ] Advanced case export and evidence handover packages

> **Design rule:** an anomaly detector reports *indicators requiring
> examination*. It must never declare a video fake solely because metadata looks
> unusual — legitimate editing, transcoding, camera firmware, and damaged files
> all produce anomalies.

---

## Deliberately out of scope for version 1

AI-generated conclusions, facial identification, automatic criminality
judgments, unrestricted network-stream discovery, disk imaging, file carving,
and a custom codec engine. KFW's initial purpose is reliable examination of
multimedia evidence, not replacing an entire digital-forensics suite.

---

## Building

Requirements (Windows, MSYS2 UCRT64):

- Qt 6.11+ (Quick, QuickControls2, Qml, Network, OpenGL), CMake 3.16+, Ninja
- MinGW GCC (UCRT64), libVLC 3.0.x SDK (headers + `libvlc.dll`, `libvlccore.dll`, `plugins/`)
- Qt 6 SQLite driver (bundled with Qt)

Fetch the libVLC SDK into `third_party/vlc-3.0.23/` (gitignored — e.g. from the
official VideoLAN 3.0.23 win64 release), then:

```sh
cmake -S kfw-app -B kfw-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build kfw-build
cmake --build kfw-build --target test   # or: ctest --test-dir kfw-build
```

The build produces `KishanForensicWorkbench.exe`; a post-build step copies the
Qt and libVLC runtime DLLs next to the exe. For distribution, see
`packaging/kfw-installer.iss` (Inno Setup) for the installer and the portable
zip layout described in `docs/architecture.md`.

---

## Developer credit

**Kishan G A** — Creator & Lead Developer

KFW is an independent application using the libVLC engine. VideoLAN's copyright
and license notices remain intact in `third_party/` and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
