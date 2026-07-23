CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99

.PHONY: all help start clean deb compose-up compose-down compose-log

all: aprsurf-bbs lastlog-viewer aprs-daemon aprsurf-msg

aprsurf-bbs: $(OBJS)

OBJS=main.o session.o menu.o wall.o state.o termutil.o bulletins.o bbsinfo.o aprsurfinfo.o userinfo.o login.o config.o db.o aprs.o 
aprs.o: aprs.c aprs.h
config.o: config.c config.h

aprsurf-bbs: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lsqlite3
db.o: db.c db.h

login.o: login.c login.h state.h

bbsinfo.o: bbsinfo.c bbsinfo.h state.h
bulletins.o: bulletins.c bulletins.h state.h
wall.o: wall.c wall.h state.h termutil.h

aprs-daemon: aprs-daemon.c
	$(CC) $(CFLAGS) -o $@ config.o db.o $< -lsqlite3

help:
	@echo "Verfügbare Befehle:"
	@echo "  make           - Baut das Programm (ham-bbs)"
	@echo "  make lastlog-viewer - Baut das Lastlog-Viewer-Programm"
	@echo "  make aprs-daemon - Baut das aprs-daemon-Programm"
	@echo "  make clean     - Löscht Objektdateien und Binary"
	@echo "  make start     - Startet die BBS auf Port 2323 per busybox-telnetd (empfohlen)"
	@echo "  make compose-up   - Startet podman compose im Hintergrund"
	@echo "  make compose-down - Stoppt podman compose und entfernt Container/Netzwerke"
	@echo "  make compose-log  - Zeigt podman compose Logs (follow)"
	@echo "  make deb       - Baut das Debianpaket (my-bbs_1.0-1.deb)"
	@echo "  make help      - Zeigt diese Hilfe an"

start: aprsurf-bbs
	@echo "Listening on port 2323 (busybox-telnetd)"
	busybox telnetd -l ./aprsurf-bbs -p 2323 -F
	
state.o: state.c state.h menu.h wall.h
termutil.o: termutil.c termutil.h

lastlog-viewer: lastlog-viewer.c
	$(CC) $(CFLAGS) -o $@ $< -lsqlite3

aprsurf-msg: aprs-msg-viewer.c config.o
	$(CC) $(CFLAGS) -o $@ $^ -lsqlite3

compose-up:
	podman compose up -d --build

compose-down:
	podman compose down

compose-log:
	podman compose logs -f

clean:
	rm -f *.o aprsurf-bbs lastlog-viewer aprs-daemon aprsurf-msg
deb:
	dpkg-deb --build my-bbs_1.0-1
