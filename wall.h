#include "db.h"

// Globaler DB-Handle (aus main.c)
extern db_handle_t *g_db;
#ifndef WALL_H
#define WALL_H

#include "session.h"

void handle_wall(Session *s, char *input);

#endif // WALL_H
