#include "aprs.h"
#include "menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>

// Helper to trim trailing spaces from callsign
void trim_callsign(char *call, size_t len) {
    for (int i = len - 1; i >= 0; --i) {
        if (call[i] != ' ') {
            call[i + 1] = '\0';
            return;
        }
    }
    call[0] = '\0';  // all spaces
}

// Function to connect to AGWPE server
int connect_to_agwpe(const char *host, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// Function to send registration frames
void send_registration(int sockfd) {
    // Frame template (36-byte header)
    unsigned char frame[36] = {0};

    // 1) 'R' login/register, DataLen=0
    memset(frame, 0, sizeof(frame));
    frame[0] = 0;             // Port
    frame[4] = 'R';           // DataKind at offset 4
    memcpy(&frame[5], "DN9RZ    ", 10);  // CallFrom
    memset(&frame[15], ' ', 10);          // CallTo spaces
    // DataLen bytes 28-31 = 0
    send(sockfd, frame, sizeof(frame), 0);

    // 2) 'G' (get parameters), DataLen=0
    memset(frame, 0, sizeof(frame));
    frame[0] = 0;             // Port
    frame[4] = 'G';
    memcpy(&frame[5], "DN9RZ    ", 10);
    memset(&frame[15], ' ', 10);
    send(sockfd, frame, sizeof(frame), 0);

    // 3) 'k' (request port info), DataLen=0
    memset(frame, 0, sizeof(frame));
    frame[0] = 0;             // Port
    frame[4] = 'k';
    memcpy(&frame[5], "DN9RZ    ", 10);
    memset(&frame[15], ' ', 10);
    send(sockfd, frame, sizeof(frame), 0);
}



// Function to receive and display APRS packets
void receive_packets(int sockfd) {
    unsigned char buffer[1024];
    int bytes_received;
    fd_set fds;
    struct timeval timeout;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);  
        FD_SET(sockfd, &fds);
        int maxfd = (STDIN_FILENO > sockfd ? STDIN_FILENO : sockfd) + 1;

        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;

        int result = select(maxfd, &fds, NULL, NULL, &timeout);

        if (result == 0) {
            continue; 
        }

        if (result > 0) {
            if (FD_ISSET(STDIN_FILENO, &fds)) break; // Taste gedrückt    
            if (FD_ISSET(sockfd, &fds)) {
                bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
                if (bytes_received <= 0) {
                    printf("Connection closed or error\n");
                    fflush(stdout);
                    break;
                }

                if (bytes_received >= 36) {  // Header is 36 bytes
                    // Parse fields by offset (Port at 0, DataKind at 4 per observation)
                    unsigned char data_kind = buffer[4];
                    unsigned int data_len = buffer[28] | (buffer[29] << 8) | (buffer[30] << 16) | (buffer[31] << 24);

                    // Compute available payload from actual recv size
                    unsigned int avail_len = (bytes_received > 36) ? (unsigned int)(bytes_received - 36) : 0;
                    if (data_len == 0 || data_len > avail_len) {
                        data_len = avail_len;  // fall back to what we actually got
                    }

                    if (data_kind == 'K' || data_kind == 'U') {
                        unsigned char *aprs_data = buffer + 36;  // Data follows header

                        // Try to locate AX.25 info start (0x03 0xF0)
                        unsigned int info_offset = 0;
                        for (unsigned int i = 0; i + 1 < data_len; ++i) {
                            if (aprs_data[i] == 0x03 && aprs_data[i + 1] == 0xF0) {
                                info_offset = i + 2;
                                break;
                            }
                        }

                        unsigned char *info = aprs_data + info_offset;
                        unsigned int info_len = (info_offset < data_len) ? (data_len - info_offset) : 0;

                        // Extract callsigns from header
                        char call_from[11];
                        char call_to[11];
                        memcpy(call_from, buffer + 8, 10);
                        call_from[10] = '\0';
                        trim_callsign(call_from, 10);
                        memcpy(call_to, buffer + 18, 10);
                        call_to[10] = '\0';
                        trim_callsign(call_to, 10);

                        // Zeit holen
                        time_t now = time(NULL);
                        struct tm *tm_info = localtime(&now);
                        char time_str[9];
                        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

                        printf("[%s] %s>%s:", time_str, call_from, call_to);
                        fflush(stdout);

                        for (unsigned int i = 0; i < info_len; ++i) {
                            if (isprint(info[i])) {
                                putchar(info[i]);
                            } else {
                                printf("\\x%02X", info[i]);
                                fflush(stdout);

                            }
                        }
                        printf("\n");
                        fflush(stdout);

                    }
                }
            }
        }
    }
    printf("Exiting APRS monitor...\n");
    fflush(stdout);
    return;
}

void handle_aprs_monitor(Session *s, char *input) {
    if (strlen(input) == 0) {
        // Bildschirm löschen (ANSI ESC [2J [H)
        printf("\033[2J\033[H");
        fflush(stdout);
        const char *host = "192.168.178.39";
        int port = 8000;

        int sockfd = connect_to_agwpe(host, port);
        if (sockfd < 0) {
            printf("Failed to connect to AGWPE server at %s:%d\n", host, port);
            fflush(stdout);
            sleep(2);
            goto exit;
        }

    printf("\nAPRS Monitor\n");
    printf("Connecting to TNC...\n");
    fflush(stdout);

    sleep(1);
    
    // Send registration
    send_registration(sockfd);

    printf("Connected to AGWPE server at %s:%d\n", host, port);
    printf("\nStarted monitoring APRS packets...\n(Press any key to quit monitor)\n\n");
    fflush(stdout);

    // Receive and display packets
    receive_packets(sockfd);
    fflush(stdout);

    exit:
    close(sockfd);

    tcflush(STDIN_FILENO, TCIFLUSH);
    s->handler = handle_menu;
    s->state_changed = 1;
    handle_menu(s, "");
    
    }

    
}
