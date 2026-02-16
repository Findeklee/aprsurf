#include "menu.h"
#include "state.h"
#include "bbsinfo.h"
#include "userinfo.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void handle_menu(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        // Lokale Zeit anzeigen
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", local);
        printf("Local Time: %s | Grid: %s\n", time_str, g_config.grid_locator);
        printf("\033[31mHam\033[37m\033[1;34mBBS\033[0m\n");
        printf("\nMain Menu\n");
        printf("=========\n");
        printf("m) Message Wall\n");
        printf("r) Read Bulletins! (Not yet)\n");
        printf("b) BBS Info\n");
        printf("u) User Info\n");
        printf("a) APRS Monitor\n");
        printf("q) Quit\n");
        printf("> "); fflush(stdout);
        return;
    }
    if (strncmp(input, "m", 1) == 0 && strlen(input) == 1) {
        switch_to_wall(s);
    } else if (strncmp(input, "b", 1) == 0 && strlen(input) == 1) {
        s->handler = handle_bbsinfo;
        s->state_changed = 1;
    } else if (strncmp(input, "u", 1) == 0 && strlen(input) == 1) {
        s->handler = handle_userinfo;
        s->state_changed = 1;
    } else if (strncmp(input, "a", 1) == 0 && strlen(input) == 1) {
        s->handler = handle_aprs_monitor;
        s->state_changed = 1;
    } else if(strncmp(input, "q", 1) == 0 && strlen(input) == 1) {
        printf("Quitting...\n");
        exit(0);
    } else if(strlen(input) > 0) {
        //printf("Menu: %.*s\n", 200, input);
    }
}
