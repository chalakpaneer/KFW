# KFW Threat Model

**Document status:** v0.1.0-alpha draft
**Scope:** KFW desktop application and its local case/evidence stores.
**Owner:** Kishan G A

---

## 1. What KFW is protecting

| # | Asset | Why it matters |
|---|---|---|
| A1 | Evidence original files | Alteration destroys evidentiary value |
| A2 | Recorded SHA-256 reference digests | Basis of all integrity claims |
| A3 | Case/evidence metadata (IDs, provenance, timestamps) | Chain-of-custody record |
| A4 | Examiner notes and event markers | Analytical record |
| A5 | Audit log | Only record of what happened, when, by whom |
| A6 | Generated reports and frame exports | What is handed to other parties |
| A7 | Application identity (branding, About, version) | Prevents misattribution of tooling |

---

## 2. Adversaries

| ID | Adversary | Capability | In scope? |
|---|---|---|---|
| T1 | Honest examiner | Normal use, possible error | Yes — mitigate by design |
| T2 | careless operator | Overwrites files, deletes exports | Yes |
| T3 | Curious co-worker with local access | Reads/edits case DB, exports | Yes |
| T4 | Local administrator / root | Full filesystem + DB access | Partially — detection only |
| T5 | Remote network attacker | KFW makes no network calls by default | Out of scope for v1 |
| T6 | Malicious evidence file | Crafted media exploiting a decoder | Yes |

---

## 3. Trust boundaries

```
[T6: hostile media file]
        │  untrusted bytes
        ▼
 ┌──────────────┐   untrusted   ┌───────────────┐
 │ libVLC engine │ ───────────► │ media adapter  │
 └──────────────┘               └───────┬───────┘
                                        │ validated, typed data
                                        ▼
                              ┌─────────────────────┐
                              │ forensic app layer   │
                              └──────────┬──────────┘
                                         │
        ┌────────────────────────────────┼───────────────────────┐
        │ trusted, local                 ▼                       │
        │                     ┌──────────────────┐               │
        │                     │ case DB (SQLite) │◄── T3/T4 ─────┤
        │                     └──────────────────┘               │
        │                     ┌──────────────────┐               │
        │                     │ evidence store   │◄── T2/T4 ─────┤
        │                     └──────────────────┘               │
        └────────────────────────────────────────────────────────┘
```

1. **Media → engine:** hostile. Evidence files are attacker-controlled input.
2. **Engine → adapter:** semi-trusted. Metadata from demuxers is reported as
   *claimed*, never as fact.
3. **App → storage:** trusted at rest, but subject to T3/T4 tampering.

---

## 4. Risks and mitigations

### R1 — Silent evidence alteration
**Threat:** T2, T4 modifies an original in the evidence store.
**Mitigation:**
- Originals opened read-only; KFW never writes to an evidence original.
- SHA-256 re-verification on every open and before every export.
- Verification status is one of exactly three values: `Verified` /
  `Mismatch` / `Not checked`.
- **Residual:** an administrator who recomputes hashes can defeat this. Flag any
  record whose `sha256` changed since the last audit entry.

### R2 — Audit log tampering
**Threat:** T3/T4 deletes or edits audit rows.
**Mitigation:**
- Append-only table; hash chain `entry_hash = SHA-256(prev_hash || row)`.
- Chain verification is a first-class operation shown in the UI.
- **Residual:** a local admin can rewrite the whole chain. For evidentiary use,
  export audit chains externally and out-of-band. State this limitation in every
  report.

### R3 — Database tampering
**Threat:** T3/T4 edits case or evidence rows.
**Mitigation:**
- SQLite has no access control; rely on OS ACLs on the case directory.
- Every report embeds the digests of the rows it used, so later DB edits are
  detectable against a distributed report.
- **Residual:** same as R2 — this is detection, not prevention.

### R4 — Media parser exploitation (T6)
**Threat:** a crafted file exploits a demuxer/decoder in libVLC.
**Mitigation:**
- Run the engine with sandboxing where available; keep the engine updated with
  upstream VLC security releases.
- Never auto-play media on import; parsing happens with an explicit examiner
  action.
- Treat engine crashes during parsing as a **recordable finding**, not a silent
  failure.
- **Residual:** KFW inherits VLC's decoder attack surface. This must be stated
  in the validation plan.

### R5 — Frame/timestamp inaccuracy
**Threat:** an exported frame is labelled with the wrong timestamp.
**Mitigation:**
- Export records both the *requested* timestamp and the *player-reported*
  timestamp plus the export method.
- Exports are asynchronous: seek → confirm render → snapshot → record.
- Any mismatch between requested and reported time is flagged in the report.

### R6 — Overclaiming in output (most important design risk)
**Threat:** a user or reader reads "hash verified" as "video is real", or an
anomaly indicator as proof of forgery.
**Mitigation:**
- Language rules enforced in UI strings and report templates:
  - Never "authentic" — use "digest matches the recorded reference digest".
  - Never "fake"/"tampered" from an anomaly detector — use "indicator
    requiring examination".
- Reports include a "Validation limitations" section by default and cannot be
  suppressed.

### R7 — Misattribution of the tool or its author
**Threat:** a reader believes KFW is a VideoLAN product, or that Kishan G A
authored the VLC engine.
**Mitigation:**
- About dialog and README credit Kishan G A for KFW's contributions and
  VideoLAN for VLC.
- Retain all upstream copyright headers and `COPYING` files.
- No VLC cone logo or VLC orange/white identity in KFW surfaces.

---

## 5. Explicitly out of scope (v1)

- Remote/network attackers: KFW performs no network I/O by default.
- Full-disk or memory forensics, file carving, disk imaging.
- Multi-user server deployment and remote authentication.
- Protection against a determined local administrator (detection only).

---

## 6. Required report disclosures

Every generated report must carry:

1. Tool name, version, and build/commit identifiers.
2. Report generation time in UTC.
3. The verification status vocabulary as used (not an implicit "pass/fail").
4. The validation-limitations section (R2, R3, R4 residuals).
5. Digests of the report's own artifacts for external verification.
