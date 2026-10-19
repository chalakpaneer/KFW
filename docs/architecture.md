# KFW Architecture

**Product:** Kishan Forensic Workbench (KFW)
**Document status:** v0.1.0-alpha baseline
**Owner:** Kishan G A — Creator & Lead Developer

---

## 1. Design principles

1. **Separate the forensic layer from the media engine.** All case, evidence,
   integrity, audit, and reporting logic lives in `kfw-app/` and must never be
   spread through VLC modules. This lets the libVLC SDK be updated without
   rewriting the case-management system.
2. **Use libVLC unmodified.** KFW consumes the vendored libVLC SDK
   (`third_party/vlc-3.0.23/`) as an external dependency and never edits
   engine, plugin, or codec internals.
3. **Never modify codec internals to add a feature.** Establish first that the
   unmodified engine can perform the operation through a public API. If it
   cannot, add a narrow adapter on top of the public API — not an engine change.
4. **Reproducibility over convenience.** Every forensic operation must be
   re-runnable from the recorded inputs and produce the same result.
5. **Indicators, not verdicts.** Analyzers report observations requiring
   examination. They never assert authenticity or forgery.

---

## 2. Layered architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     PRESENTATION LAYER                         │
│  kfw-app/ui  — Qt desktop: dashboard, examination workspace,   │
│                integrity table, report builder                 │
├─────────────────────────────────────────────────────────────────┤
│                    FORENSIC APPLICATION LAYER                   │
│  kfw-app/cases      case lifecycle, case metadata              │
│  kfw-app/evidence   evidence IDs, provenance, original metadata│
│  kfw-app/integrity  SHA-256 compute + reverify                 │
│  kfw-app/timeline   event markers, timestamped observations    │
│  kfw-app/audit      append-only audit events                   │
│  kfw-app/reporting  JSON manifests, HTML/PDF reports           │
├─────────────────────────────────────────────────────────────────┤
│                       MEDIA ADAPTER                            │
│  kfw-app/media  — the ONLY code that talks to libVLC           │
│  open · play/pause · seek · frame-step · track info ·          │
│  snapshot/frame export · media information                     │
├─────────────────────────────────────────────────────────────────┤
│                 VLC / libVLC ENGINE  (libVLC 3.0.23 SDK)       │
│  Demuxing · Decoding · Media input · Plugins                   │
└─────────────────────────────────────────────────────────────────┘

        │                        │                       │
   Case database           Evidence store           Audit records
     (SQLite)         (originals + exports)    (events + verification)
```

### 2.1 Dependency rule

Arrows point **downward only**:

```
ui → forensic layer → media adapter → libVLC
```

- `kfw-app/media` may include `third_party/vlc-3.0.23/include/` headers.
  Nothing else in `kfw-app/` may.
- The vendored SDK is read-only: nothing in KFW modifies it.
- If a change seems to require modifying libVLC itself, stop: find a public
  libVLC API or wrap the adapter differently. The SDK is never patched.

---

## 3. Component responsibilities

### 3.1 Media Adapter — `kfw-app/media/`

The single integration boundary with libVLC.

| Operation | libVLC API (libVLC 3.0) |
|---|---|
| Create engine | `libvlc_new()` |
| Open media | `libvlc_media_new_path()` → `libvlc_media_player_set_media()` |
| Play / pause / stop | `libvlc_media_player_play()` / `_pause()` / `_stop()` |
| Get/set time (seek) | `libvlc_media_player_get_time()` / `_set_time()` |
| Frame stepping | pause + `libvlc_media_player_set_time()` (delta = frame duration) |
| Duration | `libvlc_media_player_get_length()` |
| Tracks / codec info | `libvlc_media_get_tracks_info()` (via `libvlc_media_parse*`) |
| Container & meta | `libvlc_media_get_stats()`, `libvlc_media_player_get_media()` |
| Frame export | `libvlc_video_take_snapshot(mp, num, path, width, height)` — `libvlc_media_player.h` |
| State | `libvlc_media_player_get_state()` |

**Adapter contract:**

```c
typedef struct kfw_media_adapter kfw_media_adapter_t;

