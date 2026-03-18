#include "login.h"
#include "state.h"
#include <stdio.h>
#include <string.h>
#include "termutil.h"
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include "db.h"

extern db_handle_t *g_db;

void handle_login(Session *s, char *input) {
    static int esc_phase = 1;
    if (esc_phase) {
        // Warte auf ESC, um den Puffer zu synchronisieren
        if (strlen(input) == 0) {
            enable_raw_mode();
            printf("\nPlease press ESC to start login...\n");
            fflush(stdout);
            return;
        }
        // Prüfe, ob die Eingabe exakt ein ESC-Zeichen ist
        if (strlen(input) == 1 && (unsigned char)input[0] == 27) {
            esc_phase = 0;
            // Wechsel zu Canonical-Modus für callsign-Eingabe mit Echo und Backspace
            enable_canonical_mode();
            tcflush(STDIN_FILENO, TCIFLUSH);
            tcflush(STDOUT_FILENO, TCOFLUSH);
            printf("\nPlease enter callsign: ");
            fflush(stdout);
            return;
        } else {
            // Noch nicht ESC, weiter warten
            return;
        }
    }
    // Wenn noch kein Rufzeichen gesetzt ist, fordere zur Eingabe auf und schalte in den Kanonischen Modus
    if (strlen(s->callsign) == 0 && strlen(input) == 0) {
        // Prompt wurde schon oben ausgegeben
        printf("dfgdfgd");
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
        // Prüfe Rufzeichen: <=8 Zeichen, muss Buchstaben und mindestens eine Zahl enthalten
        int len = strlen(s->callsign);
        bool has_letter = false;
        bool has_digit = false;
        for (char *p = s->callsign; *p; ++p) {
            if (isalpha(*p)) has_letter = true;
            if (isdigit(*p)) has_digit = true;
        }
        if (len > 8 || !has_letter || !has_digit) {
            s->callsign[0] = '\0';
            printf("Invalid callsign. Must be <=8 characters, contain letters and at least one number.\n");
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
