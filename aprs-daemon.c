/** Prototype of aprs daemon */

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

#include "config.h"

#define SERVER_IP "192.168.178.39"
#define KISS_PORT 8001
#define BUF_SIZE 1024


const char *my_callsign = "DN9RZ-10";

// Neue Struktur für ausgehende Nachrichten in der Queue
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

/// @brief Initializes the SQLite database and creates the messages table if it doesn't exist.
void init_db() {
    sqlite3 *db;
    char *err_msg = 0;
    
    int rc = sqlite3_open("apr_bbs.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Kann DB nicht öffnen: %s\n", sqlite3_errmsg(db));
        return;
    }

    // SQL zum Erstellen der Tabelle, falls sie noch nicht existiert
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "received_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "is_read INTEGER DEFAULT 0"
        ");";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler bei Init: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Datenbank bereit (Tabelle 'messages' ist vorhanden).\n");
    }

    // Hinzufügen der Spalte message_id
    rc = sqlite3_exec(db, "ALTER TABLE messages ADD COLUMN message_id TEXT;", 0, 0, &err_msg);
    if (rc != SQLITE_OK && rc != SQLITE_ERROR) {  // SQLITE_ERROR wenn Spalte schon existiert
        fprintf(stderr, "Fehler beim Hinzufügen der Spalte: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_close(db);
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
    //unsigned char source[6] = {'D','N','9','R','Z',' '};
    //for (int i = 0; i < 6; i++) frame[p++] = (source[i] << 1);
    // Das letzte Byte der Adressliste bekommt das End-Bit 0x01
    //frame[p++] = (my_ssid << 1) | 0x61; 
    encode_ax25_call(&frame[p], my_callsign, 0); // Absender
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
    //unsigned char source[6] = {'D','N','9','R','Z',' '};
    //for (int i = 0; i < 6; i++) frame[p++] = (source[i] << 1);
    // Das letzte Byte der Adressliste bekommt das End-Bit 0x01
    //frame[p++] = (my_ssid << 1) | 0x61; 
    encode_ax25_call(&frame[p], my_callsign, 0); // Absender
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
    //unsigned char source[6] = {'D','N','9','R','Z',' '};
    //for (int i = 0; i < 6; i++) frame[p++] = (source[i] << 1);
    // Das letzte Byte der Adressliste bekommt das End-Bit 0x01
    //frame[p++] = (my_ssid << 1) | 0x61; 

    //encode_ax25_call(&frame[p], "DN9RZ-10", 0); // Absender
    encode_ax25_call(&frame[p], my_callsign, 0); // Absender
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
    printf("Ack (Pfad) gesendet für ID %s an %s\n", msg_id, sender_call);
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
    //const char *my_call = "DN9RZ "; 
    //int my_ssid = 10;
    //for (int i = 0; i < 6; i++) frame[p++] = (my_call[i] << 1);
    // Das letzte Byte der Adressliste bekommt das End-Bit 0x01
    //frame[p++] = (my_ssid << 1) | 0x61; 

    encode_ax25_call(&frame[p], my_callsign, 1); // Absender
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
        printf("Ack erfolgreich an Direwolf übergeben für: %s\n", sender_call);
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

// Ersetze die gesamte save_message_to_bbs Funktion mit diesem korrigierten Code:

void save_message_to_bbs(const char *from_call, const char *raw_payload) {
    sqlite3 *db;
    char *err_msg = 0;
    
    // 1. Text säubern (alles nach dem zweiten Doppelpunkt bis vor die ID '{')
    char clean_text[256] = {0};
    char *text_start = strchr(raw_payload + 1, ':');
    if (text_start) {
        text_start++; // Hinter den Doppelpunkt
        text_start += 4; // Direkt hinter "MSG "
        strncpy(clean_text, text_start, sizeof(clean_text)-1);
        char *id_bracket = strchr(clean_text, '{');
        if (id_bracket) *id_bracket = '\0'; // ID abschneiden
    }

    // ID extrahieren
    char msg_id[10] = {0};
    int msg_id_found = extract_msg_id((char *)raw_payload, msg_id);

    // 2. In Datenbank schreiben
    int rc = sqlite3_open("apr_bbs.db", &db);
    if (rc != SQLITE_OK) return;

    // Dubletten-Prüfung mit ID
    char *sql = sqlite3_mprintf(
        "INSERT INTO messages (sender, content, received_at, message_id) "
        "SELECT %Q, %Q, DATETIME('now'), %Q "
        "WHERE NOT EXISTS (SELECT 1 FROM messages WHERE sender=%Q AND content=%Q AND message_id %s %Q);",
        from_call, clean_text, msg_id_found ? msg_id : NULL, from_call, clean_text, msg_id_found ? "=" : "IS", msg_id_found ? msg_id : NULL
    );

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_free(sql);
    sqlite3_close(db);
}


// Funktion: Neue Nachricht zur Queue hinzufügen
void add_to_queue(const char *dest_call, const char *text) {
    // Generiere eine eindeutige ID (Zahl + Kleinbuchstabe, z.B. "1l")
    static int id_counter = 5;
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

    printf("Nachricht zur Queue hinzugefügt: %s -> %s (ID: %s)\n", dest_call, text, msg_id);
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
            printf("Nachricht mit ID %s aus Queue entfernt.\n", msg_id);
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
                printf("Nachricht gesendet (Retry %d): %s\n", current->retry_count, current->msg_id);
                sent_one = 1;  // Nur eine senden
            } else {
                // Max Retries erreicht – entfernen oder loggen
                printf("Max Retries für ID %s erreicht - entferne aus Queue.\n", current->msg_id);
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

void handle_wall_command(const char *src_call) {
    sqlite3 *db;
    int rc = sqlite3_open("apr_bbs.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Kann DB nicht öffnen für WALL: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT sender, content, received_at FROM messages ORDER BY received_at DESC LIMIT 3;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Prepare Fehler: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sender = (const char*)sqlite3_column_text(stmt, 0);
        const char *content = (const char*)sqlite3_column_text(stmt, 1);
        // const char *received_at = (const char*)sqlite3_column_text(stmt, 2);

        char response[256];
        //sprintf(response, "From %s at %s: %s", sender, received_at, content);
        sprintf(response, "%s:%s", sender, content); // Kürzeres Format für APRS
        add_to_queue(src_call, response);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

/// @brief Speichert ein Bulletin in der Datenbank.
/// @param dest_call Empfänger (z.B. "BLN1")
/// @param from_call Absender-Rufzeichen
/// @param content   Bulletin-Inhalt
void save_bulletin_to_db(const char *dest_call, const char *from_call, const char *content) {
    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open("apr_bbs.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Kann DB nicht öffnen: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Tabelle anlegen, falls sie nicht existiert
    const char *sql_create =
        "CREATE TABLE IF NOT EXISTS bulletins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "dest_call TEXT NOT NULL,"
        "sender TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "received_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    rc = sqlite3_exec(db, sql_create, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler bei Init: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    // Bulletin speichern
    char *sql_insert = sqlite3_mprintf(
        "INSERT INTO bulletins (dest_call, sender, content, received_at) "
        "VALUES (%Q, %Q, %Q, DATETIME('now'));",
        dest_call, from_call, content
    );
    rc = sqlite3_exec(db, sql_insert, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Fehler beim Einfügen: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_free(sql_insert);
    sqlite3_close(db);
}

int main() {
    char tasks[10][20]; // Array für bis zu 10 Task-IDs
    int task_index = 0;

    int sock;
    struct sockaddr_in server_addr;
    unsigned char buffer[BUF_SIZE];

    // Globale Konfiguration laden
    if (config_load(&g_config, CONFIG_PATH) != 0) {
        fprintf(stderr, "Warnung: Konnte Konfigurationsdatei %s nicht laden, benutze Defaultwerte.\n", CONFIG_PATH);
    }

    init_db();

    // 1. Socket erstellen
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket-Fehler");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(KISS_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // 2. Verbindung zu Direwolf aufbauen
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Verbindung fehlgeschlagen. Läuft Direwolf?");
        return 1;
    }

    printf("Verbunden mit KISS an %s:%d. Warte auf Pakete...\n", SERVER_IP, KISS_PORT);

    send_aprs_beacon(sock, "APX220", "!5115.52N/00622.51EBTesting APRS Daemon/Linux. Msg HELP to get started.");

    // 3. Empfangsschleife
    while (1) {
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
                printf("Verbindung verloren.\n");
                break;
            }

            char src_call[10];
            char dst_call[10];
            decode_callsign(&buffer[9], src_call); 
            printf("----------------------------------\n");
            printf("Absender: %s\n", src_call);
            
            decode_callsign(&buffer[2], dst_call); 
            printf("Empfaenger: %s\n", dst_call);

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
                printf("Payload: %.*s\n", payload_len, payload_ptr);
            } else {
                printf("Payload: (keine gefunden)\n");
            }

            // Prüfen, ob die Nachricht an uns gerichtet ist (DN9RZ-10)
            char real_dest[10];
            strncpy(real_dest, (char*)&payload_ptr[1], 9);
            real_dest[9] = '\0';

            // Bulletin-Erkennung
            int is_bulletin = (strncmp(real_dest, "BLN", 3) == 0);

            if (is_bulletin) {
                printf("Bulletin erkannt: %s\n", real_dest);
                // Hier kannst du weitere Verarbeitung für Bulletins einbauen
            }

            // pad callsign to 9 characters with spaces for comparison 
            char padded_space_callsign[10];
            memset(padded_space_callsign, ' ', 10);
            padded_space_callsign[9] = '\0'; // Null-Byte am Ende
            strncpy(padded_space_callsign, my_callsign, strlen(my_callsign));
            int is_message_for_me = (strcmp(real_dest, padded_space_callsign) == 0);
            if (is_message_for_me) {
                printf("Diese Nachricht ist für mich bestimmt!\n");
            } else {
                printf("Diese Nachricht ist NICHT für mich bestimmt.\n");
            }

            // Prüfe, ob es ein ACK für eine ausgehende Nachricht ist
            char expected_ack_prefix[20];
            sprintf(expected_ack_prefix, ":%s :ack", my_callsign);  // z. B. ":DN9RZ-10 :ack"
            if (payload_ptr && strncmp((char*)payload_ptr, expected_ack_prefix, strlen(expected_ack_prefix)) == 0) {
                char ack_id[10];
                printf("ACK-Payload erkannt: %s\n", (char*)payload_ptr);
                if (sscanf((char*)payload_ptr + strlen(expected_ack_prefix), "%9s", ack_id) == 1) {
                    size_t len = strlen(ack_id);
                    printf("Roh extrahierte ACK-ID: %s\n", ack_id);
                    int i = 0;
                    while((size_t)i < len && ack_id[i] != '\0') {
                        if (ack_id[i] == '}') {
                            ack_id[i] = '\0';
                            break;
                        }
                        i++;
                    }
                    printf("Bereinigte ACK-ID: %s\n", ack_id);
                    remove_from_queue(ack_id);
                    printf("ACK erhalten für ID: %s\n", ack_id);
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
                printf("Nachricht ist für mich und enthält eine ID: %s\n", msg_id);
                send_aprs_ack_with_path(sock, src_call, msg_id);            
            
                char task_id[20];
                sprintf(task_id, "%s-%s", msg_id, src_call);
                printf("Generierte Task-ID für Verarbeitung: %s\n", task_id);

                //int is_duplicate = 0;
                for (int i = 0; i < 10; i++) {
                    if (strcmp(tasks[i], task_id) == 0) {
                        //is_duplicate = 1;
                        printf("Dubletten-Task erkannt: %s - überspringe Verarbeitung.\n", task_id);
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
                } else if (strncmp(clean_text, "MSG ", 4) == 0) {
                    save_message_to_bbs(src_call, (char *)payload_ptr);
                    add_to_queue(src_call, "Message received and stored.");
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

    }

close(sock);
return 0;
}



