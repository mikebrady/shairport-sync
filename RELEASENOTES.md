Version 5.0
====
This major release represents a significant advancement in Shairport Sync's capabilities, introducing multi-channel audio support, high-resolution playback, and comprehensive performance improvements.

**Important Breaking Changes Notice:** Version 5.0 includes many breaking changes from previous versions of Shairport Sync. Please review the documentation carefully before upgrading. A useful guide to Version 5 Configuration File Changes in Version 5 is available [here](CONFIGURATIONFILECHANGES5.md). 

**New Features**

* **Multi-Channel and High-Resolution Audio Support**
  * 48,000 frames per second ("48k") operation.
  * 48k lossless stereo support.
  * 5.1 and 7.1 surround sound support.
  * Multi-channel and multi-rate operation on `ALSA`, PipeWire, PulseAudio, FreeBSD, `stdout` and Unix `pipe` output backends.

* **Automatic Audio Format Selection**
  * Flexible and controllable output format selection.
  * Automatic rate, sample format, and channel count selection.

* **Full FFmpeg Integration**
  * Support for transcoding.
  * Advanced resampling capabilities.
  * New audio format support.

* **Enhanced Resampling**
  * New `vernier` resampling and interpolation method optimized for low-power CPUs.
  * Better performance on resource-constrained devices.

