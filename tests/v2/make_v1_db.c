/* Builds a schema-v1 database using the ORIGINAL (0.1.0) database code, so the
 * migration test starts from a genuine old file.  Compiled against the
 * pre-v2 kfw_db.c only. */
#include "cases/kfw_db.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    kfw_db_t *db = kfw_db_open(argv[1]);
    if (!db) return 1;
    kfw_db_case_t c; memset(&c, 0, sizeof c);
    snprintf(c.title, sizeof c.title, "Legacy case");
    snprintf(c.examiner, sizeof c.examiner, "Old Examiner");
    if (kfw_db_case_insert(db, &c) != 0) return 1;
    kfw_db_evidence_t e; memset(&e, 0, sizeof e);
    snprintf(e.case_id, sizeof e.case_id, "%s", c.case_id);
    snprintf(e.orig_filename, sizeof e.orig_filename, "legacy.mp4");
    snprintf(e.sha256, sizeof e.sha256, "%s", "aa");
    if (kfw_db_evidence_insert(db, &e) != 0) return 1;
    printf("%s %d\n", c.case_id, kfw_db_schema_version(db));
    kfw_db_close(db);
    return 0;
}
