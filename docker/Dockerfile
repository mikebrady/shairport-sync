ARG NQPTP_BRANCH=main
ARG SHAIRPORT_SYNC_BRANCH=.

FROM alpine:3.17 AS builder

RUN apk -U add \
        alsa-lib-dev \
        autoconf \
        automake \
        avahi-dev \
        build-base \
        dbus \
        ffmpeg-dev \
        git \
        libconfig-dev \
        libgcrypt-dev \
        libplist-dev \
        libressl-dev \
        libsndfile-dev \
        libsodium-dev \
        libtool \
        pipewire-dev \
        mosquitto-dev \
        popt-dev \
        pulseaudio-dev \
        soxr-dev \
        xxd

##### ALAC #####
FROM builder AS alac
RUN git clone --depth=1 https://github.com/mikebrady/alac
WORKDIR /alac
RUN autoreconf -i
RUN ./configure
RUN make
RUN make install
WORKDIR /
##### ALAC END #####

##### NQPTP #####
FROM builder AS nqptp
ARG NQPTP_BRANCH
RUN git clone --depth=1 -b "$NQPTP_BRANCH" https://github.com/mikebrady/nqptp
WORKDIR /nqptp
RUN autoreconf -i
RUN ./configure
RUN make
WORKDIR /
##### NQPTP END #####

##### SPS #####
# Note: apple-alac requires alac build first.
FROM alac AS shairport-sync
ARG SHAIRPORT_SYNC_BRANCH

WORKDIR /shairport-sync
COPY . .
RUN git checkout "$SHAIRPORT_SYNC_BRANCH"
WORKDIR /shairport-sync/build
RUN autoreconf -i ../
RUN CFLAGS="-O3" CXXFLAGS="-O3" ../configure --sysconfdir=/etc --with-alsa --with-pa --with-soxr --with-avahi --with-ssl=openssl \
        --with-airplay-2 --with-metadata --with-dummy --with-pipe --with-dbus-interface \
        --with-stdout --with-mpris-interface --with-mqtt-client \
        --with-apple-alac --with-convolution --with-pw
RUN make -j $(nproc)
RUN DESTDIR=install make install
WORKDIR /
##### SPS END #####

##### STATIC FILES #####
FROM scratch AS files

# Add run script that will start SPS
COPY --chmod=755 ./docker/run.sh ./run.sh
COPY ./docker/etc/s6-overlay/s6-rc.d /etc/s6-overlay/s6-rc.d
COPY ./docker/etc/pulse /etc/pulse
##### END STATIC FILES #####

##### BUILD FILES #####
FROM scratch AS build-files

COPY --from=shairport-sync /shairport-sync/build/install/usr/local/bin/shairport-sync /usr/local/bin/shairport-sync
COPY --from=shairport-sync /shairport-sync/build/install/usr/local/share/man/man1 /usr/share/man/man1
COPY --from=nqptp /nqptp/nqptp /usr/local/bin/nqptp
COPY --from=alac /usr/local/lib/libalac.* /usr/local/lib/
COPY --from=shairport-sync /shairport-sync/build/install/etc/shairport-sync.conf /etc/
COPY --from=shairport-sync /shairport-sync/build/install/etc/shairport-sync.conf.sample /etc/
COPY --from=shairport-sync /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-dbus.conf /etc/dbus-1/system.d/
COPY --from=shairport-sync /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-mpris.conf /etc/dbus-1/system.d/
##### END BUILD FILES #####

# Shairport Sync Runtime System
FROM crazymax/alpine-s6:3.17-3.1.1.2

ENV S6_CMD_WAIT_FOR_SERVICES=1
ENV S6_CMD_WAIT_FOR_SERVICES_MAXTIME=0

RUN apk -U add \
        alsa-lib \
        avahi \
        avahi-tools \
        dbus \
        ffmpeg \
        glib \
        less \
        less-doc \
        libconfig \
        libgcrypt \
        libplist \
        libpulse \
        libressl3.6-libcrypto \
        libsndfile \
        libsodium \
        libuuid \
        pipewire \
        man-pages \
        mandoc \
        mosquitto \
        popt \
        soxr && \
    rm -rfv /lib/apk/db/* && \
    rm -rfv /etc/avahi/services/*.service && \
    addgroup shairport-sync && \
    adduser -D shairport-sync -G shairport-sync && \
    addgroup -g 29 docker_audio && \
    addgroup shairport-sync docker_audio && \
    addgroup shairport-sync audio

# Remove anything we don't need.
# Remove any statically-defined Avahi services, e.g. SSH and SFTP

# Create non-root user for running the container -- running as the user 'shairport-sync' also allows
# Shairport Sync to provide the D-Bus and MPRIS interfaces within the container
# Add the shairport-sync user to the pre-existing audio group, which has ID 29, for access to the ALSA stuff

COPY --from=files / /
COPY --from=build-files / /

ENTRYPOINT ["/init","./run.sh"]