* **Convolution and Loudness Enhancements**
  * Convolution system is now multithreaded and works on stereo and multichannel audio at 48k and 44.1k.
  * Multiple impulse response (IR) files can now be provided via `convolution_ir_files` setting.
  * New `convolution_thread_pool_size` setting for multithreaded processing (defaults to 1).
  * Loudness processing now works with stereo and multichannel audio at 48k and 44.1k.
  * Updated to the most recent [HiFi-LoFi FFT convolver](https://github.com/HiFi-LoFi/FFTConvolver).

* **MQTT Enhancements**
  * Added new `publish_retain` boolean option. When enabled, published MQTT messages have the `retain` flag set, so the MQTT broker stores the last message per topic and new subscribers receive the most recent value immediately. Thanks to [lululombard](https://github.com/lululombard) for [PR #2142](https://github.com/mikebrady/shairport-sync/pull/2142).

* **D-Bus Enhancements**
  * Added new `dbus_default_message_bus` command-line argument (can be `system` or `session`) to set the default message bus for both D-Bus native service and MPRIS service.

**Performance Improvements**

* Better operation on low-power devices down to Raspberry Pi B.
* Improved efficiency on embedded systems.
* Enhanced timestamp handling for better synchronization.
* Improved sync error calculation.
* Rebuilt buffered audio processor for cleaner handling of immediate and deferred flush requests.

**Docker Enhancements**

* Reduced Docker image sizes with slimmed-down `FFmpeg` library.
* Removed `dhclient` from Docker images for smaller footprint.

**Bug Fixes**

* Fixed MQTT warning on service startup: "Could not establish a mqtt connection". The startup script now correctly states that the `mosquitto` service is required. Thanks to [Hugo Villeneuve](https://github.com/hvilleneuve29) for [PR #2137](https://github.com/mikebrady/shairport-sync/pull/2137).
* Fixed compatibility with `mbedtls` library version 3.4+ (present on recent Linux versions). Thanks to [Christian Beier](https://github.com/bk138) for [finding](https://github.com/mikebrady/shairport-sync/issues/2115) and [fixing](https://github.com/mikebrady/shairport-sync/pull/2118) the bug.
* Fixed PulseAudio backend so that `PA_ERR_NODATA` returns "No latency data yet". Thanks to [Vladimir Shakov](https://github.com/bogdad) for the [report and fix](https://github.com/mikebrady/shairport-sync/pull/2119).
* Ensured old flush requests are deleted when a new play session starts. Thanks to [saujanyashah](https://github.com/saujanyashah) for the [report](https://github.com/mikebrady/shairport-sync/issues/2107).
* Fixed format warnings on 64-bit and 32-bit systems
* Removed compilation warnings on 32-bit builds
* Improved argument checking for `debug()`, `inform()`, `warn()` and `die()` functions
* Fixed "daemon" typos throughout codebase. Thanks to [Chris Boot](https://github.com/bootc) for [PR #1981](https://github.com/mikebrady/shairport-sync/pull/1981).
* Added warning if a convolution impulse response file cannot be read due to bad path or permissions

**Build System Improvements**

* Unified service file with variable substitution for Avahi support, making it easier to add future service dependencies. Thanks to [Hugo Villeneuve](https://github.com/hvilleneuve29).
* Network interface selection now only considers interfaces that are up, running and not loopback interfaces. Thanks to [Carl Johnson](https://github.com/SoarVermont) for the [suggestion](https://github.com/mikebrady/shairport-sync/issues/1996).

**Configuration File Changes and Deprecations**

* New settings: `convolution_ir_files` (replaces `convolution_ir_file`), `convolution_enabled` (replaces `convolution`), `convolution_max_length_in_seconds` (replaces `convolution_max_length`), `loudness_enabled` (replaces `loudness`).
* New `convolution_thread_pool_size` setting (defaults to 1).
* Deprecated settings: `convolution_ir_file`, `convolution`, `convolution_max_length`, `loudness`.
* Corresponding D-Bus methods and properties have been updated.

**Deprecation Notice**

* The Jack Audio and `soundio` backends are deprecated and will be removed in a future release. Consider using the updated PipeWire backend instead.

**Documentation Updates**

* Updated BUILD.md with latest build instructions.
* Updated AIRPLAY2.md with feature information.
* Enhanced convolution and loudness documentation.

**Maintenance**

* Fixed FFmpeg deprecation warnings.
* Bumped `actions/checkout` from 6.0.1 to 6.0.2.
* Bumped `docker/login-action` from 3.6.0 to 3.7.0.
* Bumped `docker/build-push-action` from 6.13.0 to 6.15.0.
* Bumped `docker/setup-qemu-action` from 3.4.0 to 3.6.0.
* Bumped `docker/setup-buildx-action` from 3.9.0 to 3.10.0.

Version 4.3 -- Security Updates, Bug Fixes and Enhancements
====
This update contains important security updates, bug fixes and enhancements. [NQPTP](https://github.com/mikebrady/nqptp) must also be updated, and it should be updated before updating Shairport Sync.
The Shared Memory Interface version of both Shairport Sync and NQPTP is now 10, i.e. `smi10`. Thus the version string of both apps should contain `smi10`:
```
$ nqptp -V
Version: 1.2.4. Shared Memory Interface Version: smi10.
$ shairport-sync -V
4.3-...-smi10-...
```
Notes
1. When updating NQPTP on Linux, be sure to remove the old service file as directed in the [README](https://github.com/mikebrady/nqptp/blob/main/README.md#remove-old-service-files).
2. Having completed both updates and installations, remember to restart NQPTP first and then restart Shairport Sync.

**Security Updates**
* A crashing bug in NQPTP has been fixed.
* The communications protocol used between NQPTP and Shairport Sync has been revised and made more resilient to attempted misuse.
* In Linux systems, NQPTP no longer runs as `root` -- instead it runs as the restriced user `nqptp`, with access to ports 319 and 320 set by the installer via the `setcap` utility.

**Enhancements**
* A new volume control profile called `dasl-tapered` has been added in which halving the volume control setting halves the output level.
For example, moving the volume slider from full to half reduces the output level by 10dB, which roughly corresponds with a perceived halving of the audio volume level.
Moving the volume slider from half to a quarter reduces the output level by a a further 10dB.
The tapering rate is slightly modified at the lower end of the range if the device's attenuation range is restricted (less than about 55dB).

  To activate the `dasl-tapered` profile, set the `volume_control_profile` to `"dasl_tapered"` in the configuration file and restart Shairport Sync.
  
  Many thanks to David Leibovic, aka [dasl-](https://github.com/dasl-), for this.
* On graceful shutdown, an `active_end` signal should now be generated if the system was in the active state. Addresses issue [#1647](https://github.com/mikebrady/shairport-sync/issues/1647). Thanks to [Tucker Kern](https://github.com/mill1000) for raising the issue.

**Bug Fixes**
* Fixed a bug that causes the Docker image to crash occasionally when OwnTone interrupted an existing iOS session. Thanks to [aaronk6](https://github.com/aaronk6) for the report.
* Fixed a cross-compliation error caused by not looking for the correct version of the `ar` tool. The fix was to substitute the correct version during the `autoreconf` phase. Thanks to [sternenseemann](https://github.com/sternenseemann) for raising the [issue](https://github.com/mikebrady/shairport-sync/issues/1705) and the [PR](https://github.com/mikebrady/shairport-sync/pull/1706) containing the fix.
* Updated the mDNS strings for the Classic AirPlay feature of AP2, so that it does not appear to provide MFi authentication. Addresses [this discussion](https://github.com/mikebrady/shairport-sync/discussions/1691).
* Always uses a revision number of 1 when looking for status updates on the DACP remote control port. This follows a suggestion in [Issue #1658](https://github.com/mikebrady/shairport-sync/issues/1658). Thanks to [ejurgensen](https://github.com/ejurgensen), as ever, for the report and the suggested fix.
* Fixed a `statistics` bug (the minimum buffer size was incorrectly logged) and also tidy up the statistics logging interval logic for resetting min and max counters.
* Added an important missing format string argument to a call in the Jack Audio backend. Many thanks to [michieldwitte](https://github.com/michieldwitte) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1693).

**Maintenance**
* Stopped using a deprecated FFmpeg data structure reference.
* Stopped using deprecated OpenSSL calls. Thanks to [yubiuser](https://github.com/yubiuser) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1684) -- which did some of the updating -- and for their guidance.
* Run workflow-based tests on PRs automatically. Thanks to [yubiuser](https://github.com/yubiuser) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1687).

Version 4.2
====
This release consists of enhancements and a number of important bug fixes to Version 4.1. For information on the new features of 4.1, including AirPlay 2 support, please see the section "Version 4.1" below.

If you are updating an existing installation of Shairport Sync, you must also update [NQPTP](https://github.com/mikebrady/nqptp). The reason is that this update to Shairport Sync requires NQPTP with Shared Memory Interface Version `smi9` and will not work with older versions.
* The Shared Memory Interface Version `smi*` needed by Shairport Sync is part of its version string, obtainable using `$ shairport-sync -V`.
* You can check the Shared Memory Interface Version of the installed version of `nqptp` using `$ nqptp -V`.

**Enhancements**
* Allow compilation with `libplist` version `2.3.0`. Thanks to [Markus Reiter](https://github.com/reitermarkus).
* Update GitHub Action Workflows -- many thanks to [yubiuser](https://github.com/yubiuser).
* Update MQTT documentation to correspond to updates in [Home Assistant](https://www.home-assistant.io/blog/2022/06/01/release-20226/#breaking-changes). Thanks to [hunhejj](https://github.com/hunhejj).
* Update NQPTP communication to improve performance. Shairport Sync now passes activity information to NQPTP, enabling it to process timing data more accurately.

**Docker Image Enhancements**
* Add basic PulseAudio support. Thanks to [Ferdynand Naczynski](https://github.com/nipsufn) and thanks also to [Noel Hibbard](https://github.com/noelhibbard) for championing this idea for a long time.
* Update to Alpine 3.17 and ensure services such as Avahi and D-Bus start in the correct order. Thanks to [yubiuser](https://github.com/yubiuser).

**Bug Fixes**
* Sometimes the AirPlay Device ID generated by Shairport Sync was all zeros, and so was invalid, causing connectivity problems. The cause of the problem was that `get_device_id` was not interpreting `getaddrinfo` information correctly. Thanks to [Carl Johnson](https://github.com/ridgelift) for [reporting](https://github.com/mikebrady/shairport-sync/issues/1657) the issue, finding the (rather obscure) cause of the problem and for providing [code](https://github.com/mikebrady/shairport-sync/issues/1657#issuecomment-1493423175) to fix it.
* Fix a bug reported in Issue [#1633](https://github.com/mikebrady/shairport-sync/issues/1633). The bug was that when a Realtime Audio stream (e.g. playing from Spotify on iOS or using Shairport Sync as the Sound Output on a Mac) was played, it was fine, but when it was stopped and a second stream was started, the new stream could not be heard. The problem was that the PTP clock was not being correctly revalidated for second and subsequent Realtime Audio streams. The fix was to ensure that the PTP clock is revalidated on second and subsequent plays on the same connection. Thanks again to [David Leibovic](https://github.com/dasl-) for finding the problem.
* Use TCP keepalive a little more generally -- treat it the same as a client closing the link rather than just an error.
* Remove three potential race conditions between Shairport Sync opening a TCP connection and the client checking that the connections are open. The problem was that the connections were being opened in threads that were created just before the client was given the connection information. If the threads were delayed (e.g. on a slow or busy processor), the client could use the connection information to check the connections, but find that they were not (yet) open. This could cause the client to terminate the session immediately with a `TEARDOWN`. The fix was to open the connections before creating those threads and before sending the connection information back to the client. In this way, the connections are guaranteed to be open before the client has the information it needs to try to open them, even if the threads ared delayed in starting. This bug would manifest itself by allowing play to proceed but not play anything.
* When built for AirPlay 2, ensure the hexadecimal string that prefixes the AirPlay 1 Service Name in the Bonjour text strings matches the AirPlay 2 Device ID. For example, if the Service Name is `Kitchen` and the AirPlay 2 Device ID is `b8:2f:eb:d7:85:df`, the AirPlay 1 Service Name should be `B82FEBD785DF@Kitchen`. (When built for AirPlay 1, the hexadecimal prefix is simply a hash of the Service Name.) Thanks to [Casper](https://github.com/casperghst42) for raising the [issue](https://github.com/owntone/owntone-server/issues/1578) and to [ejurgensen](https://github.com/ejurgensen) for identifying the cause of the problem.

Version 4.1.1
====
This release consists of enhancements and bug fixes to Version 4.1. For information on the new features of 4.1, including AirPlay 2 support, please see the section "Version 4.1" below.

**Enhancement**
* Use the TCP `keepalive` facility to close a play session if the client connection drops for a minute.

**Metadata Enhancements**
* Add `FramePosition` (`phbt`), `FirstFramePosition` (`phb0`), `OutputRate` (`ofps`), `OutputFormat` (`ofmt`), `StreamType` (`styp`), `ServiceName` (`svna`), `ClientName` (`snam`) properties to the D-Bus interface and to the metadata stream (codes in brackets).
* `FramePosition`/`FirstFramePosition` metadata is generated only if the `progress_interval` in the `metadata` section of the configuration file is non-zero. The progress interval can also be set by a new `SetFramePositionUpdateInterval` method in the D-Bus interface.
* `FramePosition`/`FirstFramePosition` metadata is of the form `<RTP Frame number>/<Local Time>` where the local time, in nanoseconds (a 64-bit number), is the precise time that frame should be played. The metadata is generated when the frame is placed in the output buffer, and is thus generated `audio_backend_buffer_desired_length_in_seconds` (usually 0.2 seconds) before the time in question.
* Add `xesam:albumArtist` and `xesam:composer` metadata (if available) to the metadata bundle presented in the D-Bus interface.
* Add a new metadata item: `sps:songdatakind`, derived from the `asdk` metadata token, to the metadata bundle presented in the D-Bus interface. If `0` it seems to indicate an item of a specific duration such as an audio track; if `1` it seems to mean the stream is of unknown duration, for example an internet radio stream.

**Docker Enhancements**
* Launch the `shairport-sync` app as `root` user within the Docker container rather than as the user `shairport-sync`.
* Add the configuration file and the sample configuration files back into the Docker image.

**Bug Fixes**
* Fix a bug that prevented multiple classic AirPlay instances being recognised. The bug was that the 12-digit classic AirPlay service name prefixes for each instance were all identically derived from a MAC hardware address. The fix was to modify the generation of  prefixes to depend on the service name as well as the hardware address.
* Fix a bug that prevented play to the the PulseAudio backend from resuming after a pause. The bug was due to changes in the way pauses were handled and the fix was to reopen the stream if it is closed whenever a play or latency request was made.
* Fix a long-standing bug which didn't close the socket used for the RTSP connection, potentially exhausting the sockets available.
* Fix a bug in `audio_alsa.c` when there is no hardware device name.
* Add a configuration check for the `xxd` program when building for AirPlay 2.
* Fix a compilation bug on certain platforms by trying to use `AC_CHECK_LIB` to find `libavcodec` if the `PKG_CHECK_MODULES` check fails.

**Documentation**
* Include and reference the HTML version of the man page.

Version 4.1
====

Really Big Update -- AirPlay 2!
----
Version 4.1 brings support for AirPlay 2 operation. It works with iOS, iPadOS, macOS, HomePod mini and Apple TV sources, but not with Windows.
Limited support is available for HomeKit -- Shairport Sync speakers can be added to the Home app, though not all features are working.
AirPlay 2 operation requires a companion program called NQPTP and requires a somewhat more powerful system. Please see [AIRPLAY2.md](./AIRPLAY.md) for more.

Note that you can still build Shairport Sync to support "classic" AirPlay (aka "AirPlay 1") as before.

The various guides for building and updating Shairport Sync have been consolidated into [BUILD.md](./BUILD.md). 

Here is a brief list of the high-level new features and changes (more to be added):
* AirPlay 2 operation.
* Improved `libao` backend for better compatibility with HomeBrew installations.
* Improved MQTT and D-Bus facilities.
* Automatically-generated Docker images.

There have been many many bug fixes and enhancements to the core operation of Shairport Sync.

Version 3.3.9
====
**Bug Fix**
* Version 3.3.9 fixes a build failure with convolution raised since `automake 1.16.5` and https://git.savannah.gnu.org/cgit/automake.git/commit/?id=f4a3a70f69e1dbccb6578f39ef47835098a04624.
Further details in [Fabrice Fontaine's original PR](https://github.com/mikebrady/shairport-sync/pull/1314) to the `development` branch. Many thanks to [Fabrice Fontaine](https://github.com/ffontaine) for the bug report and for the fix.

Version 3.3.8
====
**Enhancements**
* Documentation for the MQTT interface. Many thanks to [minix1234](https://github.com/minix1234)!

**Bug Fixes**
* Fix a bug in the `alsa` back end. In the interval between checking that the alsa device handle was non-`NULL` and actually using it, the handle could be set to `NULL`. The interval between check and usage is now protected.
* Fix a bug in the `alsa` precision timing code. Thanks to [durwin99](https://github.com/durwin99), [Nicolas Da Mutten](https://github.com/cleverer), [mistakenideas](https://github.com/mistakenideas), [Ben Willmore](https://github.com/ben-willmore) and [giggywithit](https://github.com/giggywithit) for the [report](https://github.com/mikebrady/shairport-sync/issues/1158).
* Fix a bug that caused Shairport Sync to hang, but not actually crash, if an `on-...` script failed.
* Fix a crash that occurred if metadata support is enabled during compilation but turned off in the configuration file. Thanks to [Tim Curtis](https://github.com/moodeaudio) for the report.
* Fix a crash that occurred playing from AirPower on Android. Thanks to [Ircama](https://github.com/Ircama) for the report.
* Fix the configure.ac file so that `--without-<feature>` configuration options are not interpreted as `--with-<feature>` options instead! Thanks to [David Racine](https://github.com/bassdr) for the report.

Version 3.3.7
====
**Docker Integration**
* Docker Hub integration. From now on, prebuilt Docker images should be available on the Docker Hub at https://hub.docker.com/r/mikebrady/shairport-sync.

**New Feature**
* For the PulseAudio backend `pa`, added a new `server` entry to the `pa` section of the configuration file, allowing you to specify a connection to a remote or a local system PulseAudio instance instead of letting PulseAudio choose. Thanks to [Guillaume Revaillot](https://github.com/grevaillot) for this new feature.

**Pesky Changes**
* The underlying timing system has been moved from 64-bit fixed-point time representation (like NTP) to a 64-bit unsigned nanoseconds representation. This should make precisely no difference to the functionality of Shairport Sync but the transition might inadvertently have introduced bugs. Problem reports gratefully received.

**Bug Fixes**
* Fixed a number of bugs that prevented Shairport Sync from terminating cleanly in response to the `MPRIS` interface's `Quit()` command. Thanks to [João Gabriel](https://github.com/jgabriel98) for reporting this issue.
* Related to the above, the code used to terminate the application after a fatal error has been cleaned up. It now uses the correct `exit()` call rather than the rather hacky `abort()` call, returning the value of the constant `EXIT_FAILURE` (typically `1`) to the caller .
* Fixed a bug whereby the start and end of active mode tokens `abeg` and `aend` where not generated or published --  `pend` tokens were being generated instead. Thanks to [minix1234](https://github.com/minix1234) for the bug report and [fix](https://github.com/mikebrady/shairport-sync/pull/1023).
* Fixed a bug calculating the instantaneous synchronisation error. This bug could occasionally cause Shairport Sync to lose synchronisation and maybe even to mute for a few seconds before resynchronising. It was caused doing modulo arithmetic incorrectly and it's been there for a while.
* Removed a bug which would affect initial synchronisation if a `FLUSH` command was received from the player at an inopportune time.
* Added code to do calculations involving the `audio_backend_latency_offset_in_seconds` and `audio_backend_silent_lead_in_time` settings correctly. Many thanks to [Tucker Kern](https://github.com/mill1000) for discovering a number of bugs associated with this and for [proposing a solution](https://github.com/mikebrady/shairport-sync/pull/1001). This prompted a closer investigation and a number of further improvements were made, and a few "hostages to fortune" removed.
* The DACP ID looks like a 64-bit number expressed in hexadecimal. It is normally quoted with leading zeroes removed, but in the `_dacp._tcp` service string, leading zeros are not removed from the DACP ID. The bug fix removes those leading zeroes. Thanks to [julianc1969](https://github.com/juliandc1969) for tracking down this bug so tenaciously!
* Make the first output backend in the list of backends the default and make its name the default `output_name`.
* Fix a flaw in resyncing -- a flush is set up but not triggered.
* Ensure metadata and cover art are enabled if metadata support is included at compilation.
* Set convolution defaults even if no configuration file is found.
* Handle the `active_remote_id` token as a string rather than an unsigned 32-bit number --  it seems ROON uses a longer character sequence.

**Enhancements**
* Added the strings `-alac` and `-jack` to the the version string returned by the  `-V` command line option if Apple ALAC decoder support and Jack Audio support are included respectively.
* Cleaned up and simplified the code that handles `FLUSH` requests coming from the player. (Debug messages are still a little verbose.)
* Improved timing estimation. Shairport Sync has been using linear least-squares regression to estimate timing drift between the (remote) source clock and the local clock. This technique is now extended to provide an estimate of the remote-to-local clock difference itself. (The remote-to-local clock difference is used to remap the timing of audio frames from the remote device's clock to the local clock.)

  Timing drift estimates are now saved when a sessions ends, keyed to the client IP number. When a new session starts from that IP number, the stored estimate is used until a new estimate can be generated from the new session.

  In practice, the timing techniques in use up to now have been very accurate, but this should result in slightly smoother rates of correction. 
* Tidied up the creation and initial opening of pipes. Suppress repeated pipe-opening error messages.
* Tidied up warnings and fatal error messages when log verbosity is zero.
* Cleaned up the code that provides a silent lead-in to play on a back end without synchronisation, e.g. a pipe or `stdout`.
* Added in commented-out code to check the timeliness of the release of audio to a back end without synchronisation, e.g. a pipe or `stdout`. TL;DR – so long as the back end does not block, frames will be released to it not more than one packet (352 frames) late.
* Logs and statistics can now be directed to the system log (default), `stdout`, `stderr` or to a file or pipe of your choice using a new setting, `log_output_to` in the `diagnostics` section of the configuration file. This is very useful when the system log is disabled or diverted.
* Audio data from the `pipe` back end and metadata from the metadata pipe are now written using standard blocking `write` commands rather than a slightly complex non-blocking write function. Pipes are now opened in non-blocking mode and changed to blocking mode when successfully opened.
* Add a default name for the pipe backend: `/tmp/shairport-sync-audio`.
* Separate threads are now used for each metadata subsystem. Until now, all metadata was processed on a single thread. This included writing to the metadata pipe and the multicast stream and supplying metadata for the `mqtt` interface and for the `dbus` and `MPRIS` interfaces. Unfortunately, that meant that a problem with any one of these subsystems could propagate into the others. Now they all run on separate threads. If one thread blocks, it will not interfere with the other subsystems.
* Cleaned up and improved the code to synchronise the first frame of audio. This should result in more accurate and reliable initial synchronisation, usually to under a millisecond, and often to within 20 or 30 microseconds. Syncronisation should improve even when the silent lead-in time is as short as 0.3 seconds or when the `audio_backend_latency_offset_in_seconds` is as much as -1.7 seconds, i.e. when only 0.3 seconds of latency are left when the latency would normally be 2.0 seconds.
* Cleaned up some confused uses of modulo arithmetic.
* Cleaned up the allocation of memory for gathering running statistics – the heap is now used instead of the stack.
* Cleaned up the display of statistics for backends that do not implement active synchronisation, e.g. the `pipe` and `STDOUT` back ends.
* Cleaned up the `audio_backend_silent_lead_in_time` setting by adding an `"auto"` setting.
* Improved synchronisation accuracy with short silence lead-ins.
* While a player is active, the DACP port number to which to send remote commands should be broadcast over ZEROCONF/Bonjour. However, if that information is not available, Shairport Sync will now check for it every two seconds.
* The timing software in the `sndio` backend does some extra sanity checking on certain time estimates, it may help a little when running on virtual machines.
* Open metadata and audio pipes with 666 permissions to allow them to be shared by other applications.

Version 3.3.6
====
**New Features**
* Resampling has been added for the [Jack Audio](http://jackaudio.org) backend. This can be used to up-sample to 192kHz. Thanks to [Pieter De Gendt](https://github.com/pdgendt) for this addition.

**Enhancements**
* Add `SetAirplayVolume` to the native D-Bus `RemoteControl` interface.
* Add `SetVolume` to the `MPRIS` interface.
* Add a few sample commands, including `MPRIS` commands, to the [Sample D-Bus Commands](https://github.com/mikebrady/shairport-sync/blob/master/documents/sample%20dbus%20commands) document.


**Bug Fixes**
* Add a `SIGCHLD` handler to remove completed processes that were used to perform program "hooks" without waiting for completion and thus prevent them from becoming zombie processes. Thanks to [patrickjane](https://github.com/patrickjane) for reporting the bug and for suggesting a solution. Addresses issue [#968](https://github.com/mikebrady/shairport-sync/issues/968).
* Fix a bug in the provision of metadata which would on occasion cause metadata from the previous track to be provided. Thanks to [Tuomas Hämäläinen](https://github.com/tuomashamalainen) and [HiFiBerry](https://github.com/hifiberry) for reporting. This bugfix addresses issue [#972](https://github.com/mikebrady/shairport-sync/issues/972).
* Make Shairport Sync compile in the forthcoming Fedora 32. Fedora 32 uses GCC-10 which [defaults to `-fno-common`](https://gcc.gnu.org/gcc-10/porting_to.html) exposing a number of issues with Shairport Sync – multiple definitions of some `enum`s and failure to define certain variables as `extern`. Many thanks to [Bill Peck](https://github.com/p3ck) for bringing this issue to notice. Address issue [#973](https://github.com/mikebrady/shairport-sync/issues/973).
* Treat the `mper` (Persistent ID of a track) metadata attribute as the 64-bit item that it really is rather than a 32-bit item as hithereto. Output it as a hexadecimal number on the MPRIS and D-Bus interfaces to correspond with the format of the persistent id obtained from AppleScript. Thanks to to Scott Simon (https://github.com/zutroy97) for finding a related bug in the [shairport-sync-metadata-reader](https://github.com/mikebrady/shairport-sync-metadata-reader).
* Hook up the `Volume` property in the `MPRIS` interface.
* Fix an incompatibilty with Forked Daapd that was causing Forked Daapd to lock up. Thanks to [@tomgadow](https://github.com/tomgadow) and [@ejurgensen](https://github.com/ejurgensen) for their help in finding and (hopefully) fixing this issue. Addresses issue [#953](https://github.com/mikebrady/shairport-sync/issues/953) and [Forked Daapd Issue #870](https://github.com/ejurgensen/forked-daapd/issues/870).
* Other minor bug fixes.

**Pesky Changes You Might Not Be Able To Ignore**
* Renamed the `Server` property in the D-Bus interface to `Client`. The rather strange language in use has it that a player like iTunes is a "client" of the AirPlay device like Shairport Sync, which is therefore considered the "server". The newly-renamed "Client" property is the IP number of the player.

Version 3.3.5
====

**Bug Fixes**
* Fix a crashing bug if output format `S24` was chosen. Thanks to [artenverho](https://github.com/artenverho) for reporting. Fixes Issue [#927](https://github.com/mikebrady/shairport-sync/issues/927).
* Fix a bug whereby if `Loudness` was enabled through the D-Bus interface, the output would be muted until the volume was changed.

**Enhancements**
* D-Bus interface enhancements: add `Convolution`, `ConvolutionGain` and `ConvolutionImpulseResponseFile` properties to the D-Bus interface. These properties can be set and changed at any time, even while playing. Implements the suggestion in Issue [#929](https://github.com/mikebrady/shairport-sync/issues/929). Thanks to [corrpel](https://github.com/corrpel) for the suggestion.
* Update the [sample dbus commands](https://github.com/mikebrady/shairport-sync/blob/master/documents/sample%20dbus%20commands) document.

**Pesky Changes**
* D-Bus interface change: the D-Bus `LoudnessFilterActive` property has been changed to `Loudness`. The sample D-Bus client has been updated accordingly.

Version 3.3.4
====

This is Version 3.3.3 with a small compilation error fixed.

Version 3.3.3
====

**Bug Fixes**
* Fixes a deferred crash that occurred in Ubuntu 14.04: the `shairport-sync` daemon would silently die after a fairly long period. It typically happened just after a DHCP address was renewed. The problem seemed to be related to having more than one `avahi` threaded polling loop (though this isn't documented anywhere). The fix was to consolidate the `avahi` stuff down to one threaded polling loop. Addresses issue [#895](https://github.com/mikebrady/shairport-sync/issues/895). Thanks to [Hans (the) MCUdude](https://github.com/MCUdude) for reporting and for initial troubleshooting.

* Fixes a potential crash when an incomplete `fmtp` parameter set is sent by the requesting client. Thanks to [Angus71](https://github.com/Angus71) for the fault report and for the repair.

* Fixed a potential crash -- if a plain HTTP packet (in fact, any packet that didn't have an RTSP-style header) was sent to the TCP session port (usually port 5000), Shairport Sync would crash! Thanks to @[dubo-dubon-duponey](https://github.com/dubo-dubon-duponey) for reporting. Fixes [#921](https://github.com/mikebrady/shairport-sync/issues/921).

* A fix ensures the hardware mixer of an `alsa` device is detected and initialised before responding to the first volume setting.

* Fixes were made to the MPRIS and native D-Bus interfaces. In particular, situations where artwork is absent are better handled, and the remote interface and advanced remote interface `availability` properties should be more resilient in the face of network problems. Addresses issue [#890](https://github.com/mikebrady/shairport-sync/issues/890).  Improvements were made to the detection of the remote services available when an audio source is playing. If the source is minimally compatible, e.g. iOS, Shairport Sync's `org.gnome.ShairportSync.RemoteControl` native `dbus` interface becomes "`available`". If the source is iTunes, then the `org.gnome.ShairportSync.AdvancedRemoteControl` interface also becomes `available`. Artwork, metadata, status and limited remote control facilities are accessible through these interfaces when they are in the `available` state. Thanks to [exoqrtx](https://github.com/exoqrtx) for bringing these issues to light and for testing.

* Fixes an error whereby the `'pvol'`volume metadata was no longer sent if Shairport Sync was configured to ignore volume control information coming from the audio source. Addresses issue [#903](https://github.com/mikebrady/shairport-sync/issues/903). Thanks to [Jordan Bass](https://github.com/jorbas) for reporting the regression and for identifying the commit and code in which the regression occurred.

**Enhancements**
* Instead of returning `EXIT_FAILURE`, return `EXIT_WITH_SUCCESS` on early exit with either "version" (`–version` or `-V`) or "help" (`–help` or `-h`) arguments. Thanks to [Henrik Nilsson](https://github.com/henriknil) for the patch.

* Normalises the `'pvol`' volume outputs so that when both the software and hardware attenuators are in use to extend the overall attenuation range, the maximum output level corresponds to the maximum output level of the hardware mixer.

* Add the option of including the file and line number of each log entry's source. The option is on by default and is settable in the configuration file and in the `dbus` interface.

* Rewrite the logic for identifying missing packets of audio and for asking for resends. It seems more robust -- there was a suspicion of the previous logic that resend requests were not made for some missing packets. In addition, requests for resends of continuous sequences of packets are rolled into one.

* Expose the advanced settings controlling the resend request logic. The new settings are in the `general` section:
  * `resend_control_first_check_time` is the time allowed to elapse before a packet is considered missing, defaulting to 0.1 sec. UDP packets don't always arrive in order and they don't need to be re-requested just because they arrive out of sequence. Essentially, therefore, this parameter is to prevent needless resend requests for packets that are already in transit.
  * `resend_control_check_interval_time` is the interval between repeated requests for a missing packet, defaulting to 0.25 seconds. 
  * `resend_control_last_check_time` is the time by which the last check should be done before the estimated time of a missing packet's transfer to the output buffer, defaulting to 0.1 seconds. In other words, if a packet is still missing 0.1 seconds before it is due to be transferred to the DAC's output buffer, don't bother asking for a resend.

* Exposes two advanced `metadata` settings related to handling cover art:
  * The setting `cover_art_cache_directory` allows you to specify where cover art files will be cached if Shairport Sync has been built with native D-Bus, MPRIS or MQTT support. The default is `/tmp/shairport-sync/.cache/coverart`. If you set it to an empty list: `""`, caching is disabled. This might be useful in, say, an embedded device, or wherever you want to minimise file writing.
  * The setting `retain_cover_art` is part of the `diagnostics` group. Set it to `"yes"` to retain cover art cached by the D-Bus, MPRIS or `mqtt` interfaces. Your directory may fill up if you leave it set!

Version 3.3.2
===
**Bug Fixes**
* Fix a bug that sometimes caused a crash when a service name was specified in the configuration file. The fix was to be more systematic in allocating and deallocating memory for temporary strings. Thanks to [Chris Boot](https://github.com/bootc), [Ari Sovijarvi](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925577#5), [Bernhard Übelacker](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925577#10) and [Jeroen Massar](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925577#17) for the bug report. Fixes [Debian Bug report #925577]( https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925577) and supercedes [Pull Request #879](https://github.com/mikebrady/shairport-sync/pull/879).
* Correct some documentation typos – thanks again to [Chris Boot](https://github.com/bootc).

**Enhancements**

Add some settings for controlling the disable_standby_mode.
* The first setting is the `disable_standby_mode_silence_threshold`, which is the amount of audio in the output device's hardware buffer. It should normally be close to the value given in the `audio_backend_buffer_desired_length_in_seconds` setting. If it drops to this value, silence is added to the buffer to prevent the output device from becoming idle.
* The second new setting is the `disable_standby_mode_silence_scan_interval` which is the time between checks of the output device's hardware buffer.

Version 3.3.1
===

**Bug Fixes**
* Fix a bug in the MQTT documentation and add sanity checking for the port chosen -- thanks to [David Crook](https://github.com/idcrook).
* Fix a bug that caused manual format and rate setting to be ignored -- thanks to [Jörg Krause](https://github.com/joerg-krause).
* Add missing support for format settings S24_LE, S24_BE, S32_LE and S32_BE.
* Fix a bug that caused dither to be too loud.
* Fix error message for invalid `disable_standby_mode` choice -- thanks to [Tim Curtis](https://github.com/moodeaudio) at [Moode Audio](https://moodeaudio.org).

Version 3.3
====
Version 3.3 is focused on stability improvements and also offers a number of enhancements. Here is a selection of the most important:

**Enhancements**
* Automatic `alsa` output device speed and format selection. The greatest bit depth and the lowest multiple of 44,100 frames per second settings are chosen automatically by default. Manual selection is still available.
* Automatic interpolation selection. If the CPU is fast enough, the better-quality `soxr` interpolation method is chosen. Otherwise `basic` interpolation is used. Manual selection is still available.
* A new `active` state. New hooks are provided to execute programs before entering and after leaving the `active` state which covers sequences of play sessions separated by short intervals. This simplifies amplifier switch-on and switch-off, for example.
* DAC crackle minimisation. This new feature is intended to minimise pops and crackles caused in some Digital to Analog Converters (DACs) when they transition between active and idle or standby operation. The new `alsa`-only `disable_standby_mode` prevents the DAC entering the standby mode by keeping it active, either permanently or while Shairport Sync is in the active state. This feature is switched off by default.
* A new backend to interface Shairport Sync to [Jack Audio](http://jackaudio.org), thanks to the work of [Jörn Nettingsmeier](https://github.com/nettings).
* A new [MQTT](https://en.wikipedia.org/wiki/MQTT) client interface, thanks to the work of [Till Zimmermann](https://github.com/tillz).
* Changes in logging -- now you should add `-u` to direct log entries to STDERR (typically the console) rather than the system log. For example, to get logs of verbosity 1 to appear on the console: `$ shairport-sync -v -u`.
* The help menu now lists all `alsa` output devices found.
* Better support for big-endian CPUs.
* Shairport Sync accepts AirPlay streams containing CD-quality uncompressed PCM.

**Bug Fixes**
* Lots of bugs – too many to list here – have been fixed that significantly improve the stability of Shairport Sync. The full list is in the Release Notes.

For more details of enhancements and bug fixes, please see the [Release Notes for 3.3](https://github.com/mikebrady/shairport-sync/releases/tag/3.3). Special thanks to [gibman](https://github.com/gibman).

Version 3.2.2
====
Please see the [Release Notes for 3.2](https://github.com/mikebrady/shairport-sync/releases/tag/3.2).

**Enhancement**
* Latency calculations have been updated to accommodate changes in iOS 12 and AirPlay connections from macOS Mojave. Thanks to [artenverho](https://github.com/artenverho) who first reported the issue.

* Formatting of the settings file `shairport-sync.conf` has been fixed, thanks to the work of [roblan](https://github.com/roblan).

**Bug Fix**
* Fixed a problem that prevented the `run_this_when_volume_is_set` script or program from running when the volume control is changed. Thanks to [shaven](https://github.com/shaven) for the report.

Version 3.2.1
====

**Bug Fix**
* Fix a bug that was causing problems when `soxr` interpolation was chosen. Problems included instability during or after a play session and possibly an extremely slight click on very loud audio. Thanks are due to [hanaguro](https://github.com/hanaguro), [FnasBas](https://github.com/FnasBas), [priitohlo](https://github.com/priitohlo), [David Krmpotić](https://github.com/davidhq) and [artenverho](https://github.com/artenverho).

Version 3.2
====
Version 3.2 is equivalent to 3.2RC13.

Version 3.2RC13
====
**Adjustment**
* Reduce the number of attempts that will be made to have a missing packet resent. It seems that in some situations on poor networks, the number of resend requests was causing problems.

Version 3.2RC12
====
**Enhancement**
* Add a "ProgressString" property to the native D-Bus interface.

**Bug Fix**
* Some code added in RC11 to monitor mutex lock times was causing some annoying issues -- fixed.

Version 3.2RC11
====
**Bug Fixes**

* Restore compatibility with Synology AudioStation/5.2. Thanks to [Jörg Krause](https://github.com/joerg-krause) for identifying both the issue itself and the likely location of the fix needed. It's not clear if the problem lies with Shairport Sync or Synology – the fix was to ensure that an RTSP reply was sent in one `SEND` call, which shouldn't be important.
* Fix a bug that was causing Shairport Sync to hang very occasionally when a play session was ending. This seems to occur under rare circumstances, therefore many thanks to [David Krmpotić](https://github.com/davidhq) both for finding the problem and for his patience in trying out different theories before the bug was finally identified. More long-term work is probably needed here.

Version 3.2RC10
====
**Compatibility Adjustment**

* Disable the use of all hardware mute mechanisms by default, for compatibility with other applications using the same output card.
* Rename the `alsa` setting `mute_using_playback_switch` to `use_hardware_mute_if_available` and make it control volume-based mute as well as playback-switch-based mute.

Version 3.2RC9
====
**Bug Fix**

* Restore the correct pathname for the Shairport Sync PID file used in FreeBSD and System V Linux installations.

Version 3.2RC8
====
**Bug Fixes**

* Fix a bug causing a play session to hang up on Cygwin 64. Fix a bug that might cause excessive resend requests. Fix a number of other (apparently) silent bugs.

Version 3.2RC7
====
**Bug Fixes**

* A very subtle bug that would occasionally cause a loud 80 millisecond buzz when a new track was selected has been located and fixed. Thanks to [mistipen](https://github.com/mistepien) and [artenverho](https://github.com/artenverho) for help with this.

Version 3.2RC6
====
**Bug Fixes**

* Restores audio sync with videos on older versions of iOS (prior to iOS 11.2) and macOS by restoring the old way of determining latency for clients identifying with AirPlay user agent strings having a version of 353 (iOS 11.1.2) or earlier. Thanks to [Mmoi-Fr](https://github.com/Mmoi-Fr) for help with this.

Version 3.2RC5
====
**Bug Fixes**
* When errors occur sending resend requests, back off for 0.5 seconds, rather than 10 seconds, and make the code actually work.
* Make the configuration options for including the dbus interface and the mpris interface be `--with-dbus-interface` and `--with-mpris-interface` as expected, not `--with-dbus` and `--with-mpris` as they actually were.

Version 3.2RC4
====
**Bug Fixes**
* 3.2RC4 now sends resend requests (correctly) in the control channel rather than (incorrectly) over the audio channel as before. This makes it compatible with AirAudio and also makes it conform to the [Unofficial AirPlay Protocol Specification](https://nto.github.io/AirPlay.html). This bug went unnoticed for so long because other AirPlay sources accept resend requests in the audio channel. Thanks to [funtax](https://github.com/funtax) and to [Janusz Kowalczyk](https://github.com/kowalcj0) for bringing the issue to the fore.

Version 3.2RC3
====
**Enhancements**
* Lost UDP packet resending code improved for better performance on busy or congested networks.

**Bug Fixes**
* 3.2RC3 removes fixes some important and long-standing bugs that could make Shairport Sync less resilient on noisy or poor quality networks.

Version 3.2RC2
====
**Bug Fixes**
* 3.2RC2 makes Shairport Sync work with AirAudio, removing a crashing bug, and includes some more debugging code.

Version 3.2RC1
====
**Enhancements**
* Shairport Sync now offers an IPC interface via D-Bus. It provides an incomplete but functional MPRIS interface and also provides a native Shairport Sync interface. Both provide some metadata and some remote control. The native D-Bus interface permits remote control of the current AirPlay or iTunes client. It includes status information about whether the remote control connection is viable or not, i.e. whether it can still be used to control the client. A remote control connection to the audio client becomes valid when the client starts AirPlaying to Shairport Sync. The connections remains valid until the audio source deselects Shairport Sync for AirPlay, or until the client disappears, or until another client starts AirPlaying to Shairport Sync. After 15 minutes of inactivity, the remote control connection will be dropped.
* OpenBSD compatibility. Shairport Sync now compiles on OpenBSD -- please consult the OpenBSD note for details.
* A new `general` option `volume_control_profile`, for advanced use only, with two options: `"standard"` which uses the standard volume control profile -- this has a higher transfer profile at low volumes and a lower transfer profile at high volumes --  or `"flat"` which uses a uniform transfer profile to linearly scale the output mixer's dB according to the AirPlay volume.
* Some DACs have a feature that the lowest permissible "attenuation" value that the built-in hardware mixer can be set to is not an attenuation value at all – it is in fact a command to mute the output completely. Shairport Sync has always checked for this feature, basically in order to ignore it when getting the true range of attenuation values offered by the mixer.
However, with this enhancement, Shairport Sync can actually use this feature to mute the output where appropriate.
* Added compatibility with [Swinsian](https://swinsian.com), a Mac music player.

**Bug Fixes**
* Better AirPlay synchronisation. Older versions of Shairport Sync added an 11,025 frame (0.25 seconds) offset to all the latencies negotiated with the sender. This seems now  incorrect for AirPlay sources. Accordingly, the logic Shairport Sync uses to determine the extra delay has been revised. The result is better sync with videos, e.g, YouTube, while iTunes and ForkedDaapd synchronisation is unaffected.
* Much faster termination of a play session, leading to increased stability playing YouTube audio, etc.
* Improved resynchronisation logic should improve performance with slow-to-download YouTube videos.
* Dithering is now off when ignore_volume_control is true. Thanks to [yejun](https://github.com/yejun) for working on this.
* Better compatibility with TuneBlade -- Shairport Sync honours the latency settings properly now.
* Fix timing error when using Airfoil as a source.
* Better handling of missing timing packets.
* Shairport Sync will now log an unexpectedy dropped or faulty RTSP connection. This might be useful on noisy networks.
* Better volume level continuity. In recent versions of iOS (11.2) and mac OS (10.13.2), when play is resumed after a pause, the volume level is not always restored, and, if software volume control is being used, Shairport Sync plays at full volume. This issue has been addressed by storing the last airplay volume setting when a play session ends and using it as a default when a new play session begins.
* Better mixer compatibility. A bug in the hardware volume control affected output devices that have hardware mixers but that do not allow the volume to be set in dB. One example is the Softvol plugin in ALSA. Shairport Sync failed silently when presented with such a device when hardware volume control is enabled: the volume events have no effect. The bug has been fixed by adding two missing lines of code to the `init()` function in `audio_alsa.c`. Thanks to [Jakub Nabaglo](https://github.com/nbgl) for finding and fixing the bug.
* A number of bug fixes due to [belboj](https://github.com/belboj). Many thanks for these!
* Enhancements to the handling of quit requests by threads, thanks(again) to [belboj](https://github.com/belboj)!

**Other Changes**
* `clip` and `svip` metadata messages are now only generated for a play connection, not for all connections (e.g. connections that just enquire if the service is present).
* `pfls` and `prsm` metadata messages are less frequent, especially when a play session starts.
* Shairport Sync now uses about an extra half megabyte of RAM for compatibility with TuneBlade's option to have a very long latency -- up to five seconds.
* Only ask for missing packets to be resent once, and if any error occurs making the request, stop for 10 seconds.
* Include the `-pthread` flag -- including the pthread library with `-lpthread` isn't always enough.
* Add optional timing annotations to debug messages -- see the new settings in the diagnostic stanza of the configuration file.

**Updated Documentation**
* CAR INSTALL and OPENBSD notes added.
* Improvements in the documentation relating to scripts -- thanks to [Niklas Janz](https://github.com/Alphakilo).
* Typo fix! Thanks to [corbinsantin](https://github.com/corbinsantin).



Version 3.1.7
====
* Stable 3.1.5 and 3.1.6 skipped to synchronise the shairport-sync.spec file contents with the release.

**Enhancement**
* The metadata output stream can include a `dapo` message carrying the DACP port number to be used when communicating with the DACP remote control. This might be useful because the port number is actually pretty hard to find and requires the use of asynchronous mdns operations. You must be using the Avahi mdns back end.

Version 3.1.4
====

**Security Update**

* The version of `tinysvcmdns` bundled in Shairport Sync has a buffer overflow bug: *"An exploitable heap overflow vulnerability exists in the tinysvcmdns library version 2016-07-18. A specially crafted packet can make the library overwrite an arbitrary amount of data on the heap with attacker controlled values. An attacker needs send a dns packet to trigger this vulnerability."* The vulnerability is addressed  by additional checking on packet sizes. See also [CVE-2017-12087](https://bugs.launchpad.net/bugs/cve/2017-12087) and [Vulnerability in tinysvcmdns](https://bugs.launchpad.net/ubuntu/+source/shairport-sync/+bug/1729668).
Thanks and [Chris Boot](https://github.com/bootc) for fixing this bug.

**Enhancement**
* The metadata output stream can include a `dapo` message carrying the DACP port number to be used when communicating with the DACP remote control. This might be useful because the port number is actually pretty hard to find and requires the use of asynchronous mdns operations. You must be using the Avahi mdns back end.

**Bug Fix**

* Somewhere in version 3.x, the `softvol` plugin got broken as the volume change is not applied anymore. Turned out that, for the `softvol` plugin, no `volume()` and `parameters()` are defined. Thanks to [Jörg Krause](https://github.com/joerg-krause) for locating and fixing this bug.

Version 3.1.3
====

**Bug Fixes**
* Fixed a bug that prevented Shairport Sync from starting automatically on systems using the System V startup system (e.g. Ubuntu 14.04). The problem was that the directory to be used – `/var/run/shairport-sync/` – was deleted on power down and needed to be recreated on startup. In it's absence, Shairport Sync would not start and would report a mysterious daemon error \#2.

Version 3.1.2
====

Shairport Sync is more stable playing audio from YouTube and SoundCloud on the Mac. 

**Pesky Changes You Should Not Ignore**
* When you update from a previous version of Shairport Sync, your output device may have been left in a muted state. You should use a command line tool like `alsamixer` or `amixer` to unmute the output device before further use.

**Change of Default**
* The default value for the `alsa` setting `mute_using_playback_switch` has been changed to `"no"` for compatibility with other audio players on the same machine. The reason is that when this setting is set to `"yes"`, the output device will be muted when Shairport Sync releases it. Unfortunately, other audio players using the output device expect it to be unmuted, causing problems. Thanks to [Tim Curtis](https://github.com/moodeaudio) at [Moode Audio](http://moodeaudio.org) and [Peter Pablo](https://github.com/PeterPablo) for clarifying the issue.

**Bug Fixes**
* Fixed bugs that made Shairport Sync drop out or become unavailable when playing YouTube videos, SoundCloud streams etc. from the Mac. Background: there has been a persistent problem with Shairport Sync becoming unavailable after playing, say, a YouTube clip in a browser on the Mac. Shairport Sync 3.1.2 incorporates a change to how certain AirPlay messages are handled. Introduced in nascent form in 3.1.1, further follow-on changes have improved the handling of player lock and have simplified and improved the handling of unexpected loss of connection. Shairport Sync also now works properly with SoundCloud clips played in a browser on the Mac.
* Using [infer](https://github.com/facebook/infer/blob/master/README.md), a number of silent issues have been detected, such as not checking some calls to `malloc` to  ensure the response is not NULL. Most of these have been addressed by additional checks.

Version 3.1.1 
====

**Bug Fixes**
* A bug in the `sndio` backend has been fixed that caused problems on some versions of Linux.
* A change has been made to how Shairport Sync responds to a `TEARDOWN` request, which should make it respond better to sequences of rapid termination and restarting of play sessions. This can happen, for example, playing YouTube videos in Safari or Chrome on a Mac.
* Choosing `soxr` interpolation in the configuration file will now cause Shairport Sync to terminate with a message if Shairport Sync has not been compiled with SoX support.
* Other small changes.

Version 3.1
====
Version 3.1 brings two new backends, optional loudness and convolution filters, improvements in non-synchronised backends, enhancements, stability improvements and bug fixes. 

**New Features**
* A `sndio` backend gives Shairport Sync native fully synchronised output on OpenBSD and FreeBSD, thanks to the work of [Tobias Kortkamp (t6)](https://github.com/t6).
* A `pa` backend now allows Shairport Sync to provide synchronised output on PulseAudio-equipped systems -- many desktop Linuxes use PulseAudio as their sound manager.
* Optional loudness and convolution filters can be incorporated in the audio processing chain, thanks to the fantastic work of [yannpom](https://github.com/yannpom).
* A volume-change program hook `run_this_when_volume_is_set` has been added to the `general` settings stanza to execute an application whenever the volume is changed.

**Pesky Changes You Should Know About**
* The `audio_backend_buffer_desired_length_in_seconds` and `audio_backend_latency_offset_in_seconds` settings have been moved from individual backend stanzas to the `general` stanza. They now have an effect on every type of backend.
* If you are using a System V (aka `systemv`) installation, please note that the default location for PID file has moved -- it is now stored at `/var/run/shairport-sync/shairport-sync.pid`. This change is needed to improve security a little and to improve compatibility across platforms. If you're not doing anything strange, this should make no difference.

**Enhancements**
* Resynchronisation, which happens when the synchronisation is incorrect by more than 50 ms by default, should be a lot less intrusive when it occurs – it should now either insert silence or skip frames, as appropriate. 
* The Linux installer has been improved and simplified and a FreeBSD installer introduced.
* A new setting, `audio_backend_silent_lead_in_time`, allows you to set the duration of the period of silence played (the "silent lead-in") before a play session starts.
* A new command-line option, `--logOutputLevel`, allows you to output the volume levels to the log whenever they are changed. This may be useful during setup.
* Improvements have been made to the handling of large items of metadata over UDP.
* A new command line option, `-j`, demonizes Shairport Sync without creating a PID file.
* A new `alsa`-only setting, `mute_using_playback_switch`, is available for advanced use.
* Other minor enhancements.

**Bug Fixes**
* Stability improvements. More care has been taken (!) to make code thread-safe, resulting in improved stability.
* Conversion from stereo to mono has been fixed to avoid clipping while preserving full resolution. Thanks to [Robert Jones (RobDeBagel)](https://github.com/RobDeBagel) for bringing this to notice.
* Short intrusions of audio at the start of a new session from the end of the previous session have been eliminated.
* Many (many!) miscellaneous bugs fixed.

Version 3.0.2
====
**Bug Fixes**
* Fixed bugs in the `ao`, `pulseaudio` and `sndio` back ends. Basically they were expecting default sample rate and depth information, and were terminating when they saw explicit rate and depth data.

Version 3.0.1
====
This update fixes one alarming and potentially very noisy bug and restores the identification of Shairport Sync as "ShairportSync" so that TuneBlade recognises it as an open source application.
**Bug Fixes**
* Fixed a bug that was causing Shairport Sync to possibly make a very loud and alarming noise whenever an audio frame was missing.
* In 2.8.6, a change was made to the way Shairport Sync identified itself, so that it could be recognised by TuneBlade as an open source application and treated preferentially. That change was inadventently lost in the transition from 2.8.6 to 3.0. Now it's restored.

Version 3.0
====

Big Update
----
Version 3 brings in support for 24-bit and 32-bit (and 8 bit!) DACs and for DACs running at multiples of 44,100 samples per second.

The most obvious audible change is if you are using software volume control and can take advantage of 32- or 24-bit DACs. Dithering can now occur on a 32-bit or 24-bit sample rather than on a 16-bit sample, making the noise floor very much lower. This is the case, for example, with a Pimoroni PHAT DAC.

Here is the list of new features:

**New Features**
* 8-bit, 16-bit, 24-bit, 24-bit three-byte (S24_3LE and S24_3BE) and 32-bit output to ALSA devices. 
* 44,100, 88,200, 176,400 and 352,800 sample per second output. This is done using simple upsampling. It's only worth doing if 44,100 samples per second output is not available.
* Internal processing including software volume control and interpolation is done after sample size and rate conversion.
* Apple ALAC decoder support. This needs the `libalac` library, available at [ALAC](https://github.com/mikebrady/alac), to be installed. Add the flag `--with-apple-alac` to the `./configure` arguments. Then you can choose the Apple ALAC decoder in the configuration file.
* Support for `mbed TLS` has been added and the use of `PolarSSL` is deprecated, as `mbed TLS` is a development of `PolarSSL` and `PolarSSL` itself is not being developed further.
* Choose Network Interface. Add a new setting, for advanced users only, in the `general` section. Use the `interface` setting to allow you to specify the interface on which to provide the AirPlay service. Omit the setting to get the default, which is to choose the interfaces automatically.
* Set Max Volume. Add a new setting, for advanced users only, in the `general` section. Use the `volume_max_db` setting to allow you to specify the maximum level to set on the hardware mixer (if chosen) or the built-in software mixer otherwise. The software mixer's range is 0.0 dB to -96.1 dB. The setting must be a number with a decimal point, e.g. 21.3.
* An experimental new back end for `libsoundio`, a C library for cross-platform real-time audio input and output. Many thanks to [Serg Podtynnyi](https://github.com/shtirlic). Please see https://github.com/mikebrady/shairport-sync/pull/433 for more details.


Pesky Changes You Cannot Ignore
----
* Processor load is up by about 11%.
* Settings have changed -- basically, any timings that were denominated in frames are now in seconds. Please refer to the shairport-sync.conf.sample file for details.
* Sox-based interpolation at higher sample rates may overload your CPU -- you might have to choose between higher sample rates and sox-based interpolation.


Version 3.0rc0 – Release Candidate 0
----
Note: all Version 3 changes are summarized above.

**New Feature**
* An experimental new back end for `libsoundio`, a C library for cross-platform real-time audio input and output. Many thanks to [Serg Podtynnyi](https://github.com/shtirlic). Please see https://github.com/mikebrady/shairport-sync/pull/433 for more details.

**Other changes**
* Updates to `man` page and  [README](README.md). Reports of typos or suggestions for improvement are welcome!

Version 3.0d24 – Development Version
----
Note: all Version 3 changes are summarized above.

**New Feature**
* Set Max Volume. Add a new setting, for advanced users only, in the `general` section. Use the `volume_max_db` setting to allow you to specify the maximum level to set on the hardware mixer (if chosen) or the built-in software mixer otherwise. The software mixer's range is 0.0 dB to -96.1 dB. The setting must be a number with a decimal point, e.g. 21.3.

Version 3.0d23 – Development Version
----
Note: all Version 3 changes are summarized above.

**New Feature**
* Choose Interface. Add a new setting, for advanced users only, in the `general` section. Use the `interface` setting to allow you to specify the interface on which to provide the AirPlay service. Omit the setting to get the default, which is to choose the interfaces automatically.

Version 3.0d22 – Development Version
----
Note: all Version 3 changes are summarized above.

**Bug Fix**
* Fixed a bug which prevented successful building in the OpenWrt build system. The problem was caused by an `#include apple_alac.h` in `player.c` which was actioned even if the apple alac decoder was not selected. This caused the OpenWrt build system to expect the standard C++ library – required by the apple alac code – to be referenced, but it was not specified on the build manifest and therefore stopped the build. The solution was to make the `#include` conditional on selecting the apple alac decoder.

Version 3.0d21 – Development Version
----
Note: all Version 3 changes are summarized above.

**Bug Fix**
* Fixed a bug which turned off resync by default. Duh.

Version 3.0d20 – Development Version
----
Note: all Version 3 changes are summarized above.

**Bug Fix**
* Fix a small and generally silent error in configure.ac so that it only looks for the systemd directory if systemd has been chosen. It caused a warning when cross-compiling.

Version 3.0d19 – Development Version
----
Note: all Version 3 changes are summarized above.

**New Feature**
* Reduces processor load back to V2.X levels by using a precalculated array of pseudorandom numbers to do dithering. Doesn't seem to make any audible difference.

Version 3.0d18 – Development Version
----
Note: all Version 3 changes are summarized above.

**New Features**
* 8-bit, 16-bit, 24-bit, 24-bit three-byte (S24_3LE and S24_3BE) and 32-bit output to ALSA devices. (Other back ends are not updated yet.)
* 44,100, 88,200, 176,400 and 352,800 sample per second output. This is done using simple upsampling.
* Internal processing including software volume control and interpolation is done after sample size and rate conversion.
* Apple ALAC decoder support. This needs the `libalac` library, available at [ALAC](https://github.com/mikebrady/alac). Add the flag `--with-apple-alac` to the `./configure` arguments. Then you can choose the Apple ALAC decoder in the configuration file.
* Support for `mbed TLS` has been added and the use of `PolarSSL` is deprecated, as `mbed TLS` is a development of `PolarSSL` and `PolarSSL` itself is not being developed further.
* Settings that were denominated in frames are now deprecated but still honoured. Deprecation warnings are issued.

Pesky Changes You Cannot Ignore
====
* Settings have changed -- basically, any timings that were denominated in frames are now in seconds. Please refer to the shairport-sync.conf.sample file for details.
* Sox-based interpolation at higher sample rates may overload your CPU -- yopu might have to choose between higher sample rates and sox-based interpolation.

**Bugs**
* Documentation is not updated.

Version 2.8.6 – Stable Candidate
----

**Enhancements**
* This release contains a small change – it identifies itself as a ShairportSync device rather than an AirPort device. This should make it possible for Tuneblade, and possibly other players, to recognise it correctly. 

Version 2.8.5 – Stable Version
----
This release includes bug fixes and minor enhancements and is recommended for all users.

Note: if you're upgrading, there is a new `./configure` option:  
====
The build process now uses the directory path `sysconfdir` to determine where to place the configuration file `shairport-sync.conf`.
The default value for `sysconfdir` is `/usr/local/etc` which is used in the BSD family, whereas `/etc` is normally used in Linux.
To retain the present behaviour of Shairport Sync, *you must add an extra parameter to the `./configure... ` command.* The parameter you must add is `--sysconfdir=/etc`. (This has been added to the sample configuration command line in README.md.)

The enhancements and bug fixes in 2.8.5 were made in versions 2.8.4.1 to 2.8.4.8 inclusive. Please read below for the full list.

Version 2.8.4.8 – Development Version
----
**Enhancements**
* Add a new metadata item `clip` (for `CL`ient `IP`). This item is a string comprising the IP number of the "client", and is sent when a play session is starting. The "client" is the sender of the audio stream, e.g. iTunes on a Mac, or the Music player in iOS.
* When synchronisation has been disabled on the ALSA device (you should only do this for testing), Shairport Sync now refrains from asking for buffer length information from the device.

Version 2.8.4.7 – Development Version
----

* This update means the build process now uses the directory path `sysconfdir` to determine where to place the configuration file `shairport-sync.conf`. The default value for `sysconfdir` is `/usr/local/etc` which is used in the BSD family, whereas `/etc` is normally used in Linux. So, to retain the present behaviour of Shairport Sync, you must add an extra parameter to the `./configure... ` command. The parameter you must add is `--sysconfdir=/etc`. (This has been added to the sample configuration command line in README.md.)
* Shairport Sync has been updated to use the value of `sysconfdir` to determine where to look for the configuration file. If `sysconfdir` has been left with its default value of `/usr/local/etc`, then Shairport Sync will look for `/usr/local/etc/shairport-sync.conf`. If, as recommended for Linux, `sysconfdir` has been set to `/etc`, then Shairport Sync will look, as before, for `/etc/shairport-sync.conf`.

**Enhancement**
* The version string output when you use the command-line option `-V` now includes the value of the `sysconfdir`, e.g. `2.8.4.7-OpenSSL-Avahi-ALSA-soxr-sysconfdir:/etc`.

Version 2.8.4.6 – Development Version
----
**Enhancement**
* Add a new `alsa` configuration setting: `use_mmap_if_available` to control the use of mmap. The default is `"yes"` -- see [#351](https://github.com/mikebrady/shairport-sync/issues/351).

Version 2.8.4.5 – Development Version
----
**Enhancement**
* Handle varying packet lengths -- this makes it compatible with the HTC Connect, HTCs AirPlay implementation. Thanks to [Jörg Krause](https://github.com/joerg-krause) for his detective work, and see [#338](https://github.com/mikebrady/shairport-sync/issues/338).

Version 2.8.4.4 – Development Version
----
**Enhancement**
* Use alsa direct access (mmap) feature to improve performance if mmap is supported. Thanks to [Yihui Xiong](https://github.com/xiongyihui).

Version 2.8.4.3 – Development Version
----
**Bug Fix**

* Set the RTSP socket to close on `exec()` of child processes; otherwise, background `run_this_before_play_begins` or `run_this_after_play_ends` commands that are sleeping prevent the daemon from being restarted because the listening RTSP port is still in use. Fixes [#329](https://github.com/mikebrady/shairport-sync/issues/329).

Version 2.8.4.2 – Development Version
----
**Bug Fixes**

* Fixed an issue where you could not compile the audio_pipe back end without enabling metadata support (thanks to [busa-projects](https://github.com/busa-projects) for reporting the issue).
* Fixed a few small issues causing compiler warnings in `mdns_dns_sd.c`.


**Other**
* Removed the INSTALL file – it's generated automatically by `autoreconf -fi` anyway – added it to the files to be ignored in `.gitignore` and added a simple `INSTALL.md` file.

Version 2.8.4.1 – Development Version
----
**Bug Fixes**

* Fixed two issues when including support for `pulseaudio`.
* Corrected two small errors in sample parameters for the UDP metadata stream settings, thanks to [rkam](https://github.com/rkam).

Version 2.8.4 – Stable Version
----

The following is a summary of the bug fixes and enhancements since version 2.8.3.

**Bug Fixes**

* Checks have been added for empty or NULL audio buffers that were causing assertion violations and subsequent abnormal program termination.

* An IPv6 bug has been fixed — a bug in the networking software would not allow an IPv6 link-local connection to be made from a client if Shairport Sync was running on a device with more than one network interface. The solution was to take account of the `config_id` information.

* Some problems have been fixed with the non-blocking write function used to write metadata.

* A bug in the volume control transfer function has been fixed, thanks to [Jörg Krause](https://github.com/joerg-krause).

**Enhancements**

* Shairport Sync now works with [AllConnect/Streambels](http://allconnectapp.com) on Android with password protection. (As with all Android clients, you should set the `drift` to something large, like 500 or 1,000, as the timekeeping of these clients isn't as accurate as that of iTunes, etc.)

* The networking subsystem has been modified to always use the same IP number during a session. Background: the computer Shairport Sync is running on can have many IP numbers active at the same time – various IPv6 numbers and also various IPv4 numbers. During a play session, when Shairport Sync has to create connections back to the source, it would use an automatically-assigned IP number for itself, but that number might not be same as the the number used earlier in the session. From now on, it will always use the same IP number it used when the connection was first established. Thanks to [ejurgensen](https://github.com/ejurgensen) for help with this.

* Experimental support has been added for a softvol plugin, thanks to the work of [Jörg Krause](https://github.com/joerg-krause) -- see [#293](https://github.com/mikebrady/shairport-sync/pull/293).

* A `playback_mode` setting has been added to allow the selection of `stereo` (default) or `mono` playback -- thanks to [faceless2](https://github.com/faceless2).

* The new default service name is now the device's `hostname`, with its first character capitalised (ASCII only).

* Substitutions can now be made in the service name. The following substitutions can be used in the service name: `%h` for the `hostname`, `%H` for the `hostname` with the first letter capitalised, `%v` for the version number and `%V` for the full version string. Maximum length is 50 characters.

* An existing `shairport-sync.service` file will not be overwritten by `sudo make install`. 

* The text strings advertising the capabilities of Shairport Sync over Bonjour/Zeroconf/Avahi have been changed and now more closely match those of an AirPort Express Base Station (First Generation). 

* It is now possible to set the amount of time to wait for the metadata pipe to become ready for writing. The setting is called `pipe_timeout` in the `metadata` section. Default is 5,000 milliseconds.

* Metadata can now be provided via UDP -- thanks to [faceless2](https://github.com/faceless2).

* Statistics output is more machine readable -- thanks to [Jörg Krause](https://github.com/joerg-krause)
* The `shairport-sync.spec` file has been updated for compatibility with building Debian packages using `checkinstall` -- thanks to [vru1](https://github.com/vru1).

Version 2.8.3.11 – Development Version
----
**Bug Fix**

Fixed some problems with the non-blocking write function used to write to the metadata pipe.

**Enhancement**

It is now possible to set the amount of time to wait for the metadata pipe to become ready for writing. The setting is called `pipe_timeout` in the `metadata` section. Default is 5,000 milliseconds.

Version 2.8.3.10 – Development Version
----
**Bug Fix**

* Restored metadata feed lost in 2.8.3.7.

Version 2.8.3.9 – Development Version
----
**Enhancements**

* Substitutions can now be made in the service name, i.e. the name that appears in iTunes, etc. The following substitutions can be used in the service name you specify: `%h` for the hostname, `%H` for the hostname with the first letter capitalised, `%v` for the version number and `%V` for the full version string. Maximum length is 50 characters.

* The new default service name is simply the hostname, with its first character capitalised.

* An existing `shairport-sync.service` file will not be overwritten by `sudo make install`. 

Version 2.8.3.7 – Development Version
----
**Enhancements**

* Shairport Sync now works with AllConnect/Streambels on Android with password protection. (As with all Android clients, you should set the `drift` to something large, like 500 or 1,000, as the timekeeping of these clients isn't as accurate as that of iTunes, etc.)

* The text strings advertising the capabilities of Shairport Sync over Bonjour/Zeroconf/Avahi have been changed and now more closely match those of an AirPort Express Base Station (First Generation). 

Version 2.8.3.6 – Development Version
----
**Bug fix**

An IPv6 link-local connection issue was fixed. A bug in the networking software would not allow an IPv6 link-local connection to be made from a client if Shairport Sync was running on a device with more than one network interface. The solution was to take account of the `config_id` information.

Version 2.8.3.5 – Development Version
----
**Enhancement**

Experimental support for a softvol plugin, thanks to the work of [Jörg Krause](https://github.com/joerg-krause) -- see [#293](https://github.com/mikebrady/shairport-sync/pull/293).

**Bug fix**

Add checks for empty or NULL audio buffers that seem to be causing assertion violations and subsequent abnormal program termination.

Version 2.8.3.4 – Development Version
----

**Bug Fix**

The networking subsystem has been modified to always use the same IP number during a session. Background: the computer Shairport Sync is running on can have many IP numbers active at the same time – various IPv6 numbers and also various IPv4 numbers. During a play session, when Shairport Sync has to create connections back to the source, it would use an automatically-assigned IP number for itself, but that number might not be same as the the number used earlier in the session. From now on, it will always use the same IP number it used when the connection was first established. Thanks to [ejurgensen](https://github.com/ejurgensen) for help with this.


Changed the `mono` setting for a `playback_mode` setting with two possible values: `stereo` (default) and `mono`.

Version 2.8.3.3 – Deleted
----

Version 2.8.3.2 – Deleted
----

Version 2.8.3.1 – Development Version
----
Added a new `mono` setting -- thanks to [faceless2](https://github.com/faceless2). Documentation to follow.

Version 2.8.3 – Stable Version
----
A bug in 2.8.2 caused Avahi to fail at startup under some circumstances with older installations. The problem was that sometimes the `regtype` setting would not be initialised properly.

Version 2.8.2 – Stable Version
----
Version 2.8.2 is derived from development version 2.9.5.7 and has stability improvements, bug fixes and a few special-purpose enhancements.

For full details, please refer to the release notes here, back as far as 2.8.1.

Version 2.9.5.7 – Development Version
----
Version 2.9.5.7 contains general bug fixes and enhancements for some special situations.

**Bug Fixes**

* Getting delay and latency information from the `alsa` subsystem has been improved -- bugs fixed, error codes handled better, arithmetic handling (hopefully) better. 
* If latency information is temporarily unavailable from the `alsa` subsystem, skip trying to synchronise until the next time.
* Some condition variables and a mutex were uninitialised, yikes! Fixed.
* A bug that set the output volume to maximum at the same time as muting the output has been fixed. AFAIK, this was inaudible, but it was scary looking.
* Recover from name collisions in Avahi.
* Detect and handle empty buffers better.

**Enhancements**

* Turn off synchronisation. This is an advanced feature and generally leads to buffer underrun or overrun.
* Set `alsa` buffer size and `alsa` period size. There are advanced features, mainly for debugging. They may be removed.
* Change the Zeroconf/Bonjour `regtype` to enable Shairport Sync to continue to run but to be invisible to AirPlay clients. Special purpose usage only.
* Output total number of packets and the play time of a session when statistics are enabled.

Version 2.9.4 – Development Version
----
Version 2.9.4 corrects some bugs in how Avahi error conditions are handled.

**Bug Fix**
* During operation, if the network disappeared, Avahi would occasionally report an error. This would cause Shairport Sync to attempt to terminate gracefully (which is the wrong thing to do in the circumstances). However, the termination attempt was actually causing an assertion violation crash. These errors are now simply logged.

Version 2.9.3 – Development Version
----
Version 2.9.3 is 2.8.1 with documentation and version changes to indicate that it's in the development branch.

Version 2.8.1 – Stable Version
----
Version 2.8.1 is derived from development version 2.9.2 and has stability improvements and important bug fixes.

For full details, please refer to the release notes here, back as far as 2.9.1.

Version 2.9.2 – Development Version
----
Version 2.9.2 focuses on further bug fixes and stability improvements.
* Enhanced stability: an important bug has been fixed in the handling of missing audio frames – i.e. what happens when a frame of audio is truly missing, after all attempts to fetch it have been unsuccessful. The bug would cause Shairport Sync to do an unnecessary resynchronisation, or, if resync was turned off, to jump out of sync. This is a long-standing bug – thanks to [Jörg Krause](https://github.com/joerg-krause) for identifying it.
* An extra diagnostic has been added which gives the mean, standard deviation and maximum values for inter-packet reception time on the audio port. It may be useful for exploring line quality.

Version 2.9.1 – Development Version
----
Version 2.9.1 focuses on bug fixes and stability improvements.
* Stability improvements are concentrated on what happens when a play sessions ends and is followed immediately by a new session. This happens in iOS 9.2 when you click to the next track or to the previous track. It also happens playing YouTube videos when a Mac's System Audio is routed through AirPlay. Thanks to [Tim Curtis](https://github.com/moodeaudio) for help with these issues.
* A workaround for an apparent flushing issue in TuneBlade has been included. Thanks to [gibman](https://github.com/gibman) for reporting this issue.
* A number of bug fixes have been made to `configure.ac` – thanks to [Jörg Krause](https://github.com/joerg-krause).

Version 2.8 – Stable Version
----
Version 2.8 is derived from version 2.7.10 with slight documentation updates. Here is a summary of changes between the last stable version – 2.6 – and this version. For full details, refer to the release notes here, back as far as 2.7.

**New Feature**
* For hardware mixers with a restricted range (including many cheaper USB DACS), the general `volume_range_db` can be used to specify a wider range than the hardware provides – the extra range is provided by software.

**Enhancements**
* The `man` manual and the html version of it are automagically rebuilt if `xml2man` and friends are available.
* Volume-setting metadata is now sent even when the volume level is to be ignored by Shairport Sync itself. 
* Shairport Sync waits a little longer before asking for missing packets to be resent. Sometimes packets are just arriving slightly out of order and don't need to be asked for again.
* The build scripts have been modified to be a little more compatible with standard practice.
* A Continuous Integration (CI) system – Travis CI – is now used to do some limited build checking (thanks guys!).
* Support added for compiling on Cygwin.
* Added `rtptime` tags to metadata and picture metadata.
* Replaced and improved the dither algorithm used with the software volume control. The new dither code gives a two bit peak-to-peak dither based on a Triangular Probability Distribution Function (TPDF).
* Disabled picture sending if pictures haven’t been asked for.

**Bug fixes**
* Fixed a bug that prevented Shairport Sync from correctly setting the hardware mixer volume if it had been altered externally. Thanks to [Tim Curtis](https://github.com/moodeaudio) for help with these issues.
* Modified the shutdown behaviour so that a shutdown followed immediately by a play request is handled better. This was causing iOS 9.2 sometimes to drop the Airplay link between tunes.
* Fixed a data-alignment bug that would cause a crash in certain circumstances on ARM processors with metadata enabled.
* Corrected the names for a few settings tags.
* Fixed some typos and misspellings.
* Miscellaneous small bug fixes.

Version 2.7.10 -- Development Version
----
**New Feature**
* If the `ignore_volume_control` setting was `yes`, Shairport Sync really did ignore volume control settings and did not send any volume metadata (i.e. `pvol` coded metadata). Now, while continuing to ignore volume control settings, it sends a `pvol` token where the first number is the AirPlay volume, as before, but the remaining three parameters are set to zero.

Version 2.7.9 -- Development Version
----
**Bug Fix**
* Oops – brown-bag update. Fixed a crashing bug introduced in the last release, caused by not checking for a hardware mixer before trying to access it, duh.

Version 2.7.8 -- Development Version
----
**Bug Fix**
* Fixed an issue whereby Shairport Sync did not reset the hardware mixer volume level before resuming playing. The issue was caused by not releasing and later reaquiring the mixer when pausing and resuming. Thanks to [Tim Curtis](https://github.com/moodeaudio) for reporting the issue.

Version 2.7.7 -- Development Version
----
**Enhancements**
* Add note about the Arch Linux Community repository package `shairport-sync`. Thanks to [Anatol Pomozov](https://github.com/anatol).
* Shairport Sync doesn't ask for packets to be resent quite so quickly -- it waits about half a second now before asking for missing packets to be resent.

**Bug Fixes**
* Improved Shairport Sync's behaviour when it's asked to stop a play session and immediately start another. The signalling system used to stop threads was sometimes stopping threads belonging to the new session. This affected iOS 9.2 users going to the next track -- sometimes the player would become unavailable for an instant and disconnect the session. Th problem still happens occasionally.
* Removed code favouring the use of "public" IPv6 addresses as source addresses when connecting to a distant IPv6 port – Neither OpenWrt nor FreeBSD can use it at present. Also, it's not clear if any problems are being caused by not favouring public IPv6 addresses.

Version 2.7.6 -- Development Version
----
**Bug Fixes**
* Look for the correct tag name for desired `ao` buffer length: `audio_backend_buffer_desired_length` rather than `audio_backend_buffer_desired_length_software`.
* Fix a few FreeBSD compilation bugs.
* Fix a few documentation issues and typos. Thanks to [Chris Boot](https://github.com/bootc).

**Enhancements**
* Add note about installing to Mac OS X. Thanks to [Serg Podtynnyi](https://github.com/shtirlic).
* Add automatic rebuild of manpage and html documentation when `xmltoman` and friends are available. Thanks to [Chris Boot](https://github.com/bootc).
* Favour the use of "public" IPv6 addresses as source addresses when connecting to a distant IPv6 port.

Version 2.7.5 -- Development Version
----
**New Features**
* Ubuntu PPA files now available at https://launchpad.net/~dantheperson.

**Enhancements**
* Broaden the use of the value `$PREFIX` instead of the path `/usr/local/bin` during configuration. Thanks to [dantheperson](https://github.com/dantheperson).

Version 2.7.4 -- Development Version
----
**Enhancements**
* Use the correct method for finding the `systemd` unit path, as recommended by Debian maintainers and
http://www.freedesktop.org/software/systemd/man/daemon.html#Installing%20Systemd%20Service%20Files. Thanks to [dantheperson](https://github.com/dantheperson).
* Rather than hardwire the path `/usr/local/bin` as the path to the shairport-sync executable, the value of `$PREFIX` is now used during configuration. Thanks to [Nick Steel](https://github.com/kingosticks).
* Add some extra diagnostic messages if the hardware buffer in the DAC is smaller than desired.
* If metadata has been enabled, but if picture sending has not been requested and the source sends pictures anyway, omit them from the metadata feed. Thanks to [Jörg Krause](https://github.com/joerg-krause).

**Bug Fixes**
* Fixed a data alignment issue in the handling of metadata on some processors. Thanks to [Jörg Krause](https://github.com/joerg-krause).
* Removed an `assert` which would terminate the program if a malformed packet of data was received.
* Look for the correct tag name for desired alsa buffer length: `audio_backend_buffer_desired_length` rather than `audio_backend_buffer_desired_length_software`.

Version 2.7.3 -- Development Version
----
**Bug Fix**
* The dither code was broken in Shairport Sync and was less than ideal anyway. It has been fixed and improved. Dither is added whenever you use the software volume control at less than full volume. See http://www.ece.rochester.edu/courses/ECE472/resources/Papers/Lipshitz_1992.pdf for a very influential paper by Lipshitz, Wannamaker and Vanderkooy, 1992. The dither code in Shairport Sync was inherited from Shairport and does not conform to the recommendations in the paper -- specifically the implementation would give one bit of dither where the paper recommends two bits peak-to-peak. The other thing is that the inherited dither code was actually broken in Shairport Sync. So, the new dither code gives a two bit peak-to-peak dither based on a Triangular Probability Distribution Function (TPDF). It sounds like a very low-level white noise, unmodulated by the audio material. It would be nice if it was even lower, but it's better than listening to the artifacts present when dithering is disabled.

Version 2.7.2 -- Development Version
----
**Bug Fix**
* Fix a bug that suppressed output of the `rtptime` associated with metadata and with picture information coming from the audio source and passed on via the metadata pipe.

**Other Changes**
* Added some more information to the log whenever problems are detected with the proposed alsa device. 

Version 2.7.1 -- Development Version
----
**Bug Fix**
* The new volume-extension code was not correctly setting the volume after a pause / resume. Fixed.

Version 2.7 -- Development Version
----
**New Features**
* Extend the volume range for some DACs. Background: some of the cheaper DACS have a very small volume range (that is, the ratio of the highest to the lowest volume, expressed in decibels, is very small). In some really cheap DACs it's only around 30 dB. That means that the difference between the lowest and highest volume settings isn't large enough. With the new feature, if you set the `general` `volume_range_db` to more than the hardware mixer's range, Shairport Sync will combine the hardware mixer's range with a software attenuator to give the desired range. For example, suppose you want a volume range of 70 dB and the hardware mixer offers only 30 dB, then Shairport Sync will make up the other 40 dB with a software attenuator. One drawback is that, when the volume is being changed, there may be a slight delay (0.15 seconds by default) as the audio, whose volume may have been adjusted in software, propagates through the system. Another slight possible drawback is a slightly heavier load on the processor.
* Check for underflow a little better when buffer aliasing occurs on very bad connections...
* Add extra debug messages to the alsa back end to diagnose strange DACs.
* Add configuration file for the `libao` back end -- to change the buffer size and the latency offset, same as for stdout.
* Add `shairport-sync.exe` to `.gitignore`.
* Add a check to support compilation on a CYGWIN platform.
* Add `rtptime` tags to metadata and picture information and add two new metadata items to precede and follow the transmission of a picture. Background: it seems that metadata and picture information for the same item, e.g. a track, are normally tagged with a timestamp called the `rtptime`; if they refer to the same item, they will have the same `rtptime` tags. The update here is to add the `rtptime` value, if available, as data to the `mdst` and `mden` metadata items, which are  sent before ("MetaData STart") and after ("MetaData ENd") a metadata sequence.
In addition, similar tags -- `pcst` ("PiCture STart") and `pcen` ("PiCture ENd") are now sent before and after a picture with the `rtptime` value, if available, sent as data.
By the way, the progress metadata (`prgr` for "PRoGRess"), which is sent just when a track starts, contains the same `rtptime` as its middle element.


Version 2.6 -- Stable Version
----
This is basically version 2.4.2 with two small fixes. It's been bumped to 2.6 because (1) the new features added between 2.4.1 and 2.4.2 deserve more than just a bug-fix increment and (2) the development versions (2.5.x) should have lower numbers than the release versions, so that releases are always seen as upgrades. For example: 2.5.0.9 --> 2.6 looks like an upgrade, whereas 2.5.0.9 --> 2.4.2 looks like a downgrade.

**Fixes**
* For `systemd` users, the `shairport-sync.service` file is updated to point to the correct location of the shairport-sync application.
* For Fedora users, the `shairport-sync.spec` file is updated to refer to 2.6.


Version 2.4.2
----
This release has important enhancements, bug fixes and documentation updates. It also appears to bring compatibility with Synology NAS devices.


**New Features**
* Source-specified Latencies. Shairport Sync now uses the latencies specified by the audio source. Background: the AirPlay protocol used by Shairport Sync allows the audio source to specify the exact delay or latency that should be applied to the audio stream. Until now, Shairport Sync ignored this information and used fixed preset latencies that were selected on the basis of the "User-Agent" setting. Using source-specified latencies means that Shairport Sync is able adapt automatically to different sources.
Using source-specified latencies is now automatic unless non-standard static latencies have been specified in the configuration file or command line. Using non-standard latencies is usually done to compensate for delays in the back end of the system. For example, if the audio amplifier being driven by Shairport Sync has an inherent delay of its own -- as happens with many home theatre and surround sound systems -- then some users have reduced the latencies used by Shairport Sync to compensate. This usage is discouraged -- the `audio_backend_latency_offset` in the appropriate backend stanza (e.g. in the "alsa" stanza) should be used for this. Static latency settings are now deprecated, and will be removed in a future version of Shairport Sync.
* Set Volume Range. This is a new setting that allows you to use just a portion of the full range of attenuation offered by a mixer. For example, if a mixer has a minimum volume of -80 dB and a maximum of +20 dB, you might wish to use only 60 dB of the 100 dB available.  This might be because the sound becomes inaudible at the lowest setting and unbearably loud at the highest setting. It is for this reason that many domestic HiFi systems have a volume control range of only 60 to 80 dB.
 Another possible reason to use this setting might be because the range specified by the mixer does not match the actual capabilities of the device. For example, the Raspberry Pi's DAC that feeds the built-in audio jack claims a range of 106 dB but has a useful range of only about 35dB. The new `volume_range_db` setting in the `general` stanza allows you to specify the maximum range from highest to lowest. The range suggested for the Raspberry Pi's built-in audio DAC, which feeds the headphone jack, is 35. Using it in this case gives the volume control a much more useful range of settings.

**Bug fixes**
* Sometimes, especially when using Shairport Sync as a system output, it would not play the audio stream. This was caused by an improperly initialised variable. Fixed. Synology NAS devices now seem to be working with Shairport Sync.
* Fix in the `shairport.c`: the USE_CUSTOM_LOCAL_STATE_DIR macro was still being used when it should have been USE_CUSTOM_PID_DIR.
* Fix a crashing bug -- if metadata was enabled but a pipename was not supplied, boom.

**Other Changes**
* Initial timing accuracy improved. The estimate of when to play the starting frame of the audio sequence has improved significantly. This leads to fewer corrections being needed at the start.
* Volume ratios expressed in decibels are now consistently denominated in voltage decibels rather than power decibels. The rationale is that the levels refer to voltage levels, and power is proportional to the square of voltage.
Thus a ratio of levels of 65535 to 1 is 96.3 dB rather than the 48.15 dB used before.
* The latency figure returned to the source as part of the response to an rtsp request packet is 11,025, which may (?) be meant to indicate the minimum latency the device is capable of. 
* An experimental handler for a GET_PARAMETER rtsp request has been added. It does nothing except log the occurrence.
* The RTSP request dispatcher now logs an event whenever an unrecognised rtsp has been made.


Version 2.4.1
----
This release has three small bug fixes and some small documentation updates.

**Bug Fixes**

Changes from the previous stable version -- 2.4 -- are summarised here:
 * The USE_CUSTOM_LOCAL_STATE_DIR macro was still being used when it should have been USE_CUSTOM_PID_DIR. This could affect users using a custom location for the PID directory.
 * A compiler error has been fixed that occurred if metadata was enabled and tinysvcmdns was included.
 * A crash has been fixed that occurred if metadata was enabled and a metadata pipe name was not specified.
(Thanks to the contributors who reported bugs.)
 
**Small Changes**
 * If a mixer being used to control volume does not have a control denominated in dB, a warning is logged and the mixer is not used.
 * Slight revisions have been made to the configuration file `configure.ac` to make compilation on FreeBSD a little easier.

Version 2.4
----
**Stable release**

This stable release is the culmination of the 2.3.X sequence of development releases.

**Change Summary**

Changes from the previous stable version -- 2.2.5 -- are summarised here:
 * Settings are now read from a configuration file. Command-line settings are supported but discouraged.
 * Metadata is now supported -- it can be delivered to a unix pipe for processing by a helper application. See https://github.com/mikebrady/shairport-sync-metadata-reader for a sample metadata reader.
 * Raw PCM audio can be delivered to standard output ("stdout") or to a unix pipe. The internal architecture has changed considerably to support this.
 * Support for compilation on OpenWrt back to Attitude Adjustment.
 * Can play unencrypted audio streams -- complatible with, e.g. Whaale.
 * Uses the libconfig library.
 * Runs on a wider range of platforms, including Arch Linux and Fedora.
 * Bug fixes.

Please note that building instructions have changed slightly from the previous version.
Also, the `-t hardware/software` option has been deprecated in the alsa back end. 

Version 2.3.13
----
**Note**
* We're getting ready to release the development branch as the new, stable, master branch at 2.4. If you're packaging Shairport Sync, you might prefer to wait a short while as we add a little polish before the release.

**Changes**
* Harmonise version numbers on the release and on the `shairport.spec` file used in Fedora.

Version 2.3.12
----
**Note**
* We're getting ready to release the development branch as the new, stable, master branch at 2.4. If you're packaging Shairport Sync, you might prefer to wait a short while as we add a little polish before the release.

**Changes**
* `update-rc.d` has been removed from the installation script for System V because it causes problems for package makers. It's now noted in the user installation instructions.
* The `alsa` group `mixer_type` setting is deprecated and you should stop using it. Its functionality has been subsumed into `mixer_name` – when you specify a `mixer_name` it automatically chooses the `hardware` mixer type.


**Enhancements**
* Larger range of interpolation. Shairport Sync was previously constrained not to make interpolations ("corrections") of more than about 1 per 1000 frames. This constraint has been relaxed, and it is now able to make corrections of up to 1 in 352 frames. This might result in a faster and undesirably sudden correction early during a play session, so a number of further changes have been made. The full set of these changes is as follows:
  * No corrections happen for the first five seconds.
  * Corrections of up to about 1 in 1000 for the next 25 seconds.
  * Corrections of up to 1 in 352 thereafter.

**Documentation Update**
* Nearly there with updates concerning the configuration file.

Version 2.3.11
----
Documentation Update
* Beginning to update the `man` document to include information about the configuration file. It's pretty sparse, but it's a start.

Version 2.3.10
----
Bug fix
* The "pipe" backend used output code that would block if the pipe didn't have a reader. This has been replaced by non-blocking code. Here are some implications:
  * When the pipe is created, Shairport Sync will not block if a reader isn't present.
  * If the pipe doesn't have a reader when Shairport Sync wants to output to it, the output will be discarded.
  * If a reader disappears while writing is occurring, the write will time out after five seconds.
  * Shairport Sync will only close the pipe on termination.

Version 2.3.9
----
* Bug fix
 * Specifying the configuration file using a *relative* file path now works properly.
 * The debug verbosity requested with `-v`, `-vv`, etc. is now honoured before the configuration file is read. It is read and honoured from when the command line arguments are scanned the first time to get a possible configuration file path.

Version 2.3.8
----
* Annoying changes you must make
 * You probably need to change your `./configure` arguments. The flag `with-initscript` has changed to `with-systemv`. It was previously enabled by default; now you must enable it explicitly.

* Changes
 * Added limited support for installing into `systemd` and Fedora systems. For `systemd` support, use the configuration flag `--with-systemd` in place of `--with-systemv`. The installation does not do everything needed, such as defining special users and groups.
 * Renamed `with-initscript` configuration flag to `with-systemv` to describe its role more accurately.
 * A System V startup script is no longer installed by default; if you want it, ask for it with the `--with-systemv` configuration flag.
 * Added limited support for FreeBSD. You must specify `LDFLAGS='-I/usr/local/lib'` and `CPPFLAGS='-L/usr/local/include'` before running `./configure --with-foo etc.`
 * Removed the `-configfile` annotation from the version string because it's no longer optional; it's always there.
 * Removed the `dummy`, `pipe` and `stdout` backends from the standard build – they are now optional and are no longer automatically included in the build.

* Bug fixes
 * Allow more stack space to prevent a segfault in certain configurations (thanks to https://github.com/joerg-krause).
 * Add missing header files(thanks to https://github.com/joerg-krause).
 * Removed some (hopefully) mostly silent bugs from the configure.ac file.
 
Version 2.3.7
----
* Changes
  * Removed the two different buffer lengths for the alsa back end that made a brief appearance in 2.3.5.
* Enhancements
 * Command line arguments are now given precedence over config file settings. This conforms to standard unix practice.
 * A `–without-pkg-config` configuration argument now allows for build systems, e.g. for older OpenWrt builds, that haven't fully implemented it. There is still some unhappiness in arch linux builds.
* More
 *  Quite a bit of extra diagnostic code was written to investigate clock drift, DAC timings and so on. It was useful but has been commented out. If might be useful in the future.

Version 2.3.5
----
* Changes
 * The metadata item 'sndr' is no longer sent in metadata. It's been replaced by 'snam' and 'snua' -- see below.
* Enhancements
 * When a play session is initiated by a source, it attempts to reserve the player by sending an "ANNOUNCE" packet. Typically, a source device name and/or a source "user agent" is sent as part of the packet. The "user agent" is usually the name of the sending application along with some more information. If metadata is enabled, the source name, if provided, is emitted as a metadata item with the type `ssnc` and code `snam` and similarly the user agent, if provided, is sent with the type `ssnc` and code `snua`.
 * Two default buffer lengths for ALSA -- default 6615 frames if a software volume control is used, to minimise the response time to pause and volume control changes; default 22050 frames if a hardware volume control is used, to give more resilience to timing problems, sudden processor loading, etc. This is especially useful if you are processing metadata and artwork on the same machine.
 * Extra metadata: when a play session starts, the "Active-Remote" and "DACP-ID" fields -- information that can be used to identify the source -- are provided as metadata, with the type `ssnc` and the codes `acre` and `daid` respectively. The IDs are provided as strings.
 * Unencrypted audio data. The iOS player "Whaale" attempts to send unencrypted audio, presumably to save processing effort; if unsuccessful, it will send encrypted audio as normal. Shairport Sync now recognises and handles unencrypted audio data. (Apparently it always advertised that it could process unencrypted audio!)
 * Handle retransmitted audio in the control channel. When a packet of audio is missed, Shairport Sync will ask for it to be retransmitted. Normally the retransmitted audio comes back the audio channel, but "Whaale" sends it back in the control channel. (I think this is a bug in "Whaale".) Shairport Sync will now correctly handle retransmitted audio packets coming back in the control channel.
* Bugfixes
 * Generate properly-formed `<item>..</item>` items of information.

Version 2.3.4
----
* Enhancement
 * When a play session starts, Shairport Sync opens three UDP ports to communicate with the source. Until now, those ports could be any high numbered port. Now, they are located within a range of 100 port locations starting at port 6001. The starting port and the port range are settable by two new general settings in `/etc/shairport-sync.conf` -- `udp_port_base` (default 6001) and `udp_port_range` (default 100). To retain the previous behaviour, set the `udp_port_base` to `0`.
* Bugfixes
 * Fix an out-of-stack-space error that can occur in certain cases (thanks to https://github.com/joerg-krause).
 * Fix a couple of compiler warnings (thanks to https://github.com/joerg-krause).
 * Tidy up a couple of debug messages that were emitting misleading information.
 
Version 2.3.3.2
----
* Bugfix -- fixed an error in the sample configuration file.

Version 2.3.3.1
----
* Enhancement
 * Metadata format has changed slightly -- the format of each item is now `<item><type>..</type><code>..</code><length>..</length><data..>..</data></item>`, where the `<data..>..</data>` part is present if the length is non-zero. The change is that everything is now enclosed in an `<item>..</item>` pair.
 
Version 2.3.2 and 2.3.3
----
These releases were faulty and have been deleted.

Version 2.3.1
-----
Some big changes "under the hood" have been made, leading to limited support for unsynchronised output to `stdout` or to a named pipe and continuation of defacto support for unsynchronised PulseAudio. Also, support for a configuration file in preference to command line options, an option to ignore volume control and other improvements are provided.

In this release, Shairport Sync gains the ability to read settings from `/etc/shairport-sync.conf`.
This gives more flexibility in adding features gives better compatibility across different versions of Linux.
Existing command-line options continue to work, but some will be deprecated and may disappear in a future version of Shairport Sync. New settings will only be available via the configuration file.

Note that, for the present, settings in the configuration will have priority over command line options for Shairport Sync itself, in contravention of the normal unix convention. Audio back end command line options, i.e. those after the `--`, have priority over configuration file settings for the audio backends.

In moving to the the use of a configuration file, some "housekeeping" is being done -- some logical corrections and other small changes are being made to option names and modes of operations, so the settings in the configuration file do not exactly match command line options.

When `make install` is executed, a sample configuration is installed or updated at `/etc/shairport-sync.conf.sample`. The same file is also installed as `/etc/shairport-sync.conf` if that file doesn't already exist. To prevent the configuration files being installed, use the configuration option `--without-configfiles`.

* Pesky Change You Must Do Something About

If you are using metadata, please note that the option has changed somewhat. The option `-M` has a new long name equivalent: `--metadata-pipename` and the argument you provide must now be the full name of the metadata pipe, e.g. `-M /tmp/shairport-sync-metadata`.

* Enhancements
 * Shairport Sync now reads settings from the configuration file `/etc/shairport-sync.conf`. This has settings for most command-line options and it's where any new settings will go. A default configuration file will be installed if one doesn't exist, and a sample file configuration file is always installed or updated. Details of settings are provided in the sample file. Shairport Sync relies on the `libconfig` library to read configuration files. For the present, you can disable the new feature (and save the space taken up by `libconfig`) by using the configure option `--without-configfile-support`.
 * New command-line option `-c <file>` or `--configfile=<file>` allows you to specify a configuration file other than `/etc/shairport-sync.conf`.
 * Session Timeout and Allow Session Interruption can now be set independently. This is really some "housekeeping" as referred to above -- it's a kind of a bug fix, where the bug in question is an inappropriate connection of the setting of two parameters. To explain: (1) By default, when a source such as iTunes starts playing to the Shairport Sync device, any other source attempting to start a play session receives a "busy" signal. If a source disappears without warning, Shairport Sync will wait for 120 seconds before dropping the session and allowing another source to start a play session. (2) The command-line option `-t` or `--timeout` allows you to set the wait time before dropping the session. If you set this parameter to `0`, Shairport Sync will not send a "busy" signal, thus allowing another source to interrupt an existing one. (3) The problem is that if you set the parameter to `0`, a session will never be dropped if the source disappears without warning.
 The (obvious) fix for this is to separate the setting of the two parameters, and this is now done in the configuration file `/etc/shairport-sync.conf` -- please see the settings `allow_session_interruption` and `session_timeout`. The behaviour of the `-t` and `--timeout` command-line options is unchanged but deprecated.
 * New Option -- "Ignore Volume Control" ('ignore_volume_control'). If you set this to "yes", the output from Shairport Sync is always set at 100%. This is useful when you want to set the volume locally. Available via the settings file only.
 * Statistics option correctly reports when no frames are received in a sampling interval and when output is not being synchronised.
 * A new, supported audio back end called `stdout` provides raw 16-bit 44.1kHz stereo PCM output. To activate, set  `output_backend = "stdout"` in the general section of the configuration file. Output is provided synchronously with the source feed. No stuffing or stripping is done. If you are feeding it to an output device that runs slower or faster, you'll eventually get buffer overflow or underflow in that device. To include support for this back end, use the configuration option `--with-stdout`.
 * Support for the `pipe` back end has been enhanced to provide raw 16-bit 44.1kHz stereo PCM output to a named pipe. To activate, set `output_backend = "pipe"` in the general section of the configuration and give the fully-specified pathname to the pipe in the pipe section of the configuration file -- see `etc/shairport-sync.conf.sample` for an example. No stuffing or stripping is done. If you are feeding it to an output device that runs slower or faster, you'll eventually get buffer overflow or underflow in that device.  To include support for this back end, use the configuration option `--with-pipe`.
 * Support for the `dummy` audio backend device continues. To activate, set  `output_backend = "dummy"` in  in the general section of the configuration. To include support for this back end, use the configuration option `--with-dummy`.
 * Limited support for the PulseAudio audio backend continues. To activate, set  `output_backend = "pulse"` in  in the general section of the configuration. You must still enter its settings via the command line, after the `--` as before. Note that no stuffing or stripping is done: if the PulseAudio sink runs slower or faster, you'll eventually get buffer overflow or underflow.
 * New backend-specific settings are provided for setting the size of the backend's buffer and for adding or removing a fixed offset to the overall latency. The `audio_backend_buffer_desired_length` default is 6615 frames, or 0.15 seconds. On some slower machines, particularly with metadata processing going on, the DAC buffer can underflow on this setting, so it might be worth making the buffer larger. A problem on software mixers only is that changes to volume control settings have to propagate through the buffer to be heard, so the larger the buffer, the longer the response time. If you're using an alsa back end and are using a hardware mixers, this isn't a problem. The `audio_backend_latency_offset` allows you emit frames to the audio back end some time before or after the synchronised time. This would be useful, for example, if you are outputting to a device that takes 20 ms to process audio; yoou would specify a `audio_backend_latency_offset = -882`, where 882 is the number of frames in 20 ms, to compensate for the device delay.

Version 2.3
-----
* Enhancements
 * Adding the System V startup script (the "initscript") is now a configuration option. The default is to include it, so if you want to omit the installation of the initscript, add the configuration option `--without-initscript`.
 * Metadata support is now a compile-time option: `--with-metadata`.
 * A metadata feed has been added. Use the option `-M <pipe-directory>`, e.g. `-M /tmp`. Shairport Sync will provide metadata in a pipe called `<pipe-directory>/shairport-sync-metadata`. (This is changed in 2.3.1.) There's a sample metadata reader at https://github.com/mikebrady/shairport-sync-metadata-reader. The format of the metadata is a mixture of XML-style tags, 4-character codes and base64 data. Please look at `rtsp.c` and `player.c` for examples. Please note that the format of the metadata may change.
Beware: there appears to be a serious bug in iTunes before 12.1.2, such that it may stall for a long period when sending large (more than a few hundred kilobytes) coverart images.

* Bugfix
 * Fix a bug when compiling for Arch Linux on Raspberry Pi 2 (thanks to https://github.com/joaodriessen).
 * Fix a bug  whereby if the ANNOUNCE and/or SETUP method fails, the play_lock mutex is never unlocked, thus blocking other clients from connecting. This can affect all types of users, but particularly Pulseaudio users. (Thanks to https://github.com/jclehner.)
 * Modify the init script to start after all services are ready. Add in a commented-out sleep command if users find it necessary (thanks to https://github.com/BNoiZe).
 * Two memory leaks fixed (thanks to https://github.com/pdgendt).
 * An error handling time specifications for flushes was causing an audible glitch when pausing and resuming some tracks. This has been fixed (thanks to https://github.com/Hamster128).

Version 2.2.5
-----
* Bugfixes
 * Fix a segfault error that can occur in certain cases (thanks again to https://github.com/joerg-krause).
 * Include header files in common.c (thanks again to https://github.com/joerg-krause).

Version 2.2.4
-----
* Bugfixes
 * Fix an out-of-stack-space error that can occur in certain cases (thanks to https://github.com/joerg-krause).
 * Fix a couple of compiler warnings (thanks to https://github.com/joerg-krause).

Version 2.2.3
-----
* NOTE: all the metadata stuff has been moved to the "development" branch. This will become the stable branch henceforward, with just bug fixes or minor enhancements. Apologies for the inconvenience.
* Bugfixes
 * Fix a bug when compiling for Arch Linux on Raspberry Pi 2 (thanks to https://github.com/joaodriessen).
 * Fix a compiler warning (thanks to https://github.com/sdigit).

Version 2.2.2
-----
* Enhancement
 * An extra latency setting for forked-daapd sources -- 99,400 frames, settable via a new option `--forkedDaapdLatency`.

Version 2.2.1
-----
* Bugfixes:
 * If certain kinds of malformed RTSP packets were received, Shairport Sync would stop streaming. Now, it generally ignores faulty RTSP packets.
 * The `with-pulseaudio` compile option wasn't including a required library. This is fixed. Note that the PulseAudio back end doesn't work properly and is just included in the application because it was there in the original shairport. Play with it for experimentation only.
 * Fix typo in init.d script: "Headphones" -> "Headphone".
* Extra documentation
 * A brief note on how to compile `libsoxr` from source is included for the Raspberry Pi.

Version 2.2
-----
* Enhancements:
 * New password option: `--password=SECRET`
 * New tolerance option: `--tolerance=FRAMES`. Use this option to specify the largest synchronisation error to allow before making corrections. The default is 88 frames, i.e. 2 milliseconds. The default tolerance is fine for streaming over wired ethernet; however, if some of the stream's path is via WiFi, or if the source is a third-party product, it may lead to much overcorrection -- i.e. the difference between "corrections" and "net correction" in the `--statistics` option. Increasing the tolerance may reduce the amount of overcorrection.

Version 2.1.15
-----
* Changes to latency calculations:
 * The default latency is now 88,200 frames, exactly 2 seconds. It was 99,400 frames. As before, the `-L` option allows you to set the default latency.
 * The `-L` option is no longer deprecated.
 * The `-L` option no longer overrides the `-A` or `-i` options.
 * The default latency for iTunes is now 99,400 frames for iTunes 10 or later and 88,200 for earlier versions.
 * The `-i` or `--iTunesLatency` option only applies to iTunes 10 or later sources.

Version 2.1.14
-----
* Documentation update: add information about the `-m` audio backend option.
The `-m` audio backend option allows you to specify the hardware mixer you are using. Not previously documented.
Functionality of shairport-sync is unchanged.

Version 2.1.13
-----
* Compilation change: Begin to use PKG_CHECK_MODULES (in configure.ac) to statically link some of the libraries used by shairport-sync. It is intended to make it easier to build in the buildroot system. While sufficient for that purpose, note that PKG_CHECK_MODULES is not used for checking all the libraries yet.
Functionality of shairport-sync is unchanged.

Version 2.1.12
-----
* Enhancement: `--statistics`
 Statistics are periodically written to the console (or the logfile) if this command-line option is included. They are no longer produced in verbose (`-v`) mode.
* Bugfixes for `tinysvcmdns`
  * A bug that prevented the device's IP number(s) and port numbers being advertised when using `tinysvcmdns` has been fixed. (Cause: name needed to have  a `.local` suffix.)
  * Bugs causing the shairport service to semi-randomly disappear and reappear seem to be fixed. (Possible cause: incorrect timing settings when using `tinysvcmdns`.)

Version 2.1.11
-----
* Enhancement
  * A man page is now installed -- do `man shairport-sync` or see it here: http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/2.1/man/shairport-sync.html.

Version 2.1.10
-----
* Bugfix
  * A bug that caused the `-t` timeout value to be incorrectly assigned has been fixed. (Cause: `config.timeout` defined as `int64_t` instead on `int`.)

Version 2.1.9
-----
* Bugfixes
  * A bug that sometimes caused the initial volume setting to be ignored has been fixed. (Cause: setting volume before opening device.)
  * a bug that caused shairport-sync to become unresponsive or unavailable has been fixed. (Cause: draining rather than flushing the alsa device before stopping.)

Version 2.1.8:
-----
* Enhancements
	* (This feature is intended to be useful to integrators.) Shairport Sync now the ability to immediately disconnect and reconnect to the sound output device while continuing to stream audio data from its client.
Send a `SIGUSR2` to the shairport-sync process to disconnect or send it a `SIGHUP` to reconnect. If shairport-sync has been started as a daemon using `shairport-sync -d`, then executing `shairport-sync -D` or `--disconnectFromOutput` will request the daemon to disconnect, and executing `shairport-sync -R` or `--reconnectToOutput` will request it to reconnect.
With this feature, you can allow Shairport Sync always to advertise and provide the streaming service, but still be able to disconnect it locally to enable other audio services to access the output device.
	
* Annoying things you should know about if you're updating from a previous version:
	* Options `--with-openssl`, `--with-polarssl` have been replaced with a new option `--with-ssl=<option>` where `<option>` is either `openssl` or `polarssl`.
	* Option `--with-localstatedir` has been replaced with `--with-piddir`. This compilation option allows you to specify the directory in which the PID file will be written. The directory must exist and be writable. Supersedes the `--with-localstatedir` and describes the intended functionality a little more accurately.

* Bugfixes
	* A small (?) bug in the flush logic has been corrected. Not causing any known problem.

Version 2.1.5:
-----
* Enhancements
	* Adds a `--with-localstatedir` configuration option. When Shairport Sync is running as a daemon, it writes its Process ID (PID) to a file. The file must be stored in part of the file system that is writable. Most build systems choose an appropriate 'local state directory' for this automatically, but some -- notably `buildroot`  -- don't always get it right for an embedded system. This compilation option allows you to specify the local state directory. Supersedes 2.1.4, which tried to do the same thing.

Version 2.1.4:
-----
* Faulty -- withdrawn. 2.1.5 does it properly.


Version 2.1.3:
-----
* Stability Improvements
	* Fixed a bug which prevented Shairport Sync starting on an IPv4-only system.

Version 2.1.2:
-----
* Stability Improvements
	* Improved buffering and flushing control, especially important on poor networks.


Version 2.1.1:
-----
* Enhancements
	* Add new -t or --timeout option. Normally, when playing audio from a source, the Shairport Sync device is unavailable to other devices requesting to play through it -- it returns a "busy" signal to those devices. If the audio source disappears without warning, the play session automatically terminates after a timeout period (default 120 seconds) and the device goes from being "busy" to being available for new play requests again. This option allows you to set that timeout period in seconds.
In addition, setting the timeout period to 0 means that play requests -- say from other devices on the network -- can interrupt and terminate the current session at any time. In other words, the "busy" feature of the device -- refusing connections from other players while playing from an existing source -- is turned off. 
	* Allow -B and -E commands to have arguments, e.g. -B '/usr/bin/logger "Starting to play"' is now legitimate.

* Annoying things you should know about if you're updating from 2.1:
	* Build now depends on the library libpopt -- see "Building and Installing" below.

* Stability Improvements
	* Fixed a bug which would silence output after a few hours.
	* Tightened up management of packet buffers.
	* Improved estimate of lead-in silence to achieve initial synchronisation.

Version 2.1:
-----

* New features:

	* Support for libsoxr, the SoX Resampler library -- see http://sourceforge.net/projects/soxr/. Briefly, Shairport Sync keeps in step with the audio source by deleting or inserting frames of audio into the stream as needed. This "interpolation" is normally inaudible, but it can be heard in some circumstances. Libsoxr allows this interpolation to be done much more smoothly and subtly. You can optionally include libsoxr support when building Shairport Sync. The big problem with libsoxr is that it is very compute intensive -- specifically floating point compute intensive -- and many embedded devices aren't powerful enough. Another issue is libsoxr is not yet in all linux distributions, so you might have to build it yourself. Available via the -S option.
	* Support for running (and optionally waiting for the completion of) programs before and after playing.  See the -B, -E and -w options.
	* A new option to vary or turn off the resync threshold. See the -r option.
	* Version and build options. See the -V option.
	* Renamed program and init script. This is not exactly a big deal, but the name of the application itself and the default init script file have been renamed from "shairport" to "shairport-sync" to avoid confusion with other versions of shairport.
	* PolarSSL can be used in place of OpenSSL and friends.
	
* Other stuff
	* Tinysvcmdns works as an alternative to, say, Avahi, but is now [really] dropped if you don't select it. Saves about 100k.
	* Lots of bug fixes.

* Annoying things you should know about if you're updating from 2.0:
	* Compile options have changed -- see the Building and Installing section below.
	* The name of the program itself has changed from shairport to shairport-sync. You should remove the old version -- you can use `$which shairport` to locate it.
	* The name of the init script file has changed from shairport to shairport-sync. You should remove the old one.

Version 2.0
----

* New features:
 * Audio synchronisation that works. The audio played by a Shairport Sync-powered device will stay in sync with the source. This allows you to synchronise Shairport Sync devices reliably with other devices playing the same source. For example, synchronised multi-room audio is possible without difficulty.
 * True mute and instant response to mute and volume control changes -- this requires hardware mixer support, available on most audio devices. Without hardware mixer support, response is also faster than before -- around 0.15 seconds.
 * Smoother volume control at the top and bottom of the range.
 * Another source can not interrupt an existing source playing via Shairport Sync. it will be given a 'busy' signal.
