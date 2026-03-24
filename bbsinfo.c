#include "bbsinfo.h"
#include "state.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

void handle_bbsinfo(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        printf("\n System Info\n\n");
        printf(" DB:    %s \n APRS:  %s:%d \n Listen: %d \n Name:  %s \n Sysop: %s \n Grid:  %s\n",
        g_config.db_path, g_config.aprs_host, g_config.aprs_port, g_config.listen_port, g_config.bbs_name, g_config.sysop_callsign, g_config.grid_locator);
        printf("\033[34m(\033[33m\033[33mx\033[34m) \033[37mExit to menu\n");
        printf("> "); fflush(stdout);
        return;
    }
    if (strncmp(input, "x", 1) == 0 && strlen(input) == 1) {
        switch_to_menu(s);
    } else if(strlen(input) > 0) {
    }
}
