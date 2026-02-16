#include "userinfo.h"
#include "state.h"
#include <stdio.h>
#include <string.h>

void handle_userinfo(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        printf("\nUser Info\n");
        printf("Callsign: %s\n", s->callsign);
        printf("x) Exit to menu\n");
        printf("> "); fflush(stdout);
        return;
    }
    if (strncmp(input, "x", 1) == 0 && strlen(input) == 1) {
        switch_to_menu(s);
    } else if(strlen(input) > 0) {
        //printf("User Info: %.*s\n", 200, input);
    }
}
