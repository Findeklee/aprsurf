#include "termutil.h"
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_canonical_mode() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);

    // IAC DO LINEMODE (255 253 34)
    // → Server fordert Client auf, Zeilenpufferung zu aktivieren (Client puffert bis Enter)
    printf("%c%c%c", 255, 253, 34);

    // IAC SB LINEMODE MODE 0 IAC SE (255 250 34 1 0 255 240)
    // → Suboption: LINEMODE MODE = 0 = komplette Zeilenpufferung im Client
    printf("%c%c%c%c%c%c%c", 255, 250, 34, 1, 0, 255, 240);

    // IAC DONT ECHO (255 254 1)
    // → Server fordert Client auf, selbst zu echoen (da Client im Line-Mode das Echo übernimmt)
    printf("%c%c%c", 255, 254, 1);

    fflush(stdout);
    tcflush(STDIN_FILENO, TCIFLUSH);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // IAC WILL ECHO (255 251 1)
    // → Server übernimmt das Echo, Client soll kein lokales Echo mehr machen
    printf("%c%c%c", 255, 251, 1);

    // IAC WILL SUPPRESS-GO-AHEAD (255 251 3)
    // → Server unterdrückt Go-Ahead-Signale (nötig für Character-Mode)
    printf("%c%c%c", 255, 251, 3);

    // IAC DO SUPPRESS-GO-AHEAD (255 253 3)
    // → Server fordert Client auf, ebenfalls Go-Ahead zu unterdrücken
    printf("%c%c%c", 255, 253, 3);

    // IAC WONT LINEMODE (255 252 34)
    // → Server lehnt Zeilenpufferung ab → Client wechselt zu Character-Mode
    //   (jeder Tastendruck wird sofort gesendet, ohne auf Enter zu warten)
    printf("%c%c%c", 255, 252, 34);

    fflush(stdout);
}

void telnet_setup() {
    // IAC WILL ECHO (255 251 1)
    // → Server kündigt an, das Echo zu übernehmen
    printf("%c%c%c", 255, 251, 1);

    // IAC WILL SUPPRESS-GO-AHEAD (255 251 3)
    // → Server unterdrückt Go-Ahead (Standard für moderne Telnet-Verbindungen)
    printf("%c%c%c", 255, 251, 3);

    // IAC DO SUPPRESS-GO-AHEAD (255 253 3)
    // → Client soll ebenfalls Go-Ahead unterdrücken
    printf("%c%c%c", 255, 253, 3);

    // IAC WONT LINEMODE (255 252 34)
    // → Server lehnt Linemode ab → Client startet direkt im Character-Mode
    printf("%c%c%c", 255, 252, 34);

    fflush(stdout);
}

// Nur Datenleitung: client_fd <-> master_fd
int filter_telnet_iac(unsigned char *buf, int len) {
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
        } else if (buf[i] == '\r') {
            i++;
            // Nachfolgendes \n oder \0 konsumieren (NVT \r\n und \r\0)
            if (i < len && (buf[i] == '\n' || buf[i] == '\0')) {
                i++;
            }
            // Immer als \n ausgeben – auch nacktes \r (z.B. SyncTerm)
            buf[j++] = '\n';
        } else if (buf[i] == '\0') {
            // loses Null-Byte überspringen
            i++;
        } else {
            buf[j++] = buf[i++];
        }
    }
    return j;
}