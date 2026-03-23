#include "login.h"
#include "state.h"
#include <stdio.h>
#include <string.h>
#include "termutil.h"
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include "db.h"
#include <stdbool.h>
#include "config.h"

#define ESC_CHAR 27
#define MAX_CALLSIGN_LENGTH 8

static void show_intro_message(void) {
    printf("Welcome to APRSurf BBS!\n");
    printf("If you have an APRS-capable radio, send a message to %s with the\ntext \"HELP\" to get started.\n", g_config.bbs_callsign);
    fflush(stdout);
}

static void show_title_file(void) {
    const char *path = "/usr/local/share/aprsurf/aprsurf-title.txt";
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        fputs(buf, stdout);
    }
    fflush(stdout);
    fclose(f);
}

extern db_handle_t *g_db;

void handle_login(Session *s, char *input) {
    static bool esc_phase = true;
    static bool title_shown = false;
    if (esc_phase) {
        // Warte auf ESC, um den Puffer zu synchronisieren
        if (strlen(input) == 0) {
            enable_raw_mode();
            printf("\nPlease press ESC to start login...\n");
            fflush(stdout);
            return;
        }
        // Prüfe, ob die Eingabe exakt ein ESC-Zeichen ist
        if (strlen(input) == 1 && (unsigned char)input[0] == ESC_CHAR) {
            esc_phase = false;
            // Wechsel zu Canonical-Modus für callsign-Eingabe mit Echo und Backspace
            enable_canonical_mode();
            tcflush(STDIN_FILENO, TCIFLUSH);
            tcflush(STDOUT_FILENO, TCOFLUSH);
            if (!title_shown) {
                printf("\033[2J\033[H");
                show_title_file();
                show_intro_message();
                title_shown = true;
            }
            printf("\nPlease enter callsign (or \"n0call\" for read-only access): ");
            fflush(stdout);
            return;
        } else {
            // Noch nicht ESC, weiter warten
            return;
        }
    }
    // Wenn noch kein Rufzeichen gesetzt ist, fordere zur Eingabe auf und schalte in den Kanonischen Modus
    if (strlen(s->callsign) == 0 && strlen(input) == 0) {
        if (!title_shown) {
            show_title_file();
            title_shown = true;
        }
        printf("Please enter callsign: ");
        fflush(stdout);
        return;
    }
    // Wenn Eingabe vorhanden, direkt übernehmen (zentral gefiltert)
    if (strlen(input) > 0) {
        strncpy(s->callsign, input, sizeof(s->callsign)-1);
        s->callsign[sizeof(s->callsign)-1] = '\0';
        // Rufzeichen in Uppercase konvertieren
        for (char *p = s->callsign; *p; ++p) {
            *p = toupper(*p);
        }
        // Prüfe Rufzeichen: <=MAX_CALLSIGN_LENGTH Zeichen, muss Buchstaben und mindestens eine Zahl enthalten
        int len = strlen(s->callsign);
        bool has_letter = false;
        bool has_digit = false;
        for (char *p = s->callsign; *p; ++p) {
            if (isalpha(*p)) has_letter = true;
            if (isdigit(*p)) has_digit = true;
        }
        if (len > MAX_CALLSIGN_LENGTH || !has_letter || !has_digit) {
            s->callsign[0] = '\0';
            printf("Invalid callsign. Must be <=%d characters, contain letters and at least one number.\n", MAX_CALLSIGN_LENGTH);
            printf("Please enter callsign: ");
            fflush(stdout);
            return;
        }
        fflush(stdout);
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        fflush(stdout);
        // Wechsel zurück zu Raw-Mode für Menüs (sofortige Tastenreaktionen)
        enable_raw_mode();
        printf("\nWelcome, %s!\r\n", s->callsign);
        fflush(stdout);
        sleep(1); // Kurze Pause, damit der User die Willkommensnachricht sieht
        // Log the login
        if (g_db) db_add_lastlog(g_db, s->callsign);
        switch_to_menu(s);
        return;
    }
}
