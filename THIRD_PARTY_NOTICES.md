# Third-Party Notices

Kishan Forensic Workbench (KFW) distributes or links against the third-party
components listed below. Their copyright notices and licenses are retained in
full in the corresponding source trees. This file is an index, not a
replacement for the original license texts.

KFW is an independent project and is not affiliated with or endorsed by
VideoLAN.

---

## 1. VLC media player / libVLC

- **Source:** `vlc/` (upstream clone from <https://code.videolan.org/videolan/vlc>)
- **Pinned baseline:** see `git -C vlc rev-parse HEAD`
- **Upstream project:** VideoLAN
- **VLC main application license:** GNU General Public License, version 2
  (see `vlc/COPYING`)
- **libVLC / libvlccore license:** GNU Lesser General Public License,
  version 2.1 (see `vlc/COPYING.LIB`)
- **Website:** <https://www.videolan.org/>

VLC's source headers state:

> This program is free software; you can redistribute it and/or modify it
> under the terms of the GNU General Public License as published by the Free
> Software Foundation; either version 2 of the License, or (at your option)
> any later version.

and for libVLC:

> This program is free software; you can redistribute it and/or modify it
> under the terms of the GNU Lesser General Public License as published by
> the Free Software Foundation; either version 2.1 of the License, or
> (at your option) any later version.

**KFW's obligations.** Because KFW links against libVLC (LGPL) and modifies or
redistributes VLC's GPL-licensed application, distribution of the combined work
is governed by the GPL for the application as a whole. KFW is therefore
released under GPLv2-or-later. Anyone distributing a binary must provide the
corresponding source for the GPL-covered parts, including any modifications.

**Trademark note.** "VLC", the cone logo, and VideoLAN marks are the property
of their respective owners. VideoLAN's legal guidance permits redistribution of
modified software subject to the applicable license terms and identifies its
trademarks separately from those licenses. KFW does not use the VLC name, cone
logo, or orange/white VLC branding as its own identity.

### Bundled VLC dependencies

VLC's `contrib/` tree builds and statically links a large number of third-party
libraries (FFmpeg, and others). Their individual licenses are recorded in
`vlc/contrib/src/*/` and in the generated package. A binary distribution must
carry the corresponding license texts for every contrib actually linked.

License selection for contribs is controlled at build time:

| Flag | Meaning |
|------|---------|
| `g`  | GPLv3 (default) |
| `l`  | LGPLv3 + additional clauses |
| `a`  | LGPLv2 + additional clauses |

---

## 2. Qt

- **Usage:** Desktop UI framework (VLC's `modules/gui/qt` and KFW's UI layer)
- **License:** GNU Lesser General Public License, version 3 (with additional
  clauses) — Qt is a commercially dual-licensed library; the open-source
  LGPL-3.0 variant is used here.
- **Website:** <https://www.qt.io/>

---

## 3. SQLite

- **Usage:** Case metadata, evidence records, audit records
- **License:** Public Domain
- **Website:** <https://www.sqlite.org/>

---

## 4. Cryptographic hashing

- **Usage:** SHA-256 evidence digests
- **Candidate libraries:** Windows CNG (`bcrypt`), OpenSSL, or libsodium — the
  chosen provider must be a well-tested, non-constant-time-naive implementation
  suitable for integrity work.
- **Note:** KFW must never implement its own SHA-256.

---

## 5. Independent media metadata parser (optional)

- **Usage:** Cross-checking container/codec metadata reported by libVLC
- **Candidate:** FFmpeg / ffprobe (LGPL/GPL, depending on configuration), or
  MediaInfo (BSD-style)
- **Status:** Optional in Release 1; required for metadata-corroboration work in
  Release 2.

---

## Attribution obligations summary

1. Keep every upstream copyright header, `COPYING`, and `COPYING.LIB` intact.
2. Do not remove or obscure VideoLAN's notices in `vlc/`.
3. Ship license texts for every dynamically or statically linked dependency in
   binary releases.
4. Provide Corresponding Source for GPL-covered components on request.
5. Identify KFW's own contributions as KFW's — do not imply authorship of the
   VLC engine.

---

*This file is maintained by KFW and must be updated whenever a third-party
dependency is added, removed, or upgraded.*
