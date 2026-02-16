#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct db_handle {
    sqlite3 *db;
};

static int db_ensure_schema(sqlite3 *db) {
    const char *sql = "CREATE TABLE IF NOT EXISTS wall (id INTEGER PRIMARY KEY AUTOINCREMENT, callsign TEXT NOT NULL, msg TEXT NOT NULL, timestamp TEXT NOT NULL, source TEXT NOT NULL); CREATE TABLE IF NOT EXISTS lastlog (id INTEGER PRIMARY KEY AUTOINCREMENT, callsign TEXT NOT NULL, timestamp TEXT NOT NULL);";
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DB schema error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 0;
    }
    return 1;
}

db_handle_t *db_init(const char *db_path) {
    db_handle_t *h = calloc(1, sizeof(db_handle_t));
    if (!h) return NULL;
    if (sqlite3_open(db_path, &h->db) != SQLITE_OK) {
        free(h);
        return NULL;
    }
    if (!db_ensure_schema(h->db)) {
        sqlite3_close(h->db);
        free(h);
        return NULL;
    }
    return h;
}

void db_close(db_handle_t *db) {
    if (!db) return;
    if (db->db) sqlite3_close(db->db);
    free(db);
}

bool db_add_message(db_handle_t *db, const char *callsign, const char *msg, const char *source) {
    if (!db || !callsign || !msg || !source) return false;
    const char *sql = "INSERT INTO wall (callsign, msg, timestamp, source) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, callsign, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, msg, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tbuf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, source, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int db_get_messages(db_handle_t *db, db_message_callback cb, void *userdata) {
    if (!db || !cb) return -1;
    const char *sql = "SELECT callsign, msg, timestamp, source FROM wall ORDER BY id DESC;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return -1;
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *callsign = (const char *)sqlite3_column_text(stmt, 0);
        const char *msg = (const char *)sqlite3_column_text(stmt, 1);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 2);
        const char *source = (const char *)sqlite3_column_text(stmt, 3);
        if (cb(callsign, msg, timestamp, source, userdata) != 0) break;
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

bool db_add_lastlog(db_handle_t *db, const char *callsign) {
    if (!db || !callsign) return false;
    const char *sql = "INSERT INTO lastlog (callsign, timestamp) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;
    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, callsign, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tbuf, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
