Troubleshooting
-----
The installation and setup of Shairport Sync is straightforward on recent Linux distributions. Issues can occasionally arise caused by problems elsewhere in the system, typically WiFi reception and/or the WiFi adapter settings, the network, the router, firewall settings or some more esoteric audio interfaces.

In this brief document will be listed some problems and some solutions, some provided by other users.

Before starting, ensure that your software is up-to-date. 

### WiFi adapter running in power-saving / low-power mode

**Problem**

Shairport Sync is installed and running, but sometimes it disappears from the network, and sometimes it suffers from long dropouts.

**Possible Cause**

This can be caused by lots of things, but one of them is that the WiFi adapter may be set to run in a low-power or power-saving mode. If it's not busy, then after a while it goes into a low-power mode. This is bad as the device needs to be always connected to the network to provide the AirPlay service. You need to turn off power-saving mode. How you do this varies with platform and with WiFi adapter – internet search is your friend. Here, for instance, is the command for the C.H.I.P. from Next Thing Co, which has built in WiFi and Linux and has the `iw` command installed:

```
iw dev wlan0 set power_save off
```
Here is the command sequence for a Raspberry Pi 3, which has built-in WiFi:

```
sudo iwconfig wlan0 power off
```
Alternatively, (also for the Raspberry Pi), add the following line:
```
wireless-power off
```
to the file `/etc/network/interfaces`.

There are some more details in some the closed issues on this repository.

### Can't play from iTunes on Windows

**Problem**

You can play from other devices but not from your Windows PC.

**Possible Solution**

Allow network discovery. This setting creates a private type network and enables Windows to access the ports and protocols necessary to use Shairport Sync.

### UFW firewall blocking connections on Raspbian (Raspberry Pi)

**Problem**

You have installed Shairport Sync successfully, the daemon is running, you can see it from your remote terminal but you are unable to play a song.

**Before you change anything to your configuration**

- Type the following command:

  `sudo ufw disable`

- Try to launch a song from your remote device on the Shairport-sync one, if this works, proceed to the next step and follow the ones described below, in the solution section.

- Enable UFW through the following command:

  `sudo ufw enable`

**Solution**

You have to allow connections to your Pi from remote devices. To do so, after re-enabling UFW (see last step of the previous section), enter the following commands in shell:

```
sudo ufw allow from 192.168.1.1/16 to any port 3689 proto tcp
sudo ufw allow from 192.168.1.1/16 to any port 5353
sudo ufw allow from 192.168.1.1/16 to any port 5000:5005 proto tcp
sudo ufw allow from 192.168.1.1/16 to any port 6000:6005 proto udp
sudo ufw allow from 192.168.1.1/16 to any port 35000:65535 proto udp
```

You may have to change the IP addresses range depending on your own local network settings.

You can check UFW config by typing `sudo ufw status` in shell. Please make sure that UFW is active, especially if you have deactivated it previously for testing purpose.

Run your song from your remote device. Enjoy !

### Stuttering audio on certain USB DACs (such as the Creative Soundblaster MP3+)

**Problem**
When using a USB DAC on a Raspberry Pi audio plays fine through other methods (such as through mpd, mopidy, mplayer or aplay) but when streamed to Shairport Sync regular dropouts or stutters are heard.

**Possible Cause**
There is a suspicion (although this is not 100% confirmed) that this is a fun latency/timing issue related to a combination of
- The Raspberry Pi's ethernet itself being a USB device resulting in shared bandwidth/interrupts with USB DACs
- Shairport Sync continually checking the latency of the USB DAC to maintain synchronisation of audio
- Quirky USB DACs (already known to be problematic on the Raspberry Pi more info available [here](https://www.raspberrypi.org/documentation/hardware/raspberrypi/usb/README.md#knownissues)
For more discussion on this issue see [issue 167](https://github.com/mikebrady/shairport-sync/issues/167) or read on for the quick fix!

**Possible Solution**
To get nice smooth audio first check the details of your USB DAC by either using 'aplay -l' which will give you output something like this:
````
**** List of PLAYBACK Hardware Devices ****
card 0: ALSA [bcm2835 ALSA], device 0: bcm2835 ALSA [bcm2835 ALSA]
  Subdevices: 8/8
  Subdevice #0: subdevice #0
  Subdevice #1: subdevice #1
  Subdevice #2: subdevice #2
  Subdevice #3: subdevice #3
  Subdevice #4: subdevice #4
  Subdevice #5: subdevice #5
  Subdevice #6: subdevice #6
  Subdevice #7: subdevice #7
card 0: ALSA [bcm2835 ALSA], device 1: bcm2835 ALSA [bcm2835 IEC958/HDMI]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: MP3 [Sound Blaster MP3+], device 0: USB Audio [USB Audio]
  Subdevices: 0/1
  Subdevice #0: subdevice #0
````

or look at your exisiting '/etc/asound.conf' file, which may look something like this

````
pcm.!default {
    type hw
    card 1
}
ctl.!default {
    type hw
    card 1
}
````
The important information you want is the card number which in this case is 1.

Now modify your 'etc/asound.conf' file (or create one if it doesn't exist) using the following template substituting the 'pcm "hw:1"' and 'card 1' sections with the card number of your device

````
pcm.!default {
    type plug
    slave.pcm {
        type dmix
        ipc_key 1024
        slave {
            pcm "hw:1"
            rate 48000        # this line is only needed for USB DACs which only support 48khz
            period_time 0
            period_size 1920
            buffer_size 19200
        }
    }
}
ctl.!default {
    type hw
    card 1
}
````
This sets the default alsa audio device to be the USB DAC via a dmixer plugin (which can be used by multiple applications at once) using a modified period and buffer size and optionally mix to 48khz. 

This will then be used by default by Shairport-Sync and any other applications using alsa. 

Note that some distributions (such as Volumio 2) don't use an asound.conf file by default, they instead specify the hardware details directly in '/etc/mpd.conf' files so some more in-depth modification is needed to override this.

(Note: not tested by Mike B.)


### Shairport not starting correctly after reboot

**Problem**

When rebooting certain devices (e.g. Raspberry Pi Zero W) whilst using a USB audio adapter, the Shairport Sync service fails to start.

**Possible Solutions**

Delay the start of the Shairport Sync service.

- Edit Shairport Sync service file `sudo nano /lib/systemd/system/shairport-sync.service`
- Update the service section to include the line `ExecStartPre=/bin/sleep 5`

Example:

```
[Service]
ExecStartPre=/bin/sleep 5
ExecStart=/usr/local/bin/shairport-sync
User=pi
Group=pi
```
