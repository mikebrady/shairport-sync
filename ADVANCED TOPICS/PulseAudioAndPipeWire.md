# Working with PulseAudio or PipeWire
Many Linux systems, especially desktop Linuxes with a GUI, have [PipeWire](https://pipewire.org) or [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) installed as [sound servers](https://en.wikipedia.org/wiki/Sound_server).
PipeWire and PulseAudio are widely used and have the great advantage of being easily able to mix audio from multiple sources. 

However, the main thing to remember about PipeWire and PulseAudio sound servers is they only become available when a user logs in -- that is, they are set up as _user services_.
Shairport Sync relies on them and therefore it must also be set up as a user service. Shairport Sync can not be set up as a system service because the PipeWire or PulseAudio services are not available when system services are launched just after system startup.

To use PipeWire or PulseAudio-based systems, Shairport Sync must be set up as a user service.

### Considerations
1. Shairport Sync will work without modification in a PipeWire- or PulseAudio-based system if built with the default ALSA backend. This is because PipeWire and PulseAudio both provide a default ALSA pseudo-device to receive and play audio from ALSA-compatible programs.
2. Shairport Sync can be built with "native" PipeWire or PulseAudio backends by adding the `--with-pipewire` or `--with-pulseaudio` configuration flags when it is being built. This has the advantage of bypassing the ALSA compatability layer.
3. To check if PipeWire support is built into Shairport Sync, check that the string `PipeWire` is included in the version string. (Enter `$ shairport-sync -V` to get the version string.)  Similarly, the version string will include `PulseAudio` if the PulseAudio backend is built in.
4. Remember to specify which backend Shairport Sync should use in the configuration file or on the command line.

## Starting Shairport Sync as a User Service
Please refer to [this](https://github.com/mikebrady/shairport-sync/blob/development/BUILD.md#5-enable-and-start-service) section to discover how to start Shairport Sync as a user service.
