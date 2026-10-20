# KFW architecture v2 — from video player to examiner workstation

Status: design for the 0.2 and 0.3 releases. Written 2026-10-20.
Copyright (C) 2026 Kishan G A. KFW is an independent project and is not
affiliated with or endorsed by VideoLAN.

## 1. Purpose

KFW lets an examiner review video evidence and be able to show, later, exactly
what was received, what was done to it, who did it, and what was found.

Design rules that every feature must obey:

1. **Originals are never modified.** Analysis and redaction produce *derived*
   artifacts, each linked to its source by evidence ID and SHA-256.
2. **Every examiner action is audited** in the hash-chained audit log.
3. **Detectors report indicators, never conclusions.** Output says "face
   detected, confidence 0.91", never "this is person X" or "this video is fake".
4. **Everything runs locally.** No evidence, frame or metadata leaves the
   machine. No cloud inference.
5. **Reproducible.** A result records the tool version, model name and
   version, parameters and input hash, so another examiner can repeat it.
6. **Known limits are stated** in the report next to every automated result.

Out of scope, deliberately: expression or emotion analysis, face recognition
(naming a person), automatic guilt or intent judgments, disk imaging, file
carving.

## 2. Layered architecture

```
┌────────────────────────────────────────────────────────────────────┐
│ UI layer (Qt Quick / QML)                                          │
│ Dashboard · Examination · Search · Reports · Analyzers · Settings  │
├────────────────────────────────────────────────────────────────────┤
│ Application shell (C++/Qt: KFWShell + list models)                 │
│ Thin bridge. No forensic logic. Runs long jobs on a job queue.     │
├────────────────────────────────────────────────────────────────────┤
│ Forensic services (C, no Qt dependency, unit-testable)             │
│ cases · evidence · integrity · timeline · audit · tags · search    │
│ reporting · export package · job queue · analyzer host             │
├──────────────────────┬─────────────────────┬───────────────────────┤
│ Media adapter        │ Analyzer plugins    │ Storage               │
│ libVLC playback,     │ built-in: black,    │ SQLite case DB        │
│ probe, tracks        │ freeze, flicker,    │ evidence store        │
│                      │ cut. Optional:      │ derived store         │
│                      │ faces, OCR, objects │ export packages       │
└──────────────────────┴─────────────────────┴───────────────────────┘
```

Rule: dependencies point downward only. Forensic services never include Qt.
The media adapter is the only code that includes libVLC headers. Analyzer
plugins see frames, not libVLC.

## 3. Modules

### 3.1 Case metadata (0.2)
Real cases carry more than a title. Add to `cases`: `case_number` (agency
reference), `agency`, `requester`, `incident_date`, `location`, `description`.
Schema version bumps to 2 with an in-place migration (`ALTER TABLE ... ADD
COLUMN`, guarded by `kfw_meta.schema_version`). Opening a v1 database upgrades
it and writes an audit event `db.migrate`.

### 3.2 Evidence intake (0.2)
- **Duplicate detection:** after hashing, look for the same SHA-256 anywhere in
  the database. Within a case: refuse by default and point to the existing
  item. In another case: allow, but record a `duplicate_of` link and warn.
- **Batch import:** import a folder or multi-selection; one audit event per
  file, a summary at the end.
- **Source metadata capture:** original path, size, file-system timestamps
  (created / modified), recorded at import as *claimed* values.
- **Read-only evidence store:** stored copy gets the read-only attribute and
  is re-verified on open when the last check is older than a configurable age.
- **Verify all:** one action that re-hashes every item in a case and reports
  verified / mismatch / missing.

### 3.3 Tags, notes and search (0.2)
- `tags(tag_id, name, color)` and `evidence_tags` / `event_tags` join tables.
- Per-evidence notes already exist; add a case-level notes field.
- **Search** across evidence filenames, event labels and bodies, notes and
  tags, with filters (evidence, kind, time range). SQLite FTS5 where available,
  `LIKE` fallback otherwise.
- Event kinds extended: Marker, Bookmark, Note, Indicator, Finding. Events get a
  `severity` (Info / Notable / Important) and `source` (Examiner / Analyzer).

### 3.4 Reports and handover (0.2)
- **Reports screen:** list of generated reports per case with their SHA-256,
  generation time and tool version; open, re-verify, regenerate.
- **Report content:** case metadata, evidence inventory with hashes,
  chain-of-custody table (from the audit log), media metadata, indicators and
  findings, examiner notes, selected frames, analyzer parameters and limits,
  tool versions, audit-chain status.
- **Chain of custody export:** every audit event touching a case, in order, as
  CSV and JSON.
- **Handover package:** a folder containing `evidence/`, `derived/`,
  `reports/`, `audit.json`, `timeline.csv` and `manifest.json`. The manifest
  lists every file with size and SHA-256 and carries its own digest. A
  `verify-package` function re-checks a package received from someone else.
- **Report signing (0.3):** Ed25519 detached signature over the manifest;
  public key shown in the About screen.

### 3.5 Examination workspace (0.2)
- Evidence list with thumbnails, status chips and tag chips.
- Timeline with severity colours, filters (source, kind, severity) and click
  to seek.
- Keyboard shortcut overlay (`?`), already-defined shortcuts made discoverable.
- Frame export with SHA-256 of the exported image and a sidecar JSON
  (evidence ID, timestamp, tool version).
- Contact sheet: N evenly spaced frames in a grid, exported as an image.
- Audio and subtitle track switching, mute and volume (done in 0.1.x).

