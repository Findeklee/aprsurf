#include "db.h"
#include <stdbool.h>
#include "wall.h"
#include "state.h"
#include "termutil.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#define WALL_MAX_SHOW 10
#define MSG_MAX_LEN 60

// Callback für db_get_messages: Zeigt die letzten n Nachrichten
static int print_wall_message(const char *callsign, const char *msg, const char *timestamp, const char *source, void *userdata, char *src_call) {
    (void)src_call; // Unused parameter
    int *counter = (int*)userdata;
    if (*counter >= WALL_MAX_SHOW) return 1;
    printf("\033[0;34m%.16s|%s|\033[31m%7s\033[34m  \n`-> \033[0m%.60s\n", timestamp, source, callsign, msg);
    (*counter)++;
    return 0;
}


void handle_wall(Session *s, char *input) {
    static char msg_buf[MSG_MAX_LEN + 2];
    static int msg_pos = 0;

    bool is_n0call = (s->callsign[0] && strcasecmp(s->callsign, "N0CALL") == 0);

    // state_changed-Callback oder Rückkehr zur Wall: Übersicht anzeigen
    if (strlen(input) == 0) {
        msg_pos = 0;
        msg_buf[0] = '\0';
        printf("\033[2J\033[H");
        printf("\033[0m\033[1;34m----[\033[33mMessage Wall\033[34m]-------------------------------------------------------------\n");
        int shown = 0;
        if (g_db) {
            db_get_messages(g_db, print_wall_message, &shown, WALL_MAX_SHOW, NULL);
        }
        if (shown == 0) printf("(No messages on the wall yet)\n");
        printf("\033[0m\033[1;34m-------------------------------------------------------------------------------");
        if (!is_n0call) {
            printf("\n\033[34m(\033[33mw\033[34m) \033[37mWrite Message        ");
        } else {
            printf("\n\033[34m \033[37mWrite disabled for this user ");
        }
        printf("\033[34m(\033[33m\033[33mx\033[34m) \033[37mExit to menu\n");
        printf("> ");
        fflush(stdout);
        return;
    }

    char c = input[0];

    // writing_message == 1: Nachricht eingeben (Raw-Mode, Zeichen für Zeichen)
    if (s->writing_message == 1) {
        if (c == '\n' || c == '\r') {
            // Enter: Bestätigung einholen
            msg_buf[msg_pos] = '\0';
            printf("\r\n");
            printf("\nYou entered:\n\"%s\"\n", msg_buf);
            printf("Save to Wall? (y/n): ");
            fflush(stdout);
            s->writing_message = 2;
            return;
        }
        if (c == 0x7F || c == 0x08) {
            if (msg_pos > 0) {
                msg_pos--;
                printf("\x08 \x08");
                fflush(stdout);
            }
            return;
        }
        if (isprint((unsigned char)c)) {
            if (msg_pos < MSG_MAX_LEN) {
                msg_buf[msg_pos++] = c;
                printf("%c", c); // Echo
                fflush(stdout);
            }
            return;
        }
        return;
    }

    // writing_message == 2: Bestätigung y/n (einzelne Taste)
    if (s->writing_message == 2) {
        s->writing_message = 0;
        if (c == 'y' || c == 'Y') {
            bool ok = false;
            if (g_db && s->callsign[0] && msg_buf[0]) {
                ok = db_add_message(g_db, s->callsign, msg_buf, "inet");
            }
            printf("%c\r\n", c); // Echo der Taste
            if (ok) {
                printf("Message saved to Wall!\n");
            } else {
                printf("Error saving message!\n");
            }
        } else {
            printf("%c\r\nAborted.\n", c); // Echo der Taste
        }
        fflush(stdout);
        sleep(1);
        switch_to_wall(s);
        return;
    }

    // Normaler Menü-Modus
    if (c == 'x') {
        switch_to_menu(s);
    } else if (c == 'w') {
        if (is_n0call) {
            printf("Write disabled for %s.\n", s->callsign);
            fflush(stdout);
            sleep(1);
            switch_to_wall(s);
            return;
        }
        msg_pos = 0;
        msg_buf[0] = '\0';
        s->writing_message = 1;
        printf("\033[31m\nEnter message  \033[0m\n");
        printf(">---------------------(Max 60 chars)----------------------<\n");
        printf(">  ");
        fflush(stdout);
    }
}
