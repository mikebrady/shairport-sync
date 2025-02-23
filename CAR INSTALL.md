# Shairport Sync for Cars
If your car audio has an AUX input, you can get AirPlay in your car using Shairport Sync. Together, Shairport Sync and an iPhone or an iPad with cellular capability can give you access to internet radio, YouTube, Apple Music, Spotify, etc. on the move. While Shairport Sync is no substitute for CarPlay, the audio quality is often much better than Bluetooth.

## The Basic Idea

The basic idea is to use a small Linux computer to create an isolated WiFi network (a "car network") and run Shairport Sync on it to provide an AirPlay service. An iPhone or an iPad with cellular capability can simultaneously connect to internet radio, YouTube, Apple Music, Spotify, etc. over the cellular network and send AirPlay audio through the car network to the AirPlay service provided by Shairport Sync. This sends the audio to the computer's DAC which is connected to the AUX input of your car audio.

Please note that Android phones and tablets can not, so far, do this trick of using the two networks simultaneously.

## Example

If you are updating an existing installation, please refer to the [updating](#updating) section below.

In this example, a Raspberry Pi Zero with WiFi and a Pimoroni PHAT DAC are used. Shairport Sync will be built for AirPlay 2 operation, but you can build it for "classic" AirPlay (aka AirPlay 1) operation if you prefer.

Please note that some of the details of setting up networks are specific to the version of Linux used.

### Prepare the initial SD Image
* Download Raspberry Pi OS (Lite) and install it onto an SD Card using `Raspberry Pi Imager`. The Lite version is preferable to the Desktop version as it doesn't include a sound server like PulseAudio or PipeWire that can prevent direct access to the audio output device.
* Before writing the image to the card, use the Settings control on `Raspberry Pi Imager` to set hostname, enable SSH and provide a username and password to use while building the system. Similarly, you can specify a wireless network the Pi will connect to while building the system. Later on, the Pi will be configured to start its own isolated network.
* The next few steps are to add the overlay needed for the sound card. This may not be necessary in your case, but in this example a Pimoroni PHAT is being used. If you do not need to add an overlay, skip these steps.
  * Mount the card on a Linux machine. Two drives should appear – a `boot` drive and a `rootfs` drive.
  * `cd` to the `firmware` director on the `boot` drive (since my username is `mike`, it will be `$ cd /media/mike/boot/firmware`).
  * Edit the `config.txt` file to add the overlay needed for the sound card. This may not be necessary in your case, but in this example a Pimoroni PHAT is being used and it needs the following entry to be added:
    ```
    dtoverlay=hifiberry-dac
    ```
  * Close the file and carefully dismount and eject the two drives. *Be sure to dismount and eject the drives properly; otherwise they may be corrupted.*
* Remove the SD card from the Linux machine, insert it into the Pi and reboot.

After a short time, the Pi should appear on your network – it may take a couple of minutes. To check, try to `ping` it at the `<hostname>.local`, e.g. if the hostname is `bmw` then use `$ ping bmw.local`. Once it has appeared, you can SSH into it and configure it.

### Boot, Configure, Update 
The first thing to do on a Pi would be to use the `raspi-config` tool to expand the file system to use the entire card. Next, do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
``` 

### Build and Install 
Let's get the tools and libraries for building and installing Shairport Sync (and NQPTP).

```
# apt install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libplist-dev libsodium-dev uuid-dev libgcrypt-dev xxd libplist-utils \
    libavutil-dev libavcodec-dev libavformat-dev
```
If you are building classic Shairport Sync, the list of packages is shorter:
```
# apt-get install --no-install-recommends build-essential git autoconf automake libtool \
    libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev \
    libavutil-dev libavcodec-dev libavformat-dev
```
#### NQPTP
Skip this section if you are building classic Shairport Sync – NQPTP is not needed for classic Shairport Sync.

Download, install, enable and start NQPTP from [here](https://github.com/mikebrady/nqptp) following the guide for Linux.

#### Shairport Sync
Download Shairport Sync, configure, compile and install it.

* Replace the `--with-airplay-2` with `--with-ffmpeg` in the `./configure` options if you are building classic Shairport Sync.

```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi # about 1.5 minutes on a Raspberry Pi B
$ ./configure --sysconfdir=/etc --with-alsa \
    --with-soxr --with-avahi --with-ssl=openssl --with-systemd --with-airplay-2
$ make # about 6 minutes on a Raspberry Pi B
# make install
# systemctl enable shairport-sync
```

### Configure Shairport Sync
Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
```
// Sample Configuration File for Shairport Sync for Car Audio with a Pimoroni PHAT
general =
{
	name = "BMW Radio";
	ignore_volume_control = "yes";
	volume_max_db = -3.00;
};

alsa =
{
	output_device = "hw:1"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
};

```
Two `general` settings are worth noting.
1. First, the option to ignore the sending device's volume control is enabled. This means that the car audio's volume control is the only one that affects the audio volume. This is a matter of personal preference.
   
2. Second, the maximum output offered by the DAC to the AUX port of the car audio can be reduced if it is overloading the car audio's input circuits and causing distortion. Again, that's a matter for personal selection and adjustment.

The `alsa` settings are for the Pimoroni PHAT – it does not have a hardware mixer, so no `mixer_control_name` is given.

The DAC's 32-bit capability is automatically selected if available, so there is no need to set it here. Similarly, since `soxr` support is included in the build, `soxr` interpolation will be automatically enabled if the device is fast enough.

### Extra Packages
A number of packages to enable the Pi to work as a WiFi base station are needed:
```
# apt install --no-install-recommends hostapd isc-dhcp-server
```
(The installer will get errors trying to set up both of these services; the errors can be ignored.)

Disable both of these services from starting at boot time (this is because we will launch them sequentially later on):
```
# systemctl unmask hostapd
# systemctl disable hostapd
# systemctl disable isc-dhcp-server
```
#### Configure HostAPD
Configure `hostapd` by creating `/etc/hostapd/hostapd.conf` with the following contents which will set up an open network with the name BMW. You might wish to change the name:
``` 
# Thanks to https://wiki.gentoo.org/wiki/Hostapd#802.11b.2Fg.2Fn_triple_AP

# The interface used by the AP
interface=wlan0

# This is the name of the network -- yours may be different
ssid=BMW

# "g" simply means 2.4GHz band
hw_mode=g

# Channel to use
channel=11

# Limit the frequencies used to those allowed in the country
ieee80211d=1

# The country code
country_code=IE

# Enable 802.11n support
ieee80211n=1

# QoS support, also required for full speed on 802.11n/ac/ax
wmm_enabled=1

```
Note that, since the car network is isolated from the Internet, you don't really need to secure it with a password.

#### Configure DHCP server

First, replace the contents of `/etc/dhcp/dhcpd.conf` with this:
```
subnet 10.0.10.0 netmask 255.255.255.0 {
     range 10.0.10.5 10.0.10.150;
     #option routers <the-IP-address-of-your-gateway-or-router>;
     #option broadcast-address <the-broadcast-IP-address-for-your-network>;
}
```
Second, modify the `INTERFACESv4` entry at the end of the file `/etc/default/isc-dhcp-server` to look as follows:
```
INTERFACESv4="wlan0"
INTERFACESv6=""
```
### Set up the Startup Sequence
Firstly, if the file `/etc/rc.local` doesn't already exist, create it, ensure it is owned by `root` and set its permissions are `755`; otherwise, it won't be executed at startup.

Now, configure the startup sequence by adding commands to `/etc/rc.local` to start `hostapd` and the `dhcp` automatically after startup. Its contents should look like this:
```
#!/bin/sh -e
#
# rc.local
#

# Shairport Sync is automatically started as a service on startup.

# If MODE is set to RUN, the system will start the WiFi access point. 

# If MODE is set to anything else, e.g. DEV, the system will not start the WiFi access point.
# Instead, you can connect the system to a network.
# If it still has the WiFi credentials of the last WiFi network it connected to,
# it can connect to it automatically.

MODE=RUN

/bin/sleep 2 # may be necessary while wlan0 becomes available
/sbin/iw dev wlan0 set power_save off  # always do this

if test $MODE = RUN ; then

  # If script execution gets in here, it starts the WiFi access point.
  /usr/sbin/hostapd -B -P /run/hostapd.pid /etc/hostapd/hostapd.conf
  /sbin/ip addr add 10.0.10.1/24 dev wlan0
  /bin/systemctl start isc-dhcp-server
  
else

  # If script execution gets in here, it starts services needed for normal operation.
  /bin/systemctl start dhcpcd || /bin/systemctl start NetworkManager || :
  /bin/sleep 2 # may be necessary while the network becomes available
  /bin/systemctl start systemd-timesyncd || :

fi

exit 0 # normal exit here
```

#### Disable Unused Services - Optional
These optional steps have been tested on a Raspberry Pi only -- they have not been tested on other systems.
Some services are not necessary for this setup and can be disabled as follows:
```
# systemctl disable keyboard-setup
# systemctl disable triggerhappy
# systemctl disable dphys-swapfile
```


#### Disable Unused Services - Mandatory
You now need to disable some services; that is, you need to stop them starting automatically on power-up. This is because they either interfere with the system's operation in WiFi Access Point mode, or because they won't work when the system isn't connected to the Internet. Only one of the `NetworkManager` and the `dhcpcd` services will be present in your system, but it's no harm to try to disable both.
```
# systemctl disable dhcpcd
# systemctl disable NetworkManager
# systemctl disable wpa_supplicant
# systemctl disable systemd-timesyncd
```
Lastly, note that the WiFi credentials you used initially to connect to your network (e.g. your home network) will have been stored in the system in plain text. This is convenient for when you want to reconnect to update (see later), but if you prefer to delete them, they will be in `/etc/wpa_supplicant/wpa_supplicant.conf`.

#### Optional: Read-only mode – Raspberry Pi Specific
This optional step is applicable to a Raspberry Pi only. Run `sudo raspi-config` and then choose `Performance Options` > `Overlay Filesystem` and choose to enable the overlay filesystem, and to set the boot partition to be write-protected. (The idea here is that this offers more protection against files being corrupted by the sudden removal of power.)

### Final Step
When you are finished, carefully power down the machine before unplugging it from power:
```
# poweroff
```
Note: doing a `reboot` here doesn't seem to work properly -- it really does seem necessary to power off.

### Ready
Install the Raspberry Pi in your car. It should be powered from a source that is switched off when you leave the car, otherwise the slight current drain will eventually flatten the car's battery.

When the power source is switched on -- typically when you start the car -- it will take around 35 seconds for the system to become available (timing based on a Raspberry Pi Zero 2 W running Bookworm).

### Enjoy!
---
## Updating
From time to time, you may wish to update this installation. Assuming you haven't deleted your original WiFi network credentials, the easiest thing is to temporarily reconnect to the network you used when you created the system. You can then update the operating system and libraries in the normal way and then update Shairport Sync.

However, if you're *upgrading* the operating system to e.g. from Bullseye to Bookworm, the names and index numbers of the output devices may change, and the names of the mixer controls may also change. You can use [`dacquery`](https://github.com/mikebrady/dacquery) to discover device names and mixer names.

#### Exit Raspberry Pi Read-Only Mode
If it's a Raspberry Pi and you have optionally enabled the read-only mode, you must take the device out of Read-only mode:  
Run `sudo raspi-config` and then choose `Performance Options` > `Overlay Filesystem` and choose to disable the overlay filesystem and to set the boot partition not to be write-protected. This is so that changes can be written to the file system; you can make the filesystem read-only again later. Save the changes and reboot the system.
#### Undo Optimisations
If you have disabled any of the services listed in the [Disable Unused Services - Optional](#disable-unused-services---optional) section, you should re-enable them. (But *do not* re-enable `NetworkManager`, `dhcpcd`, `wpa_supplicant` or `systemd-timesyncd` -- they are handled specially by the startup script.)
#### Perform Legacy Updates
Over time, the arrangements by which the system is prepared for operation has changed to make it easier to revert to normal operation when necessary for maintenance, updates, etc. A small number of the old settings need to be changed to bring them up to date with the present arrangements. Once the required changes have been made, your system will be ready for the update process detailed below. Here are those legacy changes you need to make, just once:

1. If there is a file called `/etc/dhcpcd.conf` and if the first line reads:
   ```
   denyinterfaces wlan0
   ```
   then delete that line -- it is no longer needed and will cause problems in future if it remains there.
   
   If the file `/etc/dhcpcd.conf` doesn't exist, or if the first line is not `denyinterfaces wlan0`, then you don't need to do anything.
   
2. Replace the contents of the file `/etc/rc.local` with the new contents given [above](#set-up-the-startup-sequence).

3. Disable a number of services as follows. Only one of the `NetworkManager` and the `dhcpcd` services will be present in your system, but it's no harm to try to disable both.
   ```
   # systemctl disable dhcpcd
   # systemctl disable NetworkManager
   # systemctl disable wpa_supplicant
   # systemctl disable systemd-timesyncd
   ```
4. Enable the `shairport-sync` service itself:
   ```
   # systemctl enable shairport-sync
   ```
Once you have made these one-off legacy updates, you can proceed to the next stage -- performing the update.
### Performing the Update
To update, take the following steps:
#### Temporarily reconnect to a network and update
1. Edit the startup script in `/etc/rc.local` to so that the system is no longer in the RUN mode.
   To do that, change line 15 so that it goes from this:
   ```
   MODE=RUN
   ```
   to this:
   ```
   MODE=DEV
   ```
   Do not be tempted to insert any spaces anywhere -- Unix scripting syntax is very strict!

2. Save and close the file and reboot. From this point on, the system will start normally and can be connected to a network. If it still has the WiFi credentials of the last network it was connected to, then it could automatically reconnect.

The system is now ready for updating in the normal way.
#### Revert to normal operation
When you are finished updating, you need to put the system back into its RUN mode, as follows:

1. Edit the startup script in `/etc/rc.local` to so that the MODE variable is set to RUN.
   To do that, change line 15 so that it goes from this:
   ```
   MODE=DEV
   ```
   to this:
   ```
   MODE=RUN
   ```
   Once again, do insert any spaces anywhere.

   
2. Save and close the file and reboot. The system should start as it would if it was in the car.
