# Advanced Topics
Here you will find links to some advanced features and things you can do with Shairport Sync.
* [Working with PulseAudio or PipeWire](PulseAudioAndPipeWire.md).
* [Adjusting Sync](AdjustingSync.md) – advance or delay the timing of the output from Shairport Sync to compensate for amplifier delays.
* [Metadata](Metadata.md).
* [Events](Events.md).
* [Statistics](Statistics.md).
* [Running Multiple AirPlay 2 Instances](RunningMultipleInstances.md) on one host – one independent AirPlay 2 zone per room from a single machine, VM, or set of containers.
* [Splitting a Surround Card into Per-Room Outputs](SplittingASurroundCard.md) – carve one multichannel ALSA card into several independent stereo outputs, one per room.
* Setting up an [MQTT](../MQTT.md) system.
* [Digital Signal Processing](https://github.com/mikebrady/shairport-sync/wiki/Digital-Signal-Processing-with-Shairport-Sync).
* [Car Installation](../CAR%20INSTALL.md)
– build an isolated WiFi network containing a Shairport Sync player on a Raspberry Pi. Suitable for a car radio with an `aux` input or for the stereo in that broadband-free holiday cottage.

The configuration file – which contains lots of documentation – is on your system. By default, the sample configuration file is 
placed at `/etc/shairport-sync.conf.sample` (`/usr/local/etc/shairport-sync.conf.sample` on FreeBSD).
You can also view an online version [here](../scripts/shairport-sync.conf).

Build configuration flags are discussed [here](../CONFIGURATION%20FLAGS.md).