### 3.6 Analyzer host (0.3)
A small, stable C interface so detectors are interchangeable and optional:

```c
typedef struct kfw_analyzer {
    const char *id;              /* "builtin.black", "faces.yunet" */
    const char *version;
    const char *model;           /* NULL for classic detectors */
    const char *limits;          /* plain-language known limitations */
    void *(*create)(const kfw_analyzer_params_t *);
    void  (*frame)(void *ctx, const kfw_frame_t *, kfw_result_sink_t *);
    void  (*destroy)(void *ctx);
} kfw_analyzer_t;
```

- Results are `kfw_indicator_t { ts_ms, duration_ms, kind, confidence, bbox[],
  detail_json }` written to the `indicators` table with analyzer id, version,
  model and parameters.
- Analyzers run on a **job queue** (worker thread, cancellable, progress
  reported to the UI), not inside the playback callback. Live playback keeps
  only the cheap built-in detectors.
- A job is itself audited: started, parameters, finished, indicator counts.

### 3.7 Face detection and redaction (0.3)
- **Detector:** an open, permissively licensed face-detection model run through
  ONNX Runtime or OpenCV DNN. Model file ships separately and its SHA-256 is
  recorded with every run. Candidate: YuNet (MIT licence); confirm licence and
  accuracy on CCTV-quality footage before committing.
- **Output:** bounding boxes with confidence, grouped into *tracks* by simple
  overlap tracking, so the examiner sees "a face visible 00:01:12 to 00:01:40"
  instead of thousands of per-frame boxes. No identity, no embeddings stored.
- **Review UI:** tracks appear on the timeline; the examiner can accept, reject
  or edit each one. Only accepted tracks are used for redaction. Manual boxes
  can be added for faces the detector missed.
- **Redaction export:** produces a *derived* video (blur or solid box), hashed,
  linked to the source evidence ID, with a redaction log (which tracks, which
  frames, method). Encoding uses a bundled FFmpeg invoked as a separate
  process (see §5). The original is untouched.
- **Stated limits:** misses small, blurred, angled or occluded faces; can
  false-detect. Report prints these, and requires a human review step before
  export.
- **Not built:** recognition, matching, demographics, expression, emotion.

### 3.8 Later analyzers (0.3+)
OCR of burned-in timestamps and plates (compare DVR clock with container
metadata), person and vehicle detection, frame-similarity search, audio
waveform and silence detection. Each ships through the §3.6 interface.

## 4. Data model (v2 additions)

```
cases            + case_number, agency, requester, incident_date,
                   location, description
evidence         + fs_created_utc, fs_modified_utc, duplicate_of
tags             tag_id, name, color
evidence_tags    evidence_id, tag_id
event_tags       event_id, tag_id
events           + severity, source
indicators       indicator_id, case_id, evidence_id, analyzer_id,
                 analyzer_version, model, params_json, ts_ms, duration_ms,
                 kind, confidence, bbox_json, review_state
                 (Unreviewed / Accepted / Rejected), reviewed_by
jobs             job_id, case_id, evidence_id, analyzer_id, state,
                 progress, started_utc, finished_utc, error
artifacts        existing; + source_evidence_id, method, params_json
reports          report_id, case_id, json_path, html_path, sha256,
                 generated_utc, tool_version
```

Migrations are additive only. The audit table is append-only and chained; no
feature may update or delete audit rows.

## 5. Dependencies and licensing

| Component | Use | Licence note |
|---|---|---|
| libVLC 3.0.x | playback, probe | LGPL; KFW code is GPL-2.0-or-later |
| Qt 6 | UI | LGPL, dynamic linking |
| SQLite | case DB | public domain |
| ONNX Runtime or OpenCV | optional analyzers | MIT / Apache-2.0 |
| Face model (e.g. YuNet) | face detection | check model licence before shipping |
| FFmpeg | redaction re-encode | LGPL or GPL depending on build; run as a
  separate process and ship its notices |

Every addition goes into `THIRD_PARTY_NOTICES.md` in the same commit.

## 6. Threading and stability rules

Learned from the evidence-switch crash:

- One long-lived libVLC instance per role; never create or release instances
  during normal use.
- libVLC calls from the GUI thread only, except the frame callback.
- Frame callback copies and queues; it never touches QML or the database.
- Long work (hashing, probing, analysis, export) runs on the job queue and
  reports back through queued signals. Results carry a generation number and
  are dropped if the examiner has moved on.
- UI never rebuilds a model from inside a delegate's own click handler.

## 7. Testing

- Forensic services are plain C with no Qt: unit tests run in CI without a
  display (hashing, migration, duplicate detection, search, manifest
  generation and verification, audit-chain tamper detection).
- Fixture corpus: small clips with known properties (black frames, freeze,
  multi-audio, no audio, corrupt tail, duplicate file).
- Analyzer tests use a labelled fixture and record precision/recall in
  `docs/validation-plan.md`; the numbers shipped in the report's "limits"
  section come from these tests, not from the model vendor.
- Packaging test: build the portable zip, run it on a clean Windows machine.

## 8. Release plan

**0.2 — usable on a real case**
case metadata and migration · duplicate detection · batch import · verify
all · tags · search · Reports screen · chain-of-custody export · handover
package with verify · contact sheet · shortcut overlay

**0.3 — analyzers**
job queue · analyzer host · face detection with track review · redaction
export · OCR for burned-in timestamps · report signing

**Later**
person/vehicle detection · multi-camera timeline correlation · side-by-side
comparison · Linux build · plug-in packaging
