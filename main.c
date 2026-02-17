#include <stddef.h>
#include "db.h"
// Globaler DB-Handle
db_handle_t *g_db = NULL;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#include "session.h"
#include "menu.h"
#include "wall.h"
#include "bulletins.h"
#include "state.h"
#include "login.h"
#include "termutil.h"
#include "config.h"

// Entfernt Backspace (0x08, 0x7F) und das jeweils davorstehende Zeichen
static void remove_backspaces(char *str) {
    int i = 0, j = 0;
    while (str[i]) {
        if (str[i] == 0x7F || str[i] == 0x08) {
            if (j > 0) j--;
        } else {
            str[j++] = str[i];
        }
        i++;
    }
    str[j] = '\0';
}

// Visuelles Backspace-Feedback auf Terminal
static void backspace_feedback(void) {
    write(STDOUT_FILENO, "\b \b", 3);
}

void run_mainloop(Session *current_user) {
    fd_set readfds;
    struct timeval timeout;
    char buffer[256];
    static char input_buffer[4096];
    size_t input_len = 0;

    while (1) {
        if (current_user->state_changed && current_user->handler) {
            current_user->handler(current_user, "");
            current_user->state_changed = 0;
        }
        
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
            int n = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (n <= 0) continue;

            // Im Raw-Mode: einzelne Zeichen sofort an Handler
            if (n == 1 && current_user->handler) {
                unsigned char c = (unsigned char)buffer[0];
                
                // ESC, druckbare Zeichen und Enter sofort verarbeiten
                if (c == 0x1B || (c >= 0x20 && c <= 0x7E) || c == '\n') {
                    char singlechar[2] = {(char)c, 0};
                    current_user->handler(current_user, singlechar);
                    continue;
                }
                // Backspace sofort verarbeiten
                if (c == 0x7F || c == 0x08) {
                    if (input_len > 0) {
                        input_len--;
                        backspace_feedback();
                    }
                    continue;
                }
            }
            
            // Mehrere Bytes oder Canonical-Modus: in Buffer einlesen
            for (int i = 0; i < n; ++i) {
                unsigned char c = (unsigned char)buffer[i];
                if (c == 0) continue;
                
                // Backspace verarbeiten
                if (c == 0x7F || c == 0x08) {
                    if (input_len > 0) {
                        input_len--;
                        backspace_feedback();
                    }
                    continue;
                }
                
                // Gültige Eingabezeichen puffern
                if ((c >= 0x20 && c <= 0x7E) || c == '\n' || c == 0x1B) {
                    if (input_len < sizeof(input_buffer) - 1) {
                        input_buffer[input_len++] = c;
                    }
                }
            }
            
            // Prüfe auf komplette Zeile (Canonical-Modus mit \n)
            char *newline = memchr(input_buffer, '\n', input_len);
            if (newline && current_user->handler) {
                size_t linelen = newline - input_buffer + 1;
                char line[4096];
                memcpy(line, input_buffer, linelen);
                line[linelen] = '\0';
                
                // ESC-Zeichen nicht durch remove_backspaces verarbeiten
                if (!(strlen(line) == 2 && (unsigned char)line[0] == 0x1B && line[1] == '\n')) {
                    remove_backspaces(line);
                }
                
                // Newline entfernen und an Handler übergeben
                size_t llen = strlen(line);
                if (llen > 0 && line[llen-1] == '\n') {
                    line[llen-1] = '\0';
                }
                current_user->handler(current_user, line);
                
                // State-Wechsel verarbeiten
                if (current_user->state_changed && current_user->handler) {
                    current_user->handler(current_user, "");
                    current_user->state_changed = 0;
                }
                
                // Eingabepuffer verwalten
                if (linelen < input_len) {
                    memmove(input_buffer, input_buffer + linelen, input_len - linelen);
                    input_len -= linelen;
                } else {
                    input_len = 0;
                }
            }
        }
    }
}

int main() {
    enable_raw_mode();
    telnet_setup();

    // Globale Konfiguration laden
    if (config_load(&g_config, CONFIG_PATH) != 0) {
        fprintf(stderr, "Warnung: Konnte Konfigurationsdatei %s nicht laden, benutze Defaultwerte.\n", CONFIG_PATH);
    }

    // Datenbank initialisieren
    g_db = db_init(g_config.db_path[0] ? g_config.db_path : "ham-bbs.sqlite3");
    if (!g_db) {
        fprintf(stderr, "Error: Could not open database!\n");
        exit(1);
    }

    Session *current_user = malloc(sizeof(Session));
    if (current_user == NULL) {
        perror("Failed to allocate memory for current_user");
        db_close(g_db);
        return 1;
    }
    current_user->callsign[0] = '\0';
    current_user->fd = -1;
    current_user->handler = NULL;
    current_user->writing_message = 0;
    current_user->state_changed = 0;
    // Starte mit Login-State
    current_user->handler = handle_login;
    current_user->state_changed = 1;
    run_mainloop(current_user);
    disable_raw_mode();
    free(current_user);
    db_close(g_db);
    return 0;
}
