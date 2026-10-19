# KFW Validation Plan

**Document status:** v0.1.0-alpha
**Owner:** Kishan G A

A forensic tool that has not been validated is not usable in an investigation.
This plan defines what must be proven, and how, for each release.

---

## 1. Validation principles

1. **Reproducible.** Every claim must be re-derivable from recorded inputs.
2. **Negative-tested.** A check that only ever passes proves nothing. Every
   integrity and consistency check must have a test that makes it *fail*.
3. **Corpus-based.** Playback and metadata behaviour is asserted against a known
   media corpus with recorded expected values, not eyeballed.
4. **Versioned.** Validation results are tied to a tool version and a VLC
   baseline commit.

---

## 2. Baseline validation (gate for all other work)

| ID | Requirement | Method |
|----|-------------|--------|
| B1 | Vendored libVLC SDK is unmodified | SDK tree kept read-only; hashes re-checked after SDK refresh |
| B2 | Baseline builds from a clean tree | Fresh build directory completes with exit 0 |
| B3 | Baseline libVLC plays a corpus file | Automated smoke test |
| B4 | KFW changes are individually documented and tested | Each change has a test |

**Rule:** B1 must pass before any KFW release artefact is produced. If engine
modification ever seems unavoidable, it is out of scope per
`docs/architecture.md` §6 — find a public-API alternative instead.

---

## 3. Evidence integrity tests — `tests/integrity/`

| ID | Test | Expected |
|----|------|----------|
| I1 | Hash a known fixture | Digest equals the recorded value in `tests/fixtures/manifest.json` |
| I2 | Hash the same file twice | Identical digests |
| I3 | Import → verify immediately | Status `Verified` |
| I4 | Flip one byte in the stored copy → reverify | Status `Mismatch` |
| I5 | Truncate the stored copy → reverify | Status `Mismatch` |
| I6 | Delete the stored copy → reverify | Status reported as error, never `Verified` |
| I7 | Verify an untouched original after 24h | Still `Verified` |
| I8 | Hash 0-byte file and 4 GiB file | Correct digests, bounded memory |
| I9 | Large-file hashing streams (does not load file into RAM) | Memory ceiling asserted |
| I10 | Audit row written for every import/verify/export | Row count matches operation count |
| I11 | Audit hash chain verifies after N operations | Chain valid |
| I12 | Delete a middle audit row → verify chain | Chain **fails** |

---

## 4. Playback / media adapter tests — `tests/playback/`

Run against every file in `tests/fixtures/` with expected values recorded in the
corpus manifest.

| ID | Test | Expected |
|----|------|----------|
| P1 | Open each corpus file | Success; no crash |
| P2 | Read duration | Matches corpus expectation within tolerance |
| P3 | Read container + video/audio codec | Matches expectation |
| P4 | Seek to 0%, 25%, 50%, 75%, 100% | Reported time within frame tolerance |
| P5 | Frame step forward N times | Monotonic, non-decreasing time |
| P6 | Export frame at T | File written; requested-vs-reported time delta within tolerance and **recorded** |
| P7 | Export same frame twice | Byte-identical output, identical metadata |
| P8 | Seek past EOF, seek before 0 | Clamped, no crash |
| P9 | Play corrupted/truncated file | Graceful error, no crash, event recorded |
| P10 | Open file with no video track | Audio-only path works |
| P11 | Rapid open/close 100× | No leak (handle/memory check) |
| P12 | Pause during export | Export reflects a rendered frame or fails explicitly — never silently wrong |

**Tolerance policy:** every P6/P4 result stores both requested and reported
timestamps. A non-zero delta is *data*, recorded in the report — not a silent
correction.

---

## 5. Reporting tests — `tests/reports/`

| ID | Test | Expected |
|----|------|----------|
| R1 | Generate report from a case | JSON parses; HTML renders; both derive from one model |
| R2 | JSON manifest self-hash | Verifies |
| R3 | Re-run generation with identical inputs and clock pinned | Byte-identical JSON |
| R4 | Corrupt one evidence digest, regenerate | Report reflects corruption, does not silently "fix" it |
| R5 | Evidence count in report == DB count | Equal |
| R6 | Every frame in report exists in artifact manifest with matching SHA-256 | Equal |
| R7 | Language scan for forbidden terms (`authentic`, `genuine`, `fake`, `forged` as verdicts) | No matches |
| R8 | Validation-limitations section present | Present, non-empty |

---

## 6. Engine-change register

The vendored libVLC SDK is never modified (see `docs/architecture.md` §6).
This register remains empty by policy.

| Date | File(s) | Reason no public API suffices | Test ID | Upstream status |
|------|---------|-------------------------------|---------|-----------------|
| — | — | — | — | — |

---

## 7. Known-media test corpus — `tests/fixtures/`

Each entry records: filename, source/derivation, SHA-256, container, duration,
codecs, resolution, frame rate, and any intentional defect.

Minimum corpus for Release 1:

| Fixture | Purpose |
|---|---|
| `sample_h264_mp4.mp4` | Baseline happy path |
| `sample_h265_mkv.mkv` | Different container/codec |
| `sample_aac_audio_only.m4a` | No video track |
| `sample_1s_25fps.mp4` | Frame-step arithmetic (25 fps → 40 ms) |
| `sample_1s_30fps.mp4` | Frame-step arithmetic (30 fps → 33.33 ms) |
| `sample_vfr.mkv` | Variable frame rate — tolerance handling |
| `corrupt_truncated.mp4` | Graceful failure |
| `corrupt_zero_bytes.mp4` | Graceful failure |
| `naming_edge_cases` set | Unicode, spaces, long paths, RTL filenames |

Corpus files must be either generated by a recorded, reproducible script or
redistributable under a permissive license. **Never** commit third-party
copyrighted video.

---

## 8. Release validation gate

A KFW release may not ship unless:

- [ ] B1–B4 pass
- [ ] All `tests/integrity` pass, including negative tests I4–I6, I12
- [ ] All `tests/playback` pass against the full corpus
- [ ] All `tests/reports` pass, including R7 and R8
- [ ] `THIRD_PARTY_NOTICES.md` matches the actual dependency set
- [ ] About dialog shows correct version, credit, and disclaimers
- [ ] `git -C vlc status --porcelain` clean (or §6 entry exists)
- [ ] Validation results recorded with tool version + VLC baseline SHA

---

## 9. Recurring validation

| Trigger | Action |
|---|---|
| VLC baseline update | Re-run B1–B4, then full suite |
| Dependency upgrade | Re-run integrity suite; update notices |
| Every alpha/beta build | Full suite; archive results with the build |
| Before any evidentiary use | Full suite + independent spot-check by a second examiner |
