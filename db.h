#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <stdbool.h>

// Opaque handle for DB context
struct db_handle;
typedef struct db_handle db_handle_t;

// Open (and create if not exists) the database, returns NULL on error
// db_path: path to SQLite file (e.g. from config)
db_handle_t *db_init(const char *db_path);

// Close and free the database handle
void db_close(db_handle_t *db);

// Add a message to the wall (returns true on success)
// source: z.B. "telnet" oder "aprs"
bool db_add_message(db_handle_t *db, const char *callsign, const char *msg, const char *source);

// Add any aprs message sent to my callsign to db
bool db_add_aprs_message(db_handle_t *db, const char *from_call, const char *content);

// Add a login entry to lastlog (returns true on success)
bool db_add_lastlog(db_handle_t *db, const char *callsign);

// Callback for iterating messages (for wall display etc.)
typedef int (*db_message_callback)(const char *callsign, const char *msg, const char *timestamp, const char *source, void *userdata, char *src_call);

// Iterate all messages (most recent first)
// Returns number of messages, or -1 on error
// char *src_call: who requested the messages? ignore for telnet users, only needed for APRS
int db_get_messages(db_handle_t *db, db_message_callback cb, void *userdata, int limit, char *src_call);

// Add a bulletin to the database (returns true on success)
bool db_add_bulletin(db_handle_t *db, const char *dest_call, const char *from_call, const char *content);    

int db_get_bulletins(db_handle_t *db, db_message_callback cb, void *userdata);

#endif // DB_H
