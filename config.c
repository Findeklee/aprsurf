#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Globale Instanz
Config g_config;

// Hilfsfunktion: trimmt führende/abschließende Whitespaces
// Trimmt führende und abschließende Whitespaces IN-PLACE und gibt Pointer auf den neuen Start zurück
static char *trim(char *str) {
    char *start = str;
    while(*start == ' ' || *start == '\t') start++;
    char *end = start + strlen(start) - 1;
    while(end > start && (*end == ' ' || *end == '\t' || *end == '\n')) *end-- = '\0';
    if (start != str) memmove(str, start, strlen(start) + 1);
    return str;
}

// Setzt Defaultwerte für die Config
static void config_set_defaults(Config *cfg) {
    strcpy(cfg->db_path, "/var/lib/ham-bbs.db");
    strcpy(cfg->aprs_host, "127.0.0.1");
    cfg->aprs_port = 8000;
    cfg->listen_port = 2323;
    strcpy(cfg->bbs_name, "AprSurf BBS");
    strcpy(cfg->sysop_callsign, "DL1ABC");
    strcpy(cfg->bbs_callsign, "DN9RZ-10");
    strcpy(cfg->grid_locator, "JO31EG");
    cfg->beacon_interval = 1200; // Standard: Alle 20 Minuten ein Beacon senden
    strcpy(cfg->beacon_text, "BBS telnet localhost 2323 or msg HELP");
    cfg->gps_lat = 51.15; // Beispielkoordinaten
    cfg->gps_lon = 6.22;
}
// Liest und parsed die Config-Datei
int config_load(Config *cfg, const char *filename) {
    config_set_defaults(cfg);
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[128], val[384];
        if (line[0] == '#' || line[0] == ';' || strlen(line) < 3) continue;
        if (sscanf(line, "%127[^=]=%383[^\n]", key, val) == 2) {
            trim(key); trim(val);
            if (strcmp(key, "db_path") == 0) {
                strncpy(cfg->db_path, val, sizeof(cfg->db_path)-1);
            } else if (strcmp(key, "aprs_host") == 0) {
                strncpy(cfg->aprs_host, val, sizeof(cfg->aprs_host)-1);
            } else if (strcmp(key, "aprs_port") == 0) {
                cfg->aprs_port = atoi(val);
            } else if (strcmp(key, "listen_port") == 0) {
                cfg->listen_port = atoi(val);
            } else if (strcmp(key, "bbs_name") == 0) {
                strncpy(cfg->bbs_name, val, sizeof(cfg->bbs_name)-1);
            } else if (strcmp(key, "sysop_callsign") == 0) {
                strncpy(cfg->sysop_callsign, val, sizeof(cfg->sysop_callsign)-1);
            } else if (strcmp(key, "grid_locator") == 0) {
                strncpy(cfg->grid_locator, val, sizeof(cfg->grid_locator)-1);
            } else if (strcmp(key, "bbs_callsign") == 0) {
                strncpy(cfg->bbs_callsign, val, sizeof(cfg->bbs_callsign)-1);
            } else if (strcmp(key, "beacon_interval") == 0) {
                cfg->beacon_interval = atoi(val);
            } else if (strcmp(key, "beacon_text") == 0) {
                strncpy(cfg->beacon_text, val, sizeof(cfg->beacon_text)-1);
            } else if (strcmp(key, "gps_lat") == 0) {
                cfg->gps_lat = atof(val);
            } else if (strcmp(key, "gps_lon") == 0) {
                cfg->gps_lon = atof(val);
            }

            // Unbekannte Keys werden ignoriert
        }
    }
    fclose(f);
    return 0;
}
