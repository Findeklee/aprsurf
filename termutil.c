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
    // Eingabepuffer leeren, damit keine alten Zeichen wie ^C erscheinen
    tcflush(STDIN_FILENO, TCIFLUSH);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void telnet_setup() {
    printf("%c%c%c", 255, 251, 1);
    printf("%c%c%c", 255, 251, 3);
    printf("%c%c%c", 255, 254, 34);
    fflush(stdout);
}
