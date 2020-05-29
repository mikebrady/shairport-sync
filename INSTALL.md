Simple Installation Instructions
==
Here are simple instructions for building and installing Shairport Sync on a Raspberry Pi B, 2B, 3B, 3B+ or 4B. It is assumed that the Pi is running Raspbian Buster Lite – a GUI isn't needed, since Shairport Sync runs as a daemon program. For a more thorough treatment, please go to the [README.md](https://github.com/mikebrady/shairport-sync/blob/master/README.md#building-and-installing) page.

In the commands below, note the convention that a `#` prompt means you are in superuser mode and a `$` prompt means you are in a regular unprivileged user mode. You can use `sudo` *("SUperuser DO")* to temporarily promote yourself from user to superuser, if permitted. For example, if you want to execute `apt-get update` in superuser mode and you are in user mode, enter `sudo apt-get update`.

### Configure and Update
Do the usual update and upgrade:
```
# apt update
# apt upgrade
``` 
(Separately, if you haven't done so already, consider using the `raspi-config` tool to expand the file system to use the entire card.)

### Turn Off Wi-Fi Power Management
If you are using Wi-Fi, you should turn off Wi-Fi Power Management:
```
# iwconfig wlan0 power off
```
Wi-Fi Power Management will put the Wi-Fi system in low-power mode when the Wi-Fi system considered inactive, and in this mode it may not respond to events initiated from the network, such as AirPlay requests. Hence, Wi-Fi Power Management should be turned off. (See [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/master/TROUBLESHOOTING.md#wifi-adapter-running-in-power-saving--low-power-mode) for more details.)

Reboot the Pi.

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

### Remove Old Startup Scripts
You should also remove the startup script files `/etc/systemd/system/shairport-sync.service` and `/etc/init.d/shairport-sync` if they exist – new ones will be installed if necessary.

### Reboot after Cleaning Up
If you have removed any installations of Shairport Sync or any of its startup script files in the last two steps, you should reboot and continue.

### Build and Install
Okay, now let's get the tools and sources for building and installing Shairport Sync.

First, install the packages needed by Shairport Sync:
```
# apt install -y build-essential git xmltoman autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev
```
Next, download Shairport Sync, configure, compile and install it:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa --with-soxr --with-avahi --with-ssl=openssl --with-systemd
$ make -j4
$ sudo make install
```
The `autoreconf` command may take quite a while to execute on a Raspberry Pi, so please be patient!

Now on to configuring Shairport Sync. Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
```
// Sample Configuration File for Shairport Sync on a Raspberry Pi using the built-in audio DAC
general =
{
  volume_range_db = 60; 
};

alsa =
{
  output_device = "hw:0";
  mixer_control_name = "PCM";
};

```
The `volume_range_db = 60;` setting makes Shairport Sync use only the usable part of the built-in audio card mixer's attenuation range.

The next step is to enable Shairport Sync to start automatically on boot up:
```
# systemctl enable shairport-sync
```
Finally, either reboot the Pi or start the `shairport-sync` service:
```
# systemctl start shairport-sync
```
The Shairport Sync AirPlay service should now appear on the network with a service name made from the Pi's hostname with the first letter capitalised, e.g. hostname `raspberrypi` gives a service name `Raspberrypi`. You can change the service name and set a password in the configuration file.

Connect and enjoy...
