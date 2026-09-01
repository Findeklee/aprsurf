#include "config.h"
#include <stdio.h>
#include <sqlite3.h>

int main(void) {
    if (config_load(&g_config, CONFIG_PATH) < 0) {
        fprintf(stderr, "Warning: Could not load config from %s, using defaults.\n", CONFIG_PATH);
    }

    sqlite3 *db;
    if (sqlite3_open(g_config.db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database '%s': %s\n", g_config.db_path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    const char *sql =
        "SELECT id, from_call, content, received_at "
        "FROM aprs_messages ORDER BY id ASC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    printf("%-6s  %-10s  %-24s  %s\n", "ID", "From", "Received at", "Content");
    printf("------  ----------  ------------------------  --------------------------------------------\n");

    int rc;
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int    id          = sqlite3_column_int (stmt, 0);
        const char *from   = (const char *)sqlite3_column_text(stmt, 1);
        const char *content= (const char *)sqlite3_column_text(stmt, 2);
        const char *rcvd   = (const char *)sqlite3_column_text(stmt, 3);
        printf("%-6d  %-10s  %-24s  %.80s\n",
               id,
               from    ? from    : "(null)",
               rcvd    ? rcvd    : "(null)",
               content ? content : "(null)");
        count++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
    }

    if (count == 0) {
        printf("(No APRS messages in database)\n");
    } else {
        printf("------  ----------  ------------------------  --------------------------------------------\n");
        printf("%d message(s) total.\n", count);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}
