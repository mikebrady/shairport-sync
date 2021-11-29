FROM alpine:3.12 AS builder-base
# Version 3.12 is required for now since libplist has breaking changes in v3.13 and above which are not yet standard
# in debian/ubuntu.

# General Build System:
RUN apk -U add \
        git \
        build-base \
        autoconf \
        automake \
        libtool \
        dbus \
        su-exec \
        alsa-lib-dev \
        libdaemon-dev \
        popt-dev \
        mbedtls-dev \
        soxr-dev \
        avahi-dev \
        libconfig-dev \
        libsndfile-dev \
        mosquitto-dev \
        xmltoman \
        openssh-client \
        libsodium-dev \
        ffmpeg-dev \
        xxd \
        libressl-dev \
        openssl-dev \
        libplist-dev \
        libgcrypt-dev

# ALAC Build System:
FROM builder-base AS builder-alac

RUN git clone https://github.com/mikebrady/alac
WORKDIR /alac
RUN autoreconf -fi
RUN ./configure
RUN make
RUN make install

# Shairport Sync Build System:
FROM builder-base AS builder-sps

# These will be modified by the Github Action Workflow and are required
# to ensure the correct branches are being used.
ARG SHAIRPORT_SYNC_BRANCH
RUN test -n "$SHAIRPORT_SYNC_BRANCH"
ARG NQPTP_BRANCH
RUN test -n "$NQPTP_BRANCH"

COPY --from=builder-alac /usr/local/lib/libalac.* /usr/local/lib/
COPY --from=builder-alac /usr/local/lib/pkgconfig/alac.pc /usr/local/lib/pkgconfig/alac.pc
COPY --from=builder-alac /usr/local/include /usr/local/include

RUN git clone https://github.com/mikebrady/nqptp
WORKDIR /nqptp
# Use development branch for now.
RUN git checkout "$NQPTP_BRANCH"
RUN autoreconf -fi
RUN ./configure
RUN make
RUN make install
WORKDIR /

WORKDIR /shairport-sync
COPY . .
RUN git checkout "$SHAIRPORT_SYNC_BRANCH"
RUN autoreconf -fi
# RUN ./configure \
#         --with-alsa \
#         --with-dummy \
#         --with-pipe \
#         --with-stdout \
#         --with-avahi \
#         --with-ssl=mbedtls \
#         --with-soxr \
#         --sysconfdir=/etc \
#         --with-dbus-interface \
#         --with-mpris-interface \
#         --with-mqtt-client \
#         --with-apple-alac \
#         --with-convolution \
#         --with-airplay-2
# RUN make -j $(nproc)
RUN CFLAGS="-O0 -g" CXXFLAGS="-O0 -g" ./configure --sysconfdir=/etc --with-metadata --with-dummy --with-pipe \
        --with-alsa --with-soxr --with-avahi --with-ssl=openssl --with-dbus-interface --with-stdout \
        --with-mpris-interface --with-mqtt-client --with-apple-alac --with-convolution --with-airplay-2
RUN make -j
RUN make install

# Shairport Sync Runtime System:
FROM alpine:3.12

RUN apk -U add \
        alsa-lib \
        dbus \
        popt \
        glib \
        mbedtls \
        soxr \
        avahi \
        libconfig \
        libsndfile \
        mosquitto \
        su-exec \
        libuuid \
        ffmpeg \
        libsodium \
        libplist \
        libgcrypt

RUN rm -rf  /lib/apk/db/*

# set version for s6 overlay
ARG OVERLAY_VERSION="v2.2.0.3"
ARG OVERLAY_ARCH="armhf"

# add s6 overlay
ADD https://github.com/just-containers/s6-overlay/releases/download/${OVERLAY_VERSION}/s6-overlay-${OVERLAY_ARCH}-installer /tmp/
RUN chmod +x /tmp/s6-overlay-${OVERLAY_ARCH}-installer && /tmp/s6-overlay-${OVERLAY_ARCH}-installer / && rm /tmp/s6-overlay-${OVERLAY_ARCH}-installer

COPY --from=builder-alac /usr/local/lib/libalac.* /usr/local/lib/
COPY --from=builder-sps /etc/shairport-sync* /etc/
COPY --from=builder-sps /etc/dbus-1/system.d/shairport-sync-dbus.conf /etc/dbus-1/system.d/
COPY --from=builder-sps /etc/dbus-1/system.d/shairport-sync-mpris.conf /etc/dbus-1/system.d/
COPY --from=builder-sps /usr/local/bin/shairport-sync /usr/local/bin/shairport-sync
COPY --from=builder-sps /usr/local/bin/nqptp /usr/local/bin/nqptp

# Create non-root user for running the container -- running as the user 'shairport-sync' also allows
# Shairport Sync to provide the D-Bus and MPRIS interfaces within the container

RUN addgroup shairport-sync 
RUN adduser -D shairport-sync -G shairport-sync

# Add the shairport-sync user to the pre-existing audio group, which has ID 29, for access to the ALSA stuff
RUN addgroup -g 29 docker_audio && addgroup shairport-sync docker_audio && addgroup shairport-sync audio

#COPY ./docker/start.sh /
#RUN chmod +x ./start.sh
#ENTRYPOINT [ "/start.sh" ]

# s6 start
COPY ./docker/etc /etc
ENTRYPOINT ["/init"]
