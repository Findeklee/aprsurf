#include "bulletins.h"
#include "state.h"
#include "db.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#define MAX_SHOW 10

static int print_bulletin(const char *dest_call, const char *sender, const char *content, const char *received_at, void *userdata, char *src_call) {
    (void)src_call; // Unused parameter
    int *counter = (int*)userdata;
    if (*counter >= MAX_SHOW) return 1;
    printf("\033[0;34m%.16s|%s|\033[31m%7s\033[34m  |\n`-> \033[0m%.60s\n", received_at, sender, dest_call, content);
    (*counter)++;
    return 0;
}

void handle_bulletins(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        printf("\nBulletins!\n");
        int shown = 0;
       
        db_get_bulletins(g_db, print_bulletin, &shown);
        if (shown == 0) printf("(No bulletins yet)\n");
        printf("x) Exit to menu\n");
        printf("> "); fflush(stdout);
        return;
    }
    if (strncmp(input, "x", 1) == 0 && strlen(input) == 1) {
        switch_to_menu(s);
    } else if(strlen(input) > 0) {
        //printf("BBS Info: %.*s\n", 200, input);
    }
}
