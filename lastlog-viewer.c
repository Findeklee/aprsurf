#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

int main() {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    // Open database
    rc = sqlite3_open("/var/aprsurf/aprsurf.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // Prepare SQL statement
    const char *sql = "SELECT callsign, timestamp FROM lastlog ORDER BY id ASC;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // Execute and print results
    printf("Lastlog entries:\n");
    printf("----------------\n");
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *callsign = (const char *)sqlite3_column_text(stmt, 0);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 1);
        printf("Callsign: %s, Timestamp: %s\n", callsign, timestamp);
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db));
    }

    // Cleanup
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}