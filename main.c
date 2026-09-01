#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
// #define RUN_AS_DAEMON

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pty.h>

#include "termutil.h"
#include "db.h"
#include "session.h"
#include "menu.h"
#include "wall.h"
#include "bulletins.h"
#include "state.h"
#include "login.h"
#include "config.h"

db_handle_t *g_db = NULL;
volatile sig_atomic_t running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
    }
}

void write_pidfile(const char *pidfile_path) {
    FILE *f = fopen(pidfile_path, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

static int write_all(int fd, const unsigned char *buffer, size_t length) {
    while (length > 0) {
        ssize_t written = write(fd, buffer, length);
        if (written > 0) {
            buffer += written;
            length -= (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
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
            if (n > 0 && write_all(master_fd, buffer, (size_t)n) < 0) {
                perror("[relay] write master_fd");
                break;
            }
        }

        // PTY → Client
        if (FD_ISSET(master_fd, &readfds)) {
            int n = read(master_fd, buffer, sizeof(buffer));
            if (n <= 0) {
                fprintf(stderr, "[relay] master_fd geschlossen\n");
                break;
            }
            if (write_all(client_fd, buffer, (size_t)n) < 0) {
                perror("[relay] write client_fd");
                break;
            }
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

        // \r als Fallback zu \n normalisieren (z.B. wenn \r\n in zwei TCP-Paketen ankommt)
        if (c == '\r') c = '\n';

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
    // SA_RESTART NICHT setzen: blockierendes read() soll bei SIGTERM mit EINTR
    // abbrechen, damit der Handler-Prozess sauber endet.
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Ein verlorener Client darf den Relay-Prozess nicht vor dessen
    // Handler-Aufräumpfad beenden; write() liefert dann stattdessen EPIPE.
    struct sigaction ignore_sigpipe = {0};
    ignore_sigpipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sigpipe.sa_mask);
    sigaction(SIGPIPE, &ignore_sigpipe, NULL);

    signal(SIGCHLD, SIG_IGN);  // Kindprozesse automatisch reapen, keine Zombies

    if (config_load(&g_config, CONFIG_PATH) != 0) {
        fprintf(stderr, "Warnung: Konnte Konfigurationsdatei %s nicht laden.\n", CONFIG_PATH);
    }

    g_db = db_init(g_config.db_path[0] ? g_config.db_path : "ham-bbs.sqlite3");
    if (!g_db) {
        fprintf(stderr, "Error: Could not open database!\n");
        exit(1);
    }

    #ifdef RUN_AS_DAEMON
    if (daemon(0, 0) == -1) {
            perror("Daemonisierung fehlgeschlagen");
            exit(1);
        }
        write_pidfile("/var/run/ham-bbs.pid");
    #endif

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

            // Telnet-Setup direkt auf client_fd schreiben (nicht über stdout/PTY)
            // Muss vor dem Handler-Fork passieren, damit der Client sofort in Character-Mode wechselt
            {
                static const unsigned char telnet_init[] = {
                    255, 251, 1,   // IAC WILL ECHO       – Server übernimmt Echo
                    255, 251, 3,   // IAC WILL SGA        – kein Go-Ahead
                    255, 253, 3,   // IAC DO SGA          – Client soll ebenfalls SGA unterdrücken
                    255, 252, 34,  // IAC WONT LINEMODE   – Character-Mode (kein Zeilenpuffer im Client)
                };
                write(client_fd, telnet_init, sizeof(telnet_init));
            }

            // Fork für Handler
            pid_t handler_pid = fork();
            if (handler_pid < 0) { perror("fork handler"); exit(1); }

            if (handler_pid == 0) {  // Handler-Prozess
                close(master_fd);
                close(client_fd);

                 // Neue Session-Gruppe für PTY
                setsid();

                // Slave auf STDIN/STDOUT/STDERR
                ioctl(slave_fd, TIOCSCTTY, 0);
                dup2(slave_fd, STDIN_FILENO);
                dup2(slave_fd, STDOUT_FILENO);
                close(slave_fd);

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
