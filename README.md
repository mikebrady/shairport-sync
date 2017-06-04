Shairport Sync
=============
**Welcome to Version 3 of Shairport Sync. This is a big update from Version 2. Please see the Release Notes for more information.**

Shairport Sync is an AirPlay audio player – it plays audio streamed from iTunes, iOS devices and other AirPlay sources such as Quicktime Player and ForkedDaapd, among others.
Audio played by a Shairport Sync-powered device stays synchronised with the source and hence with similar devices playing the same source. In this way, synchronised multi-room audio is possible without difficulty. (Hence the name Shairport Sync, BTW.)

Shairport Sync does not support AirPlay video or photo streaming.

This is the stable "master" branch. Changes and updates are incorporated into this branch relatively slowly. To access the development version, where all the latest changes are made first, please switch to the "development" branch.

**Please note that, between 2.8.4 and 2.8.5, there is a change in the standard ./configure arguments which you should not ignore!** If you are updating Shairport Sync from a version that is 2.8.4 or older, please read the [UPDATING](https://github.com/mikebrady/shairport-sync/blob/master/UPDATING.md) page very carefully.

More Information
----------
Shairport Sync works by using timing information and timestamps present in data coming from the audio source (e.g. an iPhone) to play audio at exactly the right time. It does this by monitoring and controlling the *latency* — the time-gap between when a sound frame is supposed to be played, as specified by its `timestamp`, and the time when it is actually played by the audio output device, usually a Digital to Audio Converter (DAC).

The latency to be used is specified by the source when it negotiates with Shairport Sync. Most sources set a latency of exactly two seconds. Recent versions of iTunes and forkedDaapd use a latency of just over 2.25 seconds.

Timestamps are measured relative to the source computer's clock – the `source clock`, but timing must be done relative to the clock of the computer running Shairport Sync – the `local clock`. This is done by synchronising the two clocks, usually to within a fraction of a millisecond, using a variant of NTP synchronisation protocols. 

To maintain the exact latency required, if an output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up. If the output device is running fast, Shairport Sync will insert frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible on normal audio material. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is less obtrusive than insertion and deletion but requires a good deal of processing power — most embedded devices probably can't support it. The process of insertion/deletion or resampling is rather inelegantly called “stuffing”.

Shairport Sync is a substantial rewrite of the fantastic work done in Shairport 1.0 by James Laird and others — please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0.

Shairport Sync is mainly designed for `alsa` and thus for Linux, although since `alsa` has been ported to FreeBSD, Shairport Sync runs in FreeBSD too. It must have direct access to the output device, which must be a real sound card capable of working with 44,100, 88,200 or 176,400 samples per second, interleaved PCM stereo of 8, 16, 24 or 32 bits. The default is 44,100 samples per second / 16 bits (you'll get a message in the logfile if there's a problem).

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

What else?
--------------
* Better Volume Control — Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder. In addition, Shairport Sync can offer an extended volume control range on devices with a restricted range.
* Hardware Mute — Shairport Sync will mute properly if the hardware supports it.
* Support for the Apple ALAC decoder.
* Bit depths of 8, 16, 24 and 32 bits, rather than the standard 16 bits.
* Fast Response — With hardware volume control, response is instantaneous; otherwise the response time is 0.15 seconds.
* Non-Interruptible — Shairport Sync sends back a "busy" signal if it's already playing audio from another source, so other sources can't disrupt an existing Shairport Sync session. (If a source disappears without warning, the session automatically terminates after two minutes and the device becomes available again.)
* Metadata — Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe  or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in iOS.
* Raw Audio — Shairport Sync can deliver raw PCM audio to standard output or to a pipe. This output is delivered synchronously with the source after the appropriate latency and is not interpolated or "stuffed" on its way through Shairport Sync.
* Autotools and Libtool Support — the Shairport Sync build process uses GNU `autotools` and `libtool` to examine and configure the build environment — important for portability and for cross compilation. Previous versions of Shairport looked at the current system to determine which packages were available, instead of looking at the target system for what packages were available.

Status
------
Shairport Sync works on a wide variety of Linux devices. It works on standard Ubuntu laptops, on the Raspberry Pi with Raspbian Wheezy and Jessie, Arch Linux and OpenWrt, and it runs on a Linksys NSLU2 and a TP-Link 710N using OpenWrt. It works with built-in audio and with a variety of USB-connected audio amplifiers and DACs, including a cheapo USB "3D Sound" dongle, a first generation iMic and a Topping TP30 amplifier with a USB DAC input. It will not work properly — if at all — with a PulseAudio (pseudo-)output device. Using a port of the `alsa` system, Shairport Sync runs rather well on FreeBSD.

Shairport Sync runs well on the Raspberry Pi on USB and I2S cards. It can drive the built-in sound card – see the note below on configuring the Raspberry Pi to make best use of it. 

At the time of writing, OpenWrt trunk does not support USB audio well on the Raspberry Pi.

Shairport Sync runs on Ubuntu, OpenWrt, Debian, Arch Linux and Fedora inside VMWare Fusion on a Mac, but synchronisation in inaccurate — possibly because the soundcard is being emulated.

Shairport Sync will output to `alsa` cards, to standard output and to pipes using appropriate backends. You can try compiling additional backends in as you wish, but it definitely will not work properly with them. Maybe someday...

For information about changes and updates, please refer to the RELEASENOTES.md file in the distribution.

Note: Historically, Shairport Sync has taken its settings from command line arguments. While this is still the case, it does not always work well across distributions. Accordingly, from version 2.4 onwards, Shairport Sync reads settings from the file `/etc/shairport-sync.conf`. Access to new features will only be provided via the settings file.

Building And Installing
---------------------

Shairport Sync may already be available as a package in your Linux distribution (search for `shairport-sync` – the package named `shairport` is a different program). Packages are available on recent versions of Debian, Ubuntu, Arch, OpenWrt and possibly more. If you wish to build and install the latest version of Shairport Sync on OpenWrt, Arch or Fedora platforms, please follow the appropriate instructions below. Limited support is also available for Mac OS X. Otherwise follow the General Build Instructions. Then, when the program has been installed, refer to the section on Configuring Shairport Sync that follows.

**Note**

The following procedures will install the `shairport-sync` application into your system. Before continuing, you should check to see if `shairport-sync` is already installed – you can use the command `$ which shairport-sync` to find where it is located, if installed. If it is installed you should delete it – you may need superuser privileges. After deleting, check again in case further copies are installed elsewhere.
(If the existing installation of `shairport-sync` is where the new copy will be installed into, it will be overwritten;  sometimes, however, the installation is to another location, so it is safer, initially, to delete previous versions manually.)

**Ubuntu:**
Personal Package Archives for Shairport Sync master and development branches are available at https://launchpad.net/~dantheperson. A `shairport-sync` installer package is available in Ubuntu 16.04.

**Debian:**
`shairport-sync` is in the Debian archive and is scheduled for release with Debian Stretch (9): https://tracker.debian.org/shairport-sync. A backport for Debian Jessie (8) may be provided given enough demand.

**OpenWrt:**
There is a Shairport Sync package in OpenWrt `trunk`. Also, there's an OpenWrt package at https://github.com/mikebrady/shairport-sync-for-openwrt, including one that builds back to `Barrier Breaker`.

**Arch Linux:**
Shairport Sync is available for `x86_64` and `i686` platforms in the Arch Linux Community Repository -- search for `shairport-sync`. See also https://www.archlinux.org/packages/.

An Arch Linux installation package, suitable for compilation on any platform, is available at  [EliaCereda/shairport-sync-PKGBUILD](https://github.com/EliaCereda/shairport-sync-PKGBUILD).

**Mac OS X:**
A [HomeBrew](http://brew.sh) package exists for Shairport Sync. With HomeBrew installed, Shairport Sync can be installed using the command `$brew install shairport-sync`. Note that the installation uses the `libao` library and so synchronisation is not available — playback glitches will occur occasionally, when the `ao` system's buffers overflow or underflow.

**Fedora:**
Please see the guide at [FEDORA.md](https://github.com/mikebrady/shairport-sync/blob/master/FEDORA.md).

**Cygwin:**
Please see the guide at [CYGWIN.md](https://github.com/mikebrady/shairport-sync/blob/master/CYGWIN.md).

Sincere thanks to all package contributors!

**General Build Instructions**
The following procedures will install the shairport-sync application into your system. Before continuing, you should check to see if shairport-sync is already installed – you can use the command `$ which shairport-sync` to find where it is located, if installed. If it is installed you should delete it – you may need superuser privileges. After deleting, check again in case further copies are installed elsewhere.

To build Shairport Sync from sources on Debian, Ubuntu, Raspbian, etc. follow these instructions.

The following libraries are required:
* OpenSSL or  mbed TLS (PolarSSL is supported but deprecated)
* Avahi
* ALSA
* libdaemon
* autoconf
* automake
* libtool
* libpopt
* libconfig

Optional:
* libsoxr
* libalac (This is a library containing the Apple ALAC decoder.)

Many Linux distributions have Avahi and OpenSSL already in place, so normally it probably makes sense to choose those options rather than tinysvcmdns or mbed TLS. The `libsoxr` library is available in recent Linux distributions, but it requires lots of processor power — chances are an embedded processor won't be able to keep up.

Debian, Ubuntu and Raspbian users can get the basics with:

- `apt-get install build-essential git xmltoman` – these may already be installed.
- `apt-get install autoconf automake libtool libdaemon-dev libasound2-dev libpopt-dev libconfig-dev`
- `apt-get install avahi-daemon libavahi-client-dev` if you want to use Avahi (recommended).
- `apt-get install libssl-dev` if you want to use OpenSSL and libcrypto, or use mbed TLS otherwise.
- `apt-get install libmbedtls-dev` if you want to use mbed TLS, or use OpenSSL/libcrypto otherwise. You can still use PolarSSL with `apt-get install libpolarssl-dev` if you want to use PolarSSL, but it is deprecated as it's not longer being supported. (It is suggested you use mbed TLS, where available. It doesn't seem to be in Raspbian at the time of writing, March 2017.)
- `apt-get install libsoxr-dev` if you want support for libsoxr-based resampling. This library is in many recent distributions, including Jessie and Raspbian Jessie; if not, instructions for how to build it from source for Rasbpian/Debian Wheezy are available at [LIBSOXR.md](https://github.com/mikebrady/shairport-sync/blob/development/LIBSOXR.md).

If you wish to include the Apple ALAC decoder, you need install it first – please refer to the [ALAC](https://github.com/mikebrady/alac) repository for more information.

**Download Shairport Sync:**

`$ git clone https://github.com/mikebrady/shairport-sync.git`

Next, `cd` into the shairport-sync directory and execute the following command:

```
$ autoreconf -i -f
```
(Note that the `autoreconf...` step may take some time on less powerful machines.)

**Choose the appropriate `--with-*` options:**

- `--with-alsa` for the ALSA audio back end. This is required.
- `--with-stdout` include an optional backend module to enable raw audio to be output through standard output (stdout).
- `--with-pipe` include an optional backend module to enable raw audio to be output through a unix pipe.
- `--with-avahi` or `--with-tinysvcmdns` for mdns support. Avahi is a widely-used system-wide zero-configuration networking (zeroconf) service — it may already be in your system. If you don't have Avahi, or similar, then consider including tinysvcmdns, which is a tiny zeroconf service embedded inside the shairport-sync application itself. To enable multicast for `tinysvcmdns`, you may have to add a default route with the following command: `route add -net 224.0.0.0 netmask 224.0.0.0 eth0` (substitute the correct network port for `eth0`). You should not have more than one zeroconf service on the same system — bad things may happen, according to RFC 6762, §15.
- `--with-ssl=openssl`, `--with-ssl=mbedtls` or `--with-ssl=polarssl` (deprecated) for encryption and related utilities using either OpenSSL, mbed TLS or PolarSSL.
- `--with-soxr` for libsoxr-based resampling.
- `--with-piddir` for specifying where the PID file should be stored. This directory is normally chosen automatically. The directory must be writable. If you use this option, you may have to edit the init script to search for the PID file in your new location.
- `--with-metadata` to add support for Shairport Sync to pipe metadata to a compatible application of your choice. See https://github.com/mikebrady/shairport-sync-metadata-reader for a sample metadata reader.
- `--with-systemv` to install a System V init script at the `make install` stage. Default is not to to install.
- `--with-systemd` to install a systemd service description at the `make install` stage. Default is not to to install.
- `--with-configfile` to install a configuration file and a separate sample file at the `make install` stage. Default is to install. An existing `/etc/shairport-sync.conf` will not be overwritten.
- `--with-pkg-config` to use pkg-config to find libraries. Default is to use pkg-config — this option is for special purpose use.
- `--with-apple-alac` to include the Apple ALAC Decoder.

**Determine if it's a `systemd` or a "System V" installation:**

At the time of writing, there are two widely-used systems for starting programs automatically at startup: `systemd` and "System V" . (There are others, but they are not considered here.) To see if the `systemd` process is running on your system, enter the following command:

`$ ps aux | grep systemd | grep -v grep`

On a system using `systemd` (this is from a Raspberry Pi running Raspbian Jessie) you'll get many lines containing `systemd`, for example:
```
pi@raspberrypi ~ $ ps aux | grep systemd | grep -v grep
root        90  0.1  0.6   8088  2764 ?        Ss   08:00   0:01 /lib/systemd/systemd-journald
root        93  0.0  0.6  11816  3004 ?        Ss   08:00   0:00 /lib/systemd/systemd-udevd
root       502  0.0  0.5   3824  2436 ?        Ss   08:00   0:00 /lib/systemd/systemd-logind
message+   528  0.0  0.7   5568  3172 ?        Ss   08:00   0:01 /usr/bin/dbus-daemon --system --address=systemd: --nofork --nopidfile --systemd-activation
pi         983  0.0  0.7   4912  3256 ?        Ss   08:00   0:00 /lib/systemd/systemd --user
pi@raspberrypi ~ $ 
```
whereas on a system without `systemd` – presumably using System V – (this is a from a Raspberry Pi running Raspbian Wheezy) , you'll get nothing:
```
pi@raspberrypi ~ $ ps aux | grep [s]ystemd
pi@raspberrypi ~ $ 
```
Choose `--with-systemd` or `--with-systemv` on the basis of the outcome.

**Choose the location of the configuration file**

A final consideration is the location of the configuration file `shairport-sync.conf`. This will be placed in the directory specified by the `sysconfdir` configuration variable, which defaults to `/usr/local/etc`. This is normal in BSD Unixes, but is unusual in Linux. Hence, for Linux installations, you need to set the `sysconfdir` variable to `/etc` using the configuration setting `--sysconfdir=/etc`.

**Sample `./configure` command with parameters for a typical Linux `systemd` installation:**

Here is an example, suitable for Linux installations that use `systemd`, such as Ubuntu 16.04 and Raspbian Jessie:

`$ ./configure --sysconfdir=/etc --with-alsa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemd`

* Omit the `--with-soxr` if the libsoxr library is not available.
* For installation into a System V system, replace the `--with-systemd` with `--with-systemv`.

**Build the application:**

Enter:

`$ make` 

to build the application.

**Install into to a `systemd` system**
To complete the installation, you need to define a `shairport-sync` group and user. This is a security measure – the user and group are relatively unprivileged, and the user does not have login priviliges. The user must be a member of the `audio` group to be able to access the audio hardware. The following commands define the group and user correctly if they do not already exist (note the use of `sudo` – omit this if you already have superuser privileges:

```
$ getent group shairport-sync &>/dev/null || sudo groupadd -r shairport-sync >/dev/null
$ getent passwd shairport-sync &> /dev/null || sudo useradd -r -M -g shairport-sync -s /usr/bin/nologin -G audio shairport-sync >/dev/null
```

Next, enter:

```
$ sudo make install
```

to install `shairport-sync` along with a `man` page, a default configuration file and a `systemd` configuration file called `shairport-sync.service` to launch it automatically at system startup.

To enable Shairport Sync to start automatically at system startup, enter:

`$ sudo systemctl enable shairport-sync`

**Install into a System V system**
If you are installing onto a System V system, enter:

```
$ sudo make install
```

to install `shairport-sync` along with a `man` page, a default configuration file and a System V startup script to launch it automatically at system startup.

To complete the installation, enter:
```
$ sudo update-rc.d shairport-sync defaults 90 10
```

**Man Page**

You can view the man page here: http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html

Configuring Shairport Sync
--------
There are two logically distinct parts to getting Shairport Sync to run properly on your machine — (1) starting and stopping it and (2) ensuring it has the right settings.

**(1) Starting and Stopping:**
Starting and stopping Shairport Sync automatically is taken care of differently in different versions of Linux – see the previous section for an example of installing into a `systemd` or a System V based system.

**(2) Settings:**
To get the best from Shairport Sync, you’ll need to (a) give Shairport Sync a service name by which it will be seen in iTunes etc., (b) specify the output device to use and (c) specify the name of the mixer volume control to use to control the output level. To get values for (b) and (c) you might need to explore the ALSA output devices with a program like `alsamixer` or similar.

Shairport Sync reads settings from a configuration file at `/etc/shairport-sync.conf`. When you run `$sudo make install`, a sample configuration file is installed (or updated) at `/etc/shairport-sync.conf.sample`. This contains all the setting groups and all the settings available, but they all are commented out (comments begin with `//`) so that default values are used. The file contains explanations of the settings, useful hints and suggestions. In addition, if the file doesn't already exist, a default configuration is installed at `/etc/shairport-sync.conf`, which should work in almost any system with a sound card.

Settings in the configuration file are grouped. For instance, there is a `general` group within which you can use the `name` tag to set the service name. Suppose you wanted to set the name of the service to `Front Room`, give the service the password `secret` and used `libsoxr` interpolation, then you should do the following:

```
general =
{
	name = "Front Room";
	password = "secret";
	interpolation = "soxr";
	// ... other general settings
};
```
(Remember, anything preceded by `//` is a comment and will have no effect on the setting of Shairport Sync.)

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

Shairport Sync can run programs just before it starts to play an audio stream and just after it finishes. You specify them using the `sessioncontrol` group settings `run_this_before_play_begins` and `run_this_after_play_ends`. This is to facilitate situations where something has to be done before and after playing, e.g. switching on an amplifier beforehand and switching it off afterwards. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

Please note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable and have the standard `#!/bin/...` first line. (This behaviour may be different from other Shairports.)

Note: Shairport Sync can take configuration settings from command line options. This is mainly for backward compatability, but sometimes still useful. For normal use, it is strongly recommended that you use the configuration file method.

**Raspberry Pi**
The Raspberry Pi has a built-in audio DAC that is connected to the device's headphone jack. An updated audio driver has greatly improved the quality of the output – see [#525](https://github.com/mikebrady/shairport-sync/issues/525) for details. To activate the updated driver, add the line:
```
audio_pwm_mode=2
```
to `/boot/config.txt` and reboot.
Apart from a loud click when used for the first time after power-up, it is quite adequate for casual listening. A problem is that it declares itself to have a very large mixer volume control range – all the way from -102.38dB up to +4dB, a range of 106.38 dB. In reality, only the top 30dB of it is in any way usable. To help get the most from the DAC, consider using the `volume_range_db` setting in the `general` group to instruct Shairport Sync to use the top of the DAC mixer's declared range. For example, if you set the `volume_range_db` figure to 30, the top 30 dB of the range will the used. With this setting on the Raspberry Pi, maximum volume will be +4dB and minimum volume will be -26dB, below which muting will occur.

From a user's point of view, the effect of using this setting is to move the minimum usable volume all the way down to the bottom of the user's volume control, rather than have the minimum usable volume concentrated very close to the maximum volume.

Another setting to consider is the `general` `drift_tolerance_in_seconds` setting: you should set it to a larger tolerance, say 10 milliseconds – `drift_tolerance_in_seconds=0.010;` – to reduce the amount of overcorrection that seems to occur when using the Raspberry Pi's built-in DAC. 

*Command Line Arguments*

As previously mentioned, you can use command line arguments to provide settings to Shairport Sync as before, though newer settings will only be available via the configuration file. For full information, please read the Shairport Sync `man` page, also available at  http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html.

Apart from the following options, all command line options can be replaced by settings in the configuration file. Here is a brief description of command line options that are not replicated by settings in the settings file.

* The `-c` option allows you to specify the location of the configuration file — default is `/etc/shairport-sync.conf`.
* The `-V` option gives you version information about  Shairport Sync and then quits.
* The `-d` option causes Shairport Sync to properly daemonise itself, that is, to run in the background. You may need sudo privileges for this.
* The `-k` option causes Shairport Sync to kill an existing Shairport Sync daemon. You may need to have sudo privileges for this.

The System V init script at `/etc/init.d/shairport-sync` has a bare minimum :
`-d`. Basically all it does is put the program in daemon mode. The program will read its settings from the configuration file, normally `/etc/shairport-sync.conf`.

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

For best results — including getting true mute and instant response to volume control and pause commands — you should access the hardware volume controls. Use `amixer` or `alsamixer` or similar to discover the name of the mixer control to be used as the `mixer_control_name`.

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

Here is an example of using soxr-based resampling and driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC and which is connected as audio device `hw:1`:
```
general = {
  name = "Kitchen";
  interpolation = "soxr";
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

For an NSLU2, which has no internal soundcard, there appears to be a bug in ALSA — you can not specify a device other than "default". Thus:

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

Metadata broadcasting over UDP
------------------------------
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
If you turn on the `general`  `statistics` setting, a heading like this will be output to the console or log file:
```
sync error in milliseconds, net correction in ppm, corrections in ppm, total packets, missing packets, late packets, too late packets, resend requests, min DAC queue size, min buffer occupancy, max buffer occupancy
```
This will be followed by the statistics themselves at regular intervals, for example:
```
      -1.3,      25.9,      25.9,        3758,      0,      0,      0,      0,   4444,  263,  270
      -2.0,      96.0,      96.0,        7516,      0,      0,      0,      0,   5357,  260,  267
      -2.0,      96.0,      96.0,       11274,      0,      0,      0,      0,   4956,  262,  268
      -2.0,      94.5,      94.5,       15032,      0,      0,      0,      0,   5430,  263,  267
      -2.0,      99.8,      99.8,       18790,      0,      0,      0,      0,   5047,  261,  268
      -2.0,      91.5,      91.5,       22548,      0,      0,      0,      0,   5258,  260,  267
      -2.0,      90.7,      90.7,       26306,      0,      0,      0,      0,   5279,  262,  267
      -2.0,      96.0,      96.0,       30064,      0,      0,      0,      0,   5551,  260,  266
```

"Sync error in milliseconds" is the average deviation from exact synchronisation. The first line of the example above indicates that the output is on average 1.3 milliseconds behind exact synchronisation. Sync is allowed to drift by the `general` `drift_tolerance_in_seconds` setting — (± 0.002 seconds) by default — before a correction will be made.

"Net correction in ppm" is actually the net sum of corrections — the number of frame insertions less the number of frame deletions — given as a moving average in parts per million. After an initial settling period, it represents the drift — the divergence between the rate at which frames are generated at the source and the rate at which the output device consumes them. The first line of the example above indicates that the output device is consuming frames 25.9 ppm faster than the source is generating them. The subsequent lines indicate that the stable actual difference in the clocks is around 95 ppm.

"Corrections in ppm" is the number of frame insertions plus the number of frame deletions (i.e. the total number of corrections), given as a moving average in parts per million. The closer this is to the net corrections, the fewer "unnecessary" corrections that are being made. Third party programs tend to have much larger levels of corrections.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time. The above sample is from an Ethernet-connected iMac driving a WiFi-connected Raspberry Pi 3 using an IQaudIO Pi-DigiAMP+.

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport Sync device is over WiFi. Late packets can sometimes be asked for and received multiple times. Sometimes late packets are sent and arrive too late, but have already been sent and received in time, so weren't needed anyway...

"Min DAC queue size" is the minimum size the queue of samples in the output device's hardware buffer was measured at. It is meant to stand at 0.15 seconds = 6,615 frames at 44,100 frames per second, and will go low if the processor is very busy. If it goes below about 2,000 then it's an indication that the processor can't really keep up.

WiFi Issues
---------
If you are using WiFi, you should ensure that WiFi power management is off. See [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/development/TROUBLESHOOTING.md) for more details.

Troubleshooting
---------------
Please refer to [TROUBLESHOOTING](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md) for a few hints, contributed by users.

