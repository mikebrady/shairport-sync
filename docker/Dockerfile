ARG NQPTP_BRANCH=main
ARG FFMPEG_BRANCH=release/7.1
ARG SHAIRPORT_SYNC_BRANCH=.
ARG ALPINE_VERSION=20250108

##### FFMPEGLITE #####
FROM alpine:$ALPINE_VERSION AS ffmpeglite
RUN apk -U add build-base git nasm pkgconf
RUN mkdir -p /ffmpeg_sources /ffmpeg_build
WORKDIR /ffmpeg_sources
ARG FFMPEG_BRANCH
RUN git clone --depth=1 -b "$FFMPEG_BRANCH" https://github.com/FFmpeg/FFmpeg
WORKDIR /ffmpeg_sources/FFmpeg
RUN ./configure \
  --prefix="/ffmpeg_build" \
  --extra-libs="-lpthread -lm" \
  --ld="g++" \
  --disable-static \
  --enable-shared \
  --enable-gpl \
  --disable-programs \
  --disable-everything \
  --disable-doc \
  --disable-iconv \
  --disable-avdevice \
  --disable-avfilter \
  --disable-swscale \
  --disable-network \
  --disable-iamf \
  --disable-pixelutils \
  --disable-postproc \
  --enable-decoder=alac \
  --enable-decoder=aac
RUN make -j $(nproc)
RUN make install
##### FFMPEGLITE END #####


##### NQPTP #####
FROM alpine:$ALPINE_VERSION AS nqptp
RUN apk -U add \
        musl-dev \
        autoconf \
        automake \
        build-base \
        git \
        linux-headers

ARG NQPTP_BRANCH
RUN git clone --depth=1 -b "$NQPTP_BRANCH" https://github.com/mikebrady/nqptp
WORKDIR /nqptp
RUN autoreconf -i
RUN ./configure
RUN make -j $(nproc)
WORKDIR /
##### NQPTP END #####

##### SPS #####
FROM alpine:$ALPINE_VERSION AS shairport-sync
RUN apk -U add \
        pkgconf \
        alsa-lib-dev \
        autoconf \
        automake \
        avahi-dev \
        build-base \
        dbus \
        git \
        libconfig-dev \
        libgcrypt-dev \
        libplist-dev \
        openssl-dev \
        libsndfile-dev \
        libsodium-dev \
        libtool \
        pipewire-dev \
        mosquitto-dev \
        popt-dev \
        pulseaudio-dev \
        soxr-dev \
        libplist-util \
        xxd

ARG SHAIRPORT_SYNC_BRANCH
WORKDIR /shairport-sync
COPY . .
RUN rm -rf build
# the above three lines or the following...
# RUN git clone --depth=1 -b "SHAIRPORT_SYNC_BRANCH" https://github.com/mikebrady/sps-private shairport-sync

COPY --from=ffmpeglite /ffmpeg_build/lib/ /usr/local/lib
COPY --from=ffmpeglite /ffmpeg_build/lib/pkgconfig/*.pc  /usr/local/lib/pkgconfig/
COPY --from=ffmpeglite /ffmpeg_build/include/  /usr/local/include

# RUN git checkout "$SHAIRPORT_SYNC_BRANCH"
# the following is to remove any build directory that might be there
# not needed if the source is a clone of the repository
# RUN rm -rf build
WORKDIR /shairport-sync/build
RUN autoreconf -i ../
RUN ../configure --sysconfdir=/etc --with-alsa --with-pulseaudio --with-soxr --with-avahi --with-ssl=openssl \
        --with-airplay-2 --with-metadata --with-dummy --with-pipe --with-dbus-interface \
        --with-stdout --with-mpris-interface --with-mqtt-client \
        --with-convolution --with-pipewire
RUN make -j $(nproc)
RUN DESTDIR=install make install
WORKDIR /
##### SPS END #####

##### STATIC FILES #####
FROM scratch AS files

# Add run script that will start SPS
COPY --chmod=755 ./docker/run.sh ./run.sh
COPY ./docker/etc/pulse /etc/pulse
##### END STATIC FILES #####

##### BUILD FILES #####
FROM scratch AS build-files
COPY --from=ffmpeglite /ffmpeg_build/lib/ /usr/local/lib
COPY --from=shairport-sync /shairport-sync/build/install/usr/local/bin/shairport-sync /usr/local/bin/shairport-sync
COPY --from=shairport-sync /shairport-sync/build/install/usr/local/share/man/man1 /usr/share/man/man1
COPY --from=nqptp /nqptp/nqptp /usr/local/bin/nqptp
COPY --from=shairport-sync /shairport-sync/build/install/etc/shairport-sync.conf /etc/
COPY --from=shairport-sync /shairport-sync/build/install/etc/shairport-sync.conf.sample /etc/
COPY --from=shairport-sync /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-dbus.conf /etc/dbus-1/system.d/
COPY --from=shairport-sync /shairport-sync/build/install/etc/dbus-1/system.d/shairport-sync-mpris.conf /etc/dbus-1/system.d/
##### END BUILD FILES #####

# Shairport Sync Runtime System
FROM alpine:$ALPINE_VERSION
COPY --from=build-files / /
RUN apk -U add \
        alsa-lib \
        avahi \
        dbus \
        glib \
        libconfig \
        libgcrypt \
        libplist \
        pipewire \
        libpulse \
        libcrypto3 \
        libsodium \
        libuuid \
        mosquitto \
        popt \
        soxr \
        curl

RUN rm -rfv /lib/apk/db/* && \
        rm -rfv /etc/avahi/services/*.service && \
        rm -rf /usr/share/man && \
        addgroup shairport-sync && \
        adduser -D shairport-sync -G shairport-sync && \
        addgroup -g 29 docker_audio && \
        addgroup shairport-sync docker_audio && \
        addgroup shairport-sync audio && \
        mkdir -p /run/dbus

# Removed anything we don't need.
# Removed any statically-defined Avahi services, e.g. SSH and SFTP

# Created non-root user for running the container -- running as the user 'shairport-sync' also allows
# Shairport Sync to provide the D-Bus and MPRIS interfaces within the container
# Added the shairport-sync user to the pre-existing audio group, which has ID 29, for access to the ALSA stuff

COPY --from=files / /
ENV ENABLE_AVAHI=1
ENTRYPOINT ["./run.sh"]
