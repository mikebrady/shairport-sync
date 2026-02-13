# Configuration Flags
When the application is being built, these flags determine what capabilities are included in Shairport Sync.
For example, to include DSP capabilities – needed for a loudness filter – you would include the `--with-convolution` flag in the list of
options at the `./configure ...` stage when building the application. 

In the following sections, Shairport Sync's compilation configuration options are detailed by catgegory.

## AirPlay 2 or AirPlay

| Flag |
| ---- |
| `--with-airplay-2` |

_AirPlay 2_ is the current version of the AirPlay protocol. It offers multi-room operation and integration with the Home application. However, the Shairport Sync implementation is doesn't support iTunes on Windows, and its integration with the Home app and support for remote control is incomplete. Additionally, it requires a fairly recent (2018 or later) version of a Debian-like Linux, Alpine Linux or FreeBSD. It has not been tested on other Linux distributions such as OpenWrt. Finally, AirPlay 2 can be lossy – in one mode of operation, audio is encoded in 256kbps AAC.

_AirPlay_ (without the "2", aka "classic AirPlay" or "AirPlay 1") is an older version of the AirPlay protocol. If offers multi-room operation to iTunes on macOS or Windows, the Music app on macOS and some third-party computer applications such as [OwnTone](https://owntone.github.io/owntone-server/). It will run on lower powered machines, including many embedded devices. This version of AirPlay is lossless – audio is received in 44,100 frames per second 16-bit interleaved stereo ALAC format. It is compatible with a wider range of Linux distributions, back to around 2012. However, support for this version of AirPlay seems to be gradually waning. It does not offer multi-room operation to iOS, iPadOS or AppleTV and is incompatible with HomePod. It is not integrated with the Home app.

To build Shairport Sync for AirPlay 2, include the `--with-airplay-2` option in the `./configure ...` options. You will also have to include extra libraries. Omitting this option will cause Shairport Sync to be built for the older AirPlay protocol. Include the `--with-ffmpeg` to build for classic AirPlay but using the FFmpeg libraries for decoding and for transcoding. This is recommended for classic AirPlay because it is better maintained and more flexible. Unfortunately, the FFmpeg library is very bulky and so many embedded devices simply don't have space for it.

## Audio Output
| Flags |
| ----- |
| `--with-alsa` |
| `--with-sndio` |
| `--with-pipewire` |
| `--with-pulseaudio` |
| `--with-jack` |
| `--with-ao` |
| `--with-stdout` |
| `--with-pipe` |

Shairport Sync has a number of different "backends" that send audio to an onward destination, e.g. an audio subsystem for output to a Digital to Audio Converter and thence to loudspeakers, or to a pipe for further processing. You can use configuration options to include support for multiple backends at build time and select one of them at runtime.

Here are the audio backend configuration options:

- `--with-alsa` Output to the Advanced Linux Sound Architecture ([ALSA](https://www.alsa-project.org/wiki/Main_Page)) system. This is recommended for highest quality.
- `--with-sndio` Output to the FreeBSD-native [sndio](https://sndio.org) system.
- `--with-pipewire` Output to the [PipeWire](https://pipewire.org) sound server.
- `--with-pulseaudio` Include the [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio) sound server.
- `--with-jack` Output to the [Jack Audio](https://jackaudio.org) system.
- `--with-ao` Output to the [libao](https://xiph.org/ao/) system. No synchronisation.
- `--with-stdout` Include an optional backend module to enable raw audio to be output through standard output (`STDOUT`).
- `--with-pipe` Include an optional backend module to enable raw audio to be output through a unix pipe.

### PulseAudio and PipeWire
If your system uses either PipeWire or PulseAudio as sound servers, Shairport Sync must be started as a user service. This is because the PipeWire or PulseAudio services -- needed by Shairport Sync -- are user services themselves, and they must be running before Shairport Sync starts. That implies that Shairport Sync must be started as a user service.

PipeWire and PulseAudio provide a default ALSA pseudo device, so that Shairport Sync can therefore use the ALSA backend with PipeWire- or PulseAudio-based systems. 

## Audio Options
| Flags |
| ----- |
| `--with-soxr` |
| `--with-apple-alac` |
| `--with-convolution` |
| `--with-ffmpeg` |

- `--with-soxr` Enables Shairport Sync to use [libsoxr](https://sourceforge.net/p/soxr/wiki/Home/)-based resampling for improved interpolation. Recommended. 
- `--with-apple-alac` Enables Shairport Sync to use the Apple ALAC Decoder. Requires [`libalac`](https://github.com/mikebrady/alac). Deprecated due to security issues.
- `--with-convolution` Includes a convolution filter that can be used to apply effects such as frequency and phase correction, and a loudness filter that compensates for the non-linearity of the human auditory system. Requires `libsndfile`. Note that this is only available with audio at 44100 frames per second at present.
- `--with-ffmpeg` Enables classic Shairport Sync to use the [FFmpeg](https://ffmpeg.org) ALAC decoder and the FFmpeg software resampler to transcode, e.g. from 44100 to 48000 frames per second. Requires FFmpeg libraries. (Note: this is for Classic AirPlay only -- `--with-ffmpeg` is automatically enabled for AirPlay 2.)

## Metadata
| Flags |
| ----- |
| `--with-metadata` |

Metadata such as track name, artist name, album name, cover art and more can be requested from the player and passed to other applications.

- `--with-metadata` Adds support for Shairport Sync to request metadata and to pipe it to a compatible application. See https://github.com/mikebrady/shairport-sync-metadata-reader for a sample metadata reader.

## Inter Process Communication

| Flags |
| ----- |
| `--with-mqtt-client` |
| `--with-dbus-interface` |
| `--with-dbus-test-client` |
| `--with-mpris-interface` |
| `--with-mpris-test-client` |

Shairport Sync offers three Inter Process Communication (IPC) interfaces: an [MQTT](https://mqtt.org) interface, an [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/)-like interface and a Shairport Sync specific "native" D-Bus interface loosely based on MPRIS. 
The options are as follows:

- `--with-mqtt-client` Includes a client for [MQTT](https://mqtt.org),
- `--with-dbus-interface`  Includes support for the native Shairport Sync D-Bus interface,
  - `--with-dbus-test-client` Compiles a D-Bus test client application,
- `--with-mpris-interface` Includes support for a D-Bus interface conforming as far as possible with the MPRIS standard,
  - `--with-mpris-test-client` Compiles an MPRIS test client application.

D-Bus and MPRIS commands and properties can be viewed using utilities such as [D-Feet](https://wiki.gnome.org/Apps/DFeet).

##### MPRIS
The MPRIS interface has to depart somewhat from full MPRIS compatibility due to logical differences between Shairport Sync and a full standalone audio player such as Rhythmbox. Basically there are some things that Shairport Sync itself can not control that a standalone player can control.
A good example is volume control. MPRIS has a read-write `Volume` property, so the volume can be read or set. However, all Shairport Sync can do is *request* the player to change the volume control. This request may or may not be carried out, and it may or may not be done accurately. So, in Shairport Sync's MPRIS interface, `Volume` is a read-only property, and an extra command called `SetVolume` is provided.

## Other Configuration Options

### Configuration Files
| Flags |
| ----- |
| `--sysconfdir=<directory>` |

Shairport Sync will look for a configuration file – `shairport-sync.conf` by default – when it starts up. By default, it will look in the directory specified by the `sysconfdir` configuration variable, which defaults to `/usr/local/etc`. This is normal in BSD Unixes, but is unusual in Linux. Hence, for Linux installations, you need to set the `sysconfdir` variable to `/etc` using the configuration setting `--sysconfdir=/etc`.

### Daemonisation
| Flags |
| ----- |
| `--with-libdaemon` |
| `--with-piddir=<pathname>` |

A [daemon](https://en.wikipedia.org/wiki/Daemon_(computing)) is a computer program that runs as a background process, rather than being under the direct control of an interactive user. Shairport Sync is designed to run as a daemon.
FreeBSD and most recent Linux distributions can run an application as a daemon without special modifications. However, in certain older distributions and in special cases it may be necessary to enable Shairport Sync to daemonise itself. Use the `--with-libdaemon` configuration option:

- `--with-libdaemon` Includes a demonising library needed if you want Shairport Sync to demonise itself with the `-d` option. Not needed for `systemd`-based systems which demonise programs differently.
- `--with-piddir=<pathname>` Specifies a pathname to a directory in which to write the PID file which is created when Shairport Sync daemonises itself and used to locate the daemon process to be killed with the `-k` command line option.

### Automatic Start
| Flags |
| ----- |
| `--with-systemd-startup` |
| `--with-systemdsystemunitdir=<dir>` |
| `--with-systemv-startup` |
| `--with-freebsd-startup` |
| `--with-cygwin-service` |

Daemon programs such as Shairport Sync should be started automatically so that the service they provide becomes available without further intervention. Typically this is done using startup scripts. Four options are provided – two for Linux, one for FreeBSD and one for CYGWIN. In Linux, the choice depends on whether [systemd](https://en.wikipedia.org/wiki/Systemd) is used or not. If `systemd` is installed, then the `--with-systemd-startup` option is suggested. If not, the `--with-systemv-startup` option is suggested.

- `--with-systemd-startup` Includes scripts to create a Shairport Sync service that can optionally launch automatically at startup or at user login on `systemd`-based Linuxes. Default is not to to install. Note: an associated special-purpose option allows you to specify where the `systemd` system startup file will be placed:
  - `--with-systemdsystemunitdir=<dir>` Specifies the directory for `systemd` service files.
- `--with-systemv-startup` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on System V based Linuxes. Default is not to to install.
- `--with-freebsd-startup` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on FreeBSD. Default is not to to install.
- `--with-cygwin-service` Includes a script to create a Shairport Sync service that can optionally launch automatically at startup on CYGWIN. Default is not to to install.

### Cryptography
| Flags |
| ----- |
| `--with-ssl=openssl` |
| `--with-ssl=mbedtls` |
| `--with-ssl=polarssl` |

AirPlay 2 operation requires the OpenSSL libraries, so the option `--with-ssl=openssl` is mandatory.

For classic Shairport Sync, the options are as follows:

- `--with-ssl=openssl` Uses the [OpenSSL](https://www.openssl.org) cryptography toolkit. This is mandatory for AirPlay 2 operation.
- `--with-ssl=mbedtls` Uses the [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) cryptography library. Only suitable for classic AirPlay operation – do not include it in an AirPlay 2 configuration.
- `--with-ssl=polarssl` Uses the PolarSSL cryptography library. This is deprecated – PolarSSL has been replaced by Mbed TLS. Only suitable for classic AirPlay operation – do not include it in an AirPlay 2 configuration.

### Zeroconf/Bonjour
| Flags |
| ----- |
| `--with-avahi` |
| `--with-tinysvcmdns` |
| `--with-external-mdns` |
| `--with-dns_sd` |

AirPlay devices advertise their existence and status using [Zeroconf](http://www.zeroconf.org) (aka [Bonjour](https://en.wikipedia.org/wiki/Bonjour_(software))).

AirPlay 2 operation requires the [Avahi](https://www.avahi.org) libraries, so the option `--with-avahi` is mandatory.

For classic Shairport Sync, the options are as follows:

The Zeroconf-related options are as follows:
- `--with-avahi` Chooses [Avahi](https://www.avahi.org)-based Zeroconf support. This is mandatory for AirPlay 2 operation.
- `--with-tinysvcmdns` Chooses [tinysvcmdns](https://github.com/philippe44/TinySVCmDNS)-based Zeroconf support (deprecated).
- `--with-external-mdns` Supports the use of ['avahi-publish-service'](https://linux.die.net/man/1/avahi-publish-service) or 'mDNSPublish' to advertise the service on Bonjour/ZeroConf.
- `--with-dns_sd` Chooses `dns-sd` Zeroconf support.

### Miscellaneous
| Flag                 | Significance |
| -------------------- | ---- |
| `--with-os=<OSType>`   | Specifies the Operating System to target: One of `linux` (default), `freebsd`, `openbsd` or `darwin`. |
| `--with-configfiles` | Installs configuration files (including a sample configuration file) during `make install`. |
| `--with-pkg-config`  | Specifies the use of `pkg-config` to find libraries. (Obselete for AirPlay 2. Special purpose use only.) |

