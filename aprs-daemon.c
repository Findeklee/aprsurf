/** Prototype of aprs daemon */
// #define RUN_AS_DAEMON
 #define DEBUG

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/select.h>
#include <signal.h>

#include "config.h"
#include "db.h"

#define KISS_PORT 8001
#define BUF_SIZE 1024

#define APRS_DEST_CALL "APZS01" 

volatile sig_atomic_t running = 1;

// Struktur für ausgehende Nachrichten in der Queue
typedef struct OutgoingMessage {
    char dest_call[10];      // Ziel-Call (z. B. "DB0ABC-1")
    char text[256];          // Nachrichtentext
    char msg_id[10];         // Eindeutige ID (z. B. "123")
    time_t sent_time;        // Zeitpunkt des letzten Sends
    int retry_count;         // Anzahl der Versuche (max. 3)
    struct OutgoingMessage *next;  // Pointer zum nächsten Element
} OutgoingMessage;

// Globale Queue (Head der verketteten Liste)
OutgoingMessage *message_queue = NULL;

time_t last_queue_process = 0;
time_t last_beacon_time = 0;

// Globaler DB-Handle
db_handle_t *g_db = NULL;

void generate_beacon_text(char *buffer, size_t buf_size) {
    snprintf(buffer, buf_size, "!%02d%05.2fN/%03d%05.2fEB%s", 
             (int)g_config.gps_lat, (g_config.gps_lat - (int)g_config.gps_lat) * 60,
             (int)g_config.gps_lon, (g_config.gps_lon - (int)g_config.gps_lon) * 60,
             g_config.beacon_text);
}

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        running = 0;
        unlink("/var/run/aprs-daemon.pid");
        fprintf(stderr, "Signal empfangen, beende APRS Daemon...\n");
    }
}   

