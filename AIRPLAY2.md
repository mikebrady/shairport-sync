# AirPlay 2
**Shairport Sync** (as of [v4.1](https://github.com/mikebrady/shairport-sync/releases/tag/4.1) and newer) offers **AirPlay 2** support for audio sources on:
- iOS devices,
- Macs from macOS 10.15 (Catalina) onwards,
- HomePod minis,
- and Apple TVs.

## What Works
- AirPlay 2 audio for iOS, HomePod mini, AppleTV and Mac players.
  * Audio is synchronised with other AirPlay 2 devices.
  * Two types of audio are received by Shairport Sync – "Realtime" streams of CD quality ALAC (like "classic" AirPlay) and "Buffered Audio" streams of AAC stereo at 44,100 frames per second.
  * The selection of stream type is made by the player.
  * Realtime streams generally have a latency of about two seconds. Buffered Audio streams typically have a latency of half a second or less.
  * In AirPlay 2 mode, Shairport Sync reverts to "classic" AirPlay when iTunes on macOS or macOS Music plays to multiple speakers and one of more of them is compatible with AirPlay only.

- Devices running Shairport Sync in AirPlay 2 mode can be [added](https://github.com/mikebrady/shairport-sync/blob/development/ADDINGTOHOME.md) to the Home app. 

## What Does Not Work
- No AirPlay 2 for Windows iTunes.
- Remote control facilities are not implemented.
- AirPlay 2 from macOS prior to 10.15 (Catalina) is not supported.

## General
Shairport Sync uses a companion application called [NQPTP](https://github.com/mikebrady/nqptp) ("Not Quite PTP")
for timing and synchronisation in AirPlay 2. NQPTP must run as `root` and must have exclusive access to ports `319` and `320`.

Lossless and High Definition Lossless material is transcoded to AAC before it reaches Shairport Sync. 

## What You Need
AirPlay 2 support needs a slightly more powerful CPU for decoding and synchronisation and more memory for bigger buffers and larger libraries. A system with the power of a Raspberry Pi 2 or Raspberry Pi Zero 2 W, or better, is recommended.

Here are some guidelines: 
* Full access, including `root` privileges, to a system at least as powerful as a Raspberry Pi 2 or a Raspberry Pi Zero 2 W.
* Ports 319 and 320 must be free to use (i.e. they must not be in use by another service such as a PTP service) and must not be blocked by a firewall.
* An up-to-date Linux or FreeBSD. This is important, as some of the libraries must be the latest available.
* Shairport Sync will not run in AirPlay 2 mode on a Mac because NQPTP, on which it relies, needs ports 319 and 320, which are already used by macOS.
* A version of the [FFmpeg](https://www.ffmpeg.org) library with an AAC decoder capable of decoding Floating Planar -- `fltp` -- material. There is a guide [here](TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only) to help you find out if your system has it.
* An audio output, for example an ALSA device (or `sndio` in FreeBSD). The device must be capable of running at 44,100 frames per second. You can use [`sps-alsa-explore`](https://github.com/mikebrady/sps-alsa-explore) to test the suitability of hardware ALSA audio devices on your device.
Other backends continue to work as with "classic" Shairport Sync.
- Multiple instances of the AirPlay 2 version of Shairport Sync can not be hosted on the same system. It seems that AirPlay 2 clients are confused by having multiple AirPlay 2 players at the same IP addresses.

## Guides
* A building guide is available at [BUILD.md](BUILD.md).
