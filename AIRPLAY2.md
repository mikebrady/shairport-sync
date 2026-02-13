# AirPlay 2
Please be aware that an official specification of AirPlay 2 has not been made public. What follows is based on what has been discovered so far.

### Streams
AirPlay 2 supports two stream types -- "Realtime Audio" and "Buffered Audio".
- Realtime audio is played after a short delay. The delay is usually around two seconds. (This is similar to the older Classic AirPlay format.)
- Buffered audio starts playing after a very short delay. Audio can arrive faster than it is played and is buffered locally in the Shairport Sync device.

### Formats
AirPlay 2 supports a variety of audio formats. This is a summary of what is known to work so far.
 - Compression
   - ALAC (Apple Lossless Advanced Codec) encoding is used for lossless streaming,
   - AAC (Advanced Audio Coding) is used for lossy streaming.
   - (BTW you can check your perception of lossy and lossless formats [here](http://abx.digitalfeed.net).)
 - Encoding
   - Signed 16- and 24-bit signed linear PCM samples ("S16" and "S24"),
   - 24-bit floating point PCM samples ("F24").
 - Rates
   - Sample rates of 44,100 and 48,000 frames per second (fps).
 - Channels
   - Stereo, 5.1 and 7.1 Surround Sound.

# Shairport Sync
This information relates to Shairport Sync Version 5.0 onwards.

Shairport Sync offers AirPlay 2 support for audio sources on:
- iOS devices,
- Macs from macOS 10.15 (Catalina) onwards,
- HomePod minis,
- Apple TVs.

## What Works
-  Audio synchronised with other AirPlay 2 devices.
-  AirPlay 2 audio playback:
   - Basic
     - ALAC/S16/44100/2 realtime audio (stereo),
     - AAC/F24/44100/2 buffered audio (stereo).
   - Better
     - AAC/F24/48000/2 buffered audio (stereo).
   - Lossless
     - ALAC/S24/48000/2 buffered audio (stereo).
   - Surround
     - AAC/F24/48000/5.1 and AAC/F24/48000/7.1 buffered audio (surround sound).
- Transcoding: the output will be transcoded if necessary (e.g. from 44,100 to 48,000 fps) to match the output device's rate.
- Mixdown: mixdown to fewer output channels (e.g. 5.1 to stereo) is automatic but can be controlled.
- Shairport Sync can revert to "classic" AirPlay if necessary.
- Devices running Shairport Sync in AirPlay 2 mode can be [added](https://github.com/mikebrady/shairport-sync/blob/master/ADDINGTOHOME.md) to the Home app.
- Shairport Sync can be built to support classic AirPlay (aka "AirPlay 1") only. Classic Airplay offers only one format, ALAC/S16/44100/2, but output transcoding is available to, for example, 48000 fps.

## What Does Not Work
- High-Definition Lossless -- 96,000 and 192,000 fps material -- is not supported.
- Dolby Atmos is not supported.
- AirPlay 2 for Windows iTunes is not supported.
- Remote control facilities are not implemented.
- AirPlay 2 from macOS prior to 10.15 (Catalina) is not supported.
- Multiple instances of the AirPlay 2 version of Shairport Sync can not be hosted on the same system. It seems that AirPlay 2 clients are confused by having multiple AirPlay 2 players at the same IP addresses.


## General
Shairport Sync uses a companion application called [NQPTP](https://github.com/mikebrady/nqptp) ("Not Quite PTP")
for timing and synchronisation in AirPlay 2. NQPTP must have exclusive access to ports `319` and `320`.

## What You Need
For AirPlay 2, a system with the power of a Raspberry Pi B, or better, is recommended.

Here are some guidelines: 
* Full access, including low-port-number network access or `root` privileges, to a system at least as powerful as a Raspberry Pi B.
* Ports 319 and 320 must be free to use (i.e. they must not be in use by another service such as a PTP service) and must not be blocked by a firewall.
* An up-to-date Linux, FreeBSD or OpenBSD system. This is important, as some of the libraries must be the latest available.

* Due to realtime timing requirements, Shairport Sync does not work well on virtual machines outputting to ALSA, PipeWire or PulseAudio. For the same reason, Shairport Sync does not work very well with with Bluetooth. YMMV of course, and you can have success where timing is not crucial, such as outputting to `stdout` or to a unix pipe.
* Shairport Sync can not run in AirPlay 2 mode on a Mac because NQPTP, on which it relies, needs ports 319 and 320, which are already used by macOS.
* A version of the [FFmpeg](https://www.ffmpeg.org) library with an AAC decoder capable of decoding Floating Planar -- `fltp` -- material must be in your system. There is a guide [here](TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only) to help you find out if your system has it.
* An audio output. For preference, the output device should be capable of accepting stereo or multichannel at 44,100 and 48,000 frames per second. With FFmpeg support, audio will be transcoded and mixed to match output device capabilities as necessary.
* You can use [`dacquery`](https://github.com/mikebrady/dacquery) to test the suitability of hardware ALSA audio devices on your system.
#### An Ideal System
For the highest quality audio with the highest fidelty and a minimum of audio processing, an ideal system would be a bare Linux system without a GUI and without PipeWire or PulseAudio, such as Raspberry Pi OS (Lite) or similar. In this case, Shairport Sync connects directly to the ALSA output DAC. For testing, the build-in DAC or a low-cost USB DAC is usually sufficient.

A problem with this setup is that Shairport Sync expects exclusive access to the audio device. If you have other audio sources, this can be problematic. In such a situation, an otherwise-bare system as described above, but with PipeWire added, can be used, so that all audio sources output to the PipeWire system, which takes care of mixing. Take care to ensure that the Unix users running the audio applications that use PipeWire are members of the `pipewire` group. 

## Guides
* A building guide is available at [BUILD.md](BUILD.md).
