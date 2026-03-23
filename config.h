#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_PATH "/usr/local/etc/aprsurf.conf"

// Beispielhafte Konfigurationsstruktur
typedef struct {
    char db_path[256];      // Pfad zur SQLite-Datenbank
    char aprs_host[128];   // APRS/Direwolf Host
    int  aprs_port;        // APRS/Direwolf Port
    int  listen_port;      // TCP/Telnet Port
    char bbs_name[64];     // Name des BBS
    char sysop_callsign[10]; // Rufzeichen des Sysops
    char bbs_callsign[10]; // Rufzeichen der BBS (z.B. "DN9RZ-10")
    char grid_locator[10]; // Grid Locator, z.B. "JO31EG"
    int beacon_interval;   // Intervall für das Senden von Beacons in Sekunden 
    char beacon_text[256]; // Text des Beacons
    double gps_lat;        // GPS-Koordinate LAT
    double gps_lon;        // GPS-Koordinate LON
} Config;

// Lädt die Konfiguration aus Datei in das Struct (Defaultwerte, dann überschreiben)
int config_load(Config *cfg, const char *filename);

// Globale Config-Instanz
extern Config g_config;

#endif // CONFIG_H