kfw_media_adapter_t *kfw_media_open(const char *path);
int64_t              kfw_media_get_time_ms(kfw_media_adapter_t *);
int                  kfw_media_set_time_ms(kfw_media_adapter_t *, int64_t ms);
int                  kfw_media_step(kfw_media_adapter_t *, int direction);
kfw_media_info_t    *kfw_media_get_info(kfw_media_adapter_t *);   /* container, codecs, duration, tracks */
int                  kfw_media_export_frame(kfw_media_adapter_t *, int64_t ms,
                                            const char *out_png, kfw_frame_meta_t *meta);
void                 kfw_media_close(kfw_media_adapter_t *);
```

`kfw_frame_meta_t` records provenance for every exported frame: source evidence
ID, source SHA-256, requested timestamp, actual reported timestamp, dimensions,
export time, and tool version.

**Important limitation:** `libvlc_video_take_snapshot` returns the *currently
rendered* frame at the player's current position. Frame-accurate export
therefore requires: seek → wait for `EndReached`/render → snapshot. The adapter
must expose this as an asynchronous operation with an explicit completion
signal so the UI never exports a frame at the wrong timestamp.

### 3.2 Evidence Integrity — `kfw-app/integrity/`

- SHA-256 computed with a vetted library (Windows CNG / OpenSSL / libsodium).
  **KFW never implements its own hash.**
- Import workflow:
  1. Register evidence record, assign `EV-####` ID.
  2. Compute SHA-256 of the source file while copying to the evidence store.
  3. Compute SHA-256 of the stored copy; compare. Record both.
  4. Record size, mtime, original path, importing examiner, UTC timestamp.
- Re-verification recomputes the digest of the stored original and compares to
  the recorded reference digest.

**Verification status vocabulary (exactly three values):**

| Status | Meaning |
|---|---|
| `Verified` | Stored bytes hash to the recorded reference digest |
| `Mismatch` | Stored bytes do **not** hash to the recorded reference digest |
| `Not checked` | No verification has been performed |

> A matching hash establishes that the **bytes** match the reference digest.
> It never establishes that the footage is genuine. UI and reports must not use
> the word "authentic".

### 3.3 Case Database — SQLite

Tables (initial schema):

```
cases        (case_id PK, title, examiner, status, created_utc, modified_utc, notes)
evidence     (evidence_id PK, case_id FK, orig_filename, orig_path, size_bytes,
              sha256, stored_path, imported_utc, provenance, file_type, verified_utc, verify_status)
media_info   (evidence_id FK, container, duration_ms, video_codec, audio_codec,
              width, height, fps, raw_json)
events       (event_id PK, case_id FK, evidence_id FK, ts_ms, kind, label, body, created_utc)
notes        (note_id PK, case_id FK, evidence_id FK, author, created_utc, body)
artifacts    (artifact_id PK, case_id FK, evidence_id FK, kind, path, sha256, created_utc)
audit        (seq PK, utc, actor, action, target_type, target_id, detail_json, prev_hash, entry_hash)
```

`audit` is append-only. `entry_hash = SHA-256(prev_hash || canonical_row)` forms
a hash chain so that deletion or edit of any row is detectable on verification.
This is **tamper-evident, not tamper-proof** — see `docs/threat-model.md`.

### 3.4 Audit — `kfw-app/audit/`

Every one of these writes an audit event:

- case create / open / close / modify
- evidence import / re-verify / export
- frame export
- report generation
- login/examiner identity change (if authentication is added)

### 3.5 Reporting — `kfw-app/reporting/`

Two outputs, always generated together from one in-memory model:

1. **JSON manifest** — machine-readable, canonical ordering, self-hashing.
2. **HTML report** — human-readable rendering of the same model.

PDF is a presentation export of the HTML, never the sole representation.

