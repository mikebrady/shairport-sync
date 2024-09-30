# Shairport Sync

增加一个新分支`main`用于自己使,保留原来的`master`分支, 用于同步原作者的更新。
Shairport Sync is an [AirPlay](https://www.pocket-lint.com/speakers/news/apple/144646-apple-airplay-2-vs-airplay-what-s-the-difference) audio player for Linux, FreeBSD and OpenBSD. It plays audio streamed from Apple devices and from AirPlay sources such as [OwnTone](https://github.com/owntone/owntone-server) (formerly `forked-daapd`).

Shairport Sync can be built as an AirPlay 2 player (with [some limitations](AIRPLAY2.md#features-and-limitations)) or as "classic" Shairport Sync – a player for the older, but still supported, AirPlay (aka "AirPlay 1") protocol.

Metadata such as artist information and cover art can be requested and provided to other applications. Shairport Sync can interface with other applications through MQTT, an MPRIS-like interface and D-Bus.

Shairport Sync does not support AirPlay video or photo streaming.

# Quick Start

- A building guide is available [here](BUILD.md).
- A Docker image is available on the [Docker Hub](https://hub.docker.com/r/mikebrady/shairport-sync). Also see [docker/README.md](docker/README.md).
- Next Steps and Advanced Topics are [here](ADVANCED%20TOPICS/README.md).
- Runtime settings are documented [here](scripts/shairport-sync.conf).
- Build configuration options are detailed in [CONFIGURATION FLAGS.md](CONFIGURATION%20FLAGS.md).
- The `man` page, detailing command line options, is [here](https://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html).
- Some advanced topics and developed in [ADVANCED TOPICS](https://github.com/mikebrady/shairport-sync/tree/master/ADVANCED%20TOPICS).

# Features

- Outputs AirPlay audio to [ALSA](https://www.alsa-project.org/wiki/Main_Page), [sndio](http://www.sndio.org), [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/), [Jack Audio](http://jackaudio.org), to a unix pipe or to `STDOUT`. It also has experimental support for [PipeWire](https://pipewire.org) and limited support for [libao](https://xiph.org/ao/) and for [libsoundio](http://libsound.io).
- Metadata — Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc. through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient. Sources that supply metadata include iTunes and the Music app in macOS and iOS.
- An interface to [MQTT](https://en.wikipedia.org/wiki/MQTT), a popular protocol for Inter Process Communication, Machine-to-Machine, Internet of Things and Home Automation projects. The interface provides access to metadata and artwork, and has limited remote control.
- Digital Signal Processing facilities – please see the [DSP Wiki Page Guide](https://github.com/mikebrady/shairport-sync/wiki/Digital-Signal-Processing-with-Shairport-Sync). (Thanks to [Yann Pomarède](https://github.com/yannpom) for the code and to [Paul Wieland](https://github.com/PaulWieland) for the guide.)
- An [MPRIS](https://specifications.freedesktop.org/mpris-spec/2.2/)-like interface, partially complete and very functional, including access to metadata and artwork, and limited remote control.
- A native D-Bus interface, including access to metadata and artwork, limited remote control and system settings.
- Better Volume Control — Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder. In addition, Shairport Sync can offer an extended volume control range on devices with a restricted range.
- Support for the [Apple ALAC decoder](https://macosforge.github.io/alac/) (library available [here](https://github.com/mikebrady/alac)).
- Output bit depths of 8, 16, 24 and 32 bits, rather than the standard 16 bits.
- Output frame rates of 44,100, 88,200, 176,000 or 352,000 frames per second.

Some features require configuration at build time – see [CONFIGURATION FLAGS.md](CONFIGURATION%20FLAGS.md).

# Status

Shairport Sync was designed to [run best](ADVANCED%20TOPICS/GetTheBest.md) on stable, dedicated, stand-alone low-power "headless" systems with ALSA as the audio system and with a decent CD-quality Digital to Analog Converter (DAC).

Shairport Sync runs on recent (2018 onwards) Linux systems, FreeBSD from 12.1 onwards and OpenBSD. It requires a system with the power of a Raspberry Pi 2 or a Pi Zero 2 or better.

Classic Shairport Sync runs on a wider variety of Linux sytems, including OpenWrt and Cygwin and it also runs on OpenBSD. Many embedded devices are powerful enough to power classic Shairport Sync.

# Heritage and Acknowledgements

The functionality offered by Shairport Sync is the result of study and analysis of the AirPlay and AirPlay 2 protocols by many people over the years. These protocols have not been officially published, and there is no assurance that Shairport Sync will continue to work in future.

Shairport Sync is a substantial rewrite of the fantastic work done in Shairport 1.0 by James Wah (aka [abrasive](https://github.com/abrasive)), James Laird and others — please see [this list](https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x) of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0.

For the development of AirPlay 2 support, special thanks are due to:

- [JD Smith](https://github.com/jdtsmith) for really thorough testing, support and encouragement.
- [ejurgensen](https://github.com/ejurgensen) for advice and [code to deal with pairing and encryption](https://github.com/ejurgensen/pair_ap).
- [ckdo](https://github.com/ckdo) for pointing the way, particularly with pairing and encryption protocols, with a [functional Python implementation](https://github.com/ckdo/airplay2-receiver) of AirPlay 2.
- [invano](https://github.com/invano) for showing what might be possible and for initial Python development.
- [Charles Omer](https://github.com/charlesomer) for Docker automation, repository management automation, testing, encouragement, enthusiasm.

Much of Shairport Sync's AirPlay 2 functionality is based on ideas developed at the [openairplay airplay2-receiver](https://github.com/openairplay/airplay2-receiver) repository. It is a pleasure to acknowledge the work of the contributors there.

Thanks to everyone who has supported and improved Shairport Sync over the years.

# More about Shairport Sync

The audio that Shairport Sync receives is sent to the computer's sound system, to a named unix pipe or to `STDOUT`. By far the best sound system to use is ALSA. This is because ALSA can give direct access to the Digital to Analog Converter (DAC) hardware of the machine. Audio samples can be sent through ALSA directly to the DAC, maximising fidelity, and accurate timing information can be obtained from the DAC, maximising synchronisation. Direct access to hardware is given through ALSA devices with names beginning with `hw:`.

## Synchronised Audio

Shairport Sync offers _full audio synchronisation_. Full audio synchronisation means that audio is played on the output device at exactly the time specified by the audio source. To accomplish this, Shairport Sync needs access to audio systems – such as ALSA on Linux and `sndio` on FreeBSD and OpenBSD – that provide very accurate timing information about audio being streamed to output devices. Ideally, Shairport Sync should have direct access to the output device used, which should be a real sound card capable of working with 44,100, 88,200 or 176,400 samples per second, interleaved PCM stereo of 8, 16, 24 or 32 bits. Using the ALSA sound system, Shairport Sync will choose the greatest bit depth available at 44,100 samples per second, resorting to multiples of 44,100 if it is not available. You'll get a message in the log if there's a problem. With all other sound systems, a sample rate of 44,100 is chosen with a bit depth of 16 bit.

Shairport Sync works well with PulseAudio, a widely used sound server found on many desktop Linuxes. While the timing information is not as accurate as that of ALSA or `sndio`, it is often impractical to remove or disable PulseAudio.

For other use cases, Shairport Sync can provide synchronised audio output to a unix pipe or to `STDOUT`, or to audio systems that do not provide timing information. This could perhaps be described as _partial audio synchronisation_, where synchronised audio is provided by Shairport Sync, but what happens to it in the subsequent processing chain, before it reaches the listener's ear, is outside the control of Shairport Sync.

## Latency, "Stuffing", Timing

AirPlay protocols use an agreed _latency_ – a time lag or delay – between the time represented by a sound sample's `timestamp` and the time it is actually played by the audio output device, typically a Digital to Audio Converter (DAC). Latency gives players time to compensate for network delays, processing time variations and so on. The latency is specified by the audio source when it negotiates with Shairport Sync. AirPlay sources set a latency of around 2.0 to 2.25 seconds. AirPlay 2 can use shorter latencies, around half a second.

As mentioned previously, Shairport Sync implements full audio synchronisation when used with ALSA, `sndio` or PulseAudio audio systems. This is done by monitoring the timestamps present in data coming from the audio source and the timing information coming back from the audio system itself. To maintain the latency required for exact synchronisation, if the output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up. If the output device is running fast, Shairport Sync will insert ("stuff") extra frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible on normal audio material. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is less obtrusive than insertion and deletion but requires a good deal of processing power — most embedded devices probably can't support it. If your computer is fast enough, Shairport Sync will, by default, automatically choose this method.

Stuffing is not done for partial audio synchronisation – the audio samples are simply presented at exactly the right time to the next stage in the processing chain.

Timestamps are referenced relative to the source computer's clock – the "source clock", but timing must be done relative to the clock of the computer running Shairport Sync – the "local clock". So, Shairport Sync synchronises the source clock and the local clock, usually to within a fraction of a millisecond. In AirPlay 2, this is done with the assistance of a companion application called [NQPTP](https://github.com/mikebrady/nqptp) using a [PTP](https://en.wikipedia.org/wiki/Precision_Time_Protocol)-based timing protocol. In classic AirPlay, a variant of [NTP](https://en.wikipedia.org/wiki/Network_Time_Protocol) synchronisation protocols is used.
