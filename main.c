#include <stddef.h>
#include "db.h"
db_handle_t *g_db = NULL;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <pty.h>

#include "session.h"
#include "menu.h"
#include "wall.h"
#include "bulletins.h"
#include "state.h"
#include "login.h"
#include "termutil.h"
#include "config.h"

volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
    }
}

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

static void backspace_feedback(void) {
    write(STDOUT_FILENO, "\b \b", 3);
}

// Nur Datenleitung: client_fd <-> master_fd
static int filter_telnet_iac(unsigned char *buf, int len) {
    int i = 0, j = 0;
    while (i < len) {
        if (buf[i] == 255) {  // IAC
            if (i + 1 >= len) break;
            unsigned char cmd = buf[i+1];
            if (cmd == 250) {
                // SB: suche IAC SE
                i += 2;
                while (i + 1 < len) {
                    if (buf[i] == 255 && buf[i+1] == 240) { i += 2; break; }
                    i++;
                }
            } else if (cmd == 255) {
                // Escaped IAC (literal 255)
                buf[j++] = 255;
                i += 2;
            } else {
                // 3-Byte Kommando (WILL/WONT/DO/DONT)
                i += 3;
            }
        } else {
            buf[j++] = buf[i++];
        }
    }
    return j;
}

void run_relay(int client_fd, int master_fd) {
    fd_set readfds;
    unsigned char buffer[4096];

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        FD_SET(master_fd, &readfds);
        int max_fd = client_fd > master_fd ? client_fd : master_fd;

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("[relay] select");
            break;
        }

        // Client → PTY (IAC filtern)
        if (FD_ISSET(client_fd, &readfds)) {
            int n = read(client_fd, buffer, sizeof(buffer));
            if (n <= 0) {
                fprintf(stderr, "[relay] client getrennt\n");
                break;
            }
            n = filter_telnet_iac(buffer, n);
            if (n > 0) write(master_fd, buffer, n);
        }

        // PTY → Client
        if (FD_ISSET(master_fd, &readfds)) {
            int n = read(master_fd, buffer, sizeof(buffer));
            if (n <= 0) {
                fprintf(stderr, "[relay] master_fd geschlossen\n");
                break;
            }
            write(client_fd, buffer, n);
        }
    }
}

// Handler-Loop läuft auf PTY-Slave (STDIN/STDOUT bereits auf slave_fd)
void run_session_loop(void) {
    Session *current_user = malloc(sizeof(Session));
    if (!current_user) {
        fprintf(stderr, "[handler] malloc failed\n");
        exit(1);
    }
    current_user->callsign[0] = '\0';
    current_user->fd = -1;
    current_user->handler = handle_login;
    current_user->writing_message = 0;
    current_user->state_changed = 1;

    static char line_buf[256];
    static int line_pos = 0;

    while (running) {
        // state_changed VOR read() prüfen – kein Zeichen nötig um neuen State anzuzeigen
        while (current_user->state_changed && current_user->handler) {
            current_user->state_changed = 0;
            current_user->handler(current_user, "");
        }

        char c;
        int n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            fprintf(stderr, "[handler] read fehler oder EOF\n");
            break;
        }

        // \r ignorieren (Telnet sendet \r\n)
        if (c == '\r') continue;

        struct termios term;
        tcgetattr(STDIN_FILENO, &term);
        int is_canonical = (term.c_lflag & ICANON) != 0;

        if (is_canonical) {
            if (c == '\n') {
                line_buf[line_pos] = '\0';
                line_pos = 0;
                if (current_user->handler) {
                    current_user->handler(current_user, line_buf);
                }
            } else if (c == 0x7F || c == 0x08) {
                if (line_pos > 0) line_pos--;
            } else {
                if (line_pos < (int)sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        } else {
            char single[2] = {c, '\0'};
            if (current_user->handler) {
                current_user->handler(current_user, single);
            }
        }
    }

    free(current_user);
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_DFL);

    if (config_load(&g_config, CONFIG_PATH) != 0) {
        fprintf(stderr, "Warnung: Konnte Konfigurationsdatei %s nicht laden.\n", CONFIG_PATH);
    }

    g_db = db_init(g_config.db_path[0] ? g_config.db_path : "ham-bbs.sqlite3");
    if (!g_db) {
        fprintf(stderr, "Error: Could not open database!\n");
        exit(1);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_config.listen_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, 5) < 0) {
        perror("listen"); exit(1);
    }

    fprintf(stderr, "Telnet-Daemon lauscht auf Port %d...\n", g_config.listen_port);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running) perror("accept");
            continue;
        }
        fprintf(stderr, "[main] neue verbindung, client_fd=%d\n", client_fd);

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(client_fd); continue; }

        if (pid == 0) {  // Kindprozess pro Client
            close(server_fd);

            int master_fd, slave_fd;
            if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) < 0) {
                perror("openpty"); close(client_fd); exit(1);
            }

            // Telnet-Setup DIREKT auf client_fd, vor PTY/fork
            // stdout temporär auf client_fd umbiegen
            int saved_stdout = dup(STDOUT_FILENO);
            dup2(client_fd, STDOUT_FILENO);
            telnet_setup();
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);

            // Fork für Handler
            pid_t handler_pid = fork();
            if (handler_pid < 0) { perror("fork handler"); exit(1); }

            if (handler_pid == 0) {  // Handler-Prozess
                close(master_fd);
                close(client_fd);

                // Slave auf STDIN/STDOUT/STDERR
                dup2(slave_fd, STDIN_FILENO);
                dup2(slave_fd, STDOUT_FILENO);
                close(slave_fd);

                // Neue Session-Gruppe für PTY
                setsid();

                fprintf(stderr, "[handler] gestartet PID=%d\n", getpid());
                enable_raw_mode();
                fprintf(stderr, "[handler] raw mode gesetzt\n");

                run_session_loop();

                fprintf(stderr, "[handler] session loop beendet\n");
                exit(0);
            }

            // Relay-Prozess
            close(slave_fd);
            fprintf(stderr, "[relay] starte relay client_fd=%d master_fd=%d\n", client_fd, master_fd);
            run_relay(client_fd, master_fd);

            fprintf(stderr, "[relay] beendet, kill handler %d\n", handler_pid);
            kill(handler_pid, SIGTERM);
            waitpid(handler_pid, NULL, 0);
            close(master_fd);
            close(client_fd);
            exit(0);

        } else {
            close(client_fd);
        }
    }

    close(server_fd);
    db_close(g_db);
    return 0;
}