Report sections: case information · evidence inventory · acquisition and
hash-verification records · media metadata and technical observations ·
selected frames with timestamps and extraction details · examiner notes and
event timeline · tool versions, report-generation time, validation limitations ·
artifact manifest.

### 3.6 Anomaly detection — `kfw-app/detect/`

C-based detectors consuming per-frame statistics (mean luminance, frame-diff
ratios, luma deltas) and emitting indicators: black segments, freeze segments,
flicker, and hard cuts. Indicators become timeline events with cooldown-based
deduplication. Indicators are investigative leads only — they never assert
authenticity (§1.5). Covered by `tests/detect/`.

---

## 4. Repository layout

```
KFW/
├── kfw-app/                KFW-owned forensic code
│   ├── cases/
│   ├── evidence/
│   ├── integrity/
│   ├── media/
│   ├── timeline/
│   ├── audit/
│   ├── reporting/
│   ├── detect/
│   └── ui/
├── tests/
│   ├── fixtures/           Known-media test corpus (with expected digests)
│   ├── integrity/          Hash compute, reverify, mismatch detection
│   ├── playback/           Adapter: open, seek, step, info, frame export
│   └── reports/            Manifest/report consistency checks
├── docs/
│   ├── architecture.md     (this file)
│   ├── threat-model.md
│   └── validation-plan.md
├── packaging/              Inno Setup installer script
├── third_party/            Vendored libVLC 3.0.23 SDK (gitignored)
├── THIRD_PARTY_NOTICES.md
└── README.md
```

**Build integration:** `kfw-app/` has its own CMake build definition and links
against the vendored `libvlc.dll` at build time; at runtime the DLL and its
`plugins/` tree are copied next to the exe (portable layout). The SDK is never
compiled into KFW nor modified.

---

## 5. Technology choices

| Component | Choice | Rationale |
|---|---|---|
| Core media engine | VLC / libVLC | Embeddable, plugin-based, no codec work needed |
| Desktop UI | Qt | Already VLC's UI toolkit; one toolchain |
| Forensic services | C/C++ | Close integration with libVLC and Qt; Python for test tooling |
| Case metadata | SQLite | Zero-config, single-file, portable |
| File integrity | SHA-256 via vetted library | Do not roll your own |
| Media metadata | libVLC primary, independent parser optional | Corroboration without single-source trust |
| Reports | JSON manifest + HTML (PDF export) | Machine- and human-readable |
| Tests | C/C++ unit + integration + known-media corpus | Reproducible validation |

### Build toolchain (Windows)

- **Compiler:** MinGW GCC (MSYS2 UCRT64).
- **UI framework:** Qt 6.11+ (Quick, QuickControls2, Qml, Network, OpenGL).
- **Build system:** CMake + Ninja.
- **Media engine:** libVLC 3.0.x SDK (vendored under `third_party/vlc-3.0.23/`,
  gitignored — fetch the official VideoLAN win64 3.0.23 release there).

Configure and build:

```sh
cmake -S kfw-app -B kfw-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build kfw-build
```

The post-build step deploys Qt and libVLC runtime DLLs next to the exe, so the
result runs standalone.

---

## 6. Engine-change policy

The vendored libVLC SDK is treated as read-only:

1. KFW never modifies engine, plugin, or codec internals.
2. All media functionality goes through the public libVLC API from
   `kfw-app/media/` only.
3. Upgrading VLC = replacing the SDK tree in `third_party/` and re-running the
   playback and detect test suites.

Network protocols, streaming modules, and codec internals are **out of scope**.

---

## 7. Known limitations (v0.1.0-alpha)

- Audit chain is tamper-**evident**, not tamper-proof. A local administrator can
  modify the database and its logs. See `docs/threat-model.md`.
- SQLite offers no access control of its own; file-system ACLs and external
  validation are required for evidentiary use.
- Frame-exact export depends on renderer timing; each export records the
  timestamp actually reported by the player so discrepancies are visible.
- Anomaly indicators are investigative leads only and carry no conclusion about
  authenticity.
