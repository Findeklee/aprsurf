FROM debian:bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libsqlite3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN make clean && make aprsurf-bbs aprs-daemon

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    netcat-openbsd \
    telnet \
    && rm -rf /var/lib/apt/lists/*

# Feste Pfade aus deinem Code:
# CONFIG_PATH ist /usr/local/etc/aprsurf.conf
# DB liegt laut Beispiel unter /var/lib/ham-bbs.db
RUN mkdir -p /usr/local/bin /usr/local/etc /var/lib
COPY --from=build /src/aprsurf-bbs /usr/local/bin/aprsurf-bbs
COPY --from=build /src/aprs-daemon /usr/local/bin/aprs-daemon
COPY --from=build /src/aprsurf.conf /usr/local/etc/aprsurf.conf

# Laufzeit-Default: BBS
CMD ["/usr/local/bin/aprsurf-bbs"]
