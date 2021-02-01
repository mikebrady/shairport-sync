
Shairport Sync
=============
Shairport Sync is an AirPlay audio player – it plays audio streamed from iTunes, iOS, Apple TV and macOS devices and AirPlay sources such as Quicktime Player and [ForkedDaapd](http://ejurgensen.github.io/forked-daapd/), among others.

Audio played by a Shairport Sync-powered device stays synchronised with the source and hence with similar devices playing the same source. In this way, synchronised multi-room audio is possible for players that support it, such as iTunes and the macOS Music app.

Shairport Sync runs on Linux, FreeBSD and OpenBSD. It does not support AirPlay video or photo streaming.

More Information
----------
Shairport Sync offers *full audio synchronisation*, a feature of AirPlay that previous implementations do not provide. Full audio synchronisation means that audio is played on the output device at exactly the time specified by the audio source. To accomplish this, Shairport Sync needs access to audio systems – such as `alsa` on Linux and `sndio` on FreeBSD – that provide very accurate timing information about output devices. Shairport Sync must have direct access to the output device used, which must be a real sound card capable of working with 44,100, 88,200 or 176,400 samples per second, interleaved PCM stereo of 8, 16, 24 or 32 bits. The default is 44,100 samples per second / 16 bits (you'll get a message in the logfile if there's a problem).

Alternatively, Shairport Sync works well with PulseAudio, a widely used sound server found on many desktop Linuxes. While the timing information is not as accurate as that of `alsa` or `sndio`, it is often impractical to remove or disable PulseAudio. In that case, the `pa` backend can be used. An older backend for PulseAudio called `pulse` does not support synchronisation and is deprecated.

For other use cases, Shairport Sync can provide synchronised audio output to a unix pipe or to standard output, or to audio systems that do not provide timing information. This could perhaps be described as *partial audio synchronisation*, where synchronised audio is provided by Shairport Sync, but what happens to it in the subsequent processing chain, before it reaches the listener's ear, is outside the control of Shairport Sync.

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

Synchronisation, Latency, "Stuffing"
---------
The AirPlay protocol uses an agreed *latency* – the time difference, or delay, between the time represented by a sound sample's `timestamp` and the time it is actually played by the audio output device, typically a Digital to Audio Converter (DAC). The latency to be used is specified by the audio source when it negotiates with Shairport Sync. Most sources set a latency of two seconds. Recent versions of iTunes and forkedDaapd use a latency of just over 2.25 seconds. A latency of this length allows AirPlay players to correct for network delays, processing time variations and so on.

As mentioned previously, Shairport Sync implements full audio synchronisation when used with `alsa`, `sndio` or PulseAudio systems. This is done by monitoring the timestamps present in data coming from the audio source and the timing information from the audio system, e.g. `alsa`. To maintain the  latency required for exact synchronisation, if the output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up. If the output device is running fast, Shairport Sync will insert frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible on normal audio material. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is less obtrusive than insertion and deletion but requires a good deal of processing power — most embedded devices probably can't support it. The process of insertion/deletion or resampling is rather inelegantly called “stuffing”.

Stuffing is not done for partial audio synchronisation – the audio samples are simply presented at exactly the right time to the next stage in the processing chain.

Timestamps are referenced relative to the source computer's clock – the `source clock`, but timing must be done relative to the clock of the computer running Shairport Sync – the `local clock`. So, another thing Shairport Sync has to do is to synchronize the source clock and the local clock, and it does this usually to within a fraction of a millisecond, using a variant of NTP synchronisation protocols.

What else?
--------------
* Better Volume Control — Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder. In addition, Shairport Sync can offer an extended volume control range on devices with a restricted range.
* Hardware Mute — Shairport Sync can mute properly if the hardware supports it.
* Support for the Apple ALAC decoder.
* Output bit depths of 8, 16, 24 and 32 bits, rather than the standard 16 bits.
* Output frame rates of 44,100, 88,200, 176,000 or 352,000 frames per second.
* Fast Response — With hardware volume control, response is instantaneous; otherwise the response time is 0.20 seconds with `alsa`, 0.35 seconds with `sndio`.
* Non-Interruptible — Shairport Sync sends back a "busy" signal if it's already playing audio from another source, so other sources can't disrupt an existing Shairport Sync session. (If a source disappears without warning, the session automatically terminates after two minutes and the device becomes available again.)
* Metadata — Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in macOS and iOS.
* Compiles on Linux, Cygwin, FreeBSD, OpenBSD.
* Outputs to [`alsa`](https://www.alsa-project.org/wiki/Main_Page), [`sndio`](http://www.sndio.org), [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/), [JACK](http://jackaudio.org), to a unix pipe or to `STDOUT`. It also has limited support for [libao](https://xiph.org/ao/) and for [`soundio`](http://libsound.io).
* An [MPRIS](https://specifications.freedesktop.org/mpris-spec/2.2/) interface, partially complete and very functional, including access to metadata and artwork, and some limited remote control.
* An interface to [MQTT](https://en.wikipedia.org/wiki/MQTT), an often-used protocol in home automation projects.
* A native D-Bus interface, including access to metadata and artwork, some limited remote control and some system settings.

Heritage
-------
Shairport Sync is a substantial rewrite of the fantastic work done in Shairport 1.0 by James Laird and others — please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0.

Status
------
Shairport Sync works on a wide variety of Linux devices, FreeBSD and OpenBSD. It works with built-in audio and with a variety of USB-connected audio amplifiers and DACs. Shairport Sync runs well on the Raspberry Pi. It can drive the built-in sound card – see the note below on configuring the Raspberry Pi to make best use of it. It runs well on the Raspberry Pi Zero W with a suitable USB or I2S card.

Shairport Sync will work with PulseAudio, which is installed in many desktop Linuxes. PulseAudio normally runs in the *user mode* but can be configured to run in *system mode*, though this is not recommended. Shairport Sync can work with it in either mode.

Shairport Sync runs natively on FreeBSD and OpenBSD using the `sndio` sound system.

Shairport Sync runs on Ubuntu, OpenWrt, Debian, Arch Linux, Fedora, FreeBSD and OpenBSD inside VMWare Fusion on a Mac, but synchronisation in inaccurate — possibly because the sound card is being emulated.

For information about changes and updates, please refer to the RELEASENOTES.md file in the distribution.

Building And Installing
---------------------
See [here](https://github.com/mikebrady/shairport-sync/blob/master/INSTALL.md) for simple installation instructions for modern Linux systems like Rapsberry Pis.

**Docker:** Shairport Sync is available as a prebuilt Docker image at https://hub.docker.com/r/mikebrady/shairport-sync. Alternatively, you can roll your own using the Dockerfile provided. Note that, at present, the Shairport Sync Docker image does not work on a Mac, due to limitations in the Docker system.

Shairport Sync may already be available as a package in your Linux distribution (search for `shairport-sync` – the package named `shairport` is a different program). Packages are available on recent versions of Debian, Ubuntu, Arch, OpenWrt and possibly more:

**Ubuntu:** A `shairport-sync` installer package is available for Ubuntu. Additionally, a Personal Package Archives for Shairport Sync master and development branches are available at https://launchpad.net/~dantheperson.

**Debian:** shairport-sync is in the Debian archive.

**OpenWrt:** There is a Shairport Sync package in OpenWrt trunk. Also, there's an OpenWrt package at https://github.com/mikebrady/shairport-sync-for-openwrt, including one that builds back to Barrier Breaker.

**Arch Linux:** Shairport Sync is available for x86_64 and i686 platforms in the Arch Linux Community Repository -- search for `shairport-sync`. See also https://www.archlinux.org/packages/.

To build and install the latest version of Shairport Sync, an [Arch Linux build and installation guide](https://github.com/mikebrady/shairport-sync-for-arch-linux) is available, based on original work by [Elia Cereda](https://github.com/EliaCereda).

**Mac OS X:** A HomeBrew package exists for Shairport Sync. With HomeBrew installed, Shairport Sync can be installed using the command:
```
$brew install shairport-sync
```
Note that the installation uses the libao library and so synchronisation is not available — playback glitches will occur occasionally, when the ao system's buffers overflow or underflow.

**Fedora:** Please see the guide at [FEDORA.md](https://github.com/mikebrady/shairport-sync/blob/master/FEDORA.md).

**Cygwin:** Please see the guide at [CYGWIN.md](https://github.com/mikebrady/shairport-sync/blob/master/CYGWIN.md).

**Gentoo:** Shairport Sync is available in a overlay, please see the guide at [GENTOO.md](https://github.com/mikebrady/shairport-sync/blob/master/GENTOO.md).

Sincere thanks to all package contributors!

If you wish to build and install the latest version of Shairport Sync on Debian, Ubuntu, Fedora or Arch platforms, please continue to follow these instructions. When the program has been compiled and installed, refer to the section on Configuring Shairport Sync that follows. To build Shairport Sync from sources on FreeBSD please refer to [FREEBSD.md](https://github.com/mikebrady/shairport-sync/blob/master/FREEBSD.md).

**Remove Old Versions of Shairport Sync and its Startup Scripts**

You should check to see if `shairport-sync` is already installed – you can use the command `$ which shairport-sync` to find where it is located, if installed. If it is installed you should delete it – you may need superuser privileges. After deleting, check again in case further copies are installed elsewhere.

You should also remove the startup script and service definition files `/etc/systemd/system/shairport-sync.service`, `/lib/systemd/system/shairport-sync.service`, `/etc/init.d/shairport-sync`, `/etc/dbus-1/system.d/shairport-sync-dbus.conf` and `/etc/dbus-1/system.d/shairport-sync-mpris.conf` if they exist – new ones will be installed if necessary.

If you removed any installations of Shairport Sync or any of its startup script files, you should reboot.

**Determine The Configuration Needed**

Shairport Sync has a number of different "backends" that connect it to the system's audio handling infrastructure. Most recent Linux distributions that have a GUI – including Ubuntu, Debian and others – use PulseAudio to handle sound. In such cases, it is inadvisable to attempt to disable or remove PulseAudio. Thus, if your system uses PulseAudio, you should build Shairport Sync with the PulseAudio backend. You can check to see if PulseAudio is running by opening a Terminal window and entering the command `$ pactl info`. Here is an example of what you'll get if PulseAudio is installed, though the exact details may vary:
```
$ pactl info
Server String: unix:/run/user/1000/pulse/native
Library Protocol Version: 30
Server Protocol Version: 30
Is Local: yes
Client Index: 9
Tile Size: 65472
User Name: mike
Host Name: ubuntu
Server Name: pulseaudio
Server Version: 8.0
Default Sample Specification: s16le 2ch 44100Hz
Default Channel Map: front-left,front-right
Default Sink: alsa_output.pci-0000_02_02.0.analog-stereo
Default Source: alsa_input.pci-0000_02_02.0.analog-stereo
Cookie: 96f9:3e8d
$
```
If PulseAudio in not installed, you'll get something like this:
```
$  pactl info
-bash: pactl: command not found
$
```
If your system does not use PulseAudio, then it is likely that it uses the Advanced Linux Sound Architecture (ALSA), so you should build Shairport Sync with the ALSA backend. By the way, many systems with PulseAudio also have ALSA (in fact, PulseAudio is effectively a client of ALSA); in those cases you should choose the PulseAudio backend.

If PulseAudio is not installed, there is no necessity to install it for Shairport Sync. In fact, Shairport Sync works better without it.

**Building**

To build Shairport Sync from sources on Debian, Ubuntu, Raspbian, etc. follow these instructions.

The following libraries are required:
* OpenSSL or  mbed TLS (PolarSSL is supported but deprecated)
* Avahi
* ALSA and/or PulseAudio
* libdaemon
* autoconf
* automake
* libtool
* libpopt
* libconfig

Optional:
* libsoxr
* libalac (This is a library containing the Apple ALAC decoder.)

Many Linux distributions have Avahi and OpenSSL already in place, so normally it probably makes sense to choose those options rather than tinysvcmdns or mbed TLS. The `libsoxr` library is available in recent Linux distributions, but it requires lots of processor power.

Debian, Ubuntu and Raspbian users can get the basics with:

- `# apt install build-essential git xmltoman` – these may already be installed.
- `# apt install autoconf automake libtool libdaemon-dev libpopt-dev libconfig-dev`
- `# apt install libasound2-dev` for the ALSA libraries
- `# apt install libpulse-dev` for the PulseAudio libraries
- `# apt install avahi-daemon libavahi-client-dev` if you want to use Avahi (recommended).
- `# apt install libssl-dev` if you want to use OpenSSL and libcrypto, or use mbed TLS otherwise.
- `# apt install libmbedtls-dev` if you want to use mbed TLS, or use OpenSSL/libcrypto otherwise. You can still use PolarSSL with `apt install libpolarssl-dev` if you want to use PolarSSL, but it is deprecated as it's not longer being supported.
- `# apt install libsoxr-dev` if you want support for libsoxr-based resampling. This library is in recent distributions; if not, instructions for how to build it from source on Linux are available at [LIBSOXR.md](https://github.com/mikebrady/shairport-sync/blob/master/LIBSOXR.md).
- `# apt install libsndfile1-dev` if you want to use the convolution filter.

If you wish to include the Apple ALAC decoder, you need install it first – please refer to the [ALAC](https://github.com/mikebrady/alac) repository for more information.

**Download Shairport Sync:**
```
$ git clone https://github.com/mikebrady/shairport-sync.git
```

Next, `cd` into the shairport-sync directory and execute the following commands:
```
$ autoreconf -i -f
```
(Note that the `autoreconf...` step may take some time on less powerful machines.)

**Choose the appropriate `--with-*` options:**

(Don't worry -- there's a recommended set of configuration options further down.)

- `--with-alsa` include the ALSA backend module to audio to be output through the Advanced Linux Sound Architecture (ALSA) system directly. This is recommended for highest quality.
- `--with-pa` include the PulseAudio audio back end. This is recommended if your Linux installation already has PulseAudio installed. Although ALSA would be better, it requires direct and exclusive access to to a real (hardware) soundcard, and this is often impractical if PulseAudio is installed.
- `--with-stdout` include an optional backend module to enable raw audio to be output through standard output (stdout).
- `--with-pipe` include an optional backend module to enable raw audio to be output through a unix pipe.
- `--with-soundio` include an optional backend module to enable raw audio to be output through the soundio system.
- `--with-avahi` or `--with-tinysvcmdns` for mdns support. Avahi is a widely-used system-wide zero-configuration networking (zeroconf) service — it may already be in your system. If you don't have Avahi, or similar, then consider including tinysvcmdns, which is a tiny zeroconf service embedded inside the shairport-sync application itself. To enable multicast for `tinysvcmdns`, you may have to add a default route with the following command: `route add -net 224.0.0.0 netmask 224.0.0.0 eth0` (substitute the correct network port for `eth0`). You should not have more than one zeroconf service on the same system — bad things may happen, according to RFC 6762, §15.
- `--with-ssl=openssl`, `--with-ssl=mbedtls` or `--with-ssl=polarssl` (deprecated) for encryption and related utilities using either OpenSSL, mbed TLS or PolarSSL.
- `--with-libdaemon` include a demonising library needed if you want to be able to demonise Shairport Sync with the `-d` option. Not needed for `systemd`-based systems which demonise programs differently.
- `--with-soxr` for libsoxr-based resampling.
- `--with-piddir` for specifying where the PID file should be stored. This directory is normally chosen automatically. The directory must be writable. If you use this option, you may have to edit the init script to search for the PID file in your new location.
- `--with-metadata` to add support for Shairport Sync to pipe metadata to a compatible application of your choice. See https://github.com/mikebrady/shairport-sync-metadata-reader for a sample metadata reader.
- `--with-configfiles` to install a configuration file and a separate sample file at the `make install` stage. Default is to install. An existing `/etc/shairport-sync.conf` will not be overwritten.
- `--with-pkg-config` to use pkg-config to find libraries. Default is to use pkg-config — this option is for special purpose use.
- `--with-apple-alac` to include the Apple ALAC Decoder.
- `--with-convolution` to include a convolution filter that can be used to apply effects such as frequency and phase correction, and a loudness filter that compensates for human ear non-linearity. Requires `libsndfile`.
- `--with-systemd` to include a script to create a Shairport Sync service that can optionally launch automatically at startup on `systemd`-based Linuxes. Default is not to to install.
- `--with-systemv` to include a script to create a Shairport Sync service that can optionally launch automatically at startup on System V based Linuxes. Default is not to to install.

**Determine if it's a `systemd` or a "System V" installation:**

If you wish to have Shairport Sync start automatically when your system boots, you need to figure out what so-called "init system" your system is using. (If you are using Shairport Sync with PulseAudio, as installed in many desktop systems, this section doesn't apply.)

There are a number of init systems in use: `systemd`, `upstart` and "System V" among others, and it's actually difficult to be certain which one your system is using. Fortunately, for Shairport Sync, all you have to do is figure out if it's a `systemd` init system or not. If it is not a `systemd` init system, you can assume that it is either a System V init system or else it is compatible with a System V init system. Recent systems tend to use `systemd`, whereas older systems use `upstart` or the earlier System V init system.

The easiest way is to look at the first few lines of the `init` manual. Enter the command:

```
$ man init
```
In a `systemd` system, the top lines of the `man` page make it clear that it's a `systemd` system, as follows (from Ubuntu 16.04):
```
SYSTEMD(1)                          systemd                         SYSTEMD(1)

NAME
       systemd, init - systemd system and service manager
...
```
Other init systems will look considerably different. For instance, in an `upstart` system,  the top lines of the `man` page indicate it's using `upstart` system, as follows (from Ubuntu 14.04):

```
init(8)                                    System Manager's Manual                                   init(8)

NAME
       init - Upstart process management daemon

SYNOPSIS
       init [OPTION]...
...
```
In a System V system, the top lines of the `man` page are as follows (from Debian 7.11):
```
INIT(8)               Linux System Administrator's Manual              INIT(8)

NAME
       init, telinit - process control initialization

SYNOPSIS
       /sbin/init [ -a ] [ -s ] [ -b ] [ -z xxx ] [ 0123456Ss ]
       /sbin/telinit [ -t SECONDS ] [ 0123456sSQqabcUu ]
       /sbin/telinit [ -e VAR[=VAL] ]

...
```
If your system is definitely a `systemd` system, choose `--with-systemd` below. Otherwise, choose `--with-libdaemon --with-systemv`.

**Choose the location of the configuration file**

A final consideration is the location of the configuration file `shairport-sync.conf`. This will be placed in the directory specified by the `sysconfdir` configuration variable, which defaults to `/usr/local/etc`. This is normal in BSD Unixes, but is unusual in Linux. Hence, for Linux installations, you need to set the `sysconfdir` variable to `/etc` using the configuration setting `--sysconfdir=/etc`.

**Sample `./configure` command with parameters for a typical Linux `systemd` installation:**

Here is a recommended set of configuration options suitable for Linux installations that use `systemd`, such as Ubuntu 15.10 and later, and Raspbian Buster, Stretch and Jessie. It specifies both the ALSA and PulseAudio backends and includes a sample configuration file and an script for automatic startup on system boot:

`$ ./configure --sysconfdir=/etc --with-alsa --with-pa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemd`

* Omit the `--with-soxr` if the libsoxr library is not available.
* For installation into a System V system, replace the `--with-systemd` with `--with-libdaemon --with-systemv`.
* If you intend to use Shairport Sync with PulseAudio in the standard user mode, it can not be a system service, so you should omit both `--with-systemd` and `--with-systemv`.

**Build and Install the Application:**

Enter:

```
$ make
```

to build the application.

Assuming you have used the `./configure` settings suggested above, you can now install the application:

```
$ sudo make install
```
The user and group `shairport-sync` will be created if necessary, and a runtime folder will be created at `/var/run/shairport-sync` if you have chosen `--with-systemv`. In addition, a `man` page, a default configuration file and automatic power-on startup script will be installed.


**Complete installation into to a `systemd` system**

If you have chosen the `--with-systemd` configuration option, then, to enable Shairport Sync to start automatically at system startup, enter:

```
$ sudo systemctl enable shairport-sync
```
You should then reboot the system.

**Complete installation into a System V system**

If you have chosen the `--with-systemv` configuration option, then, to enable Shairport Sync to start automatically at system startup, enter:
```
$ sudo update-rc.d shairport-sync defaults 90 10
$ sudo update-rc.d shairport-sync enable
```
You should then reboot the system.

**Man Page**

You can view the man page here: http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html

Configuring Shairport Sync
--------
There are two logically distinct parts to getting Shairport Sync to run properly on your machine: (1) starting and stopping it and (2) ensuring it has the right settings.

**(1) Starting and Stopping:**
Starting and stopping Shairport Sync automatically is taken care of differently in different versions of Linux – see the previous section for an example of installing into a `systemd` or a System V based system.

If you are using PulseAudio in the standard "user" mode, you can not start Shairport Sync automatically at boot time – it must be started when the user logs in to a GUI session. Various GUI tools exist to enable you to build a Startup Application. However, there does not appear to be a similar tool for terminating the program when you log out. Also, if the GUI switches to another user, the user's PulseAudio session is disconnected from the output device.

**(2) Settings:**
To get the best from Shairport Sync, you’ll need to (a) give Shairport Sync a service name by which it will be seen in iTunes etc. and (b) specify the backend you wish to use - `alsa` for the ALSA backend, or `pa` for the PulseAudio back end. If only one backend is included at compilation, or if the backend is ALSA, there is no need to explicitly specify the backend.

For the ALSA backend you may need to (c) specify the output device to use and (d) specify the name of the mixer volume control to use to control the output level. To get values for (c) and (d) you might need to explore the ALSA output devices with a program like `alsamixer` or similar.

Shairport Sync reads settings from a configuration file at `/etc/shairport-sync.conf` (note that in FreeBSD it will be at `/usr/local/etc/shairport-sync.conf`). When you run `$sudo make install`, a sample configuration file is installed or updated at `/etc/shairport-sync.conf.sample` (`/usr/local/etc/shairport-sync.conf.sample` in FreeBSD). This contains all the setting groups and all the settings available, but they all are commented out (comments begin with `//`) so that default values are used. The file contains explanations of the settings, useful hints and suggestions. In addition, if the file doesn't already exist, a default configuration is installed, which should work in almost any system with a sound card.

Settings in the configuration file are grouped. For instance, there is a `general` group within which you can use the `name` tag to set the service name. Suppose you wanted to set the name of the service to `Front Room` and give the service the password `secret`, then you should do the following:

```
general =
{
  name = "Front Room";
  password = "secret";
  // ... other general settings
};
```
(Remember, anything preceded by `//` is a comment and will have no effect on the setting of Shairport Sync.)

**Important:** You should *never* use an important password as the AirPlay password for a Shairport Sync player -- the password is stored in Shairport Sync's configuration file in plain text and is thus completely vulnerable.

No backend is specified here, so it will default to the `alsa` backend if more than one back end has been compiled. To route the output to PulseAudio, add:

```
  output_backend = "pa";
```
to the `general` group.

The `alsa` group is used to specify properties of the output device. The most obvious setting is the name of the output device which you can set using the `output_device` tag.

The following `alsa` group settings are very important for maximum performance. If your audio device has a mixer that can be use to control the volume, then Shairport Sync can use it to give instant response to volume and mute commands and it can offload some work from the processor.
* The `mixer_control_name` tag allows you to specify the name of the mixer volume control.
* The `mixer_device` tag allows you specify where the mixer is. By default, the mixer is on the `output_device`, so you only need to use the `mixer_device` tag if the mixer is elsewhere. This can happen if you specify a *device* rather than a *card* with the `output_device` tag, because normally a mixer is associated with a *card* rather than a device. Suppose you wish to use the output device `5` of card `hw:0` and the mixer volume-control named `PCM`:

```
alsa =
{
  output_device = "hw:0,5";
  mixer_device = "hw:0";
  mixer_control_name = "PCM";
  // ... other alsa settings
};
```

The `pa` group is used to specify settings relevant to the PulseAudio backend. You can set the "Application Name" that will appear in the "Sound" control panel.

Shairport Sync can run programs just before it starts to play an audio stream and just after it finishes. You specify them using the `sessioncontrol` group settings `run_this_before_play_begins` and `run_this_after_play_ends`. This is to facilitate situations where something has to be done before and after playing, e.g. switching on an amplifier beforehand and switching it off afterwards. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

Note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line. (This behaviour may be different from other Shairports.)

Shairport Sync can run a program whenever the volume is set or changed. You specify it using the `general` group setting `run_this_when_volume_changes`. This is to facilitate situations where something has to be done when the volume changes, e.g. adjust an external mixer value. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the command has been completed before continuing. Again, please note that the full path to the program must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line.

Note: Shairport Sync can take configuration settings from command line options. This is mainly for backward compatibility, but sometimes still useful. For normal use, it is strongly recommended that you use the configuration file method.

**Raspberry Pi**

The Raspberry Pi Models A and B have a built-in audio DAC that is connected to the device's headphone jack. Apart from a loud click when used for the first time after power-up, it is now quite adequate for casual listening.

To get the benefits of improvements in the Pi's software and firmware, you should update to the Raspian release of October 2018 or later, as a number of improvements have been made to the built-in DAC.

Do the usual update and upgrade:
```
# apt update
# apt upgrade
```
To make Shairport Sync output to the built-in audio DAC and use its hardware mixer, in the `alsa` section of the configuration file, set the output device and mixer as follows:
```
alsa =
{
  output_device = "hw:0"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
  mixer_control_name = "PCM"; // the name of the mixer to use to adjust output volume. If not specified, volume in adjusted in software.
  // ... other alsa settings
```
(Remember to uncomment the lines by removing the `//` at the start of each.) When these changes have been made, reboot the machine.

A problem with the built-in DAC that it declares itself to have a very large mixer volume control range – all the way from -102.38dB up to +4dB, a range of 106.38 dB. In reality, only the top 60 dB of it is in any way usable. To help get the most from it, consider using the `volume_range_db` setting in the `general` group to instruct Shairport Sync to use the top of the DAC mixer's declared range. For example, if you set the `volume_range_db` figure to 60, the top 60 dB of the range will the used. With this setting on the Raspberry Pi, maximum volume will be +4dB and minimum volume will be -56dB, below which muting will occur.

From a user's point of view, the effect of using this setting is to move the minimum usable volume all the way down to the bottom of the user's volume control, rather than have the minimum usable volume concentrated very close to the maximum volume.

*Command Line Arguments*

As previously mentioned, you can use command line arguments to provide settings to Shairport Sync as before, though newer settings will only be available via the configuration file. For full information, please read the Shairport Sync `man` page, also available at  http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html.

Apart from the following options, all command line options can be replaced by settings in the configuration file. Here is a brief description of command line options that are not replicated by settings in the settings file.

* The `-c` option allows you to specify the location of the configuration file.
* The `-V` option gives you version information about  Shairport Sync and then quits.
* The `-d` option causes Shairport Sync to properly daemonise itself, that is, to run in the background. You may need sudo privileges for this.
* The `-k` option causes Shairport Sync to kill an existing Shairport Sync daemon. You may need to have sudo privileges for this.

The System V init script at `/etc/init.d/shairport-sync` has a bare minimum :
`-d`. Basically all it does is put the program in daemon mode. The program will read its settings from the configuration file.

Examples
--------

Here are some examples of complete configuration files.

```
general = {
  name = "Joe's Stereo";
};

alsa = {
  output_device = "hw:0";
};
```

This gives the service a particular name — "Joe's Stereo" and specifies that audio device hw:0 be used.

For best results with the `alsa` backend — including getting true mute and instant response to volume control and pause commands — you should access the hardware volume controls. Use `amixer` or `alsamixer` or similar to discover the name of the mixer control to be used as the `mixer_control_name`.

Here is an example for for a Raspberry Pi using its internal soundcard — device hw:0 — that drives the headphone jack:
```
general = {
  name = "Mike's Boombox";
};

alsa = {
  output_device = "hw:0";
  mixer_control_name = "PCM";
};
```

Here is an example of driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC and which is connected as audio device `hw:1`:
```
general = {
  name = "Kitchen";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "PCM";
};
```

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:
```
general = {
  name = "Front Room";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "Speaker";
};
```

For a first generation Griffin iMic on a Raspberry Pi:
```
general = {
  name = "Attic";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "PCM";
};
```

For an NSLU2, which has no internal sound card, there appears to be a bug in ALSA — you can not specify a device other than "default". Thus:

On an NSLU2, to drive a first generation Griffin iMic:
```
general = {
  name = "Den";
};

alsa = {
  mixer_control_name = "PCM";
};
```

On an NSLU2, to drive the "3D Sound" USB card:
```
general = {
  name = "TV Room";
};

alsa = {
  mixer_control_name = "Speaker";
};
```

Finally, here is an example of using the PulseAudio backend:
```
general = {
  name = "Zoe's Computer";
  output_backend = "pa";
};

```


Metadata
--------

Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in macOS and iOS.

**Metadata broadcasting over UDP**

As an alternative to sending metadata to a pipe, the `socket_address` and `socket_port` tags may be set in the metadata group to cause Shairport Sync to broadcast UDP packets containing the track metadata.

The advantage of UDP is that packets can be sent to a single listener or, if a multicast address is used, to multiple listeners. It also allows metadata to be routed to a different host. However UDP has a maximum packet size of about 65000 bytes; while large enough for most data, Cover Art will often exceed this value. Any metadata exceeding this limit will not be sent over the socket interface. The maximum packet size may be set with the `socket_msglength` tag to any value between 500 and 65000 to control this - lower values may be used to ensure that each UDP packet is sent in a single network frame. The default is 500. Other than this restriction, metadata sent over the socket interface is identical to metadata sent over the pipe interface.

The UDP metadata format is very simple - the first four bytes are the metadata *type*, and the next four bytes are the metadata *code* (both are sent in network byte order - see https://github.com/mikebrady/shairport-sync-metadata-reader for a definition of those terms). The remaining bytes of the packet, if any, make up the raw value of the metadata.

Latency
-------
Latency is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the audio output device, usually a Digital to Audio Converter (DAC), irrespective of any internal delays, processing times, etc. in the computer.

Shairport Sync uses latencies supplied by the source, typically either 2 seconds or just over 2.25 seconds. You shouldn't need to change them.

Problems can arise when you are trying to synchronise with speaker systems — typically surround-sound home theatre systems — that have their own inherent delays. You can compensate for an inherent delay using the appropriate backend (typically `alsa`) `audio_backend_latency_offset_in_seconds`. Set this offset (in frames) to compensate for a fixed delay in the audio back end; for example, if the output device delays by 100 ms, set this to -0.1.

Resynchronisation
-------------
Shairport Sync actively maintains synchronisation with the source.
If synchronisation is lost — say due to a busy source or a congested network — Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 0.050 seconds — i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
* You can vary the resync threshold, or turn resync off completely, with the `general` `resync_threshold_in_seconds` setting.

Tolerance
---------
Playback synchronisation is allowed to wander — to "drift" — a small amount before attempting to correct it. The default is 0.002 seconds, i.e. 2 ms. The smaller the tolerance, the  more  likely it is that overcorrection will occur. Overcorrection is when more corrections (insertions and deletions) are made than are strictly necessary to keep the stream in sync. Use the `statistics` setting to monitor correction levels. Corrections should not greatly exceed net corrections.
* You can vary the tolerance with the `general` `drift_tolerance_in_seconds` setting.

Some Statistics
---------------
If you turn on the `general`  `statistics` setting, a heading like this will be output to the log file (or to `STDERR` if the `-u` command line option is chosen):
```
sync error in milliseconds, net correction in ppm, corrections in ppm, total packets, missing packets, late packets, too late packets, resend requests, min DAC queue size, min buffer occupancy, max buffer occupancy, source nominal frames per second, source actual frames per second, output frames per second, source clock drift in ppm, source clock drift sample count, rough calculated correction in ppm
```
This will be followed by the statistics themselves at regular intervals, for example:
```
      0.15,       0.0,       0.0,        1003,      0,      0,      0,      0,   8539,  208,  225,   44099.95,   44274.21,   44099.68,      0.00,     0,     -6.12
      0.34,       0.0,       0.0,        2006,      0,      0,      0,      0,   8591,  206,  225,   44099.99,   44169.96,   44099.76,      0.00,     0,     -5.27
      0.89,       0.0,       0.0,        3009,      0,      0,      0,      0,   8571,  218,  225,   44100.01,   44152.81,   44099.85,      0.00,     0,     -3.66
      0.85,       0.0,       0.0,        4012,      0,      0,      0,      0,   8619,  218,  225,   44100.00,   44142.80,   44099.80,      0.00,     0,     -4.62
      1.29,       0.0,       0.0,        5015,      0,      0,      0,      0,   8623,  218,  225,   44100.00,   44129.39,   44099.83,      0.00,     0,     -3.85
      1.41,       0.0,       0.0,        6018,      0,      0,      0,      0,   8619,  216,  225,   44099.99,   44127.74,   44099.86,      0.00,     0,     -2.76
      1.53,       0.0,       0.0,        7021,      0,      0,      0,      0,   8603,  218,  225,   44100.00,   44109.60,   44099.81,      0.00,     0,     -4.21
      1.81,       0.0,       0.0,        8024,      0,      0,      0,      0,   8655,  215,  225,   44100.00,   44110.01,   44099.82,      0.00,     0,     -4.16
      2.00,     -82.1,      82.1,        9027,      0,      0,      0,      0,   8481,  208,  225,   44100.00,   44110.69,   44099.83,      0.00,     0,     -3.97
      1.96,      -8.5,       8.5,       10030,      0,      0,      0,      0,   8599,  209,  225,   44099.99,   44108.84,   44099.86,      0.00,     0,     -3.02
      1.94,       0.0,       0.0,       11033,      0,      0,      0,      0,   8583,  217,  225,   44100.00,   44109.33,   44099.87,      0.00,     0,     -2.93
      1.90,      -5.7,       5.7,       12036,      0,      0,      0,      0,   8601,  219,  225,   44100.00,   44110.01,   44099.85,     20.47,    10,    -23.88
      2.02,     -36.8,      36.8,       13039,      0,      0,      0,      0,   8488,  216,  225,   44100.00,   44107.18,   44099.85,     20.39,    12,    -23.83
      1.96,     -17.0,      17.0,       14042,      0,      0,      0,      0,   8554,  218,  226,   44100.00,   44108.80,   44099.84,     20.63,    15,    -24.13
      1.99,     -31.2,      31.2,       15045,      0,      0,      0,      0,   8592,  216,  225,   44100.00,   44106.85,   44099.85,     20.95,    18,    -24.39
      1.98,     -28.3,      28.3,       16048,      0,      0,      0,      0,   8627,  219,  225,   44099.99,   44109.01,   44099.88,     21.23,    20,    -23.90
      2.01,     -31.2,      31.2,       17051,      0,      0,      0,      0,   8582,  217,  225,   44100.00,   44108.86,   44099.84,     21.57,    22,    -25.10
      1.96,     -19.8,      19.8,       18054,      0,      0,      0,      0,   8556,  215,  226,   44100.00,   44103.79,   44099.86,     21.59,    24,    -24.85
      1.98,     -25.5,      25.5,       19057,      0,      0,      0,      0,   8563,  219,  225,   44100.00,   44102.62,   44099.86,     22.26,    25,    -25.47
      2.00,     -28.3,      28.3,       20060,      0,      0,      0,      0,   8569,  214,  225,   44100.00,   44103.22,   44099.88,     22.57,    27,    -25.41
      1.98,     -42.5,      42.5,       21063,      0,      0,      0,      0,   8570,  216,  225,   44100.00,   44104.14,   44099.85,     22.74,    28,    -26.08
      1.89,     -31.2,      31.2,       22066,      0,      0,      0,      0,   8524,  218,  225,   44100.00,   44104.66,   44099.87,     22.89,    30,    -25.89
      1.96,       0.0,       0.0,       23069,      0,      0,      0,      0,   8650,  218,  225,   44100.00,   44105.36,   44099.88,     22.92,    32,    -25.73
      1.98,     -34.0,      34.0,       24072,      0,      0,      0,      0,   8475,  219,  225,   44100.00,   44106.04,   44099.88,     22.90,    34,    -25.70
      2.07,    -138.8,     138.8,       25075,      0,      0,      0,      0,   8490,  218,  226,   44100.00,   44104.93,   44099.87,     23.87,    35,    -26.92
      1.33,       0.0,       0.0,       26078,      0,      0,      0,      0,   8617,  216,  225,   44100.00,   44104.66,   44099.88,     23.68,    38,    -26.43
      1.67,     -22.7,      22.7,       27081,      0,      0,      0,      0,   8562,  217,  225,   44100.00,   44106.16,   44099.87,     25.06,    40,    -27.93
      1.98,     -28.3,      28.3,       28084,      0,      0,      0,      0,   8544,  218,  225,   44100.00,   44105.21,   44099.87,     25.91,    41,    -28.84
      1.99,     -22.7,      22.7,       29087,      0,      0,      0,      0,   8569,  219,  225,   44100.00,   44102.30,   44099.87,     25.91,    41,    -28.74
      1.66,     -56.6,      56.6,       30090,      0,      0,      0,      0,   8554,  218,  225,   44100.00,   44103.07,   44099.86,     26.38,    44,    -29.46
      1.44,       0.0,       0.0,       31093,      0,      0,      0,      0,   8642,  218,  225,   44100.00,   44102.18,   44099.87,     26.08,    46,    -28.98
      1.56,       0.0,       0.0,       32096,      0,      0,      0,      0,   8647,  218,  225,   44100.00,   44102.76,   44099.87,     25.79,    48,    -28.64
      1.68,       0.0,       0.0,       33099,      0,      0,      0,      0,   8647,  217,  225,   44100.00,   44103.41,   44099.88,     25.66,    49,    -28.51
      1.92,     -14.2,      14.2,       34102,      0,      0,      0,      0,   8639,  217,  225,   44100.00,   44102.38,   44099.87,     25.45,    51,    -28.40
      2.00,     -45.3,      45.3,       35105,      0,      0,      0,      0,   8593,  216,  225,   44100.00,   44103.23,   44099.86,     25.35,    53,    -28.61
      1.98,     -17.0,      17.0,       36108,      0,      0,      0,      0,   8609,  218,  225,   44100.00,   44102.54,   44099.87,     25.21,    55,    -28.12
      1.99,     -31.2,      31.2,       37111,      0,      0,      0,      0,   8575,  217,  225,   44100.00,   44101.98,   44099.88,     25.11,    57,    -27.88
      1.99,     -14.2,      14.2,       38114,      0,      0,      0,      0,   8624,  218,  225,   44100.00,   44104.44,   44099.87,     24.95,    60,    -27.97
      1.99,     -28.3,      28.3,       39117,      0,      0,      0,      0,   8582,  209,  225,   44100.00,   44104.91,   44099.86,     24.88,    62,    -27.98
      1.86,     -17.0,      17.0,       40120,      0,      0,      0,      0,   8504,  207,  225,   44100.00,   44104.52,   44099.88,     24.66,    63,    -27.39
      1.96,     -56.6,      56.6,       41123,      0,      0,      0,      0,   8517,  212,  226,   44100.00,   44102.29,   44099.88,     24.54,    66,    -27.34
      1.90,     -36.8,      36.8,       42126,      0,      0,      0,      0,   8363,  218,  240,   44100.00,   44116.79,   44099.87,     24.55,    68,    -27.48
      1.89,       0.0,       0.0,       43129,      0,      0,      0,      0,   8487,  227,  241,   44100.00,   44117.57,   44099.88,     24.56,    69,    -27.32
      1.99,     -22.7,      22.7,       44132,      0,      0,      0,      0,   8576,  227,  240,   44100.00,   44118.53,   44099.87,     24.57,    70,    -27.44
      1.98,     -39.7,      39.7,       45135,      0,      0,      0,      0,   8593,  228,  240,   44100.00,   44113.88,   44099.87,     24.60,    73,    -27.43
      2.00,     -34.0,      34.0,       46138,      0,      0,      0,      0,   8577,  227,  240,   44100.00,   44117.10,   44099.88,     24.65,    75,    -27.46
      1.98,     -22.7,      22.7,       47141,      0,      0,      0,      0,   8591,  204,  240,   44100.00,   44102.78,   44099.87,     24.67,    77,    -27.59
      1.98,     -17.0,      17.0,       48144,      0,      0,      0,      0,   8587,  216,  225,   44100.00,   44102.38,   44099.88,     24.67,    79,    -27.42
      1.99,     -28.3,      28.3,       49147,      0,      0,      0,      0,   8590,  214,  225,   44100.00,   44102.70,   44099.87,     24.66,    80,    -27.57
      1.85,     -62.3,      62.3,       50150,      0,      0,      0,      0,   8518,  217,  226,   44100.00,   44103.16,   44099.87,     24.70,    83,    -27.55
      1.90,     -28.3,      28.3,       51153,      0,      0,      0,      0,   8617,  216,  225,   44100.00,   44102.60,   44099.88,     24.66,    85,    -27.42
      1.89,      -5.7,       5.7,       52156,      0,      0,      0,      0,   8615,  218,  225,   44100.00,   44103.37,   44099.88,     24.65,    86,    -27.39
      1.96,     -19.8,      19.8,       53159,      0,      0,      0,      0,   8600,  219,  225,   44100.00,   44103.77,   44099.87,     24.62,    89,    -27.53
      1.99,     -22.7,      22.7,       54162,      0,      0,      0,      0,   8567,  216,  225,   44100.00,   44103.12,   44099.87,     24.57,    92,    -27.45
```

"Sync error in milliseconds" is the average deviation from exact synchronisation. The first line of the example above indicates that the output is on average 0.15 milliseconds behind exact synchronisation. Sync is allowed to drift by the `general` `drift_tolerance_in_seconds` setting — (± 0.002 seconds) by default — before a correction will be made.

"Net correction in ppm" is actually the net sum of corrections — the number of frame insertions less the number of frame deletions — given as a moving average in parts per million. After an initial settling period, it represents the drift — the divergence between the rate at which frames are generated at the source and the rate at which the output device consumes them. In this case, the drift is negligible, but it can routinely be up to 150 ppm, especially with older machines.

"Corrections in ppm" is the number of frame insertions plus the number of frame deletions (i.e. the total number of corrections), given as a moving average in parts per million. The closer this is to the net corrections, the fewer "unnecessary" corrections are being made. Third party programs tend to have much larger levels of corrections.

"Min DAC queue size" is the minimum size the queue of samples in the output device's hardware buffer was measured at. It is meant to stand at 0.2 seconds = 8,820 frames at 44,100 frames per second, and will go low if the processor is very busy. If it goes below about 2,000 then it's an indication that the processor can't really keep up.

"Source nominal frames per second" is the rate at which audio is being sent to Shairport Sync according to information supplied by the source itself.

"Source actual frames per second" is the rate at which frames of audio are actually received by Shairport Sync. This can vary a great deal due to network conditions, but over a long time (more than 30 minutes) it should settle down to an accurate value. It does not account for retransmitted packets.

"Output frames per second" is the actual rate at which frames of audio are taken by the output device. On a system with a well-conditioned `ntp`-based clock (and without output underruns) this figure should be very accurate after playing material continuously for a period.

"Source clock drift in ppm" is an estimate of the difference in timekeeping between the audio source and the Shairport Sync device. It is calculated from a linear regression of drift sample data. The number of samples the estimate is based on is given in the next column, "Source clock drift sample count".

"Rough calculated correction in ppm" is a very crude estimate of the amount of interpolation that needs to applied, on average, to keep sync. It is not really to be relied on at this time.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time. The above sample is from an iPhone XS Max to Shairport Sync running on a WiFi-connected Raspberry Pi 3 using an [IQaudIO Pi-DigiAMP+](http://iqaudio.co.uk/hats/9-pi-digiamp.html).

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport Sync device is over WiFi. Late packets can sometimes be asked for and received multiple times. Sometimes late packets are sent and arrive too late, but have already been sent and received in time, so weren't needed anyway...


WiFi Issues
---------
If you are using WiFi, you should ensure that WiFi power management is off. See [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md) for more details.

Troubleshooting
---------------
Please refer to [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md) for a few hints, contributed by users.

MQTT
---------------
Please refer to the [MQTT INFO](https://github.com/mikebrady/shairport-sync/blob/master/MQTT.md) page for additional info on building, configuring and using MQTT to interface shairport-sync with common home automation systems (contributed by users).

Packaging status
----------------

[![Packaging status](https://repology.org/badge/vertical-allrepos/shairport-sync.svg)](https://repology.org/project/shairport-sync/versions)
