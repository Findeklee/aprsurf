CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c99
all: ham-bbs lastlog-viewer aprs-daemon

ham-bbs: $(OBJS)

OBJS=main.o session.o menu.o wall.o state.o termutil.o bbsinfo.o userinfo.o login.o config.o db.o aprs.o
aprs.o: aprs.c aprs.h
config.o: config.c config.h

ham-bbs: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lsqlite3
db.o: db.c db.h

login.o: login.c login.h state.h

bbsinfo.o: bbsinfo.c bbsinfo.h state.h
wall.o: wall.c wall.h state.h termutil.h

aprs-daemon: aprs-daemon.c
	$(CC) $(CFLAGS) -o $@ config.o $< -lsqlite3

help:
	@echo "Verfügbare Befehle:"
	@echo "  make           - Baut das Programm (ham-bbs)"
	@echo "  make lastlog-viewer - Baut das Lastlog-Viewer-Programm"
	@echo "  make aprs-daemon - Baut das aprs-daemon-Programm"
	@echo "  make clean     - Löscht Objektdateien und Binary"
	@echo "  make start     - Startet die BBS auf Port 2323 per busybox-telnetd (empfohlen)"
	@echo "  make deb       - Baut das Debianpaket (my-bbs_1.0-1.deb)"
	@echo "  make help      - Zeigt diese Hilfe an"

start: ham-bbs
	@echo "Listening on port 2323 (busybox-telnetd)"
	busybox telnetd -l ./ham-bbs -p 2323 -F
	
state.o: state.c state.h menu.h wall.h
termutil.o: termutil.c termutil.h

lastlog-viewer: lastlog-viewer.c
	$(CC) $(CFLAGS) -o $@ $< -lsqlite3

clean:
	rm -f *.o ham-bbs lastlog-viewer aprs-daemon

deb:
	dpkg-deb --build my-bbs_1.0-1
