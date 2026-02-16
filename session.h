#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>

typedef struct Session {
    char callsign[10];
    char input_buffer[256];
    int fd;
    void (*handler)(struct Session *s, char *input);
    int writing_message;
    int state_changed;
} Session;

#endif // SESSION_H