void write_pidfile(const char *pidfile_path) {
    FILE *f = fopen(pidfile_path, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

// removes KISS endbyte 0xC0 from text and replaces it with \0
void make_clean_text(unsigned char *input, char *output) {
    int j = 0;
    for (int i = 0; input[i] != '\0' && j < 255; i++) {
        if ((unsigned char)input[i] == 0xC0) {
            output[j++] = '\0';
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

/**
 * Kodiert ein Rufzeichen im AX.25-Format.
 * @param out_buf  Ein Buffer von mindestens 7 Bytes.
 * @param call     Das Rufzeichen als String (z.B. "DN9RZ-10" oder "DB0KX")
 * @param is_last  1, wenn dies die letzte Adresse im Header ist (setzt Bit 0), sonst 0.
 */
void encode_ax25_call(unsigned char *out_buf, const char *call, int is_last) {
    char call_part[7] = "      "; // 6 Leerzeichen
    int ssid = 0;
    
    // 1. String zerlegen (Call und SSID trennen)
    char temp[12];
    strncpy(temp, call, 11);
    temp[11] = '\0';
    
    char *dash = strchr(temp, '-');
    if (dash) {
        *dash = '\0';
        ssid = atoi(dash + 1);
    }
    
    // 2. Rufzeichen-Teil kopieren und in Großbuchstaben wandeln
    for (int i = 0; i < 6 && temp[i] != '\0'; i++) {
        call_part[i] = toupper((unsigned char)temp[i]);
    }
    
    // 3. In den Buffer shiften (7 Bytes)
    for (int i = 0; i < 6; i++) {
        out_buf[i] = (unsigned char)(call_part[i] << 1);
    }
    
    // 4. SSID Byte (Bit 1-4 = SSID, Bit 0 = Extension Bit, Rest meist 01100000)
    // 0x60 ist der Standard-Offset (01100000 in Binär)
    out_buf[6] = (unsigned char)((ssid << 1) | 0x60 | (is_last ? 0x01 : 0x00));
}

/// @brief Sends an APRS message, optionally with a custom message ID for acknowledgments.
/// @param sock Socket descriptor for sending the message.
/// @param dest_call Destination callsign (e.g., "DB0ABC-1").
/// @param text The message text to send.
/// @param custom_id Optional custom message ID for acknowledgment tracking.
void send_aprs_message(int sock, const char *dest_call, const char *text, const char *custom_id) {
    unsigned char frame[256];
    int p = 0;

    // 1. KISS Start
    frame[p++] = 0xC0; 
    frame[p++] = 0x00; 

    // 2. ZIEL (User-Call)
    unsigned char target[6] = {' ',' ',' ',' ',' ',' '};
    int target_ssid = 0;
    for (int i = 0; i < 6 && dest_call[i] != '\0' && dest_call[i] != '-'; i++) {
        target[i] = dest_call[i];
    }
    char *dash = strchr(dest_call, '-');
    if (dash) target_ssid = atoi(dash + 1);
    for (int i = 0; i < 6; i++) frame[p++] = (target[i] << 1);
    frame[p++] = (target_ssid << 1) | 0x60; 

    // 3. ABSENDER (Deine BBS: DN9RZ-10)
    encode_ax25_call(&frame[p], g_config.bbs_callsign, 0); // Absender
    p += 7;
    
    // 4. PFAD (WIDE1-1, WIDE2-1)
    unsigned char p1[6] = {'W','I','D','E','1',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p1[i] << 1);
    frame[p++] = (1 << 1) | 0x60; 

    unsigned char p2[6] = {'W','I','D','E','2',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p2[i] << 1);
    frame[p++] = (1 << 1) | 0x61; // Ende Adressliste

    // 5. AX.25 Control & PID
    frame[p++] = 0x03; 
    frame[p++] = 0xF0; 

    // 6. Payload zusammenbauen
    char payload[100];
    if (custom_id && strlen(custom_id) > 0) {
        // Nachricht mit ID für Ack-Anforderung
        sprintf(payload, ":%-9s:%s{%s}", dest_call, text, custom_id);
    } else {
        // Einfache Nachricht ohne ID (Fire and Forget)
        sprintf(payload, ":%-9s:%s", dest_call, text);
    }
    
    memcpy(&frame[p], payload, strlen(payload));
    p += strlen(payload);

    // 7. KISS Ende
    frame[p++] = 0xC0; 

    send(sock, frame, p, 0);
}

void send_aprs_beacon(int sock, const char *dest_call, const char *text) {
    unsigned char frame[256];
    int p = 0;

    // 1. KISS Start
    frame[p++] = 0xC0; 
    frame[p++] = 0x00; 

    // 2. ZIEL (User-Call)
    unsigned char target[6] = {' ',' ',' ',' ',' ',' '};
    int target_ssid = 0;
    for (int i = 0; i < 6 && dest_call[i] != '\0' && dest_call[i] != '-'; i++) {
        target[i] = dest_call[i];
    }
    char *dash = strchr(dest_call, '-');
    if (dash) target_ssid = atoi(dash + 1);
    for (int i = 0; i < 6; i++) frame[p++] = (target[i] << 1);
    frame[p++] = (target_ssid << 1) | 0x60; 

    // 3. ABSENDER (Deine BBS: DN9RZ-10)
    encode_ax25_call(&frame[p], g_config.bbs_callsign, 0); // Absender
    p += 7;
    
    // 4. PFAD (WIDE1-1, WIDE2-1)
    unsigned char p1[6] = {'W','I','D','E','1',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p1[i] << 1);
    frame[p++] = (1 << 1) | 0x60; 

    unsigned char p2[6] = {'W','I','D','E','2',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p2[i] << 1);
    frame[p++] = (1 << 1) | 0x61; // Ende Adressliste

    // 5. AX.25 Control & PID
    frame[p++] = 0x03; 
    frame[p++] = 0xF0; 

    // 6. Payload zusammenbauen
    char payload[100];
    sprintf(payload, "%s", text);
    
    memcpy(&frame[p], payload, strlen(payload));
    p += strlen(payload);

    // 7. KISS Ende
    frame[p++] = 0xC0; 

    send(sock, frame, p, 0);
}

/**
 * Extrahiert die Message-ID aus einer APRS-Nachricht.
 * Beispiel: "Hallo Welt{42" -> extrahiert "42"
 * * @param message_ptr Pointer auf den Textanfang (nach dem 2. Doppelpunkt)
 * @param id_output   Buffer, in dem die ID gespeichert wird (sollte ca. 10 Bytes groß sein)
 * @return 1 wenn eine ID gefunden wurde, 0 wenn nicht.
 */
int extract_msg_id(const char *message_ptr, char *id_output) {
    char *bracket = strchr(message_ptr, '{');
    
    if (bracket != NULL) {
        int i = 0;
        char *start = bracket + 1;
        
        // Lies nur so lange, wie es alphanumerische Zeichen sind
        // und maximal 5 Zeichen (APRS Standard für IDs)
        while (isalnum(start[i]) && i < 5) {
            id_output[i] = start[i];
            i++;
        }
        id_output[i] = '\0'; 
        return (i > 0); // Erfolg, wenn mindestens ein Zeichen gefunden wurde
    }
    
    return 0;
}

void send_aprs_ack_with_path(int sock, const char *sender_call, const char *msg_id) {
    unsigned char frame[150];
    int p = 0;

    // 1. KISS Start
    frame[p++] = 0xC0; 
    frame[p++] = 0x00; 

    // 2. ZIEL (Der Absender der msg)
    unsigned char target[6] = {' ',' ',' ',' ',' ',' '};
    int target_ssid = 0;
    // Manuelles Kopieren ohne Null-Byte Risiko
    for (int i = 0; i < 6 && sender_call[i] != '\0' && sender_call[i] != '-'; i++) {
        target[i] = sender_call[i];
    }
    char *dash = strchr(sender_call, '-');
    if (dash) target_ssid = atoi(dash + 1);

    for (int i = 0; i < 6; i++) frame[p++] = (target[i] << 1);
    frame[p++] = (target_ssid << 1) | 0x60; 

    // 3. ABSENDER (Deine BBS: DN9RZ-10)
    encode_ax25_call(&frame[p], g_config.bbs_callsign, 0); // Absender
    p += 7;

    // 4. PFAD 1: WIDE1-1
    unsigned char p1[6] = {'W','I','D','E','1',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p1[i] << 1);
    frame[p++] = (1 << 1) | 0x60; 

    // 5. PFAD 2: WIDE2-1
    unsigned char p2[6] = {'W','I','D','E','2',' '};
    for (int i = 0; i < 6; i++) frame[p++] = (p2[i] << 1);
    frame[p++] = (1 << 1) | 0x61; // 0x61 markiert das ENDE der Adressliste

    // 6. AX.25 Control & PID
    frame[p++] = 0x03; 
    frame[p++] = 0xF0; 

    // 7. APRS Payload
    char payload[50];
    // Hier ist das %-9s okay, da es nur ein String in der Payload ist
    sprintf(payload, ":%-9s:ack%s", sender_call, msg_id);
    memcpy(&frame[p], payload, strlen(payload));
    p += strlen(payload);

    // 8. KISS Ende
    frame[p++] = 0xC0; 

    send(sock, frame, p, 0);
    #ifdef DEBUG
    fprintf(stderr, "Ack (Pfad) gesendet für ID %s an %s\n", msg_id, sender_call);
    #endif
}

void send_aprs_ack(int sock, const char *sender_call, const char *msg_id) {
    unsigned char frame[100];
    int p = 0;
    char call_clean[7];
    int ssid = 0;

    // 1. KISS Start
    frame[p++] = 0xC0; // FEND
    frame[p++] = 0x00; // Data on Port 0

    // 2. AX.25 Header: ZIEL (Der, dem wir das Ack schicken)
    // Wir initialisieren mit Leerzeichen (0x20)
    memset(call_clean, ' ', 6);
    sscanf(sender_call, "%6[^ -]-%d", call_clean, &ssid);
    
    // Sicherstellen, dass nach dem Call bis Stelle 6 Leerzeichen stehen (kein \0)
    for (int i = 0; i < 6; i++) {
        if (call_clean[i] == '\0') call_clean[i] = ' ';
    }
    for (int i = 0; i < 6; i++) frame[p++] = (call_clean[i] << 1);
    frame[p++] = (ssid << 1) | 0x60; 

    // 3. AX.25 Header: ABSENDER (Deine BBS: DN9RZ-10)
    // "DN9RZ" + 1 Leerzeichen = 6 Zeichen
    encode_ax25_call(&frame[p], g_config.bbs_callsign, 1); // Absender
    p += 7;

    // 4. AX.25 Control & PID
    frame[p++] = 0x03; // UI-frame
    frame[p++] = 0xF0; // No layer 3

    // 5. APRS Payload (Das Ack)
    // Das Ziel-Call im Textfeld muss exakt 9 Zeichen lang sein
    char payload[40];
    
    // Hilfs-Variable um den nackten Call ohne SSID für das 9-er Feld zu haben
    char call_only[7];
    memset(call_only, 0, 7);
    sscanf(sender_call, "%6[^ -]", call_only);

    // Format: :CALL-SSID:ackID
    // %-9s füllt automatisch mit Leerzeichen auf 9 Stellen auf
    sprintf(payload, ":%-9s:ack%s", sender_call, msg_id);
    
    memcpy(&frame[p], payload, strlen(payload));
    p += strlen(payload);

    // 6. KISS Ende
    frame[p++] = 0xC0; // FEND

    // Senden
    if (send(sock, frame, p, 0) < 0) {
        perror("Senden fehlgeschlagen");
    } else {
        #ifdef DEBUG
        fprintf(stderr, "Ack erfolgreich an Direwolf übergeben für: %s\n", sender_call);
        #endif
    }
}

// Diese Funktion dekodiert ein AX.25 Rufzeichen
void decode_callsign(unsigned char *buf, char *call) {
    int i;
    for (i = 0; i < 6; i++) {
        call[i] = (buf[i] >> 1) & 0x7F; // Bit-Shift und Maskierung
    }
    call[6] = '\0';

    // Leerzeichen am Ende abschneiden
    for (i = 5; i >= 0 && (call[i] == ' ' || call[i] == 0); i--) {
        call[i] = '\0';
    }

    // SSID extrahieren
    int ssid = (buf[6] >> 1) & 0x0F;
    if (ssid > 0) {
        sprintf(call + strlen(call), "-%d", ssid);
    }
}

unsigned char *find_payload(unsigned char *ax25_start, int total_len) {
    // Wir starten nach Ziel (7 Bytes) und Absender (7 Bytes)
    int offset = 14; 

    // 1. Digipeater überspringen (falls vorhanden)
    // Wir prüfen das letzte Bit (Bit 0) des SSID-Bytes. 
    // Ist es 0, folgt noch eine Adresse. Ist es 1, war das die letzte Adresse.
    while (offset < total_len) {
        if (ax25_start[offset - 1] & 0x01) { 
            break; // Ende der Adressliste gefunden
        }
        offset += 7; // Nächsten Digi-Block überspringen
    }

    // 2. Control Field (0x03) und PID (0xF0) überspringen
    // Diese zwei Bytes kommen direkt nach der Adressliste
    if (ax25_start[offset] == 0x03 && ax25_start[offset + 1] == 0xF0) {
        return &ax25_start[offset + 2];
    }

    return NULL; // Falls kein Standard-UI-Frame
}

// Funktion: Neue Nachricht zur Queue hinzufügen
void add_to_queue(const char *dest_call, const char *text) {
    // Generiere eine eindeutige ID (Zahl + Kleinbuchstabe, z.B. "1l")
    static int id_counter = 1;
    char msg_id[10];
    char letters[] = "abcdefghijklmnopqrstuvwxyz";
    int letter_index = (id_counter - 1) % 26;  // Zyklisch durch Buchstaben
    sprintf(msg_id, "%d%c", id_counter++, letters[letter_index]);

    OutgoingMessage *new_msg = malloc(sizeof(OutgoingMessage));
    if (!new_msg) return;  // Fehlerbehandlung

    strcpy(new_msg->dest_call, dest_call);
    strcpy(new_msg->text, text);
    strcpy(new_msg->msg_id, msg_id);
    new_msg->sent_time = 0;  // Noch nicht gesendet
    new_msg->retry_count = 0;
    new_msg->next = NULL;

    // An Queue anhängen (einfach ans Ende)
    if (!message_queue) {
        message_queue = new_msg;
    } else {
        OutgoingMessage *current = message_queue;
        while (current->next) current = current->next;
        current->next = new_msg;
    }

    #ifdef DEBUG
    fprintf(stderr, "Nachricht zur Queue hinzugefügt: %s -> %s (ID: %s)\n", dest_call, text, msg_id);
    #endif
}

// Funktion: Nachricht aus Queue entfernen (nach ACK)
void remove_from_queue(const char *msg_id) {
    OutgoingMessage *current = message_queue;
    OutgoingMessage *prev = NULL;

    while (current) {
        if (strcmp(current->msg_id, msg_id) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                message_queue = current->next;
            }
            free(current);
            #ifdef DEBUG
            fprintf(stderr, "Nachricht mit ID %s aus Queue entfernt.\n", msg_id);
            #endif
            return;
        }
        prev = current;
        current = current->next;
    }
}

// Funktion: Queue verarbeiten (senden und Timeouts handhaben)
void process_queue(int sock) {
    time_t now = time(NULL);
    OutgoingMessage *current = message_queue;
    int sent_one = 0;  // Flag, um nur eine Nachricht pro Aufruf zu senden

    while (current && !sent_one) {
        if (current->sent_time == 0 || (now - current->sent_time) > 5) {  // 5 Sek Timeout
            if (current->retry_count < 4) {  // Max 3 Retries (0,1,2,3) 
                send_aprs_message(sock, current->dest_call, current->text, current->msg_id);
                current->sent_time = now;
                current->retry_count++;
                fprintf(stderr, "Nachricht gesendet (Retry %d): %s\n", current->retry_count, current->msg_id);
                sent_one = 1;  // Nur eine senden
            } else {
                // Max Retries erreicht – entfernen oder loggen
                #ifdef DEBUG
                fprintf(stderr, "Max Retries für ID %s erreicht - entferne aus Queue.\n", current->msg_id);
                #endif
                remove_from_queue(current->msg_id);
                // current ist jetzt ungültig, also neu starten
                current = message_queue;
                continue;
            }
        }
        if (!sent_one) {
            current = current->next;
        }
    }
}

int queue_wall_messages(const char *callsign, const char *msg, const char *timestamp, const char *source, void *userdata, char *dest_call) {
    char response[256];
    (void)timestamp;
    (void)source;
    (void)userdata;
    sprintf(response, "%s:%s", callsign, msg); // Kürzeres Format für APRS
    add_to_queue(dest_call, response);
    return 0; // Weiter mit nächsten Nachrichten
}

void handle_wall_command(char *src_call) {
    int shown = 0;
    db_get_messages(g_db, queue_wall_messages, &shown, 3, src_call);
}

int main() {
    char tasks[10][20]; // Array für bis zu 10 Task-IDs
    int task_index = 0;

    int sock;
    struct sockaddr_in server_addr;
    unsigned char buffer[BUF_SIZE];

    char beacon_text[100];

#ifdef RUN_AS_DAEMON
   if (daemon(0, 0) == -1) {
        perror("Daemonisierung fehlgeschlagen");
        exit(1);
    }
    write_pidfile("/var/run/aprs-daemon.pid");
#endif

    // Signal-Handler für sauberes Beenden registrieren
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Globale Konfiguration laden
    fprintf(stderr, "Lade Konfiguration...%s\n", CONFIG_PATH);
    if (config_load(&g_config, CONFIG_PATH) != 0) {
        fprintf(stderr, "Warnung: Konnte Konfigurationsdatei %s nicht laden, benutze Defaultwerte.\n", CONFIG_PATH);
    }

    // Datenbank initialisieren
    g_db = db_init(g_config.db_path[0] ? g_config.db_path : "ham-bbs.sqlite3");
    if (!g_db) {
        fprintf(stderr, "Error: Could not open database!\n");
        exit(1);
    }

    // 1. Socket erstellen
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket-Fehler");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(KISS_PORT);
    inet_pton(AF_INET, g_config.aprs_host, &server_addr.sin_addr);

    // 2. Verbindung zu Direwolf aufbauen
    fprintf(stderr, "Verbinde zu Direwolf an %s:%d...\n", g_config.aprs_host, KISS_PORT);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Verbindung fehlgeschlagen. Läuft Direwolf?");
        return 1;
    }

    fprintf(stderr, "Verbunden mit KISS an %s:%d. Warte auf Pakete...\n", g_config.aprs_host, KISS_PORT);

    // send_aprs_beacon(sock, "APX220", "!5115.52N/00622.51EBTesting APRS Daemon/Linux. Msg HELP to get started.");
    if (g_config.beacon_interval != 0){
        generate_beacon_text(beacon_text, sizeof(beacon_text));
        send_aprs_beacon(sock, APRS_DEST_CALL, beacon_text);
        last_beacon_time = time(NULL);
    }
    // 3. Empfangsschleife
    while (running) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 1;   // 1 Sekunde Timeout
        tv.tv_usec = 0;

        int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (ret > 0 && FD_ISSET(sock, &readfds)) {
            // Daten am Socket verfügbar
            memset(buffer, 0, BUF_SIZE);
            ssize_t bytes_received = recv(sock, buffer, BUF_SIZE, 0);
            if (bytes_received <= 0) {
                fprintf(stderr, "Verbindung verloren.\n");
                break;
            }

            char src_call[10];
            char dst_call[10];
            decode_callsign(&buffer[9], src_call); 
            #ifdef DEBUG
            fprintf(stderr, "----------------------------------\n");
            fprintf(stderr, "Absender: %s\n", src_call);
            #endif
            decode_callsign(&buffer[2], dst_call); 
            #ifdef DEBUG
            fprintf(stderr, "Empfaenger: %s\n", dst_call);
            #endif

            unsigned char *ax25_start = &buffer[2]; // AX.25 beginnt nach dem KISS-Header
            unsigned char *payload_ptr = find_payload(ax25_start, bytes_received);
            if (payload_ptr) {
                // Länge der Payload berechnen (bis zum nächsten 0xC0 oder Buffer-Ende)
                int payload_len = 0;
                for (int i = 0; payload_ptr + i < buffer + bytes_received; i++) {
                    if (payload_ptr[i] == 0xC0) break;  // KISS-Ende
                    payload_len++;
                }
                
                // Drucke die Payload ohne binary Bytes
                #ifdef DEBUG
                fprintf(stderr, "Payload: %.*s\n", payload_len, payload_ptr);
                #endif
            } else {
                #ifdef DEBUG
                fprintf(stderr, "Payload: (keine gefunden)\n");
                #endif
            }

            // Prüfen, ob die Nachricht an uns gerichtet ist (DN9RZ-10)
            char real_dest[10];
            strncpy(real_dest, (char*)&payload_ptr[1], 9);
            real_dest[9] = '\0';

            // Bulletin-Erkennung
            int is_bulletin = (strncmp(real_dest, "BLN", 3) == 0);

            if (is_bulletin) {
                #ifdef DEBUG
                fprintf(stderr, "Bulletin erkannt: %s\n", real_dest);
                #endif
                //save_bulletin_to_db(real_dest, src_call, (char*)payload_ptr);
                char bulletin_clean_text[256] = {0};
                make_clean_text(payload_ptr, (char*)&bulletin_clean_text);
                //db_add_bulletin(g_db, real_dest, src_call, (char*)payload_ptr);
                db_add_bulletin(g_db, real_dest, src_call, (char*)bulletin_clean_text);
                // continue; // Bulletins werden nicht weiter verarbeitet
                goto msgloop_end; // Direkt zum Ende der Nachrichtenschleife springen, da Bulletins nicht weiter verarbeitet werden 
            }

            // pad callsign to 9 characters with spaces for comparison 
            char padded_space_callsign[10];
            memset(padded_space_callsign, ' ', 10);
            padded_space_callsign[9] = '\0'; // Null-Byte am Ende
            strncpy(padded_space_callsign, g_config.bbs_callsign, strlen(g_config.bbs_callsign));
            int is_message_for_me = (strcmp(real_dest, padded_space_callsign) == 0);
            if (is_message_for_me) {
                char aprs_clean_text[256] = {0};
                make_clean_text(payload_ptr, (char*)&aprs_clean_text);
                db_add_aprs_message(g_db, src_call, (char*)aprs_clean_text); 
                #ifdef DEBUG
                fprintf(stderr, "Diese Nachricht ist für mich bestimmt!\n");
                #endif
            } else {
                #ifdef DEBUG
                fprintf(stderr, "Diese Nachricht ist NICHT für mich bestimmt.\n");
                #endif
                goto msgloop_end; // Direkt zum Ende der Nachrichtenschleife springen, da Nachrichten, die nicht für uns sind, nicht weiter verarbeitet werden
            }

            // Prüfe, ob es ein ACK für eine ausgehende Nachricht ist
            char expected_ack_prefix[20];
            sprintf(expected_ack_prefix, ":%s :ack", g_config.bbs_callsign);  // z. B. ":DN9RZ-10 :ack"
            if (payload_ptr && strncmp((char*)payload_ptr, expected_ack_prefix, strlen(expected_ack_prefix)) == 0) {
                char ack_id[10];
                #ifdef DEBUG
                fprintf(stderr, "ACK-Payload erkannt: %s\n", (char*)payload_ptr);
                #endif
                if (sscanf((char*)payload_ptr + strlen(expected_ack_prefix), "%9s", ack_id) == 1) {
                    size_t len = strlen(ack_id);
                    #ifdef DEBUG
                    fprintf(stderr, "Roh extrahierte ACK-ID: %s\n", ack_id);
                    #endif
                    int i = 0;
                    while((size_t)i < len && ack_id[i] != '\0') {
                        if (ack_id[i] == '}') {
                            ack_id[i] = '\0';
                            break;
                        }
                        i++;
                    }
                    #ifdef DEBUG
                    fprintf(stderr, "Bereinigte ACK-ID: %s\n", ack_id);
                    #endif
                    remove_from_queue(ack_id);
                    #ifdef DEBUG
                    fprintf(stderr, "ACK erhalten für ID: %s\n", ack_id);
                    #endif
                    goto msgloop_end; // Direkt zum Ende der Nachrichtenschleife springen, da ACKs nicht weiter verarbeitet werden  
                }
            }

            // Message-ID extrahieren
            char msg_id[10];
            int msg_id_found = extract_msg_id((char *)payload_ptr, msg_id);

            // Text extrahieren (wie in save_message_to_bbs)
            char clean_text[256] = {0};
            char *text_start = strchr((char*)payload_ptr + 1, ':');
            if (text_start) {
                text_start++; // Hinter den Doppelpunkt
                strncpy(clean_text, text_start, sizeof(clean_text)-1);
                char *id_bracket = strchr(clean_text, '{');
                if (id_bracket) *id_bracket = '\0'; // ID abschneiden
            }

            if (is_message_for_me && msg_id_found) {
                #ifdef DEBUG
                fprintf(stderr, "Nachricht ist für mich und enthält eine ID: %s\n", msg_id);
                #endif
                send_aprs_ack_with_path(sock, src_call, msg_id);           
            
                char task_id[20];
                sprintf(task_id, "%s-%s", msg_id, src_call);
                #ifdef DEBUG
                fprintf(stderr, "Generierte Task-ID für Verarbeitung: %s\n", task_id);
                #endif

                //int is_duplicate = 0;
                for (int i = 0; i < 10; i++) {
                    if (strcmp(tasks[i], task_id) == 0) {
                        //is_duplicate = 1;
                        #ifdef DEBUG
                        fprintf(stderr, "Dubletten-Task erkannt: %s - überspringe Verarbeitung.\n", task_id);
                        #endif
                        goto msgloop_end;
                    }
                }

                if (task_index > 9) task_index = 0; // Einfach überschreiben, wenn voll 
                strcpy(tasks[task_index], task_id);
                task_index++;
                if (strcmp(clean_text, "HELP") == 0) {
                    add_to_queue(src_call, "Available commands: HELP, MSG <message>, WALL");
                } else if (strcmp(clean_text, "WALL") == 0) {
                    handle_wall_command(src_call);
                    last_queue_process = time(NULL)+5; // Queue in 5 Sekunden verarbeiten, damit die Antwort nicht direkt nach dem ACK kommt
                } else if (strncmp(clean_text, "MSG ", 4) == 0) {
                    db_add_message(g_db, src_call, clean_text + 4, "APRS");
                    add_to_queue(src_call, "Message received and stored.");
                    last_queue_process = time(NULL)+5; // Queue in 5 Sekunden verarbeiten, damit die Antwort nicht direkt nach dem ACK kommt
                } else if (strncmp (clean_text, "PING", 4) == 0) {
                    add_to_queue(src_call, "PONG");
                } else {
                }
            }      

            fflush(stdout);

msgloop_end:
            ; // leeres Statement für goto
        }

        // Periodische Aufgaben (z.B. Queue verarbeiten)
        time_t now = time(NULL);
        if (now - last_queue_process >= 5) {
            process_queue(sock);
            last_queue_process = now;
        }

        // send beacon every x minutes
        if (now - last_beacon_time >= g_config.beacon_interval && g_config.beacon_interval != 0) {
            send_aprs_beacon(sock, APRS_DEST_CALL, beacon_text);
            last_beacon_time = now;
        }
    }

close(sock);
return 0;
}

