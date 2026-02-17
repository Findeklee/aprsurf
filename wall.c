#include "db.h"
#include <stdbool.h>
#include "wall.h"
#include "state.h"
#include "termutil.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define WALL_MAX_SHOW 10

// Callback für db_get_messages: Zeigt die letzten n Nachrichten
static int print_wall_message(const char *callsign, const char *msg, const char *timestamp, const char *source, void *userdata, char *src_call) {
    (void)src_call; // Unused parameter
    int *counter = (int*)userdata;
    if (*counter >= WALL_MAX_SHOW) return 1;
    printf("\033[0;34m%.16s|%s|\033[31m%7s\033[34m  |\n`-> \033[0m%.60s\n", timestamp, source, callsign, msg);
    (*counter)++;
    return 0;
}


void handle_wall(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        printf("\033[0m\033[1;34m----[\033[33mMessage Wall\033[34m]--------------------------------------------------------------\n");
        int shown = 0;
        if (g_db) {
            db_get_messages(g_db, print_wall_message, &shown, WALL_MAX_SHOW, NULL);
        }
        if (shown == 0) printf("(No messages on the wall yet)\n");
        printf("\033[0m\033[1;34m--------------------------------------------------------------------------------");
        printf("\n\033[34m(\033[33mw\033[34m) \033[37mWrite Message        ");
        printf("\033[34m(\033[33m\033[33mx\033[34m) \033[37mExit to menu\n");
        printf("> "); fflush(stdout);
        return;
    }
    if (s->writing_message) {
        s->writing_message = 0;
        enable_raw_mode();
        // Eingabe auf 60 Zeichen begrenzen
        char msg[61];
        strncpy(msg, input, 60);
        msg[60] = '\0';
        // ask for confirmation
        printf("\nYou entered:\n\"%s\"\n", msg);
        printf("Save to Wall? (y/n): "); fflush(stdout);
        char conf[2];
        if (fgets(conf, sizeof(conf), stdin) == NULL) {
            printf("Aborted.\n");
            sleep(1);
            switch_to_wall(s);
            return;
        }
        if (conf[0] != 'y' && conf[0] != 'Y') {
            printf("Aborted.\n");
            sleep(1);
            switch_to_wall(s);
            return;
        }
        // In DB speichern
        bool ok = false;
        if (g_db && s->callsign[0] && msg[0]) {
            ok = db_add_message(g_db, s->callsign, msg, "inet");
        }
        if (ok) {
            printf("Message saved to Wall!\n");
        } else {
            printf("Error saving message!\n");
        }
        sleep(1);
        // State auf Wall zurücksetzen, um neu zu laden
        switch_to_wall(s);
        return;
    }
    if (strncmp(input, "x", 1) == 0 && strlen(input) == 1) {
        switch_to_menu(s);
    } else if (strncmp(input, "w", 1) == 0 && strlen(input) == 1) {
        s->writing_message = 1;
        enable_canonical_mode();
        printf("\033[31m\nEnter message  \033[0m\n");
        printf(">---------------------(Max 60 chars)----------------------<\n");
        printf(">  ");
        fflush(stdout);
    } else if(strlen(input) > 0) {
        //printf("Wall: %.*s\n", 200, input);
        //printf("\nWall Menu:\n  x) Exit to menu\n  w) Write message\n> ");
    }
}
