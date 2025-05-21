# Build and Install Shairport Sync
This guide is for a basic installation of Shairport Sync in a recent (2018 onwards) Linux or FreeBSD.

Shairport Sync can be built as an AirPlay 2 player (with [some limitations](AIRPLAY2.md#features-and-limitations)) or as "classic" Shairport Sync – a player for the older, but still supported, AirPlay (aka "AirPlay 1") protocol. Check ["What You Need"](AIRPLAY2.md#what-you-need) for some basic system requirements.

Overall, you'll be building and installing two programs – Shairport Sync itself and [NQPTP](https://github.com/mikebrady/nqptp), a companion app that Shairport Sync uses for AirPlay 2 timing. If you are building classic Shairport Sync, NQPTP is unnecessary and can be omitted.

In the commands below, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `apt-get update` in superuser mode and you are in user mode, enter `sudo apt-get update`.

## 1. Prepare
#### Remove Old Copies of Shairport Sync
Before you begin building Shairport Sync, it's best to remove any existing copies of the application, called `shairport-sync`. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
```
Remove it as follows:
```
# rm /usr/local/bin/shairport-sync
```
Do this until no more copies of `shairport-sync` are found.
#### Remove Old Service Files
You should also remove any of the following service files that may be present:
* `/etc/systemd/system/shairport-sync.service`
* `/etc/systemd/user/shairport-sync.service`
* `/lib/systemd/system/shairport-sync.service`
* `/lib/systemd/user/shairport-sync.service`
* `/etc/init.d/shairport-sync`

New service files will be installed if necessary at the `# make install` stage.
#### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its service files in the last two steps, you should reboot.

## 2. Get Tools and Libraries
Okay, now let's get the tools and libraries for building and installing Shairport Sync (and NQPTP).

### Debian / Raspberry Pi OS / Ubuntu
```
# apt update
# apt upgrade # this is optional but recommended
# apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# apt update
# apt upgrade # this is optional but recommended
# apt-get install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev
```
Building on Ubuntu 24.10, and possibly other distributions, requires systemd-dev. It does no harm to attempt to install it -- it will simply fail if the package doesn't exist:
```
# apt install --no-install-recommends systemd-dev # it's okay if this fails because the package doesn't exist
```
### Fedora (Fedora 40)
For AirPlay 2 operation, _before you install the libraries_, please ensure the you have [enabled](https://docs.fedoraproject.org/en-US/quick-docs/rpmfusion-setup) RPM Fusion software repositories to the "Nonfree" level. If this is not done, the FFmpeg libraries will lack a suitable AAC decoder, preventing Shairport Sync from working in AirPlay 2 mode. 
```
# yum update
# yum install --allowerasing make automake gcc gcc-c++ \
    git autoconf automake avahi-devel libconfig-devel openssl-devel popt-devel soxr-devel \
    ffmpeg ffmpeg-devel libplist-devel libsodium-devel libgcrypt-devel libuuid-devel vim-common \
    alsa-lib-devel
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# yum update
# yum install make automake gcc gcc-c++ \
    git autoconf automake avahi-devel libconfig-devel openssl-devel popt-devel soxr-devel \
    alsa-lib-devel
```
### Arch Linux
After you have installed the libraries, note that you should enable and start the `avahi-daemon` service.
```
# pacman -Syu
# pacman -Sy git base-devel alsa-lib popt libsoxr avahi libconfig \
    libsndfile libsodium ffmpeg vim libplist
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# pacman -Syu
# pacman -Sy git base-devel alsa-lib popt libsoxr avahi libconfig
```
Enable and start the `avahi-daemon` service.
```
# systemctl enable avahi-daemon
# systemctl start avahi-daemon
```
### FreeBSD
First, update everything:
```
# freebsd-update fetch
# freebsd-update install
# pkg
# pkg update
```
Next, install the Avahi subsystem. FYI, `avahi-app` is chosen because it doesn’t require X11. `nss_mdns` is included to allow FreeBSD to resolve mDNS-originated addresses – it's not actually needed by Shairport Sync. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this.
```
# pkg install avahi-app nss_mdns
```
Add these lines to `/etc/rc.conf`:
```
dbus_enable="YES"
avahi_daemon_enable="YES"
```
Next, change the `hosts:` line in `/etc/nsswitch.conf` to
```
hosts: files dns mdns
```
Reboot for these changes to take effect.

Next, install the packages that are needed for Shairport Sync and NQPTP:
```
# pkg install git autotools pkgconf popt libconfig openssl alsa-utils libsoxr \
      libplist libsodium ffmpeg e2fsprogs-libuuid vim
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# pkg install git autotools pkgconf popt libconfig openssl alsa-utils libsoxr
```
## 3. Build
### NQPTP
Skip this section if you are building classic Shairport Sync – NQPTP is not needed for classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp).

### Shairport Sync
#### Build and Install
Download Shairport Sync, branch and configure, compile and install it. Before executing the commands, please note the following:

* If building for FreeBSD, replace `--with-systemd` with `--with-os=freebsd --with-freebsd-service`.
* Omit the `--with-airplay-2` from the `./configure` options if you are building classic Shairport Sync.
* If you wish to add extra features, for example an extra audio backend, take a look at the [configuration flags](CONFIGURATION%20FLAGS.md). For this walkthrough, though, please do not remove the `--with-alsa` flag.

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
$ make
# make install
```
By the way, the `autoreconf` step may take quite a while – please be patient!

## 4. Test
At this point, Shairport Sync should be built and installed but not running. If the user you are logged in as is a member of the unix `audio` group, Shairport Sync should run from the command line:
```
$ shairport-sync
```
* Add the `-v` command line option to get some diagnostics. 
* Add the `--statistics` option to get some infomation about the audio received.

The AirPlay service should appear on the network and the audio you play should come through to the default ALSA device. (Use `alsamixer` or similar to adjust levels.)

If you have problems, please check the items in Final Notes below, or in the [TROUBLESHOOTING.md](TROUBLESHOOTING.md) guide.

Note: Shairport Sync will run indefinitely -- use Control-C it to stop it.

## 5. Enable and Start Service
If your system has a Graphical User Interface (GUI) it probably uses PulseAudio or PipeWire for audio services. If that is the case, please review [Working with PulseAudio or PipeWire](https://github.com/mikebrady/shairport-sync/blob/master/ADVANCED%20TOPICS/PulseAudioAndPipeWire.md).
Otherwise, once you are happy that Shairport Sync runs from the command line, you should enable and start the `shairport-sync` service. This will launch Shairport Sync automatically as a background "daemon" service when the system powers up:

### Linux
```
# systemctl enable shairport-sync
```
### FreeBSD
To make the `shairport-sync` daemon load at startup, add the following line to `/etc/rc.conf`:
```
shairport_sync_enable="YES"
```
## 6. Check
Reboot the machine. The AirPlay service should once again be visible on the network and audio will be sent to the default ALSA device.

## 7. Final Notes
A number of system settings can affect Shairport Sync. Please review them as follows:

### Power Saving
If your computer has an `Automatic Suspend` Power Saving Option, you should experiment with disabling it, because your computer has to be available for AirPlay service at all times.
### WiFi Power Management – Linux
If you are using WiFi, you should turn off WiFi Power Management:
```
# iwconfig wlan0 power off
```
or
```
# iw dev wlan0 set power_save off
```
The motivation for this is that WiFi Power Management will put the WiFi system in low-power mode when the WiFi system is considered inactive. In this mode, the system may not respond to events initiated from the network, such as AirPlay requests. Hence, WiFi Power Management should be turned off. See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.

(You can find WiFi device names (e.g. `wlan0`) with `$ ifconfig`.)
### Firewall
If a firewall is running (some systems, e.g. Fedora, run a firewall by default), ensure it is not blocking ports needed by Shairport Sync and [NQPTP](https://github.com/mikebrady/nqptp/blob/main/README.md#firewall).

## 8. Connect and enjoy...
### Add to Home
With AirPlay 2, you can follow the steps in [ADDINGTOHOME.md](ADDINGTOHOME.md) to add your device to the Apple Home system.
### Wait, there's more...
Instead of using default values for everything, you can use the configuration file to get finer control over the setup, particularly the output device and mixer control -- see [Finish Setting Up](ADVANCED%20TOPICS/InitialConfiguration.md).

Please take a look at [Advanced Topics](ADVANCED%20TOPICS/README.md) for some ideas about what else you can do to enhance the operation of Shairport Sync. For example, you can adjust synchronisation to compensate for delays in your system.
