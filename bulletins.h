#ifndef BULLETINS_H
#define BULLETINS_H

#include "session.h"
#include "db.h"

// Globaler DB-Handle (aus main.c)
extern db_handle_t *g_db;
void handle_bulletins(Session *s, char *input);

#endif // BULLETINS_H
