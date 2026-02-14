# Build and Install Shairport Sync
This guide is for a basic installation of Shairport Sync in a recent (2018 onwards) Linux or FreeBSD.

## Important Note – Upgrading to Version 5!

If you have been using Shairport Sync prior to Version 5.0 and are rebuilding or reinstalling Shairport Sync, be aware that a few important things have changed.
While the overall operation of Shairport Sync has not changed much, it is really important to fully remove existing startup scripts and to check and update configuration files. Some important changes are highlighted here:

1. The default sample rate has changed from 44,100 to 48,000 for buffered audio. Real-time audio streams remain at 44,100.
2. Shairport Sync will also play surround sound (5.1 and 7.1) and lossless (48k) audio.
3. Shairport Sync will automatically switch output rates and formats to correspond to input rates and formats. This can be controlled.
4. Many build flags  have changed: for example `--with-systemd` is now `--with-systemd-startup`.
5. Many configuration settings names and facilities have changed. For example `convolution` is now `convolution_enabled`. Another example is that convolution is now multi-threaded, so a new `convolution_thread_pool_size` setting is available.
7. Installation has changed: when Shairport Sync and NQPTP are installed, their startup scripts have changed to provide them with more suitable privileges. You must remove any existing startup scripts.
8. Jack Audio is deprecated and will be removed in a future update. Consider using PipeWire instead.

A useful guide to Version 5 Configuration File Changes is available [here](CONFIGURATIONFILECHANGES5.md). 

## 0. General

