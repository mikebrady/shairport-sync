Troubleshooting
-----
The installation and setup of Shairport Sync is straightforward on recent Linux distributions. Issues can occasionally arise caused by problems elsewhere in the system, typically WiFi reception and/or the WiFi adapter settings, the network, the router, firewall settings or some more esoteric audio interfaces.

In this brief document will be listed some problems and some solutions, some provided by other users.

1. Before starting, ensure that your software is up-to-date. This document always refers to the most recent version of Shairport Sync -- see [here](https://github.com/mikebrady/shairport-sync/releases) for information about the most recent release.
2. If you have set `interpolation` in the `general` section of the configuration file to to `soxr`, comment it out or set it to `auto` or `basic` as the `soxr` setting can cause lower-powered devices to bog down at critical times, e.g. see [this report](https://github.com/mikebrady/shairport-sync/issues/631#issuecomment-366305203).
3. Ensure the volume setting of your output device is set to some reasonable value and ensure it is not muted. For ALSA devices, the `alsamixer` command-line tool is very good for this. For other sound systems, please consult the relevant documentation.

### WiFi adapter running in power-saving / low-power mode

**Check Throughput**

 You can check WiFi throughput using, for example, https://thepi.io/how-to-use-your-raspberry-pi-to-monitor-broadband-speed/

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

Here is another option, suggested by [davidhq](https://github.com/davidhq) in [#653](https://github.com/mikebrady/shairport-sync/issues/653#issuecomment-391100620):

```
$ sudo nano /etc/network/if-up.d/off-power-manager
```

Type:
```
#!/bin/sh
/sbin/iwconfig wlan0 power off
```
Then:
```
sudo chmod +x /etc/network/if-up.d/off-power-manager
```

There are some more details in some the closed issues on this repository.

### VPNs

To see the AirPlay service Shairport Sync provides, your devices must be on the same subnet as Shairport Sync. If, say, a device such as an iPhone is on a VPN and Shairport Sync is not, or vice-versa, then even though the devices might be physically on the same network, they are effectively on separate networks due to the VPN and the AirPlay service will not be accessible to the device. So, when you are troubleshooting, look out for VPN issues. 

### Faulty WiFi
For an example of what it can take to track down a bad WiFi situation – in this case, a faulty WiFi adapter – please look at [this report](https://github.com/mikebrady/shairport-sync/issues/689).

### Can't play from iTunes on Windows

**Problem**

You can play from other devices but not from your Windows PC.

**Possible Solution**

Allow network discovery. This setting creates a private type network and enables Windows to access the ports and protocols necessary to use Shairport Sync.

### UFW firewall blocking ports (commonly includes Raspberry Pi)

**Problem**

You have installed Shairport Sync successfully, the daemon is running, you can see it from your remote terminal but you are unable to play a song.

**Before you change anything to your configuration**

- Type the following command:

  `sudo ufw disable`

- Try to launch a song from your remote device on the Shairport-sync one, if this works, proceed to the next step and follow the ones described below, in the solution section.

- Enable UFW through the following command:

  `sudo ufw enable`

**Solution**

You have to allow connections to your Shairport Sync device from remote devices. To do so, after re-enabling UFW (see last step of the previous section), enter the following commands in shell:

```
sudo ufw allow 3689/tcp
sudo ufw allow 5353
sudo ufw allow 5000/tcp  # if using Airplay 1
sudo ufw allow 7000/tcp  # if using Airplay 2
sudo ufw allow 6001:6010/udp
sudo ufw allow 6001:6010/tcp  # if using AirPlay 2
```

You can check UFW config by typing `sudo ufw status` in shell. Please make sure that UFW is active, especially if you have deactivated it previously for testing purposes. Check out the [ufw man pages](http://manpages.ubuntu.com/manpages/man8/ufw.8.html) for more.

Run your song from your remote device. Enjoy !

### Shairport Sync Won't Start Automatically After Reboot
This refers to slower machines, such as the Raspberry Pi Zero or the original (single core) Raspberry Pi, running a recent Linux that uses `systemd`.

**Problem**

Having compiled Shairport Sync properly using the README guide, and having completed the `make install` step, and having enabled startup on reboot using `$ sudo systemctl enable shairport-sync`, Shairport Sync will start manually upon entering `$sudo systemctl enable shairport-sync`, but it will not start automatically after a reboot.

**Possible Cause**

On lower-powered machines, such as the Raspberry Pi Zero or the original (single core) Raspberry Pi, particularly with a USB sound card, it may be that the sound system is not ready when Shairport Sync is automatically started. The result is that Shairport Sync cannot see the device it needs and shuts down.

**Possible Solution**

A good solution is to delay the automatic startup of Shairport Sync by a few seconds using the `systemd` timer mechanism:

Create a file called `shairport-sync.timer` and place it alongside `shairport-sync.service` in `/lib/systemd/system`. The file should contain the following:
```
[Unit]
Description=Shairport Sync AirPlay receiver

[Timer]                       
OnBootSec=10s              
Unit=shairport-sync.service

[Install]    
WantedBy=multi-user.target  
```
You need to disable the `shairport-sync` service because the timer is calling the service, and you need to enable the `shairport-sync` timer:

```
# systemctl disable shairport-sync
# systemctl start shairport-sync.timer
# systemctl enable shairport-sync.timer
```
See also #179, with thanks to @maumi and others.

**Alternative Solution**

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

or look at your existing '/etc/asound.conf' file, which may look something like this

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

### Buffer underflow to audio backend

**Problem **

Audio may seem to pause or drop for several seconds. iOS devices may regularly disconnect altogether and display an error message. This may be caused by a combination of factors listed above such as slow WiFi or limited resources on the device running Shairport Sync.

**Possible Solution **

If none of the above steps completely remove the issue, try increasing the audio backend buffer setting in the backend section of shairport-sync.conf. (This section may vary depending on the value of the "output_backend" setting.)

For example:

````
audio_backend_buffer_desired_length = 19845;
````
Is triple the default for the ALSA backend and effectively solves the above issue with a Pi Zero on a busy network.
