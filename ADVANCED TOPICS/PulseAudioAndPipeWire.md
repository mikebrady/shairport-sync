# Working with PulseAudio or PipeWire
Many Linux systems, especially desktop Linuxes with a GUI, have [PipeWire](https://pipewire.org) or [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) installed as [sound servers](https://en.wikipedia.org/wiki/Sound_server).
PipeWire and PulseAudio are widely used and have the great advantage of being easily able to mix audio from multiple sources. The slight downside is that audio may be further processed (e.g. transcoded) on its way to the output device.

To use PipeWire or PulseAudio-based systems, Shairport Sync must be set up as a user service.

### Considerations
1. Shairport Sync will work without modification in a PipeWire- or PulseAudio-based system if built with the default ALSA backend. This is because PipeWire and PulseAudio both provide a default ALSA pseudo-device to receive and play audio from ALSA-compatible programs.
2. Shairport Sync can be built with "native" PipeWire or PulseAudio backends by adding the `--with-pipewire` or `--with-pulseaudio` configuration flags when it is being built. This has the advantage of bypassing the ALSA compatability layer.
3. To check if PipeWire support is built into Shairport Sync, check that the string `PipeWire` is included in the version string. (Enter `$ shairport-sync -V` to get the version string.)  Similarly, the version string will include `PulseAudio` if the PulseAudio backend is built in.
4. Remember to specify which backend Shairport Sync should use in the configuration file or on the command line.

## Automatic Startup of Shairport Sync

The main thing to remember about PipeWire and PulseAudio sound servers is that the services they offer only become available when a user logs in -- that is, they are set up as _user services_.
Shairport Sync relies on them, so it must also be set up as a user service; it can not be set up as a system service because the PipeWire or PulseAudio services needed by Shairport Sync are not available when system services are launched just after system startup. 

### Starting Shairport Sync as a User Service
To make Shairport Sync start as a user service, (assuming you built and installed Shairport Sync using the BUILD.md guide), ensure you are logged in as the appropriate user and enter:
```
$ systemctl --user enable shairport-sync
```
Make sure it is _not enabled_ as a system service:
```
# systemctl disable shairport-sync
```
and then reboot.

#### Problems with User Service
Shairport Sync will function perfectly well as a user service, but there are a number of things to bear in mind:

1. The AirPlay service will only be available when the user is logged in. When the user logs out, Shairport Sync will terminate and the AirPlay service will disappear.
2. If the Linux system is a desktop Linux with a GUI, audio will be sent to the default output only when the user is logged in through the GUI.

Automatic user login may help address these problems.