Shairport Sync can be built as an AirPlay 2 player (with [some limitations](AIRPLAY2.md#features-and-limitations)) or as classic Shairport Sync – a player for the older, but still supported, "classic" AirPlay (aka "AirPlay 1") protocol. Check ["What You Need"](AIRPLAY2.md#what-you-need) for some basic system requirements.

Note that Shairport Sync does not work well in virtual machines – YMMV.

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
Do this until no more copies of `shairport-sync` are found. Shairport Sync could also be found in `/usr/bin/shairport-sync` or elsewhere.
#### Remove Old Configuration Files
If you want to preserve any configuration settings you have made, you should make a note of them and then delete the configuration file.
This is suggested because there may be new configuration options available, which will be present but disabled in the
updated configuration file that will be installed.
You can then apply your previous settings to the updated configuration file.

The configuration file is typically at `/etc/shairport-sync.conf` or `/usr/local/etc/shairport-sync.conf`.

#### Remove Old Service Files
You should also remove any of the following service files that may be present:
* `/etc/systemd/system/shairport-sync.service`
* `/etc/systemd/user/shairport-sync.service`
* `/lib/systemd/system/shairport-sync.service`
* `/lib/systemd/user/shairport-sync.service`
* `/etc/dbus-1/system.d/shairport-sync-dbus.conf`
* `/etc/dbus-1/system.d/shairport-sync-mpris.conf`
* `/etc/init.d/shairport-sync`
* `~/.config/systemd/user/shairport-sync.service`
  
New service files will be installed if necessary at the `# make install` stage.

(In FreeBSD, there is no need to remove the file at `/usr/local/etc/rc.d/shairport-sync` – it's always replaced in the `make install` step.)
#### Reboot after Cleaning Up
If you removed any installations of Shairport Sync or any of its service or configuration files in the last three steps, you should reboot.

## 2. Get Tools and Libraries
Okay, now let's get the tools and libraries for building and installing Shairport Sync (and NQPTP).

### Debian / Raspberry Pi OS / Ubuntu
```
# apt update
# apt upgrade # this is optional but recommended
# apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev uuid-dev libgcrypt-dev xxd libplist-utils \
    libavutil-dev libavcodec-dev libavformat-dev
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# apt update
# apt upgrade # this is optional but recommended
# apt-get install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libavutil-dev libavcodec-dev libavformat-dev
```
Building on Ubuntu 24.10 or Debian 13 ("Trixie") and later – and possibly on other distributions – requires `systemd-dev`. It does no harm to attempt to install it – the install will simply fail if the package doesn't exist:

```
# apt install --no-install-recommends systemd-dev # it's okay if this fails because the package doesn't exist
```

### Fedora (Fedora 40)
Important: to get the correct version of FFmpeg, _before you install the libraries_, please ensure the you have [enabled](https://docs.fedoraproject.org/en-US/quick-docs/rpmfusion-setup) RPM Fusion software repositories to the "Nonfree" level. If this is not done, the FFmpeg libraries will lack a suitable AAC decoder, preventing Shairport Sync from working in AirPlay 2 mode. 
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
    ffmpeg ffmpeg-devel alsa-lib-devel
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
# pacman -Sy git base-devel alsa-lib popt libsoxr avahi libconfig ffmpeg
```
Enable and start the `avahi-daemon` service.
```
# systemctl enable avahi-daemon
# systemctl start avahi-daemon
```
### FreeBSD
This is for FreeBSD 14.3. First, update everything:
```
# freebsd-update fetch
# freebsd-update install
# pkg update
# pkg upgrade
```
Next, install the Avahi subsystem.
FYI, `avahi-app` is chosen because it doesn’t require X11. If you are using a GUI with your FreeBSD system, select `avahi` rather than `avahi-app`. 
`nss_mdns` is included to allow FreeBSD to resolve mDNS-originated addresses – it's not actually needed by Shairport Sync. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this.
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

Next, install the packages that are needed for Shairport Sync and NQPTP. If you will be using using `--with-sndio` instead of `--with-alsa` – see below – you can omit the `alsa-utils` package.

If you are building Shairport Sync for AirPlay 2, install the following packages:
```
# pkg install git autotools pkgconf popt libconfig openssl alsa-utils libsoxr \
      libplist libsodium ffmpeg libuuid vim
```

If you are building classic Shairport Sync, the list of packages is shorter:
```
# pkg install git autotools pkgconf popt libconfig openssl alsa-utils ffmpeg
```
## 3. Build
### NQPTP
Skip this section if you are building classic Shairport Sync – NQPTP is not needed for classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp).

### Shairport Sync
#### Build and Install
Download Shairport Sync and configure, compile and install it. Before executing the commands, please note the following:

##### Build Options
* **FreeBSD:** For FreeBSD, replace `--with-systemd-startup` with `--with-os=freebsd --with-freebsd-startup`.
  * Optionally, replace `--with-alsa` with the backend for FreeBSD's native audio system `--with-sndio`.
  * If you omit the `--sysconfdir=/etc` entry, `/usr/local/etc` will be used as the `sysconfdir`, which is conventionally used in FreeBSD.
* **Classic Shairport Sync:** For classic Shairport Sync, replace `--with-airplay-2` with `--with-ffmpeg`.
  * You can actually omit `--with-ffmpeg` when building classic Shairport Sync, but it is not recommended. While you'll save space (you can omit the FFMpeg libraries in the build and run-time environments), transcoding, e.g. from 44,100 to 48,000 frames per second, will not be available and less well-maintained and less secure decoders will be used.
* **Extra Features:** If you wish to add extra features, for example an extra audio backend, take a look at the [configuration flags](CONFIGURATION%20FLAGS.md). For this walkthrough, though, please do not change too much!

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi # about 1.5 minutes on a Raspberry Pi B
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd-startup --with-airplay-2
$ make # just over 7 minutes on a Raspberry Pi B
# make install
```
## 4. Test
At this point, Shairport Sync should be built and installed but not running. Now you can test it out from the command line. Before you start testing, though, if you have built Shairport Sync for AirPlay 2 operation, ensure `NQPTP` is running. 

To check the installation, enter:
```
$ shairport-sync
```
* Add the `-v` command line option to get some diagnostics. 
* Add the `--statistics` option to get some infomation about the audio received.


The AirPlay service should appear on the network and the audio you play should come through to the default audio output device. You can use the AirPlay volume control or the system's volume controls or a command-line tool like `alsamixer` to adjust levels.

If you have problems, please check the hints in Final Notes below, or in the [TROUBLESHOOTING.md](TROUBLESHOOTING.md) guide.

When you are finished running Shairport Sync, use Control-C it to stop it.

## 5. Enable and Start Service
### Linux
If your system uses either PipeWire or PulseAudio as sound servers (most "desktop" Linuxes use one or the other), Shairport Sync must be started as a user service. This is because the PipeWire or PulseAudio services – needed by Shairport Sync – are user services themselves, and they must be running before Shairport Sync starts. That implies that Shairport Sync must be started as a user service.

#### Checking for PipeWire or PulseAudio
If PipeWire is installed and running, the following command should return status information, as follows:
```
$ systemctl --user status pipewire
● pipewire.service - PipeWire Multimedia Service
     Loaded: loaded (/usr/lib/systemd/user/pipewire.service; enabled; preset: enabled)
...
```
If not, it will return something like this:
```
$ systemctl --user status pipewire
Unit pipewire.service could not be found.
```
Similarly, if PulseAudio is installed, the following command return status information:
```
$ systemctl --user status pulseaudio
```
If PipeWire or PulseAudio is installed, you must enable Shairport Sync as a _user service_ only.

#### Enable Shairport Sync as a User Service

To enable Shairport Sync as a user service that starts automatically when the user logs in, reboot, ensure you are logged in as that user and then run the following convenience script:
```
$ sh user-service-install.sh
```
This will run a few checks, install a user startup script and start Shairport Sync immediately. (Run `$ sh user-service-install.sh --dry-run` initially if you prefer...)

##### User Service Limitations.
1. If Shairport Sync is installed as a user service, it is activated when that user logs in and deactivated when the user logs out.
On an unattended system, this difficulty can be overcome by using automatic user login.
2. If your system has a GUI (that is, if it's a "desktop Linux"), then audio will only be routed to the speakers when the user is logged in through the GUI.

#### Enable Shairport Sync as a System Service

If your system does not have either PipeWire or PulseAudio installed (see how to check above), then you can enable Shairport Sync as a system service that starts automatically when the system boots up. To do so – assuming you have followed the build guide successfully – enter the following command:
```
# systemctl enable shairport-sync
```
This enables Shairport Sync to start automatically when the system boots up. Please remember, this will not work if PipeWire or PulseAudio are installed on your system.

You should not enable Shairport Sync as a user service and a system service at the same time!

### FreeBSD
To make the `shairport-sync` daemon load at startup, add the following line to `/etc/rc.conf`:
```
shairport_sync_enable="YES"
```
## 6. Check
Reboot the machine. The AirPlay service should once again be visible on the network and audio will be sent to the default audio output.

## 7. Final Notes
A number of system settings can affect Shairport Sync. Please review them as follows:
### ALSA Output Devices
If your system is _not_ using PipeWire or PulseAudio (see [above](#checking-for-pipewire-or-pulseaudio)), it probably uses ALSA directly. The following hints might be useful:
1. To access ALSA output devices, a user must be in the `audio` group. To add the current user to the `audio` group, enter:
   ```
   $ sudo usermod -aG audio $USER
   ```
2. Ensure that the ALSA default output device is not muted and has the volume turned up – `alsamixer` is very useful for this. 
3. To explore the ALSA output devices on your system, consider using the [dacquery](https://github.com/mikebrady/dacquery) tool.

### Power Saving
If your computer has an `Automatic Suspend` Power Saving Option, you should experiment with disabling it, because your computer has to be available for AirPlay service at all times.
### WiFi Power Management – Linux
If you are using WiFi, you should turn off WiFi Power Management. On a Raspberry Pi, for example, you can use the following commands:
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

At this point, you should have a basic functioning Shairport Sync installation. If you want more control – for example, using the ALSA backend, if you want to use a specific DAC, or if you want AirPlay to control the DAC's volume control – you can use settings in the configuration file or you can use command-line options.

#### Configuration Sample File
When you run `# make install`, a configuration file is installed if one doesn't already exist. Additionally, a sample configuration file called `shairport-sync.conf.sample` is _always_ installed. This contains all the setting groups and all the settings available, commented out so that default values are used. The file contains explanations of the settings, useful hints and suggestions. The configuration file and the sample configuration file are installed in the `sysconfdir` you specified at the `./configure...` step above.

Please take a look at [Advanced Topics](ADVANCED%20TOPICS/README.md) for some ideas about what else you can do to enhance the operation of Shairport Sync. For example, you can adjust synchronisation to compensate for delays in your system.
