Building Shairport Sync for AirPlay 2
==
* Before building Shairport Sync, you might wish to [check the features and limitations](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md#features-and-limitations) of the AirPlay 2 service it provides.
* Recent versions of Linux / FreeBSD are required for AirPlay 2 operation. At the time of writing, May 2, 2021, everything is on the latest version of the software -- macOS 11.3, iOS 14.5, Raspberry Pi OS 5.10.17-v7l+ (Buster), FreeBSD 12.1, Ubuntu 18.04 -- fully updated. A further requirement is an AAC decoder capable of decoding Planar Floating Point `"fltp"` material. Unfortuately, some prominent Linux distributions -- notably Fedora 36 -- do not include this decoder. See [here](https://github.com/mikebrady/shairport-sync/blob/development/TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only) for more on this.
* Be very careful with audio systems capable of very high volume output -- the volume control in this software may not be reliable!
* Shairport Sync relies on a companion program called [`nqptp`](https://github.com/mikebrady/nqptp) to monitor timing signals. This program uses ports 319 and 320 and replaces any PTP service you have on the computer. 
 FYI, most computers do not have a PTP service running. They often use a (totally different) [Network Timing Protocol (NTP)](http://www.ntp.org) service to keep the system clock synchronised with civil time. The POSIX Shared Memory Interface (SMI) Version numbers of `nqptp` and Shairport Sync must match. If they don't, you'll get a message in the logs. It means that one of the programs is out of date with respect to the other.
* If you would like for Shairport Sync to decode Apple ALAC streams, the additional module, [`alac`](https://github.com/mikebrady/alac) is needed.

Instructions
==
Build instructions are different from previous versions of Shairport Sync and may change further. For now, leave the settings in the configuration file at default except as noted below.

Overall, you'll be building and installing two programs. The first one is `nqptp` and the second one is Shairport Sync itself. Build and install `nqptp` first.

In the commands below, for a Debian/Ubuntu-like Linux, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `apt-get update` in superuser mode and you are in user mode, enter `sudo apt-get update`.

### Turn Off WiFi Power Management
If you are using WiFi, you should turn off WiFi Power Management:
```
# iwconfig wlan0 power off
```
WiFi Power Management will put the WiFi system in low-power mode when the WiFi system is considered inactive. In this mode, the system may not respond to events initiated from the network, such as AirPlay requests. Hence, WiFi Power Management should be turned off. (See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.)

### Update Your System
Do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
```
Reboot.

### Remove Old Copies
Before you begin building Shairport Sync, it's best to search for and remove any existing copies of the application, called `shairport-sync`. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
```
Remove it as follows:
```
# rm /usr/local/bin/shairport-sync
```
Do this until no more copies of `shairport-sync` are found.

### Remove Old Service Files
You should also remove any of the following service files that may be present: `/etc/systemd/system/shairport-sync.service`, `/lib/systemd/system/shairport-sync.service`, `/etc/dbus-1/system.d/shairport-sync-dbus.conf`, `/etc/dbus-1/system.d/shairport-sync-mpris.conf`, and `/etc/init.d/shairport-sync`. New ones will be installed if necessary at the `# make install` stage.

### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot.

### Tools and Libraries
Okay, now let's get the tools and libraries for building and installing Shairport Sync.

First, install the packages needed by Shairport Sync:
```
# apt install --no-install-recommends build-essential git xxd xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev
```

### nqptp ###
Download, install, enable and start `nqptp` from [here](https://github.com/mikebrady/nqptp).

### alac ###
If you would like for Shairport Sync to decode Apple ALAC streams, the additional module, [`alac`](https://github.com/mikebrady/alac) is needed. Use the guide in the repository to install it.

### Shairport Sync

#### Please use `git` to clone the repository!
As you probably know, you can download the repository in two ways: (1) using `git` to clone it  -- recommended -- or (2) downloading the repository as a ZIP archive. Please use the `git` method. The reason it that when you use `git`, the build process can incorporate the `git` build information in the version string you get when you execute the command `$ shairport-sync -V`. This will be very useful for identifying the exact build if you are making comments or bug reports. Here is an example:
```
Version with git information:
4.0-dev-138-g2789572-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc

Version without git information:
4.0-dev-AirPlay2-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc
```

#### Build and Install
Download Shairport Sync, check out the `development` branch and configure, compile and install it:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
$ make -j
# make install
```
By the way, the `autoreconf` step may take quite a while -- be patient!
If you installed [`alac`](https://github.com/mikebrady/alac) add the `--with-apple-alac` flag to the ./configure command.

#### Configure
Now to configure Shairport Sync. In this walkthrough, it is configured for an `alsa` output device.

A list of `alsa` output devices is given at the end of the help information. For example, on a Raspberry Pi, at the end of the output from the command `$ shairport-sync -h`, the following appears:

```
...
Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hw:Headphones"
```
Using a program such as `alsamixer`, the name of a mixer (i.e. an attenuator or volume control that could be used to control the output level) can be determined. In this case, the name of the mixer is `Headphone`.

Following this, here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`. Note that everything is case-sensitive:
```
// Sample Configuration File for Shairport Sync on a Raspberry Pi using the built-in audio DAC
general =
{
  volume_range_db = 60;
};

alsa =
{
  output_device = "hw:Headphones";
  mixer_control_name = "Headphone";
};

```
The `volume_range_db = 60;` setting makes Shairport Sync use only the usable part of the Raspberry Pi's built-in audio card mixer's attenuation range. It may not be necessary for other output devices.

#### Automatic Start

To enable Shairport Sync to start automatically on boot up:
```
# systemctl enable shairport-sync
```
Now, either reboot or start the `shairport-sync` service:
```
# systemctl start shairport-sync
```

#### Running From the Command Line

You may wish to run Shairport Sync from the command line (but remember to ensure it is not already running as a daemon). To enable debug messages and statistics, use the following:

```
$ shairport-sync -vu --statistics
```
The user doesn't need to be privileged, but must be a member of the `audio` group for access to the `alsa` subsystem.

### Using Shairport Sync

The Shairport Sync AirPlay service should appear on the network with a service name made from the machine's hostname with the first letter capitalised, e.g. hostname `ubuntu` gives a service name `Ubuntu`. You can change the service name in the configuration file.

Connect and enjoy...

### Adding to Home

Follow the steps in [ADDINGTOHOME.md](https://github.com/mikebrady/shairport-sync/blob/development/ADDINGTOHOME.md) to add your Shairport Sync device to the Apple Home system.

### Update Your Mac!

Many AirPlay 2 bugs have been fixed in recent versions of macOS, so it is strongly recommended that you update your Mac.
