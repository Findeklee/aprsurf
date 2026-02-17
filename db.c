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

int db_get_messages(db_handle_t *db, db_message_callback cb, void *userdata, int limit, char *src_call) {
    if (!db || !cb) return -1;
    if (limit <= 0) limit = 100; // Default limit
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT callsign, msg, timestamp, source FROM wall ORDER BY id DESC LIMIT %d;", limit);
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return -1;
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *callsign = (const char *)sqlite3_column_text(stmt, 0);
        const char *msg = (const char *)sqlite3_column_text(stmt, 1);
        const char *timestamp = (const char *)sqlite3_column_text(stmt, 2);
        const char *source = (const char *)sqlite3_column_text(stmt, 3);
        if (cb(callsign, msg, timestamp, source, userdata, src_call) != 0) break;
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

bool db_add_bulletin(db_handle_t *db, const char *dest_call, const char *from_call, const char *content) {
    if (!db || !dest_call || !from_call || !content) return false;

    // Tabelle anlegen, falls sie nicht existiert
    const char *sql_create =
        "CREATE TABLE IF NOT EXISTS bulletins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "dest_call TEXT NOT NULL,"
        "sender TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "received_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    char *err_msg = NULL;
    int rc = sqlite3_exec(db->db, sql_create, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler bei Init: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    // Bulletin speichern
    char *sql_insert = sqlite3_mprintf(
        "INSERT INTO bulletins (dest_call, sender, content, received_at) "
        "VALUES (%Q, %Q, %Q, DATETIME('now'));",
        dest_call, from_call, content
    );
    //rc = sqlite3_exec(db, sql_insert, 0, 0, &err_msg);
    rc = sqlite3_exec(db->db, sql_insert, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler beim Einfügen: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_free(sql_insert);
    return rc == SQLITE_DONE;
}

int db_get_bulletins(db_handle_t *db, db_message_callback cb, void *userdata) {
    if (!db || !cb) return -1;
    const char *sql = "SELECT dest_call, sender, content, received_at FROM bulletins ORDER BY id DESC;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return -1;
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *dest_call = (const char *)sqlite3_column_text(stmt, 0);
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *content = (const char *)sqlite3_column_text(stmt, 2);
        const char *received_at = (const char *)sqlite3_column_text(stmt, 3);
        if (cb(dest_call, sender, content, received_at, userdata, NULL) != 0) break;
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}