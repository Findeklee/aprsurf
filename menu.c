#include "menu.h"
#include "state.h"
#include "bbsinfo.h"
#include "userinfo.h"
#include "config.h"
#include "bulletins.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static void show_menu_ascii(void) {
    const char *path = "/usr/local/share/aprsurf/aprsurf-menu.txt";
    FILE *f = fopen(path, "r");
    if (!f) {
        // show default menu if file not found
        printf("\nMain Menu\n");
        printf("=========\n");
        printf("w) Message Wall\n");
        printf("b) Read Bulletins!\n");
        printf("i) BBS Info\n");
        printf("s) System Info\n");
        printf("a) APRS Monitor\n");
        printf("q) Quit\n");
        printf("> "); fflush(stdout);
        return;
    } else {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            fputs(buf, stdout);
        }
        fflush(stdout);
        fclose(f);
    }   
}

void handle_menu(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        // Lokale Zeit anzeigen
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", local);
        printf("\033[34m  %s | Local Time: %s | Grid: %s\n", g_config.bbs_callsign, time_str, g_config.grid_locator);
        show_menu_ascii();
        return;
    }
    if (strncmp(input, "w", 1) == 0 && strlen(input) == 1) {
        switch_to_wall(s);
    } else if (strncmp(input, "b", 1) == 0 && strlen(input) == 1) {
        s->handler = handle_bulletins;
        s->state_changed = 1;
    } else if (strncmp(input, "i", 1) == 0 && strlen(input) == 1) {
        s->handler = handle_bbsinfo;
        s->state_changed = 1;
    } else if (strncmp(input, "s", 1) == 0 && strlen(input) == 1) {
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
