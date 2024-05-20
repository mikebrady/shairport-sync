Troubleshooting
-----
The installation and setup of Shairport Sync is straightforward on recent Linux distributions. Issues can occasionally arise caused by problems elsewhere in the system, typically WiFi reception and/or the WiFi adapter settings, the network, the router, firewall settings or some more esoteric audio interfaces.

In this brief document will be listed some problems and some solutions, some provided by other users.

1. Before starting, ensure that your software is up-to-date. This document always refers to the most recent version of Shairport Sync -- see [here](https://github.com/mikebrady/shairport-sync/releases) for information about the most recent release.
2. If you have set `interpolation` in the `general` section of the configuration file to to `soxr`, comment it out or set it to `auto` or `basic` as the `soxr` setting can cause lower-powered devices to bog down at critical times, e.g. see [this report](https://github.com/mikebrady/shairport-sync/issues/631#issuecomment-366305203).

### No/Low Sound
#### ALSA
Let's say you've just installed or updated Shairport Sync and you are testing it for the first time after installing or updating.
If you are using the default ALSA backend, don't forget to check two simple things:
1. Check that the volume on the output device is turned up. If the output device has a "mixer" i.e. a volume control, Shairport Sync does not, by default, try to control it. Therefore, if it happens to be very low or even at zero, you might not hear audio that is actually coming through to the device. (You can get Shairport Sync to control a mixer -- see [here](https://github.com/mikebrady/spsdoc/blob/main/ADVANCED%20TOPICS/InitialConfiguration.md) for some hints.)
2. Check that the output device is not muted. Some audio applications (including very old versions of Shairport Sync) leave the output device mixer in a muted state after use to minimise the possibility of noise. However, this is not generally compatible with other audio players using the same device, as they generally expect the device to be unmuted. 

You can use `alsamixer` for both of these checks. A muted output has the letter(s) `M` as its value. Select it and type `M` again to unmute. 

#### JACK
If you’re using the JACK backend, check that you have connected your inputs and outputs. You do have to connect them yourself.

#### Network issues
If the audio end looks fine, there are a couple of simple things on the network end worth checking:

1. Is the wi-fi interface up?
2. Are Shairport Sync and the Apple device on the same wi-fi network?
3. Is the Apple device reachable from the system running Shairport Sync? (That is, are there any routing issues?) It *is* possible for the mDNS multicast to reach Shairport Sync even though the two devices can’t actually talk to each other.

### Sync is slightly off!
Please see [Adjusting Sync](./ADVANCED%20TOPICS/AdjustingSync.md).

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

### Ubiquiti Routers
Ubiquiti router settings occasionally cause services that use Boujour/Zeroconf -- including Shairport Sync -- not to appear on the network. (AirPrint is another example of a service that can be affected by this problem.) [This link](https://community.ui.com/questions/Issues-with-Bonjour-on-UDM-Pro/6db37e3f-73cf-4aaf-8dba-8a196fd0e2b1) to a Ubiquiti community discussion might be helpful.

### Faulty WiFi
For an example of what it can take to track down a bad WiFi situation – in this case, a faulty WiFi adapter – please look at [this report](https://github.com/mikebrady/shairport-sync/issues/689).

### Using your iOS device as a hotspot.
An iOS device that is being used as a WiFi hotspot can not play audio to another device.

### Can't play from iTunes on Windows

**Problem**

You can play from other devices but not from your Windows PC.

**Possible Issue -- AirPlay 2**
iTunes on Windows is not compatible with Shairport Sync when it is built for AirPlay 2. This is unlikely to change, unfortunately. However, iTunes on Windows remains compatible with "classical" Shairport Sync.

**Possible Issue -- AirPlay**
The Windows Firewall is preventing access to Shairport Sync.

Solution: Allow network discovery. This setting creates a private type network and enables Windows to access the ports and protocols necessary to use Shairport Sync.

### UFW firewall blocking ports (commonly includes Raspberry Pi)

**Problem**

You have installed Shairport Sync successfully, the daemon is running, you can see it from your remote terminal but you are unable to play a song.

**Before you change anything to your configuration**

- Type the following command:

  `sudo ufw disable`

- Try to launch a song from your remote device on the Shairport Sync one. If this works, proceed to the next step and follow the ones described below, in the solution section.

- Enable UFW through the following command:

  `sudo ufw enable`

**Solution**

You have to allow connections to your Shairport Sync device from remote devices. To do so, after re-enabling UFW (see last step of the previous section), enter the following commands in shell:

**Classic AirPlay:**
```
sudo ufw allow 3689/tcp
sudo ufw allow 5353
sudo ufw allow 5000/tcp
sudo ufw allow 6000:6009/udp
```

**AirPlay 2:**
```
sudo ufw allow 319:320/udp
sudo ufw allow 3689/tcp
sudo ufw allow 5353
sudo ufw allow 5000/tcp
sudo ufw allow 7000/tcp
sudo ufw allow 6000:6009/udp
sudo ufw allow 32768:60999/udp
sudo ufw allow 32768:60999/tcp
```

The range `32768:60999` represents the [ephemeral port range](https://en.m.wikipedia.org/wiki/Ephemeral_port) which seems to be standardised on those values. 

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

If none of the above steps completely remove the issue, try increasing the audio backend buffer setting in the backend section of shairport-sync.conf. (This section may vary depending on the value of the `"output_backend"` setting.)

For example:

````
audio_backend_buffer_desired_length = 19845;
````
...is triple the default for the ALSA backend and effectively solves the above issue with a Pi Zero on a busy network.

### AAC Decoder Issues (AirPlay 2 only)

To play AirPlay 2 Buffered Audio streams, Shairport Sync needs an AAC decoder capable of decoding Planar Floating Point -- `fltp` -- AAC material. Unfortunately, not all systems have such a decoder. To troubleshoot this, the idea here is to use the [`ffmpeg`](https://www.ffmpeg.org) app, which may already be installed in your system, to list AAC decoders and to check that the default AAC decoder has `fplp` capability. Here is an example of where the AAC decoder _can_ decode `fltp` material. The system is a Raspberry Pi running 64-bit Raspberry Pi OS Lite 11 (Bullseye):
```
$ ffmpeg -decoders | grep -i aac

ffmpeg version 4.3.4-0+deb11u1+rpt1 Copyright (c) 2000-2021 the FFmpeg developers
  built with gcc 10 (Raspbian 10.2.1-6+rpi1)
  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --enable-shared --libdir=/usr/lib/arm-linux-gnueabihf --cpu=arm1176jzf-s --arch=arm
  WARNING: library configuration mismatch
  avutil      configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avcodec     configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avformat    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avdevice    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avfilter    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avresample  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  swscale     configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  swresample  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  postproc    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  libavutil      56. 51.100 / 56. 51.100
  libavcodec     58. 91.100 / 58. 91.100
  libavformat    58. 45.100 / 58. 45.100
  libavdevice    58. 10.100 / 58. 10.100
  libavfilter     7. 85.100 /  7. 85.100
  libavresample   4.  0.  0 /  4.  0.  0
  libswscale      5.  7.100 /  5.  7.100
  libswresample   3.  7.100 /  3.  7.100
  libpostproc    55.  7.100 / 55.  7.100
 A....D aac                  AAC (Advanced Audio Coding)
 A....D aac_fixed            AAC (Advanced Audio Coding) (codec aac)
 A....D aac_latm             AAC LATM (Advanced Audio Coding LATM syntax)
```
At the bottom you can see three decoders. The first one, called `aac`, is the decoder chosen by Shairport Sync. Checking its capabilities:

```
$ ffmpeg -h decoder=aac
ffmpeg version 4.3.4-0+deb11u1+rpt1 Copyright (c) 2000-2021 the FFmpeg developers
  built with gcc 10 (Raspbian 10.2.1-6+rpi1)
  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --enable-shared --libdir=/usr/lib/arm-linux-gnueabihf --cpu=arm1176jzf-s --arch=arm
  WARNING: library configuration mismatch
  avutil      configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avcodec     configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avformat    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avdevice    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avfilter    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  avresample  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  swscale     configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  swresample  configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  postproc    configuration: --prefix=/usr --extra-version=0+deb11u1+rpt1 --toolchain=hardened --incdir=/usr/include/arm-linux-gnueabihf --enable-gpl --disable-stripping --enable-avresample --disable-filter=resample --enable-gnutls --enable-ladspa --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcaca --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libflite --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgme --enable-libgsm --enable-libjack --enable-libmp3lame --enable-libmysofa --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librabbitmq --enable-librsvg --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libsrt --enable-libssh --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libwebp --enable-libx265 --enable-libxml2 --enable-libxvid --enable-libzmq --enable-libzvbi --enable-lv2 --enable-omx --enable-openal --enable-opencl --enable-opengl --enable-sdl2 --enable-mmal --enable-neon --enable-rpi --enable-v4l2-request --enable-libudev --enable-epoxy --enable-pocketsphinx --enable-libdc1394 --enable-libdrm --enable-vout-drm --enable-libiec61883 --enable-chromaprint --enable-frei0r --enable-libx264 --libdir=/usr/lib/arm-linux-gnueabihf/neon/vfp --cpu=cortex-a7 --arch=armv6t2 --disable-thumb --enable-shared --disable-doc --disable-programs
  libavutil      56. 51.100 / 56. 51.100
  libavcodec     58. 91.100 / 58. 91.100
  libavformat    58. 45.100 / 58. 45.100
  libavdevice    58. 10.100 / 58. 10.100
  libavfilter     7. 85.100 /  7. 85.100
  libavresample   4.  0.  0 /  4.  0.  0
  libswscale      5.  7.100 /  5.  7.100
  libswresample   3.  7.100 /  3.  7.100
  libpostproc    55.  7.100 / 55.  7.100
Decoder aac [AAC (Advanced Audio Coding)]:
    General capabilities: dr1 chconf 
    Threading capabilities: none
    Supported sample formats: fltp
    Supported channel layouts: mono stereo 3.0 4.0 5.0 5.1 7.1(wide)
AAC decoder AVOptions:
  -dual_mono_mode    <int>        .D..A..... Select the channel to decode for dual mono (from -1 to 2) (default auto)
     auto            -1           .D..A..... autoselection
     main            1            .D..A..... Select Main/Left channel
     sub             2            .D..A..... Select Sub/Right channel
     both            0            .D..A..... Select both channels
```
You can see the line:
```
Supported sample formats: fltp
```
towards the bottom. This means that the AAC decoder is suitable for use by Shairport Sync

Here are the same commands, performed on a system which does not support `fltp` decoding:
```
$ ffmpeg -decoders | grep -i aac
ffmpeg version 5.0.1 Copyright (c) 2000-2022 the FFmpeg developers
  built with gcc 12 (GCC)
  configuration: --prefix=/usr --bindir=/usr/bin --datadir=/usr/share/ffmpeg --docdir=/usr/share/doc/ffmpeg --incdir=/usr/include/ffmpeg --libdir=/usr/lib64 --mandir=/usr/share/man --arch=x86_64 --optflags='-O2 -flto=auto -ffat-lto-objects -fexceptions -g -grecord-gcc-switches -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -fstack-protector-strong -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -m64 -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection' --extra-ldflags='-Wl,-z,relro -Wl,--as-needed -Wl,-z,now -specs=/usr/lib/rpm/redhat/redhat-hardened-ld -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -Wl,--build-id=sha1 -Wl,-dT,/builddir/build/BUILD/ffmpeg-5.0.1/.package_note-ffmpeg-5.0.1-6.fc36.x86_64.ld' --disable-htmlpages --enable-pic --disable-stripping --enable-shared --disable-static --enable-gpl --enable-version3 --enable-libsmbclient --disable-openssl --enable-bzlib --enable-frei0r --enable-chromaprint --enable-gcrypt --enable-gnutls --enable-ladspa --enable-libshaderc --enable-vulkan --disable-cuda-sdk --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libdc1394 --enable-libdrm --enable-libfdk-aac --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgsm --enable-libilbc --enable-libjack --enable-libmodplug --enable-libmp3lame --enable-libmysofa --enable-libopenh264-dlopen --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librav1e --enable-librsvg --enable-librubberband --enable-libsnappy --enable-libsvtav1 --enable-libsoxr --enable-libspeex --enable-libssh --enable-libsrt --enable-libtesseract --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvmaf --enable-libvorbis --enable-libv4l2 --enable-libvpx --enable-libwebp --enable-libxml2 --enable-libzimg --enable-libzmq --enable-libzvbi --enable-lto --enable-libmfx --enable-vaapi --enable-vdpau --enable-openal --enable-opencl --enable-opengl --enable-pthreads --enable-vapoursynth --enable-muxers --enable-demuxers --enable-hwaccels --disable-encoders --disable-decoders --disable-decoder='mpeg4,h263,h264,hevc,vc1' --enable-encoder=',libfdk_aac,ac3,apng,ass,ayuv,bmp,ffv1,ffvhuff,flac,gif,h263_v4l2m2m,h264_amf,h264_nvenc,h264_qsv,h264_v4l2m2m,h264_vaapi,hevc_amf,hevc_nvenc,hevc_qsv,hevc_v4l2m2m,hevc_vaapi,huffyuv,ilbc,jpegls,jpeg2000,libaom,libaom_av1,libcodec2,libgsm,libilbc,libmp3lame,libopenh264,libopenjpeg,libopus,librav1e,libschroedinger,libspeex,libsvtav1,libtheora,libtwolame,libvorbis,libvpx_vp8,libvpx_vp9,libwebp,libwebp_anim,mjpeg,mjpeg_qsv,mjpeg_vaapi,mp2,mp2fixed,mpeg1video,mpeg2video,mpeg2_qsv,mpeg2_vaapi,mpeg4_v4l2m2m,opus,pam,pbm,pcm_alaw,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_mulaw,pcm_s16be,pcm_s16be_planar,pcm_s16le,pcm_s16le_planar,pcm_s24be,pcm_s24le,pcm_s24le_planar,pcm_s32be,pcm_s32le,pcm_s32le_planar,pcm_s8,pcm_s8_planar,pcm_u16be,pcm_u16le,pcm_u24be,pcm_u24le,pcm_u32be,pcm_u32le,pcm_u8,pcx,pgm,pgmyuv,png,ppm,rawvideo,sgi,srt,ssa,sunrast,targa,text,tiff,v210,v308,v408,v410,vc1_qsv,vc1_v4l2m2m,vorbis,vp8_qsv,vp8_v4l2m2m,vp8_vaapi,vp9_qsv,vp9_vaapi,webvtt,wrapped_avframe,xbm,xwd,y41p,yuv4,zlib,' --enable-decoder=',libfdk_aac,ac3,ansi,apng,ass,av1_qsv,ayuv,bmp,dirac,exr,ffv1,ffvhuff,ffwavesynth,flac,gif,gsm,huffyuv,ilbc,jpeg2000,libaom,libaom_av1,libcodec2,libdav1d,libgsm,libilbc,libopenh264,libopenjpeg,libopus,libschroedinger,libspeex,libvorbis,libvpx_vp8,libvpx_vp9,mjpeg,mjpeg_qsv,mp1,mp1float,mp2,mp2float,mp3,mp3float,mpeg1video,mpeg1_v4l2m2m,mpeg2video,mpeg2_qsv,mpeg2_v4l2m2m,opus,pam,pbm,pcm_alaw,pcm_bluray,pcm_dvd,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_mulaw,pcm_s16be,pcm_s16be_planar,pcm_s16le,pcm_s16le_planar,pcm_s24be,pcm_s24le,pcm_s24le_planar,pcm_s32be,pcm_s32le,pcm_s32le_planar,pcm_s8,pcm_s8_planar,pcm_u16be,pcm_u16le,pcm_u24be,pcm_u24le,pcm_u32be,pcm_u32le,pcm_u8,pcx,pgm,pgmyuv,pgssub,pgx,png,ppm,rawvideo,sgi,srt,ssa,sunrast,targa,text,theora,tiff,v210,v210x,v308,v408,v410,vorbis,vp3,vp5,vp6,vp6a,vp6f,vp8,vp8_qsv,vp8_v4l2m2m,vp9,vp9_qsv,vp9_v4l2m2m,webp,webvtt,wrapped_avframe,xbm,xwd,y41p,yuv4,zlib,'
  libavutil      57. 17.100 / 57. 17.100
  libavcodec     59. 18.100 / 59. 18.100
  libavformat    59. 16.100 / 59. 16.100
  libavdevice    59.  4.100 / 59.  4.100
  libavfilter     8. 24.100 /  8. 24.100
  libswscale      6.  4.100 /  6.  4.100
  libswresample   4.  3.100 /  4.  3.100
  libpostproc    56.  3.100 / 56.  3.100
 A....D libfdk_aac           Fraunhofer FDK AAC (codec aac)
```
As you can see just one AAC decoder in available, the `Fraunhofer FDK AAC`. Examining it more closely:
```
$ ffmpeg -h decoder=aac
ffmpeg version 5.0.1 Copyright (c) 2000-2022 the FFmpeg developers
  built with gcc 12 (GCC)
  configuration: --prefix=/usr --bindir=/usr/bin --datadir=/usr/share/ffmpeg --docdir=/usr/share/doc/ffmpeg --incdir=/usr/include/ffmpeg --libdir=/usr/lib64 --mandir=/usr/share/man --arch=x86_64 --optflags='-O2 -flto=auto -ffat-lto-objects -fexceptions -g -grecord-gcc-switches -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1 -fstack-protector-strong -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -m64 -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection' --extra-ldflags='-Wl,-z,relro -Wl,--as-needed -Wl,-z,now -specs=/usr/lib/rpm/redhat/redhat-hardened-ld -specs=/usr/lib/rpm/redhat/redhat-annobin-cc1 -Wl,--build-id=sha1 -Wl,-dT,/builddir/build/BUILD/ffmpeg-5.0.1/.package_note-ffmpeg-5.0.1-6.fc36.x86_64.ld' --disable-htmlpages --enable-pic --disable-stripping --enable-shared --disable-static --enable-gpl --enable-version3 --enable-libsmbclient --disable-openssl --enable-bzlib --enable-frei0r --enable-chromaprint --enable-gcrypt --enable-gnutls --enable-ladspa --enable-libshaderc --enable-vulkan --disable-cuda-sdk --enable-libaom --enable-libass --enable-libbluray --enable-libbs2b --enable-libcdio --enable-libcodec2 --enable-libdav1d --enable-libdc1394 --enable-libdrm --enable-libfdk-aac --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libgsm --enable-libilbc --enable-libjack --enable-libmodplug --enable-libmp3lame --enable-libmysofa --enable-libopenh264-dlopen --enable-libopenjpeg --enable-libopenmpt --enable-libopus --enable-libpulse --enable-librav1e --enable-librsvg --enable-librubberband --enable-libsnappy --enable-libsvtav1 --enable-libsoxr --enable-libspeex --enable-libssh --enable-libsrt --enable-libtesseract --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvmaf --enable-libvorbis --enable-libv4l2 --enable-libvpx --enable-libwebp --enable-libxml2 --enable-libzimg --enable-libzmq --enable-libzvbi --enable-lto --enable-libmfx --enable-vaapi --enable-vdpau --enable-openal --enable-opencl --enable-opengl --enable-pthreads --enable-vapoursynth --enable-muxers --enable-demuxers --enable-hwaccels --disable-encoders --disable-decoders --disable-decoder='mpeg4,h263,h264,hevc,vc1' --enable-encoder=',libfdk_aac,ac3,apng,ass,ayuv,bmp,ffv1,ffvhuff,flac,gif,h263_v4l2m2m,h264_amf,h264_nvenc,h264_qsv,h264_v4l2m2m,h264_vaapi,hevc_amf,hevc_nvenc,hevc_qsv,hevc_v4l2m2m,hevc_vaapi,huffyuv,ilbc,jpegls,jpeg2000,libaom,libaom_av1,libcodec2,libgsm,libilbc,libmp3lame,libopenh264,libopenjpeg,libopus,librav1e,libschroedinger,libspeex,libsvtav1,libtheora,libtwolame,libvorbis,libvpx_vp8,libvpx_vp9,libwebp,libwebp_anim,mjpeg,mjpeg_qsv,mjpeg_vaapi,mp2,mp2fixed,mpeg1video,mpeg2video,mpeg2_qsv,mpeg2_vaapi,mpeg4_v4l2m2m,opus,pam,pbm,pcm_alaw,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_mulaw,pcm_s16be,pcm_s16be_planar,pcm_s16le,pcm_s16le_planar,pcm_s24be,pcm_s24le,pcm_s24le_planar,pcm_s32be,pcm_s32le,pcm_s32le_planar,pcm_s8,pcm_s8_planar,pcm_u16be,pcm_u16le,pcm_u24be,pcm_u24le,pcm_u32be,pcm_u32le,pcm_u8,pcx,pgm,pgmyuv,png,ppm,rawvideo,sgi,srt,ssa,sunrast,targa,text,tiff,v210,v308,v408,v410,vc1_qsv,vc1_v4l2m2m,vorbis,vp8_qsv,vp8_v4l2m2m,vp8_vaapi,vp9_qsv,vp9_vaapi,webvtt,wrapped_avframe,xbm,xwd,y41p,yuv4,zlib,' --enable-decoder=',libfdk_aac,ac3,ansi,apng,ass,av1_qsv,ayuv,bmp,dirac,exr,ffv1,ffvhuff,ffwavesynth,flac,gif,gsm,huffyuv,ilbc,jpeg2000,libaom,libaom_av1,libcodec2,libdav1d,libgsm,libilbc,libopenh264,libopenjpeg,libopus,libschroedinger,libspeex,libvorbis,libvpx_vp8,libvpx_vp9,mjpeg,mjpeg_qsv,mp1,mp1float,mp2,mp2float,mp3,mp3float,mpeg1video,mpeg1_v4l2m2m,mpeg2video,mpeg2_qsv,mpeg2_v4l2m2m,opus,pam,pbm,pcm_alaw,pcm_bluray,pcm_dvd,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_mulaw,pcm_s16be,pcm_s16be_planar,pcm_s16le,pcm_s16le_planar,pcm_s24be,pcm_s24le,pcm_s24le_planar,pcm_s32be,pcm_s32le,pcm_s32le_planar,pcm_s8,pcm_s8_planar,pcm_u16be,pcm_u16le,pcm_u24be,pcm_u24le,pcm_u32be,pcm_u32le,pcm_u8,pcx,pgm,pgmyuv,pgssub,pgx,png,ppm,rawvideo,sgi,srt,ssa,sunrast,targa,text,theora,tiff,v210,v210x,v308,v408,v410,vorbis,vp3,vp5,vp6,vp6a,vp6f,vp8,vp8_qsv,vp8_v4l2m2m,vp9,vp9_qsv,vp9_v4l2m2m,webp,webvtt,wrapped_avframe,xbm,xwd,y41p,yuv4,zlib,'
  libavutil      57. 17.100 / 57. 17.100
  libavcodec     59. 18.100 / 59. 18.100
  libavformat    59. 16.100 / 59. 16.100
  libavdevice    59.  4.100 / 59.  4.100
  libavfilter     8. 24.100 /  8. 24.100
  libswscale      6.  4.100 /  6.  4.100
  libswresample   4.  3.100 /  4.  3.100
  libpostproc    56.  3.100 / 56.  3.100
Decoder libfdk_aac [Fraunhofer FDK AAC]:
    General capabilities: dr1 chconf 
    Threading capabilities: none
libfdk-aac decoder AVOptions:
  -conceal           <int>        .D..A...... Error concealment method (from 0 to 2) (default noise)
     spectral        0            .D..A...... Spectral muting
     noise           1            .D..A...... Noise Substitution
     energy          2            .D..A...... Energy Interpolation
  -drc_boost         <int>        .D..A...... Dynamic Range Control: boost, where [0] is none and [127] is max boost (from -1 to 127) (default -1)
  -drc_cut           <int>        .D..A...... Dynamic Range Control: attenuation factor, where [0] is none and [127] is max compression (from -1 to 127) (default -1)
  -drc_level         <int>        .D..A...... Dynamic Range Control: reference level, quantized to 0.25dB steps where [0] is 0dB and [127] is -31.75dB, -1 for auto, and -2 for disabled (from -2 to 127) (default -1)
  -drc_heavy         <int>        .D..A...... Dynamic Range Control: heavy compression, where [1] is on (RF mode) and [0] is off (from -1 to 1) (default -1)
  -level_limit       <boolean>    .D..A...... Signal level limiting (default auto)
  -drc_effect        <int>        .D..A...... Dynamic Range Control: effect type, where e.g. [0] is none and [6] is general (from -1 to 8) (default -1)

```
It does not indicate that it supports `fltp` decoding (and, indeed, it definitely does not support it). The decoder is [based on fixed-point arithmetic](https://wiki.hydrogenaud.io/index.php?title=Fraunhofer_FDK_AAC), and will [only accept 16-bit PCM input](https://wiki.hydrogenaud.io/index.php?title=Fraunhofer_FDK_AAC#Sample_Format) (presumably for encoding, not decoding, which is our focus), so this is not unexpected.
