#!/bin/sh
# Builds and runs the platform-independent forensic-services tests on Linux.
# Needs: gcc, libsqlite3 (dev headers preferred).  If only the runtime library
# is present, point STUB_SQLITE at a directory holding a minimal sqlite3.h.
#
#   tests/run_linux.sh            # from the repository root
set -eu
cd "$(dirname "$0")/.."
INC="-Ikfw-app -Itests -I."
[ -n "${STUB_SQLITE:-}" ] && INC="$INC -I$STUB_SQLITE"
LIB="${SQLITE_LIB:--lsqlite3}"
SRC="kfw-app/cases/kfw_db.c kfw-app/evidence/kfw_evidence.c kfw-app/integrity/kfw_integrity.c \
     kfw-app/reporting/kfw_report.c kfw-app/timeline/kfw_timeline.c \
     kfw-app/util/kfw_util.c kfw-app/export/kfw_package.c"
OUT="${TMPDIR:-/tmp}/kfw_tests"; mkdir -p "$OUT"

echo "== format-string check (-Werror=format) =="
for f in $SRC; do gcc -std=gnu11 -fsyntax-only -Wformat=2 -Werror=format -Wno-format-nonliteral \
    -Wno-format-truncation $INC "$f"; done
echo ok

echo "== SHA-256 known-answer tests =="
gcc -std=gnu11 -w $INC tests/integrity/test_sha256_vectors.c kfw-app/integrity/kfw_integrity.c -o "$OUT/sha"
"$OUT/sha"

echo "== integrity + report suites =="
for t in tests/integrity/test_integrity.c tests/reports/test_reports.c; do
    gcc -std=gnu11 -w $INC "$t" $SRC $LIB -o "$OUT/t"; (cd "$OUT" && ./t | tail -1)
done

echo "== v2: migration, duplicates, tags, search, custody, packages =="
gcc -std=gnu11 -w $INC tests/v2/make_v1_db.c kfw-app/cases/kfw_db.c kfw-app/integrity/kfw_integrity.c $LIB -o "$OUT/mkv1_new" 2>/dev/null || true
# The v1 fixture must come from the ORIGINAL 0.1.0 database code:
if [ -n "${V1_SOURCE_TREE:-}" ]; then
    gcc -std=gnu11 -w -I"$V1_SOURCE_TREE/kfw-app" -I"$V1_SOURCE_TREE" ${STUB_SQLITE:+-I$STUB_SQLITE} \
        tests/v2/make_v1_db.c "$V1_SOURCE_TREE/kfw-app/cases/kfw_db.c" kfw-app/integrity/kfw_integrity.c $LIB -o "$OUT/mkv1"
    rm -f "$OUT/v1.db"*; "$OUT/mkv1" "$OUT/v1.db" >/dev/null; V1="$OUT/v1.db"
else
    echo "(V1_SOURCE_TREE not set: migration tests skipped)"; V1=""
fi
gcc -std=gnu11 -w $INC tests/v2/test_v2.c $SRC $LIB -o "$OUT/v2"
"$OUT/v2" $V1 | tail -3
