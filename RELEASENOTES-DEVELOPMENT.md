Version 5.0-post-dev-13-g5893eabf
==
* The MQTT client in Shairport Sync has a Home-Assistant-specific configuration key called `object_id` used to define the entity's unique ID for HA. The term `object_id` has been deprecated by HA in favour of a new term: `defauly_entity_id`. This has now been added to Shairport Sync's MQTT client.
   
  The deprecated `object_id` term will be removed in a future release. Thanks to [Markus Reiter](https://github.com/reitermarkus) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2148).

Version 5.0-post-dev-10-g3888999d
==
* Remove requirement for the `mosquitto` service to be running when Shairport Sync is built with the `--with-mqtt-client` configuration flag. The service does not, in fact, need to be running in every case.
This reverses a change made in [PR](https://github.com/mikebrady/shairport-sync/pull/2137) at [Version 5.0-dev-161](https://github.com/mikebrady/shairport-sync/blob/development/RELEASENOTES-DEVELOPMENT.md#version-50-dev-161-g5b3659c2).

  Thanks to [David Crook](https://github.com/idcrook) for the [bug report](https://github.com/mikebrady/shairport-sync/issues/2150).

Version 5.0-post-dev-6-gd32d5cb9
==
* Fix handling of missing `const` type qualifier and some follow-on warnings. Thanks to [Rudi Heitbaum](https://github.com/heitbaum) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2146).

Version 5.0-post-dev-1-g963380a8
==
* This is essentially release Version 5.0 or 5.0.0.

Version 5.0-dev-168-g846e0e3d
==
**MQTT Enhancement**
* Add a new `publish_retain` boolean option to the MQTT section of the configuration file (normally `/etc/shairport-sync.conf`).

  When enabled, published MQTT messages will have the `retain` flag set, so that the MQTT broker stores the last message per topic and new subscribers receive the most recent value immediately.

  Thanks to [lululombard](https://github.com/lululombard) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2142).

**Dependabot Update**
* A Dependabot update to some image building tools was added.

Version 5.0-dev-161-g5b3659c2
==
**Bug Fix**
* Fix an MQTT warning and simplify building the `systemd` startup script. The warning occured if Shairport Sync was built with MQTT support. For that case, the startup script omitted stating that the `mosquitto` service was required and had to be active before starting Shairport Sync.

  Thanks to [hvilleneuve29](https://github.com/hvilleneuve29) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2137).

**Dependabot Update**
* A Dependabot update to some image building tools was added.

Version 5.0-dev-155-g45063d10
==
**Bug Fix**
* Finally! Add [Carl Johnson](https://github.com/SoarVermont)'s [suggestion](https://github.com/mikebrady/shairport-sync/issues/1996) to only consider interfaces that are up, running and not loopback interfaces for a valid MAC address. Thanks to Carl for his suggestion and for his gentle reminder.

Versions 5.0-dev-153-g525e49cd, 5.0-dev-151-g16b6534a and 5.0-dev-146-g691d71f3
==
**Updates**
* Tighten up the checking of arguments given to `debug()`, `inform()`, `warn()` and `die()` to avoid silly errors. No known issues, but better sure than sorry. Thanks to [ejurgensen](https://github.com/ejurgensen) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2124) which prompted the review.

Version 5.0-dev-142-gfcacc4e5
==
**Updates**
* Continuing to address some issues with AutoMix on AirPlay, skipping tracks, and other unreported issues, this update represents some "housekeeping" -- cleaning up some old code based on a better (?) understanding of protocols.

  Your bug reports are welcome.
  
  Here are some of the details:
  * Cleaned up `get_play_lock()` and `release_play_lock()` stuff and placed them in the right places for AP2, AP1 compatibiity and Classic AP.
  * Fixed the setting of flags and GID in Bonjour.
  * The `principal_conn` is correctly released when terminating a listener loop.
  * The `airplay_stream_type` is correctly set for a classic airplay session.
  * Moved NQPTP clock setup and start from Setup Phase 1 to Setup Phase 2.
  * Stopped a player thread from resetting the NTPTP client clock when it finishes.
  * Modified and greatly simplified the `TEARDOWN` handling in AP2 mode.
  * Allowed threads to 'naturally' terminate when ports are closed by the client.
  * Improved clearing of flush requests in new play sessions.
  * Added, but disabled, code to remove previously decoded buffers of data from the player if replacement buffers come in later. (This happens when input changes from AAC to ALAC on the fly, and it's unclear whether this is an AirPlay 2 implementation bug or not.) It's unclear which buffers should have primacy -- the older ones or the newer ones -- so for simplicity new buffers that would replace buffers that are already decoded are simply dropped.


Version 5.0-dev-106-gec0016fb
==
**Bug Fix**
* Fix a bug in the PulseAudio backend so that `PA_ERR_NODATA` returns "No latency data yet". Thanks to [Vladimir Shakov](https://github.com/bogdad) for the [report and fix](https://github.com/mikebrady/shairport-sync/pull/2119).

Version 5.0-dev-100-ga1238882
==
**Bug Fix**
* Fixed a bug whereby Shairport Sync would not work with `mbedtls` library from version 3.4 (present on recent versions of Linux, e.g. Debian 13...).

  Many thanks to [Christian Beier](https://github.com/bk138) for [finding the bug](https://github.com/mikebrady/shairport-sync/issues/2115) and for [fixing it](https://github.com/mikebrady/shairport-sync/pull/2118).

Version 5.0-dev-93-g1417cd6f
==
**Bug Fix**
* Ensure old flush requests are deleted when a new play session starts. Thanks to [saujanyashah](https://github.com/saujanyashah) for the [report](https://github.com/mikebrady/shairport-sync/issues/2107).

Version 5.0-dev-87-ge12a3ed7
==
**Updates -- This is Significant**
* To address some issues around AutoMix on AirPlay, a long-overdue rebuild of the buffered audio processor and been done, using everything we have figured out until now. It implements a cleaner method to handle both immediate and deferred flush requests. Lots of old flags and redundant code have been removed. Some diagnostics have been left in the code. The changes are pretty far-reaching, and it is possible that new issues have been inadventently introduced and old ones revived. Please let us know if you discover any issue, especially if you can readily cause the issue to occur. As you may know, it can be difficult to determine if an issue is caused by AirPlay 2 itself or by Shairport Sync.

Version 5.0-dev-83-g38592252
==
**Enhancements**
* _Convolution Update._ The convolution system is now multithreaded and works on stereo and multichannel audio at 48k and 44.1k.
  * Multiple impulse response (IR) files can now be provided to the convolution system through a new setting: `convolution_ir_files` (the old setting: `convolution_ir_file` is now deprecated). When convolution starts, Shairport Sync will look for an IR file with a sample rate matching the input (44.1k or 48k) and channel count. If an exact match can not be found, it will start again and look for a single-channel IR file with the matching rate. It will always choose the first match in the file list supplied with the `convolution_ir_files` setting or the `D-Bus` method.
  * Multithreading of convolution processing is now possible -- use the `convolution_thread_pool_size` setting to set the number of threads to use during convolution. (Note: high levels of multicore operation can cause audible power supply noise on some systems, due to rapid power switching or frequency-ramping of CPU cores.)
  
  * The [HiFi-LoFi FFT convolver](https://github.com/HiFi-LoFi/FFTConvolver) has been updated to the latest available.
* _Loudness Update._ The loudness code now works with stereo and multichannel audio at 48k and 44.1k.

**Deprecations**
* The Jack audio backend is deprecated and will be removed in the future. It seems to be very little used, and most of its functionality is now available through PipeWire. 
 
**Configuration File and D-Bus Changes**
* A new `convolution_thread_pool_size` configuration file setting has been added, defaulting to 1. This is not available in the `D-Bus` interface.
* The `convolution` setting is deprecated -- use `convolution_enabled` instead.
* The `convolution_max_length` setting is deprecated -- use `convolution_max_length_in_seconds` instead.
* The `convolution_ir_file` setting is deprecated -- use `convolution_ir_files` (i.e. plural!) instead.
* The `loudness` setting is deprecated -- use `loudness_enabled` instead.
* Corresponding `D-Bus` methods and properties have been updated.

**Bug Fix**
* Fix an FFmpeg deprecation warning.
  
Version 5.0-dev-65-g8545938d
==
**Enhancement**
* Add a new command-line argument `dbus_default_message_bus` which can be `system` or `session`. This becomes the default message bus used both by the D-Bus native service and the MPRIS service. In its absence, the default is to use the `system` message bus. Example of use:
  ```
  $ shairport-sync ... --dbus_default_message_bus=session ....
  ```
* Add a warning if a convolution impulse response file can not be read, e.g. due to a bad path name or a permissions error.

**Bug fix**
* If the native D-Bus interface is active and a convolution impulse response file is specified, don't open it a second time on startup. (BTW, this is not known to have caused any problems.)

Version 5.0-dev-50-g10700356
==
**Docker Update**
* Add an `ENABLE_AVAHI` environment variable to enable or disable `avahi` and `dbus` processes in the Docker image. The default is to enable both, an has always been the case. Thanks to [Adam Oleksy](https://github.com/admo) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2071).

Version 5.0-dev-47-g03fb1d69
===
**Tiny Update**
* Remove a redundant `s6-overlay` directory from the `docker` `etc` subdirectory. The directory was obsolete and wasn't copied into Docker images anyway...
* Various Dependabot updates.
  
Version 5.0-dev-39-g77d0deec
==
**Dependabot Updates**
* A number of deferred Dependabot-originated updates have been accepted. They all relate to (limited) automated testing and Docker image creation.

Version 5.0-dev-34-ga5a54621
==
**Bug Fix**
* Fixed a bug whereby the volume setting was ignored if the input was not at 44100 FPS and the loudness DSP setting was enabled. Thanks to [microfx](https://github.com/microfx) for the [report](https://github.com/mikebrady/shairport-sync/issues/2039) and for helping to track down the issue.

Version 5.0-dev-25-ge05448ae
==
**Bug Fixes and Enhancements**
* A convenience script [`user-service-install.sh`](user-service-install.sh) has been added to install a `systemd` user service startup script.
  * Associated with this, a user service startup script is no longer installed at the `# make install` step in a `systemd` based Linux. This is because:
    * The user service startup script would be owned by `root` and therefore would not be editable or removable by the user,
    * The user service startup script would be only be installed correctly if the command was `$ sudo make install` and would not work properly if the command was `# make install` because the script would be installed in the wrong place.
  * Update the [BUILD.md](BUILD.md) guide to reflect the introduction of the new script.
  * Thanks to [petcol](https://github.com/petcol) for their [help and persistence](https://github.com/mikebrady/shairport-sync/issues/2003) in identifying and resolving this problem.
* Ignore empty configuration setting strings. Specifically, log a warning and behave as if the configuration setting was not given. Thanks to [mantheman](https://github.com/mantheman) for the initial report.
* Add a `disconnect` command to the MQTT interface. Thanks to [Jordan Zucker](https://github.com/jzucker2) for the [PR](https://github.com/mikebrady/shairport-sync/pull/2001).
* In the Docker images, remove any left-over `pid` files before starting the D-Bus and Avahi services. Thanks to [Kir Belevich](https://github.com/deepsweet) for the [report](https://github.com/mikebrady/shairport-sync/issues/2009).


Version 5.0-dev
==
This is a major update adding exciting new features and improvements to Shairport Sync:
* 48k operation;
* 48k lossless stereo from Apple devices e.g. Homepod mini, Apple TV 4K;
* 48k 5.1 and 7.1 surround sound from Apple devices e.g. Homepod mini, Apple TV 4K;
* Full FFmpeg integration for flexibility and security;
* Multichannel ALSA, PipeWire, PulseAudio, FreeBSD, stdout and unix pipe output backends;
* Flexible, controllable, automatic matching of output format to input format;
* Improved synchronization;
* A new "vernier" resampling and interpolation method for low-power CPUs;
* Lower powered devices operation, including the original Raspberry Pi B;
* Reduced Docker image sizes;
* A `systemd` user startup script.
* Many bug fixes.

44.1k operation is retained.

**Deprecation Note** The stand-alone ALAC decoder by John Hammerton, used in all versions of Shairport Sync until now, and Apple's own ALAC decoder, optional for Shairport Sync, are now deprecated due to maintainance and security issues. You may find it be necessary to use one of them (for classic AirPlay reception only) in devices with very restricted storage, but security considerations mean that these decoders will be fully dropped in future. 

**Warning:** This is a breaking update. That means you probably can not simply update an existing installation of Shairport Sync without a little bit of work. To update correctly you must:
* Remove the Shairport Sync device from Apple Home -- you can add it back later;
* Review the list of libraries needed for building Shairport Sync, as there are some extras -- see the updated [BUILD.md](BUILD.md) guide;
* Review configuration flags when building Shairport Sync -- some names have changed, e.g. `--with-systemd` becomes `--with-systemd-startup`;
* Fully delete and reinstall Shairport Sync and NQPTP, along with all their service files;
* Review and update settings:
  * Some of the names of the setting sections have changed, e.g. the `pa` section becomes the `pulseaudio` section;
  * The behaviour of Shairport Sync backends has changed, e.g. rates and channel counts can change dynamically.

Version 4.3.7-dev-24-gbc5001eb
==
This is effectively release 4.3.7.

**Dependabot Updates**

* A number of deferred Dependabot-originated updates have been accepted. They all relate to (limited) automated testing and Docker image creation.
  
Version 4.3.7-dev-11-g7367d042
==
**Automated Docker Build Fixes**

* Thanks to [Charles Omer](https://github.com/charlesomer) for [fixes](https://github.com/mikebrady/shairport-sync/pull/1961) to the Github Action scripts for building and pushing Docker images.
  
Version 4.3.7-dev-3-g802058fe
==
**Docker Enhancement**

* Add `curl` to the Docker images to facilitate automations. Thanks to [ironcrafter54](https://github.com/ironcrafter54) for the [suggestion](https://github.com/mikebrady/shairport-sync/discussions/1960).

Version 4.3.7-dev
==
* This is effectively Version 4.3.6-dev-24-g015a5829, which is also effectively Version 4.3.6.

Version 4.3.6-dev-24-g015a5829 
==
**MQTT Documentation Addition**

Show how to use [MQTTThing](https://github.com/arachnetech/homebridge-mqttthing#readme) with [Homebridge](https://homebridge.io/) to represent Shairport Sync’s `active` status. Thanks to [keefar](https://github.com/keefar) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1955). 

Version 4.3.6-dev-19-g39781ece
==
**Documentation Fix**

Fix a typo in the configuration file. Many thanks to [Justin](https://github.com/OptimoSupreme).

Version 4.3.6-dev-17-gce73230e
==
**Documentation Fix**

Correct and clarify the [statistics documentation](https://github.com/mikebrady/shairport-sync/blob/master/ADVANCED%20TOPICS/Statistics.md).
1. The descriptions of Net Sync PPM and All Sync PPM were partly swapped -- this has been corrected.
2. Generally clarify the language and harmonize the wording with the introduction.

Many thanks to [willmo](https://github.com/willmo) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1950).
    
Version 4.3.6-dev-13-g00e595ce
==
**Docker Build Optimistion**

Ensure the `/run/dbus` directory, needed for dbus to start, is defined.

Version 4.3.6-dev-9-gf7401ec7
==
**Docker Build Optimisation**

Improve the Dockerfile build sequences as follows:
1. Build each dependency in a separate stage, facilitating parallel builds.
2. Define default value for ARGS, permitting easier local docker builds.
3. Clone the specified branch only, and to a depth of 1, thus getting relevant data faster.
4. Copy files in a one-off command, reducing the amount of Docker layers created.

Many thanks to [David Girón](https://github.com/duhow) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1945).

Version 4.3.6-dev-3-ge4d3c6f7
==
**Bug Fix**
*  Fix a potential bug by checking that a buffer is not zero before attempting to deallocate it.

Version 4.3.6-dev-1-g409397d3
==
This is effectively the same as the release version 4.3.5.

Version 4.3.5-dev-56-g758648a5
==
**Deprecation Note**
* The `soundio` backend is being deprecated and will be removed in a future update. It seems that nobody is using it, and as it stands it is very inflexible.

  _"If this be error and upon me proved,"_ -- as [Shakespeare wrote](https://www.poetryfoundation.org/poems/45106/sonnet-116-let-me-not-to-the-marriage-of-true-minds) -- please let us know...

Version 4.3.5-dev-53-gaa3af3b3
==
**Bug Fix**
* Fix a problem in the Jack Audio backend `audio_jack.c`. The problem was that if the system's jack server was running at anything other than 44,100 frames per second, the delay information being returned by the backend was incorrect. Thanks to [lucianoiam](https://github.com/lucianoiam) for [reporting the problem](https://github.com/mikebrady/shairport-sync/issues/1926) and for checking the proposed solution. 

Version 4.3.5-dev-48-gfe743fdf
==
**Docker Bug Fixes**
* Stop advertising the SFTP and SSH services on Bonjour. Thanks to [https://github.com/jpbaril](https://github.com/jpbaril) for bringing this to notice. 

**Bug Fix**
* Due to recent changes in the MQTT client, a compilation error occured when building for Classic Airplay only. Fixed.

Version 4.3.5-dev-44-ge4c2ce4a
==
**Enhancements**
* Add Home Assistant MQTT Auto-Discovery and active/playing State Messages, thanks to [aaronk6](https://github.com/aaronk6).
* Send `pvol` metadata on mute, thanks to [Tucker Kern](https://github.com/mill1000).

**Bug Fix**
* Check `swr_init()` and exit if error code found, thanks to [Andrew May](https://github.com/acmay).

Version 4.3.5-dev-29-g17b134b0
==
**PipeWire Enhancement**
* Initialise the PipeWire backend so that the PipeWire stream is initially inactive.

Version 4.3.5-dev-27-g8e6bd06c
==
**PipeWire Enhancement**
* Until now, the stream in the PipeWire backend remained permanently active. This meant that external programs could not tell when audio was really playing or not. Now the stream will go inactive (i.e. "paused") when play is stopped and active when play resumes.
  Note that in AirPlay 2, play often stops momentarily between tracks, and so there will be momentary changes from active to inactive and back to active again in the PipeWire backend. This may change as the backend improves. Thanks to [Nemo157](https://github.com/Nemo157) for the Idea: https://github.com/mikebrady/shairport-sync/discussions/1889.
  
Version 4.3.5-dev-23-g1687b2a4
==
**Docker Enhancement**
* The AirPlay 2 Docker image now incorporates the PipeWire backend. Thanks to [Maxim](https://github.com/irdkwmnsb) for the PR: https://github.com/mikebrady/shairport-sync/pull/1880.
  
Version 4.3.5-dev-6-g597aba0c
==
**FreeBSD Bug Fix**
* Modify the `sndio` backend (native to OpenBSD, also used in FreeBSD) to deal with an intermittent bug. Specifically, use an explicit `is_running` flag to keep track of the playing status of the backend. Thanks to [Jan Przybylak](https://github.com/janprzy), [Klemens Nanni](https://github.com/klemensn) and [Amanda Stjerna](https://github.com/amandasystems) for their help and persistence tracking down this problem. Resolves Issue: https://github.com/mikebrady/shairport-sync/issues/1765.

**Configuration Comment Update**
* Update comments in the `sndio` section of the sample configuration file.

Version 4.3.5-dev-3-ge28c566a
==
**Bug Fix**
* Fix a double-free fault introduced by replacing `avcodec_close` with `avcodec_free_context`. It seems as if the context is freed by a subsequent `av_free` anyway... Sigh.

Version 4.3.5-dev-1-g9909bc21
==
**Bug Fix**
* Use `(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO` now in place of bit-mapped `AV_CH_LAYOUT_STEREO` when setting up the FFmpeg software resampler with `swr_init()`. It was possible to do this starting at FFmpeg 5.1, but is mandatory in FFmpeg 7. Also remove now-deprecated `avcodec_close`. Thanks to [Deyan Dragov](https://github.com/itsdeyan) for the report: https://github.com/mikebrady/shairport-sync/issues/1876.

Version 4.3.4-dev-25-g15a7090a
==
**Change**
* Revert the `session_timeout` to two minutes (120 seconds). In other words, if a player becomes disconnected for more than two minutes while playing, the session is terminated.

**Bug Fixes**
* Enable the disconnected session timeout for AirPlay 2 operation -- it had been disabled by default.
* Fix a bug that prevented a fatal error that occured while directly accessing the alsa output device from terminating the application cleanly.

Version 4.3.4-dev-21-g105299a7
==
**Bug Fix**
* Fixed a bug that prevented Shairport Sync from recovering cleanly if a player disconnected without warning. The problem was that the player thread would not respond to cancel request. Fixed by moving a `pthreadtestcancel()` call to the innermost loop.
  
  Associated with this, some obsolete watchdog code has been removed.
  
  Also, the `session_timeout` default has been changed from 120 seconds to 60 seconds.
  
  Thanks to [Tobias Huttinger](https://github.com/tobiashuttinger) for the bug report -- Issue [#1870](https://github.com/mikebrady/shairport-sync/issues/1870).
  

Version 4.3.4-dev-13-gc064b8ba
==
**Bug Fixes**
* Reorder some of the files to be included in `shairport.c` to fix compilation errors in old versions of Mac OS X.
* Avoid using `TCP_KEEPINTVL` and `TCP_KEEPCNT` if they are not defined (they are not defined in older versions of Mac OS X). Thanks to [Sergey Federov](https://github.com/barracuda156) for raising these [issues](https://github.com/mikebrady/shairport-sync/issues/1860) and pointing at a potential solution.
* Fix a race condition with the metadata queues. The problem was that the queues were being initialised by threads launched by the main thread which, having started the threads, proceeded to use the queues. But if the threads were late in starting, the queues might not be initialised by the time the main thread tried to use them. Fixed by initialising the queues in the main thread. 

Version 4.3.4-dev-10-gaade1b39
==
**Docker Enhancement -- Continued**
* Move the `ulimit -n ...` command to set the maximum file limit from the `01-startup/script.sh` (where it didn't seem to have any effect) to the `02-dbus/run`. Thanks to [rp86](https://github.com/rp86) for the feedback.

Version 4.3.4-dev-8-g111e247b
==
**Docker Enhancement**
* Limit maximum open file handles in Docker images. Thanks to [Florian Wallner](https://github.com/powerflo) for [Issue #1756](https://github.com/mikebrady/shairport-sync/issues/1756), [iVolt1](https://github.com/iVolt1) for pointing to a fix and [rp86](https://github.com/rp86) for the reminder!
* Increase the level of optimisation while building the Shairport Sync application itself.

Version 4.3.4-dev-6-g5f64722a
==
**Enhancement -- HDMI Ports**
* As a further enhancement for HDMI, enable access to mixers on a HDMI output device. The issue appears to be that a mixer can not be "attached" to a device whose name begins with `hdmi:`, so it must be attached to the same device, but with the `hdmi:` prefix replaced by `hw:`.
  
Version 4.3.4-dev-3-g238e4bbf
==
**Enhancement -- HDMI Ports**
* If your device has HDMI ports, the Shairport Sync help command has previously listed them with a `hw:` prefix, e.g. `hw:vc4hdmi0`. Unfortunately, if you use a name like that as an `output_device` name for a HDMI device in Shairport Sync, the device may not be usable.
  
  But if you use the `hdmi:` prefix, e.g. `hdmi:vc4hdmi0`, it _may_ work: something capable of receiving audio must be connected to the HDMI port and it must be powered on _when your device boots up_.

  The enhancement is to update the help text for the ALSA backend to denote HDMI devices using the `hdmi:` prefix rather than `hw:`. Now, for example, on a Raspberry Pi 4, the output is:
  ```
  Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hdmi:vc4hdmi0"
      "hdmi:vc4hdmi1"
  ```
  Previously it was:
  ```
  Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hw:vc4hdmi0"
      "hw:vc4hdmi1"
  ```
  ...which is technically correct but unfortunately not very useful.

  Get more information about the hardware output devices using [sps-alsa-explore](https://github.com/mikebrady/sps-alsa-explore) (also available as a Docker image).
  
Version 4.3.4-dev-1-gc945f3a9
==
This is effectively release version 4.3.3 -- the starting point for 4.3.4.

Version 4.3.3-dev-81-ge0b13829
==
**Bug Fix**
* Undo the changes made in Version 4.3.3-dev-46. Unfortunately, the changes in [PR 1831](https://github.com/mikebrady/shairport-sync/pull/1831) make the Classic Airplay build of Shairport Sync invisible to iOS 17.5 and iPadOS 17.5. Thanks to [poker335](https://github.com/poker335) for identifying the issue and suggesting the fix. 

Version 4.3.3-dev-79-gf8e8550e
==
**Enhancements**
* A number of minor documentation updates. Thanks to [Ambrose Li](https://github.com/acli) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1851).

Version 4.3.3-dev-73-g8d678c43
==
**Enhancement**
* Add support for the `mdebtls3` library. (Note: `mdebtls3` is optional for Classic Airplay builds; it is not used in AirPlay 2 builds).
  Many thanks to [orangepizza](https://github.com/orangepizza) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1846).
  
Version 4.3.3-dev-66-gb054c004
==
**Enhancement**
* Simplify the macOS build check.
* Add some extra `CFLAG` settings to `configure.ac` for `libconfig`, `libpopt`, `openssl` and `libcrypto` to enable compilation on macOS without extra flag settings.
  
Version 4.3.3-dev-61-gcc963bf0
==
**Enhancement**
* Use `macos-13` instead of `macos-latest` as the `github` runner for the Mac-compatible build check.
  
Version 4.3.3-dev-58-g85f8ea12
==
**Enhancement**
* Use `PKG_CHECK_MODULES`, if it's available, to find the `ao` lib. 

Version 4.3.3-dev-53-gc0f5112b
===
**Bug Fix**
* Remove some almost-never-used and therefore untested code, and remove a potentially misleading comment from the ALSA backend.
  
Version 4.3.3-dev-49-gb5ea2b10
===
**MQTT Documentation Updates**
* Thanks to [Craig Lockwood](https://github.com/1ockwood) for [improvements](https://github.com/mikebrady/shairport-sync/pull/1838) to the [MQTT](MQTT.md) document. The `binary_sensor` example has been updated and the rest of the document has been edited for clarity, length and consistency. 

Version 4.3.3-dev-46-g6eba4fd8
===
**Enhancement**
* This enhancement modifies the AirPlay attributes advertised by Shairport Sync to allow AirPlay clients to see only the player of the appropriate protocol -- Classic Airplay ("AirPlay 1") or AirPlay 2.

  For example, if you run two instances of Shairport Sync, one built for Classic Airplay and the other for AirPlay 2, MacOS and iOS will recognize only the AirPlay 2 instance.

  Without the enhancement, MacOS and iOS will detect both Classic AirPlay and AirPlay 2 devices at the same time. (TuneBlade recognised only the Classic Airplay device, since it is not compatible with AirPlay 2.)

  Thanks to [Oleh Kuzhylnyi](https://github.com/kuzhylol) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1831).

Version 4.3.3-dev-43-g5922f9d1
===
**Enhancement**
* If a network interface is specified in the configuration file, limit the addresses used for PTP to that network interface only. Thanks to [Ryan Mounce](https://github.com/rmounce) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1813).

Version 4.3.3-dev-41-g0f07107c
===
**Enhancment**
* Look for `DACP-ID` and `Active-Remote` in the `GET /info` request, where it sometimes appears in an AirPlay 2 session originating from an Intel Mac.

Version 4.3.3-dev-40-g98627932
===
**Enhancement**
* Add a configuration option `--without-create-user-group` to optionally disable the creation of the `shairport-sync` user and group during a `make install`. This makes packaging easier for some systems, e.g. Yocto. Thanks to [hvilleneuve29](https://github.com/hvilleneuve29) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1820).

Version 4.3.3-dev-38-gab59f201
===
**Docker Compose Enhancement**
* Set `S6_KEEP_ENV = 1` to enable the passing of environment variables from the docker compose file into the image. Thanks to [HNKNTA](https://github.com/HNKNTA) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1812).

Version 4.3.3-dev-35-gd6ad6da1
===
**Documentation Update**
* The introduction paragraph now more clearly mentions the hard facts/requirements with "as of v4.1 and newer" and the supported devices are displayed in bullet list format for better readabilty. Thanks to [porg](https://github.com/porg) for the [PR](https://github.com/mikebrady/shairport-sync/pull/1818).

Version 4.3.3-dev-32-gcf10ff9d
===
**Enhancement**
* Implements the idea in [Issue 1808](https://github.com/mikebrady/shairport-sync/issues/1808) to make Fedora package-building easier. Thanks to [Bill Peck](https://github.com/p3ck) for the suggestion and continued support for Fedora.

Version 4.3.3-dev-30-g4aee4ec8
===
**Enhancements**
* Start using the `pledge(2)` facilities in OpenBSD. In OpenBSD, limit the set of system calls Shairport Sync is allowed to make, most importantly, permit `fork(2)`/`execve(2)` if and only if user defined commands are run. [PR 1803](https://github.com/mikebrady/shairport-sync/pull/1803).

**Bug Fixes**
* Do not print the password in debug logs. [PR 1811](https://github.com/mikebrady/shairport-sync/pull/1811).
* If a string argument is a NULL in some debug messages, output the string `(null)`. [PR 1810](https://github.com/mikebrady/shairport-sync/pull/1810).

Thanks again to [Klemens Nanni](https://github.com/klemensn) for these PRs.

Version 4.3.3-dev-21-g926cd56e
===

**Enhancements**
* Updates to documentation regarding OpenBSD -- [PR 1797](https://github.com/mikebrady/shairport-sync/pull/1797).

**Bug Fixes**
* Fix an OpenBSD compilation warning -- [PR 1797](https://github.com/mikebrady/shairport-sync/pull/1797).
* Terminate Shairport Sync if the configuration file exists but can not be opened -- [PR 1798](https://github.com/mikebrady/shairport-sync/pull/1798).
  
Thanks to [Klemens Nanni](https://github.com/klemensn) for these PRs.

Version 4.3.3-dev-14-gf37d1b8e
===
***Pesky Changes You Can't Ignore***
* The `man` entry for Shairport Sync has moved from Section 7 ("Miscellaneous information") to Section 1 ("General commands (tools and utilities)").
  For this reason, you should delete the file `/usr/local/share/man/man7/shairport-sync.7` if it exists.
  FYI, the `man` entry will now be placed at `/usr/local/share/man/man1/shairport-sync.1` at the `make install` step.
  Thanks to [Klemens Nanni](https://github.com/klemensn) for noticing this and for providing [PR 1795](https://github.com/mikebrady/shairport-sync/pull/1795) to deal with it.

**Bug Fixes**
* A number of OpenBSD-specific issues have been discovered and fixed by [Klemens Nanni](https://github.com/klemensn). Thanks to them for [PR 1794](https://github.com/mikebrady/shairport-sync/pull/1794) and [PR 1793](https://github.com/mikebrady/shairport-sync/pull/1793).

**Enhancement**
* Add a separate `install-config-files` Makefile target to selectively enable or disable the installation of configuration files at `make install`. Thanks to [Zane van Iperen](https://github.com/vs49688) for [PR 1782](https://github.com/mikebrady/shairport-sync/pull/1782).

Version 4.3.3-dev-4-g7e741465
===
* A slight change to [CAR INSTALL](https://github.com/mikebrady/shairport-sync/blob/development/CAR%20INSTALL.md), to add a two-second delay when accessing the WiFi device for the first time. It may be necessary in some cases, e.g. the Pi 3B.

**Bug Fix**
* Use `pthread_rwlock_wrlock` instead of the incorrect use of `pthread_rdlock_wrlock` when tearing down a connection. (This didn't seem to cause any problems, but it was definitely a bug, now fixed.)

Version 4.3.3-dev-1-g9ab85989
===
* Forgot to push tag 4.3.3-dev, so "back" to 4.3.3-dev-1. Apologies for the confusion. The commit hash is good.
* Further updates to [CAR INSTALL](https://github.com/mikebrady/shairport-sync/blob/development/CAR%20INSTALL.md), hopefully to make updates a bit simpler.
  
Version 4.3.3-dev-7-ga7603893
===
* Remove the a few compilation warnings during a Docker build -- the warnings were that some variables possibly being used uninitialised.
  
Version 4.3.3-dev-3-g2a5c49d3
===
* Update [CAR INSTALL](https://github.com/mikebrady/shairport-sync/blob/development/CAR%20INSTALL.md) for Bookworm, which uses `NetworkManager` and does not use `dhcpcd`.
* Changes to the car installation setup may result in more stable WiFi operation.

Version 4.3.2-dev-58-gb70dd463
===
This became Release 4.3.2, approximately.

**Investigation -- continued**
* Return `501` ("Not Implemented") instead of `200` ("OK") in response to a `POST` message with the argument `/feedback` on a Classic AirPlay connection. 

Version 4.3.2-dev-56-g4cd3c1da
===
**Investigation**
* Return `200` -- "OK" in response to a `POST` message with the argument `/feedback` on a Classic AirPlay connection. Continue to return `500` -- "Internal Server Error" for all other `POST` messages. This is part of an investigation into [Issue #1745](https://github.com/mikebrady/shairport-sync/issues/1745).

Version 4.3.2-dev-51-g98679bbb
===
**Enhancement**
* Add a new `pw` (PipeWire) backend setting -- `sink_target`. Leave the setting commented out to use the sink target already selected by the PipeWire system.

**Bug Fixes**
* Fix a bug in picking up the PipeWire `node_name` -- it was being sought in the `pa` stanza instead of the `pw` stanza of the configuration file.
* Fix memory management bugs in the PipeWire backend -- settings strings were being deallocated twice on exit.
* Fix memory management bugs in the PulseAudio backend -- settings strings were being deallocated twice on exit.
  
Version 4.3.2-dev-47-g288e5eb6
===
* In the PipeWire backend, change the default for `node_name` from "shairport-sync" to "Shairport Sync".

Version 4.3.2-dev-45-g3763b321
===
**Enhancement**
* Add two `pw` (PipeWire) backend settings -- `application_name` (default: "Shairport Sync") and `node_name` (default: "shairport-sync"). The `application_name` is used when referring to Shairport Sync in the GUI and the `node_name` is used in, for example, output from `pw-link`. Thanks to [alexsarmiento](https://github.com/alexsarmiento) for the [suggestion](https://github.com/mikebrady/shairport-sync/issues/1742).

**Bug Fixes**
* Minor bug fixes to the PulseAudio backend.

Version 4.3.2-dev-41-gdc197ae7
===
**Bug Fix**
* Fix underrun ("xrun") errors in the new PipeWire backend. These errors seem harmless, and the only place they were noticed was in the listing from `pw-top` on recent versions of PipeWire (e.g. PipeWire 0.3.80 in Fedora 38). Anyway, they are gone now.

Version 4.3.2-dev-38-gbde2bcc8
===
**Enhancement**
* The PipeWire backend (`pw`) has been completely rewritten and now provides full sychronisation. 

**Bug Fixes**
* Stability improvements to the PulseAudio (`pa`) backend. some bugs with the use of mutexes and the treatment of a full FIFO buffer were identified and fixed.

Version 4.3.2-dev-30-gc36d322a
===
**Bug Fix**
* If the volume control was at its absolute minimum (-144.0), audio would be muted even if `ignore_volume_control` was enabled in the configuration file. The fix was to mute only if `ignore_volume_control` is not enabled. Thanks to [mc510](https://github.com/mc510) for reporting the [issue](https://github.com/mikebrady/shairport-sync/issues/1737).

Version 4.3.2-dev-27-g04e3f891
===
**Bug Fixes**
* Fix bugs in the handling of an Avahi client disconnection event. Due to its rarity, Shairport Sync was not handling Avahi client disconnection events correctly. Specificially, it was not cleaning up before deleting the disconnected Avahi client and attempting to create a new one. This has now been fixed.

  It is worth noting that these disconnection events are very rare and often indicate that there is something wrong with the computer as a whole.

Version 4.3.2-dev-25-gc0467468
===
* Make the latency in the PipeWire backend 200,000 instead of 20,000. Should help with DAC underrun.

Version 4.3.2-dev-23-gc4975d5c
===
* Fixed some debug messages that were missing arguments. In principle, these could cause crashes just when you need them to work properly, sigh. As far as is known, however, they did not cause problems. Many thanks to [Nathan Gray](https://github.com/n8gray) for the [report](https://github.com/mikebrady/shairport-sync/issues/1734).

Version 4.3.2-dev-21-ga3f12d68
===
* When a connection terminates abruptly while is it the `principal_conn`, make sure it sets the `principal_conn` to `NULL` and cleans up the Bonjour flags, if appropriate.

Version 4.3.2-dev-19-g91c0803d
===
* Lock access to the `principal_conn` (i.e. the connection that may be playing) using a read-write lock rather than a mutex. Use read locking when checking that a connection is currently the principal connection before altering any system-wide values such as mDNS flags. This eliminates a number of data race conditions.
* As soon as it has been decided that the current playing session (as specified in the `principal conn`) is to be preempted, demote it from `principal_conn` status before stopping play, closing the connection, etc.
* Set the status flags correctly for a Classic Airplay session on an AirPlay 2 system.
 
Version 4.3.2-dev-4-g20a45def
===
* Bump version information.
  
Version 4.3.1-dev-3-g2fb81ca9
===
**Exploration**
* Add some slightly noisy diagnostics to try to locate a fault.

Version 4.3.1d-2-ge6a2a429
===
**Bug Fix**
* Fix a recently-introduced bug that prevented Shairport Sync being added to Home.

Version 4.2.1d0-67-gef14cace
===
**IMPORTANT Bug Fixes**
* A recent security audit has identified a number of issues with NQPTP that have been fixed in the latest update to NQPTP and in this corresponding update to Shairport Sync.
* Please update to the latest `development` branch version of NQPTP before updating Shairport Sync. (Shairport Sync now requires the Shared Memory Interface version 10, i.e. `smi10`.)
* When updating NQPTP on Linux, ensure you remove the old service file as directed in the README.
* Having completed both updates and installations, remember to restart NQPTP first and then restart Shairport Sync.

Version 4.2.1d0-62-g556419e8
====
**Bug Fix**
* Fix a bug introducted in the last `development` push, 4.2.1d0-60-g702e8285, whereby volume control changes would be ignored after a pause in playing of longer than about 8 minutes.

Version 4.2.1d0-60-g702e8285
====
**Bug Fix**
* Fixes a bug that causes the Docker image to crash occasionally when OwnTone attempts to interrupt an existing iOS session. Thanks to [aaronk6](https://github.com/aaronk6) for the report.

Version 4.2.1d0-56-ga9000de6
====
**Bug Fixes**
* The `active_end` enhancement in 4.2.1d0-23 didn't work and has been rewritten here.
* A crashing bug in NQPTP has been fixed in the `development` version -- please update. (The updated version will be incorporated automatically in the `development` version of the Docker image.)

Version 4.2.1d0-51-gbc2fda52
====
**Enhancement**
* Modify the new `dasl_tapered` volume control profile to work better with mixers with a restricted attenuation range.
  
  This volume control profile has the property that halving the AirPlay volume reduces the output level by 10 dB, which corresponds to roughly halving the perceived volume.
  
  For example, if the AirPlay volume goes from 0.0 to -15.0, the output level will decrease by 10 dB.
Halving the AirPlay volume again, from -15 to -22.5, will decrease output by another 10 dB.
Halving the AirPlay volume once more, from -22.5 to -25.25, decreases the output by a further 10 dB,
meaning that at AirPlay volume -25.25, the volume is decreased by 30 dB relative to the level at AirPlay volume 0.0.

  Now, if the attenuation range of the mixer is only 30 dB, then changing the AirPlay volume in the range from -25.25 down to -30.0
would have no further effect on the output level, since it's already at the lowest possible level permitted by the attenuator. This "dead zone" is about one sixth of the full range of the AirPlay volume control.

  To work around this, the "flat" output level is used if it gives a higher output dB value than the calculation described above. It means that changing the AirPlay volume, even at low levels, will always change the output level, while the original advantage of the `dasl_tapered` profile is retained at higher AirPlay volumes.

  In practice, if the device's attenuation range is over about 50 dB, the flat output level will hardly be needed at all.

Version 4.2.1d0-46-g57061dfb
====
**Enhancement**
* Add a new volume control profile called `dasl-tapered`. Shairport Sync already has two volume control profiles -- the `standard` profile -- which is the default -- and a `flat` profile. A feature of the `dasl-tapered` profile is that halving the volume control setting (e.g. moving the volume slider from full to half, or from half to a quarter) reduces the output level by 10dB, which roughly corresponds with a perceived halving of the audio volume level. Many thanks to David Leibovic, aka [dasl-](https://github.com/dasl-), for this.
To activate the `dasl-tapered` profile, set the `volume_control_profile` to `"dasl_tapered"` in the configuration file and restart Shairport Sync.
There may be some more minor adjustments to this profile for devices with a restricted attenuation range...

Version 4.2.1d0-40-g9ef4e351
====
**Bug Fix**
* Fix a cross-compliation error caused by not looking for the correct version of the `ar` tool. The fix was to substitute the correct version during the `autoreconf` phase. Thanks to [sternenseemann](https://github.com/sternenseemann) for raising the [issue](https://github.com/mikebrady/shairport-sync/issues/1705) and the [PR](https://github.com/mikebrady/shairport-sync/pull/1706) containing the fix.

Version 4.2.1d0-37-g9a8f1ba1
====
**Bug Fix**
* Update the mDNS strings for the Classic AirPlay feature of AP2, so that it does not appear to provide MFi authentication. Addresses [this discussion](https://github.com/mikebrady/shairport-sync/discussions/1691).

Version 4.2.1d0-35-g6acd73da
====
**Update**
* Confirm the bug fix made in [4.2.1d0-26](#version-421d0-26-g716aa051) to Classic Airplay Remote Control: always use a revision number of 1 when looking for status updates on the DACP remote control port.

Version 4.2.1d0-33-gd4e00380
====
**Bug Fix**
* If an `active_end` signal is generated when the system is shutdown gracefully, ensure it is sent before shutting down the metadata transmission system, duh.

Version 4.2.1d0-31-gb0f2fb9a
====
**Bug Fix**
* Fix a bug that could ocassionally cause a crash if `allow_session_interruption` is set to `"yes"` and a Classic AirPlay session is interrupted by a play session request from another device.

Version 4.2.1d0-28-g0ca5fd3c
====
**Tiny Bug Fix**
* Fix a `statistics` bug (the minimum buffer size was incorrectly logged) and also tidy up the statistics logging interval logic for resetting min and max counters.

Version 4.2.1d0-26-g716aa051
====
**Bug Fix**
* Classic Airplay Remote Control. Always use a revision number of 1 when looking for status updates on the DACP remote control port. This is ia quick-and-dirty change to try out a (good) suggestion in [Issue #1658](https://github.com/mikebrady/shairport-sync/issues/1658) -- if it works change will be made permanent. Thanks to [ejurgensen](https://github.com/ejurgensen), as ever, for the report and the suggested fix.

Version 4.2.1d0-23-g46a215b6
====
**Enhancement**
* On graceful shutdown, an `active_end` signal should now be generated if the system was in the active state. Addresses issue [#1647](https://github.com/mikebrady/shairport-sync/issues/1647). Thanks to [Tucker Kern](https://github.com/mill1000) for raising the issue.

Version 4.2.1d0-21-g2f52e699
====
**Bug Fix**
* Add an important missing format string argument to a call in the Jack Audio backend. Many thanks to [michieldwitte](https://github.com/michieldwitte) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1693).

Version 4.2.1d0-17-g17414f57
====
**Enhancements**
* Stop using a deprecated FFmpeg data structure reference.
* Stop using deprecated OpenSSL calls. Thanks to [yubiuser](https://github.com/yubiuser) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1684) -- which did some of the updating -- and for their guidance.
* Run workflow-based tests on PRs automatically. Thanks to [yubiuser](https://github.com/yubiuser) for their [PR](https://github.com/mikebrady/shairport-sync/pull/1687).

Version 4.2.1d0-5-gc3d99f96
====
**Enhancement**
* Increase the level of detail in the debug log at verbosity level 2 (i.e. `-vv`).

Version 4.1-dev-922-gffeeacc4
====
This is (approximately) Version 4.2 release.

**Enhancement**
* When built for AirPlay 2, include the Shared Memory Interface (SMI) version number in the version string, in the form: `smi*` where `*` is the version number, e.g. `smi9`. This must be same as the SMI version number of NQPTP, which can be listed by entering `$ nqptp -V`.

Version 4.1-dev-919-gbd068fb6
====
**Enhancements**
* Allow compilation with `libplist` 2.3.0. Thanks to [Markus Reiter](https://github.com/reitermarkus) for the update.
* Updates to GitHub Action Workflows -- many thanks to [yubiuser](https://github.com/yubiuser) for this detailed work.
* Include PulseAudio support in the Shairport Sync builds within the Docker images. Thanks to [Ferdynand Naczynski](https://github.com/nipsufn) and thanks also to [Noel Hibbard](https://github.com/noelhibbard) for championing this idea for a long time.
* Documentation Updates.

Version 4.1-dev-896-g03961830
====
**Bug Fix**
* In some situations, the Device ID generated by Shairport Sync was all zeros, and so was invalid, causing connectivity problems. The cause of the problem was that `get_device_id` was not interpreting `getaddrinfo` information correctly. Thanks to [Carl Johnson](https://github.com/ridgelift) for [reporting](https://github.com/mikebrady/shairport-sync/issues/1657) the issue, finding the (rather obscure) cause of the problem and for providing [code](https://github.com/mikebrady/shairport-sync/issues/1657#issuecomment-1493423175) to fix it.

Version 4.1-dev-886-ge74300d7
====
**Updates and Enhancements**
* Merge [Pull Request #1635](https://github.com/mikebrady/shairport-sync/pull/1635) and [Pull Request #1636](https://github.com/mikebrady/shairport-sync/pull/1636) . The PRs update the MQTT documentation to correspond to updates in [Home Assistant](https://www.home-assistant.io/blog/2022/06/01/release-20226/#breaking-changes). Thanks to [hunhejj](https://github.com/hunhejj) for these.
* Merge [Pull Request #1624](https://github.com/mikebrady/shairport-sync/pull/1624). The PR updates the Docker images to use Alpine 3.17 and ensure services such as Avahi and D-Bus start in the correct order. Thanks to [yubiuser](https://github.com/yubiuser) for their work on this.

Version 4.1-dev-876-g29ca3225
====
**Bug Fix**
* Fix a bug reported in Issue [#1633](https://github.com/mikebrady/shairport-sync/issues/1633). The bug was that when a Realtime Audio stream (e.g. playing from Spotify on iOS or using Shairport Sync as the Sound Output on a Mac) was played, it was fine, but when a second stream was started, it could not be heard. The problem was that the PTP clock was not being correctly revalidated for second and subsequent Realtime Audio streams. The fix was to ensure that the PTP clock is revalidated on second and subsequent plays on the same connection. Thanks again to [David Leibovic](https://github.com/dasl-) for finding the problem.

Version 4.1-dev-874-g551734b3
====
**Bug Fix**
* Use TCP keepalive a little more generally -- treat it the same as a client closing the link rather than just an error.

Version 4.1-dev-872-g65c6975e
====
**Bug Fix**
* Remove three potential race conditions between Shairport Sync opening a TCP connection and the client checking that the connections are open. The problem was that the connections were being opened in threads that were created just before the client was given the connection information. If the threads were delayed (e.g. on a slow or busy processor), the client could use the connection information to check the connections, but find that they were not (yet) open. This could cause the client to terminate the session immediately with a `TEARDOWN`. The fix was to open the connections before creating those threads and before sending the connection information back to the client. In this way, the connections are guaranteed to be open before the client has the information it needs to try to open them, even if the threads ared delayed in starting. This bug would manifest itself by allowing play to proceed but not play anything.

Version 4.1-dev-870-gc924387a
====
**Bug Fix**
* When built for AirPlay 2, ensure the hexadecimal string that prefixes the AirPlay 1 Service Name in the Bonjour text strings matches the AirPlay 2 Device ID. For example, if the Service Name is `Kitchen` and the AirPlay 2 Device ID is `b8:2f:eb:d7:85:df`, the AirPlay 1 Service Name should be `B82FEBD785DF@Kitchen`. (When built for AirPlay 1, the hexadecimal prefix is simply a hash of the Service Name.)

   Thanks to [Casper](https://github.com/casperghst42) for raising the [issue](https://github.com/owntone/owntone-server/issues/1578) and to [ejurgensen](https://github.com/ejurgensen) for identifying the cause of the problem.

Version 4.1-dev-862-g6eb70eee
====
***Pesky Changes You Can't Ignore***

* **Important**. Please update and re-start NQPTP. The Shared Memory Interface protocol that Shairport Sync and NQPTP use to communicate with one another has been updated to version 9 to reflect changes in NQPTP's operation.

**Enhancements**
* A new "high volume check" attempts to prevent the unpleasant surprise of a new session playing at the same very high volume as the previous session if that previous session was some time, e.g. some hours, ago. How it works is that if a timeout period has elapsed since the last play session and the last play session was very loud, a lower volume will be suggested for new play sessions. Most iOS and iPadOS apps accept the suggestion as the new volume. However, Macs, HomePods and AppleTVs use the suggested volume only when connecting the first time. The timeout, loudness threshold and suggested volume are all settable in the configuration file. Please refer to the sample configuration file for more details. The feature is disabled by default.

* The PulseAudio backend has been reworked to simplify the code and to remove a number of issues. Hopefully nothing has been broken.

Version 4.1-dev-846-gb5bb2fef
====
* Improvements to the detection of a missing or non-existent output device. Look for `ENODEV` and `ENOENT` errors when opening or playing to an output device.

Version 4.1-dev-843-gae5910e2
====
**Bug Fix**
* Stop endless error messages from the `stdout` backend.

**Enhancements**
* Call the unfixable error handler if the output device can't be opened or generates errors while being used. If no program is attached to the `run_this_if_an_unfixable_error_is_detected` hook in the `sessioncontrol` section of the configuration file, the program will exit instead.
* Shorten the amount of time to wait after a client disappears before terminating a play session.

Version 4.1-dev-824-g8e5393ec
====
**Enhancements**
* Add `man` page and `man` utilities to the Docker images.
* Enable the MQTT client to parse the `phbt` and `phb0` Playing HeartBeaT metadata tokens.

Version 4.1-dev-818-gf15c3255
====
**Enhancement**
* Add a configuration check for the `xxd` program when building for AirPlay 2. Cleans up an annoying problem mentioned in Issue [#1341](https://github.com/mikebrady/shairport-sync/issues/1341). Thanks to [David Girón](https://github.com/duhow) for the reminder.

Version 4.1-dev-815-gcb097034
====
**Enhancement**
* Use [TCP `keepalive`](https://en.wikipedia.org/wiki/Keepalive) to close down a play session when the client disappears, e.g. when the player goes out of range of WiFi. A play session is now terminated after a break in connection lasting more than approximately one minute. Addresses the problem reported in Issue [#1584](https://github.com/mikebrady/shairport-sync/issues/1584). Thanks to [JanLp](https://github.com/JanLp) for the report.

Version 4.1-dev-809-g8a69f5ea
====
**Enhancements**
* Add another item of metadata with the code `phb0` and the DBus property name `FirstFramePosition`. This is similar to the `phbt` and `FramePosition` metadata, except that it is updated for the first frame of a play session. It can be ignored, as a `phbt` token is also sent when the first frame is output.

Version 4.1-dev-805-g50738c36
====
**Enhancements**
* Very provisional additions to the metadata -- add some functionality for Shairport Sync to send information about what frame of audio is being played at a particular time. It can be set up to periodically generate a `phbt` (a Playing HeartBeaT) item comprising the RTP frame number of the audio being played at a particular instant -- the computer's system time in nanoseconds. The two numbers are presented as a string, e.g. `595422363/4697675115573193`. Set the interval in seconds with the new `metadata` setting `progress_interval` – 0.0 means no heartbeat is generated. 
* Equivalent additions have been made to the native `DBus` interface: the `phbt` information appears as a `FramePosition` property and the update interval can be set using the `SetFramePositionUpdateInterval(Seconds)` method.

Please note that this might all disappear or change radically.
 
Version 4.1-dev-798-g803eddbd
====
**Docker**
* Within the Docker container, launch Shairport Sync as `root` user rather than as the user `shairport-sync`. This is experimental, to investigate possible device access issues.

Version 4.1-dev-795-g7f279efa
====
**Bug Fix**
* Fix a bug if the ALSA device name isn't available. (Happens in Fedora 37 on Arm in VMWare Fusion on an Apple Silicon Mac.)

Version 4.1-dev-790-g03d291f4
====
**Bug Fix -- Second Attempt!**
* Second attempt to fix a bug caused in the transition to version 4.1, where a change was made to how the 12-hex-digit prefix to the classic AirPlay service name was derived. Previously it was a hash of the service name, and in 4.1 this was changed to deriving it from one of the device's MAC addresses. This seems to cause problems with multiple instances of classic Shairport Sync running on the same system, as they would all get the same prefix. The fix was to generate the prefix from a hash of the service name and the device's MAC address, which should be unique for every instance. Addresses issue [#1581](https://github.com/mikebrady/shairport-sync/issues/1581). Thanks to [Christoph Bloemer (?)](https://github.com/chbloemer) for the report.
The first attempt failed because not all the changes needed were done. 

Version 4.1-dev-789-g75aa150e
====
**Oops**
* Reverted the fix below. Something still to be done...

Version 4.1-dev-786-gfc117e73
====
**Bug Fix**
* Fix a bug caused in the transition to version 4.1, where a change was made to how the 12-hex-digit prefix to the classic AirPlay service name was derived. Previously it was a hash of the service name, and in 4.1 this was changed to deriving it from one of the device's MAC addresses. This seems to cause problems with multiple instances of classic Shairport Sync running on the same system, as they would all get the same prefix. The fix was to generate the prefix from a hash of the service name and the device's MAC address, which should be unique for every instance. Addresses issue [#1581](https://github.com/mikebrady/shairport-sync/issues/1581). Thanks to [Christoph Bloemer (?)](https://github.com/chbloemer) for the report.

Version 4.1-dev-784-g60e913dd
====
**Bug Fix**
* In the PulseAudio backend, Whenever a play or latency request is made, reopen the stream if it is closed. Addresses issue [#1580](https://github.com/mikebrady/shairport-sync/issues/1580). Thanks, as ever, to [Noel Hibbard](https://github.com/noelhibbard) for the report.

Version 4.1-dev-781-g9af924dd
====
**Metadata Enhancements**

The following new metadata items have been added to the metadata pipe, MQTT and the native D-Bus interface.
1. `core`/`asdk`. This is [glossed](https://github.com/tchapi/shairport-sync-ui/blob/master/DMAP_DAAP_Codes.md) as `daap.songdatakind`. It seems to indicate whether the audio is timed or not, e.g. a track or album has the value `0`, and a continuous untimed stream such as a radio station is `1`, so it is potentially very useful for remote control displays, as it seems to indicate whether a progress bar, etc. could be used or not. The value is included, if available, in the native DBus interface in the metadata bundle with the tag `sps:songdatakind`.
2. `ssnc`/`styp`. This is the type of data stream currently being received by Shairport Sync. Values are `Classic`, `Buffered` or `Realtime`. Included in the native D-Bus interface as `StreamType` in the `org.gnome.ShairportSync.RemoteControl` interface.
3. `ssnc`/`ofmt`. This is the output format of audio going to the output device. Values are `S32`, `S24` and so on. S=signed, U=unsigned, 32/24/16/8 is the bit depth, BE/LE is Big-Endian or Little-Endian. Note that `S24` means signed 24 bits in a 32-bit space; `S24_3LE` means Signed 24 bits in a 3-byte little-endian form, etc. Each frame is always a stereo pair. Included in the native D-Bus interface as `OutputFormat` in the `org.gnome.ShairportSync` interface.
4. `ssnc`/`ofps`. This is the output rate in frames per second, usually `44100`. Included in the native D-Bus interface as `OutputRate` in the `org.gnome.ShairportSync` interface.

Version 4.1-dev-776-g27754a62
====
**Docker Enhancements**
* Place the configuration file and the sample configuration file in the Docker image at `/etc/shairport-sync.conf` and `/etc/shairport-sync.conf.sample`. Thanks to [inDomus](https://github.com/KC-inDomus) and [tamasfodor1988](https://github.com/tamasfodor1988) for the report, Issue [#1575](https://github.com/mikebrady/shairport-sync/issues/1575).

Version 4.1-dev-774-g3e4b67e9
====
**Metadata Enhancements**
* Add the service name, e.g. `"Den Speakers"` as metadata. This has the code `"svna"` in the metadata pipe and the name `"service_name"` in the MQTT interface.
* Add two properties to the D-Bus metadata interface:
   1. `ServiceName` -- see above.
   2. `ClientName` -- the name of the current client, e.g. `"Joes's iPad"`.

Version 4.1-dev-772-g085bf128
====
**Bug Fix**
* Fix a long-standing bug whereby the socket used for the RTSP connection wasn't closed when the connection was closed by the client, potentially exhausting the sockets available. Many thanks to [Lars Strojny](https://github.com/lstrojny) for the report: [#1567](https://github.com/mikebrady/shairport-sync/issues/1567).

**Enhancements**
* Small documentation updates.
* More use of GitHub actions to check standard builds.

Version 4.1-dev-756-g0a8b1231
====
**Docker Updates**
* Move to using `latest` tags for the latest release and `rolling` for intermediate (non-release) updates on the master branch. Pushes to the `development` branch will result in Docker images with the `development` tag.

**Enhancement**
* Tidy up  `--display_config` output to skip empty configuration file stanzas and to note when there are no active settings whatever in the file.

Version 4.1-dev-747-ga4da36b4
====
**Docker Updates**
* Extra command-line arguments passed when the Docker image is launched are passed to the `shairport-sync` instance as described on Docker Hub.
* The `shairport-sync` instance runs as `user` and `group` `shairport-sync`, which has reduced privileges.
* The "classic" docker image now uses the `s6-overlay` to manage processes, the same way as the standard (AirPlay 2) docker image. It does makes the image bigger, so takes a little longer to download.

Version 4.1-dev-745-gbb4e9078
====
**Bug Fix**
* Fix a bug that caused new metadata not to be passed on immediately to the D-Bus, MPRIS and MQTT interfaces. Thanks to [phelbas](https://github.com/phelbas) for finding the bug and the fix in Issue [#1560](https://github.com/mikebrady/shairport-sync/issues/1560).

**Enhancement**
* Add two new metadata tokens for AirPlay 2 buffered audio streams only, which can be paused for a period of about five minutes before the session is ended. They are for Pause (`paus`) and Resume (`pres`).
* Clean up the generation of `MPRIS` and D-Bus `PlayerStatus` values. Until now, Shairport Sync used extra information to try to determine the state a session was in -- `Playing`, `Paused` or `Stopped`. The extra information used what (1) when the first frame was played, (2) when a flush was requested and when (3) frame play was resumed. Unfortunately, these are noisy signals and are unreliable for this purpose, so they are now no longer used.

  The situation now is that `PlayerStatus` becomes `Playing` when a play session begins and becomes `Stopped` when a play session ends, unless it is an AirPlay 2 buffered audio session.
  
  In the case of a AirPlay 2 buffered audio session, a play session will be `Paused` when play stops (but the play session is not ended). If play resumes within a period determined by the client (about five minutes), `PlayerStatus` will return to `Playing`. Otherwise, the play session is ended by the client and `PlayerStatus` will transition to `Stopped`.

Version 4.1-dev-738-g9f7584eb
====
**Enhancement**
* It seems that this missing session key issue discovered and discussed below is a transient problem: some client apps omit the session key occasionally but include it the rest of the time. So, to make the problem a bit less intrusive for users, the way a missing session key is dealt with has been changed. The new arrangement is that instead of dropping the AirPlay connection completely as noted below, the audio is simply skipped. From the user's perspective, the music simply won't play, but the AirPlay connection won't be dropped. When they start it again, the session key will hopefully be present and the audio will play. Let's hope that this will be less disruptive for users and that this issue goes away as clients are updated. Thanks again to [Mike](https://github.com/xska2) for his help with this in [#1551](https://github.com/mikebrady/shairport-sync/issues/1551).

Version 4.1-dev-735-g6a55774f
====
**Bug Fix**
* Very occasionally, and for as-yet unknown reasons, an AirPlay 2 session may not include an important parameter called a "session key". This was causing Shairport Sync to crash. With this update, Shairport Sync will now simply drop the entire connection if a session doesn't include a "session key". Addresses the crashing issue reported in [#1551](https://github.com/mikebrady/shairport-sync/issues/1551). Big thanks to [Mike](https://github.com/xska2) for his huge assistance in tracking this down. 

Version 4.1-dev-730-g63e0dfda
====
**Minor Debugging Enhancements**
* Improve debugging of a Shairport Sync daemon process created with `libdaemon`.
* List the command line when Shairport Sync starts with a verbosity of 1 or more.

Version 4.1-dev-726-g5e6e6344
====
**Enhancement**
* Enhance `--displayConfig` to log information about the OS as well as about Shairport Sync itself, and use Shairport Sync's standard logging -- it's not stuck on `STDERR` anymore.

Version 4.1-dev-721-g93c1f1ae
====
**Pesky Things You Can't Ignore**

If you are updating from a previous version, after you have pulled the update, please redo the `autoreconf -fi` and the `./configure...` steps (and the do a `make clean` for good measure) before `make`ing the executable -- there have been many changes to the build process.

**Bug Fix**
* Correction to `Makefile.am` to make it work both on FreeBSD and Linux both in the source directory and in a subsidiary build directory.

Version 4.1-dev-717-g86539bc5
====
**Pesky Things You Can't Ignore**

If you are updating from a previous version, after you have pulled the update, please redo the `autoreconf -fi` and the `./configure...` steps (and the do a `make clean` for good measure) before `make`ing the executable -- there have been many changes to the build process.

**Enhancements**
* A new command-line-only option is included: `--displayConfig`. This prints configuration information to `STDERR` and should be useful when debugging issues.
* Help text is reordered and updated.
* The `man` page is updated.
* The `man` contents are no longer automatically built when Shairport Sync is built. This is okay because the contents are normally static. The `man` folder has a new `Makefile`.
* The `xmltoman` application is not now needed when building Shairport Sync.
* When updating the `man` page, `xsltproc` is now used instead of `xmlmantohtml`.

Version 4.1-dev-701-g65daef30
====
**Bug Fix**
* Fix a bug in the generation of version information from git tags. The fix is to use lightweight tags as well as annotated tags. GitHub marks releases with lightweight tags, so this should make version and release information correspond better.

**Enhancement**
* Add an new Advanced Topic -- [Adjusting Sync](ADVANCED%20TOPICS/AdjustingSync.md) explaining how to compensate for amplifier delays such as might be found on TVs or AVRs.
 
Version 4.1-dev-694-g234c00ad
====
**Bug Fix**
* Fixed a memory allocation (`malloc`) bug caused by allocating on byte too few for a character string -- yep, the trailing `NUL`. Special thanks to [ageorgios](https://github.com/ageorgios) for their help in locating and fixing the bug! Thanks also to [MTxx87](https://github.com/MTxx87) and [Mike](https://github.com/xska2) for reporting what seems to be the same bug.

Version 4.1-dev-688-g7dcad083
====
**Bug Fixes**
* Fixed two memory leaks in Shairport Sync.
* Reinstate the use of `libcrypt` again with the `pair_ap` library to avoid an apparent memory leak in `libcrypto`.
* Quieten a noisy debug message in the `dummy` backend.

Version 4.1-dev-604-gf96fa2f7
====
**Bug Fix**
* If the D-Bus interface was enabled, each change in volume control was calling the volume event hook twice. Fixed.

Version 4.1-dev-601-g98744211
====
**Bug Fix**
* Recent versions of Shairport Sync have a "heartbeat" checker, to try to determine when the client has stopped running without warning -- typically when it has gone to sleep. Unfortunately, it interferes with some non-Apple clients, so it has been disabled in this update.

Version 4.1-dev-599-g2bc62c7d
====

**Enhancements**
* Added some GitHub Actions to test different combinations of builds -- hopefully the result will be fewer bugs. Thanks to [Charles Omer](https://github.com/charlesomer) for the inspiration to try these in the first place.

**Bug Fixes**
* Fixed a bug where the DAC was not closed after the active timeout when the `alsa` `disable_standby_mode` was set to `"auto"`. Thanks to [Tim Curtis](https://github.com/moodeaudio) for an exemplary [bug report](https://github.com/mikebrady/shairport-sync/issues/1546).
* Fixed a couple of bugs (found with GitHub Actions) in the installers on both `systemd` and System V systems. 

Version 4.1-dev-551-g30aaabc4
====
**Update**
* Updated to the latest version of the [`pair_ap`](https://github.com/ejurgensen/pair_ap) library. Thanks again to [ejurgensen](https://github.com/ejurgensen/pair_ap/commits?author=ejurgensen) for this invaluable resource.

**Enhancement**
* Removed the requirement for the [`libgcrypt`](https://gnupg.org/software/libgcrypt/index.html) library. OpenSSL is mandatory for other parts of Shairport Sync when operating in the AirPlay 2 mode and is now used in place of `libgcrypt`.

Version 4.1-dev-548-g7dc077e2
====
* Removed the workaround for the apparent bug in AirPlay 2 Buffered Streams in iOS 16.0. A number of approaches were tried. The most aurally successful is left in the code but is disabled. Basically, it delays the timing of the frames following the 2,112 frame discontinuity by 2,112 frames. This eliminates the timing discontinuity but delays the AirPlay stream by 47.9 ms, and this can be heard in some situations. So it looks as if the bug -- if that's what it is -- can't easily be worked around. It has not been fixed by iOS 16.0.2, unfortunately.

Version 4.1-dev-543-g24f06b81
====
**Enhancements**
* Add a workaround for an apparent bug in AirPlay 2 Buffered Streams in iOS 16.0. After playing exactly 22,528 frames of audio, iOS 16.0 sends 2,112 frames of audio with the same timestamps as the previous 2,112 frames. This makes the frames that follow 47.9 ms (2112/44100 seconds) late. The workaround is to drop these extra 2,112 frames.
* If an AirPlay 2 Buffered Streams is being skipped or scrubbed, the audio that follows will generate an audible click due to an AAC decoding transient because preceding audio frames are missing. Shairport Sync now mutes the first 2,048 frames, down from 3,072 frames before.

**Bug Fix**
* Give the RTSP idle checker a longer timeout -- 10 seconds -- and confine its operation to AirPlay 2 Runtime Streams and classic AirPlay only. This is to stop an occasional RTSP idle timeout silencing an AirPlay 2 Buffered Audio session.

Version 4.1-dev-532-g8dfebea2
====
**Enhancements**
* Improve the response of Shairport Sync when a Mac is woken up from sleep and continues playing e.g. a YouTube video. Shairport Sync should resume play in most circumstances. (BTW, the HomePod mini does not resume in these circumstances.)

**Bug Fix**
* Make the RTSP idle checker a bit more resilient -- wait for the full timeout even if interrupted by e.g. `SIGINT`. Also signal to NQPTP when to stop listening when a session goes idle or stops.

Version 4.1-dev-526-gfd880056
====
***Pesky Changes You Can't Ignore***

* **Important**. Please update and re-enable NQPTP. The Shared Memory Interface protocol that Shairport Sync and NQPTP use to communicate with one another has been updated to reflect changes in NQPTP's operation.

Apart from moving to the new SMI interface for NQPTP, this update consists of many small enhancements and bug fixes. (Yeah, and the build number really did go backwards...)

Version 4.1-dev-535-gdafd1726
====
**Bug Fix**
* Fix yet another bug in the Makefile.am. Thanks to [Isioma Nnodum](https://github.com/mikebrady/shairport-sync/issues?q=is%3Apr+author%3Aicompuiz) for finding and [fixing](https://github.com/mikebrady/shairport-sync/pull/1536) it.

Version 4.1-dev-520-g1092a8c1
====
**Enhancements**
* It is now possible to build Shairport Sync in a separate directory, thanks mainly to the work [#1493](https://github.com/mikebrady/shairport-sync/pull/1493) and [#1500](https://github.com/mikebrady/shairport-sync/pull/1500) of [Lukas Rusak](https://github.com/lrusak). A number of subsequent changes were necessary to make it work for FreeBSD.
* Shairport Sync should build on piCore. Thanks to [jackaroo](https://github.com/94jackaroo) for [reporting](https://github.com/mikebrady/shairport-sync/pull/1528) and checking.

Version 4.1-dev-516-ga617e607
====
**Updates and Enhancements**
* The Docker image has been enhanced. Thanks, as always, to [Charles Omer](https://github.com/charlesomer). More changes are on the way here.

**Bug Fixes**
* With Realtime Streams, if even one timing control packet was missing, Shairport Sync would work as if the sender had disappeared and cause a playback interruption. This has been modified so that it takes at least three missing control packets in a row to conclude that the sender was gone. Thanks to [Devid Leibovic](https://github.com/dasl-) for a [very thorough investigation](https://github.com/mikebrady/shairport-sync/issues/1515) of this issue.

Version 4.1-dev-494-gd34db2a4
====
* The [CAR INSTALL.md](https://github.com/mikebrady/shairport-sync/blob/development/CAR%20INSTALL.md) guide has been updated for AirPlay 2 and for Raspberry Pi OS (Bullseye).
* A few noisy debug messages have been removed or quietened. Thanks to [David Leibovic](https://github.com/dasl-) for [the report](https://github.com/mikebrady/shairport-sync/issues/1513).

Version 4.1-dev-468-gcb03d5c6
====
**Updates and Enhancements**
* The `development` branch now builds again on macOS with [brew](https://brew.sh). (Remember, though, that Shairport Sync [will not function in AirPlay 2 mode on a Mac](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md)).
* The `libao` backend has been updated to address an issue with the system becoming unavailable. The biggest change was to close the driver as soon as playing stops, reopening it when playing restarts.
* The `libao` drivers are now listed in the help text. If available, it is suggested that the `alsa` driver be used in preference to the `pulseaudio` driver.

**Bug Fixes**
* The classic AirPlay build now works properly again with `tinysvcmdns` and `dns_sd`.

Version 4.1-dev-466-g05dfcdfd
====
**Enhancements**

These are for advanced users.
* Add a new `Protocol` property to the D-Bus interface. Values: `AirPlay` if built for AirPlay and `AirPlay 2` if build for AirPlay 2.
* Add a new `Volume` property to the D-Bus interface. This enables you to get and set the volume _locally_. Use this carefully: it means that you can set the volume of the player without changing it at the source (e.g. iTunes / Music). The source will not be informed of the change, so the volume it sets and and the actual volume may become different. It is preferable to use remote control (if available) to "ask" the player to change the volume.
* Add a new `DropSession` method. This will immediately and forcibly terminate a playing session without informing the source. Use it carefully. It is preferable to use remote control (if available) to "ask" the player to stop playing.

Version 4.1-dev-454-ga774a6fc
====
**Enhancement**
* Strip the trailing dot and everything after it in the hostname when using it as the basis for a service name. E.g. `freebsd.local` becomes `freebsd`.

**Bug Fixes**
* Fix a few compilation bugs and portability issues, and turn off deprecation warnings when compiling for Mac OS.

**Note:** After this update, you'll need to run `autoreconf -fi` and the `./configure...` steps again.

Version 4.1-dev-439-gb7a11cae
====
Documentation update. Comments, corrections and suggestions welcome.

Version 4.1-dev-429-g449e024c
====
**Enhancement**
* With the ALSA backend, if the `default` device isn't available, Shairport Sync will now try to use `"hw:0"` instead. Background: the default  ALSA device is sometimes a pseudo device provided by a sound server such as PipeWire or PulseAudio to play audio from ALSA-compatible programs. But it's not always available to all users, especially daemons like Shairport Sync. This simple change will try to use the first ALSA hardware device -- card 0 -- if the default output device is inaccessible with error code `EHOSTDOWN`. It allows Shairport Sync to work on default settings with Fedora 36.
 
Version 4.1-dev-424-g4729e325
====
**Enhancements**
* Shairport Sync should no longer terminate if its ALSA output device is busy. This is useful when using ALSA devices in systems with sound servers such as PulseAudio or PipeWire. Shairport Sync now simply waits until the device becomes available. Shairport Sync retains exclusive access to the device (and so the sound server can not use it) until it is finished playing.

* Wait up to ten seconds (was two seconds) for NQPTP to come online when starting up -- AirPlay 2 only.

Version 4.1-dev-420-g23d0502f
====
**Bug Fix**
* This commit reverses the enhancement in Version 4.1-dev-418-g5011dc09 because it doesn't work properly. Specifically, some service files are not being renamed and installed correctly. Apologies for the inconvenience.
**Note** After pulling this update, you should rerun the `$ autotools -fi` and `$ ./configure...` steps.

Version 4.1-dev-418-g5011dc09
====
**Enhancement**
* The `Makefile.am` file has been updated to allow allow building and installing from a separate build directory. Thanks to [Lukas Rusak](https://github.com/lrusak) for the [contribution](https://github.com/mikebrady/shairport-sync/pull/1493).
**Note** After pulling this update, you should rerun the `$ autotools -fi` and `$ ./configure...` steps.

Version 4.1-dev-412-gf218a4d4
====

**Note** -- A bug has been fixed in NQPTP -- please update it.

**Reversion**
* The change to the allowed settling time mentioned below has been reverted back to 5.0 seconds – the issue this was meant to address turned out to be a bug in NQPTP.

Version 4.1-dev-409-g6bfeb49f
====
**Bug Fix**
* Shorten the settling time (from 5.0 seconds to 0.75 seconds) allowed when a new AirPlay 2 player, with its own clock, is added or removed from the devices currently playing. The problem was that if something else happens during the settling time, the changeover to the new clock may fail. This doesn't eliminate the issue, but hopefully it stops it being a problem for the user.

Version 4.1-dev-376-gca6e768f
====
**Bug Fix**
* Fix a bug that permitted clipping to occur when the `playback_mode` as set to `mono`. Thanks to [xStatts](https://github.com/xStatts) for the [report](https://github.com/mikebrady/shairport-sync/issues/1491) and for checking the fix.

Version 4.1-dev-357-gf551274a
====
Explore remote control a bit more. No progress to report...

Version 4.1-dev-352-g4d608216
====
**Bug Fix**
* Fix a bug in the activity monitor that could potentially allow the `run_this_before_entering_active_state` hook to be called more than once when the player was going active. It could also potentially allow the `run_this_after_exiting_active_state` hook to be called more than once when the player was leaving the idle state, but only if the `active_state_timeout` was set to 0. Thanks to [Ben Willmore](https://github.com/ben-willmore) for the [report](https://github.com/mikebrady/shairport-sync/issues/1440).

**Enhancement**
* Allow procedures hooked to `run_this_before_entering_active_state` and `run_this_after_exiting_active_state` to run without the `activity_monitor_mutex` in the locked state.

Version 4.1-dev-350-g82849e0d
====
**Change**
* Return 501 instead of 404 for unimplemented GETs in AirPlay 2 mode.

Version 4.1-dev-347-g6556a79d
====
**Bug Fix**
* If a speaker with its own master clock (e.g. a HomePod mini) was added and then removed from the group of output speakers in AirPlay 2 mode, Shairport Sync would stop working. 

**Note** -- there is an important update to NQPTP out as well.

Version 4.1-dev-340-g4705be9b
====
**Enhancement**
* Improve the AAC decoder check by iterating through all formats the AAC decoder supports.

Version 4.1-dev-332-g38c43f07
====
**Bug Fixes**
* Fixed a bug where the AAC decoder check would not recognise the AAC decoder in `ffmpeg` version 3. Thanks to [GXJ0102](https://github.com/GXJ0102) for help with this.
* Fixed a bug where, on a very fast CPU, `"basic"` interpolation would be chosen instead of `"soxr"` interpolation.

**Enhancement**
* Relax the "fully-up-to-date" system requirement slighty -- Shairport Sync will build and run on Ubuntu 18.04 LTS.

Version 4.1-dev-317-g0dc34a46
====
***Pesky Changes You Can't Ignore***

A change has been made the `shairport-sync`service/startup files, so before updating, please rerun the `./configure...` step and (Linux only) remove the existing service file as follows:
```
$ ./configure ...  # whatever your preferred options are...
# rm /lib/systemd/system/shairport-sync.service # Linux only -- no need for this in FreeBSD
```
**Enhancements**
* The AAC decoder check for AirPlay 2 operation was temporarily removed but has now been reinstated and is working again. If it is causing problems, please let us know. A section has been added to the [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/development/TROUBLESHOOTING.md#aac-decoder-issues-airplay-2-only) document.
* Debugging and statistical information now outputs to `STDERR` by default, so the rather confusing `-u` command line option is no longer needed. This has the following implications:
  * If Shairport Sync is run from a terminal window, then messages, warnings, debug and statistical information will appear on the terminal window.
  * If Shairport Sync is run as a daemon, then `STDERR` will be redirected to the system log.
  * If you wish to use [syslog](https://en.wikipedia.org/wiki/Syslog) (e.g. you might wish to see the different levels of highlighting of different categories of information),
there is a new command line option `--log-to-syslog`. (This is used by the modified startup scripts.)
* The `-u` option is now redundant and is deprecated (see above).
* A new command line option `--log-to-syslog` allows you to direct messages, warnings, debug and statistical information to [syslog](https://en.wikipedia.org/wiki/Syslog). For full effect, it should be the first argument in your list of command line arguments. It is really intended for when Shairport Sync is run as a daemon.

Version 4.1-dev-283-ga62e4b0b
====
**Enhancements**
* FreeBSD compatibility. Shairport Sync (and NQPTP) can now work in AirPlay 2 mode on a FreeBSD system. Note that the build instructions and the configuration parameters have changed -- `libdaemon` is no longer needed and should not be included.
* Certain Linux distributions -- notably Fedora 36 -- do not include a suitable AAC decoder for decoding AirPlay 2 Buffered Audio streams. Shairport Sync now includes a check to ensure that the AAC decoder can decode the stream, which seems to be encoded in Floating Point Linear Planar AAC ("`fltp`") format. (The AAC decoder is provided by the [`libavcodec`](https://www.ffmpeg.org/libavcodec.html) library, part of the [`ffmpeg`](https://www.ffmpeg.org) multimedia framework. For troubleshooting, the  [`ffmpeg`](https://www.ffmpeg.org/ffmpeg.html) command line tool is very useful.)

Version-4.1-dev-266-g1d228870
====
**Minor Changes**
* Cosmetic changes -- some debug and informational messages altered.

Version-4.1-dev-255-g8a2756b7
====
**Bug Fix**
* Fix an audible interruption that could happen in certain circumstances when an output device was added or removed to a group of output devices. The "certain circumstances" are when the following two conditions are true:
  1. An AirPlay 2 Realtime Stream is playing,
  2. The new output device's clock takes or relinquishes control of output timing.

For example, (i) when playing Spotify on an iOS device (this currently uses an AirPlay 2 Realtime Stream) to a Shairport Sync device and (ii) adding or removing a HomePod mini as an output device (the HomePod mini's clock will take or relinquish control of output timing).

**Enhancement**
* When a Shairport Sync device is added as an output device to a stream that is already playing, it now mutes for two seconds to improve synchronisation when it joins in and actually starts producing audio. The previous slight lack of synchronisation was inaudible, but would require some interpolation before settling down.

Version-4.1-dev-249-g38bbad6f
====
**Bug Fix**

* Fix a bug in the `systemd` startup file. The bug was that even when Shairport Sync was built without Avahi support, Shairport Sync would not launch until the Avahi service was available. Thanks to [klslz](https://github.com/klslz) for reporting the problem in issue [#1454](https://github.com/mikebrady/shairport-sync/issues/1454).


Version-4.1-dev-247-ga295c2a2
====
**Bug Fix**

* Fix a bug in the logic for suppressing clicks when the `alsa` `disable_standby_mode` is active.

Version-4.1-dev-245-gf34a69fa
====
**Enhancement**

* AirPlay 2 only. Two new metadata messages have been added. They might be useful for amplifier switching, etc.
1. The `conn` message signifies "AirPlay 2 connect" and is sent when an AirPlay 2 device selects this player. It is sent _before_ killing any current play session.

   (Note: in AirPlay 2 operation, the existing `clip` message is identical, but it is sent _after_ any existing play session has been terminated.)

2. The `disc` message signifies "AirPlay 2 disconnect".


Version-4.1-dev-242-g3877acc7
====
**Bug Fix**
* Fix a crash that would occur after days of continuous play. The issue was that each time a cover art file was created (and previous cover art deleted) the coverart directory was opened with `open()`, but was not closed with `close()`.

Version-4.1-dev-238-g1dc481c3
====
**Enhancement**
* Add some extra diagnostic information when large sync errors occur: if available, add the total number of frames sent to the output device and also, if available, add th
e current delay, in frames, in the output device.

Version-4.1-dev-234-ge8721889
====
**Bug Fix**
* Fixed a bug using HomePod Software 15.4. The bug was that when a Shairport Sync device was added as a HomePod output device, visually it didn't appear to join the group of output devices and additionally its volume control could not be accessed. The bug was fixed by increasing the maximum permissible length of ID strings in Shairport Sync.

Version-4.1-dev-229-g08afa822
====
**Enhancement**
* Add _Minimum Buffer Size_ to statistcs information for a Buffered Audio stream (and remove the minimum and maximum number of decoded buffers -- they are not too interesting). The Minimum Buffer Size gives the smallest size, in bytes of kilibytes, of the buffer of audio that has yet to be decoded during the last statistics interval. The larger it is, the longer Shairport Sync can play while a network outage is occuring. It can legitimately be zero at the start of a track, and obviously it can reach zero at the end of a track, but if it is zero while a track is playing, it may indicate problems with the network.

Version-4.1-dev-224-g29d2155e
====
***Pesky Change You Can't Ignore***

A change has been made the `shairport-sync` `systemd` service file, so before updating, please remove the existing service file with the following command:
```
# rm /lib/systemd/system/shairport-sync.service
```
**Bug Fixes**
* Interpret `disable_standby_mode` to determine if a flush should also close an `alsa` device as follows:
  1. If `disable_standby_mode` is set to `"no"` (default) or `"never"`, then a flush will immediately close the `alsa` device.
  2. If `disable_standby_mode` is `"auto"`, a flush will not close the `alsa` device. The device will be closed at the end of the active timeout.
  3. If `disable_standby_mode` is `"yes"` or `"always"`, a flush will not close the `alsa` device. The device will never be closed.

* Remove the invalid `Provides` entry from the `systemd` service file. Thanks to [David Crook](https://github.com/idcrook) for reporting Issue [#1448](https://github.com/mikebrady/shairport-sync/issues/1448).

Version-4.1-dev-214-gadf0845e
====
**Enhancement**
* Check to see if the alsa_handle is NULL in `precision_delay_and_status`.

Version-4.1-dev-211-g9bac04e2
====
**Bug Fix**
* Check for `NULL` pointers before dereferencing delay and state variable pointers in `audio_alsa.c`'s `delay` and `*-delay_and_status` functions. Thanks to [leirace](https://github.com/leirace) for the report, Issue [#1441](https://github.com/mikebrady/shairport-sync/issues/1441).

Version-4.1-dev-209-gb4cd4fbc
====
***Pesky Change You Can't Ignore***

A change has been made the `shairport-sync` `systemd` service file, so before updating, please remove the existing service file with the following command:
```
# rm /lib/systemd/system/shairport-sync.service
```
**Enhancements**
* Improve the timing service interface: Instead of opening and closing a Shared Memory Interface (SMI) whenever timing information was needed from NQPTP, the SMI  is now opened only once, at startup.   Overall, this reduces overhead at critical times and seems to improve the initial accuracy of synchronisation. It also requires the NQPTP service to be available and accessible at startup.
* Improve `alsa` flush performance: Add new code to better handle a flush request in the `alsa` backend. This new code results in smoother operation and timing accuracy is improved when continuing after a flush.
* Define the Shairport Sync service in the `systemd` service file.

**Bug Fix**
* Fix a bug that caused a crash if an incorrect `wait_for_completion` option was chosen. Fixed a few similar bugs too. Thanks to [HiFiBerry](https://github.com/hifiberry) for reporting the issue at [#1431](https://github.com/mikebrady/shairport-sync/issues/1431).

Version-4.1-dev-193-g4848608d
====
* Don't try to decode commands at debug level 2 -- leave it to level 3.

Version-4.1-dev-191-ge07c202b
====
#### Enhancement
* This enhancement relates to MQTT with empty payloads. Many MQTT brokers and clients treat messages with empty payloads in a special way. For instance, [MQTT Explorer](http://mqtt-explorer.com) hides messages with empty payloads and [Node-RED](https://nodered.org) seems to use them to release and garbage-collect data received previously.
Many Shairport Sync messages contain no extra data and so would naturally have empty payloads. This is causing problems -- see [#1425](https://github.com/mikebrady/shairport-sync/issues/1425) and [#1375](https://github.com/mikebrady/shairport-sync/issues/1375). To avoid these problems, a short "dummy" payload consisting of the character string "--" is now added to MQTT messages that contain no extra data. This can be changed with a new MQTT option in the configuration file: `empty_payload_substitute`. Thanks to [DOCaCola](https://github.com/DOCaCola) and [UMB8998](https://github.com/UMB8998) for reporting on this.

Version-4.1-dev-188-gf135d857
====
#### Bugfixes
* Ensure that when `ignore_volume_control` is true, the volume metadata has the current, max and min values of the [now ignored] attenuator set to 0.0. Thanks to [Michael Daley](https://github.com/greend139) for reporting in Issue [#1417](https://github.com/mikebrady/shairport-sync/issues/1417).
* Add a fix for v3 of s6 overlay and other improvements to the Docker image. Thanks to [Charles Omer](https://github.com/charlesomer) for the fix and the improvements and thanks to [rdrdog](https://github.com/rdrdog) for reporting the issue.

#### Other Changes
* Add code to check certain 32-bit and 64-bit conversions.

Version-4.1-dev-173-gdeb11654
====
#### Enhancements
* Add some sanity checking of the latency offset and buffer size to check if they can be accommodated.
* Discard non-sentinel UDP timing packets that are received just when a timing thread starts -- they may relate to a prior session.

Version-4.1-dev-171-g703717a5
====
#### Bugfix
* Clear PTP clock information when a Realtime stream stops playing. This _may_ address one of the issues reported in [#1404](https://github.com/mikebrady/shairport-sync/issues/1404).

Version-4.1-dev-169-g6ced6bc6
====
#### Enhancement
* Add a new `mixer_control_index` setting to the `alsa` section of the configuration file. A mixer is fully identified by a name and index. The index defaults to zero, and, until now, it could only be set using the `alsa` command-line argument `-i`. Thanks to [flipoidix](https://github.com/flipoidix) for bringing this issue to notice.

Version-4.1-dev-164-gf79222f7
====
#### Bugfix
* Fix a crashing bug in AirPlay 2 mode which occurs if no configuration file is found. This particularly affects the Docker image, as it contains no configuration file. Thanks to [Charles Omer](https://github.com/charlesomer) for reporting.

Version-4.1-dev-160-g9c2f9f7a
====
#### Bugfix
* Fix a crashing bug in AirPlay 1 ("Classic") mode, whereby if a session was abruptly interrupted, e.g. by the sudden loss of a network, Shairport Sync would crash. The problem was that the interrupted session was not correctly terminated. Thanks to [th0u](https://github.com/th0u) for reporting issue [#1398](https://github.com/mikebrady/shairport-sync/issues/1398).

Version-4.1-dev-158-gb5860fc4
====
#### Enhancements
The following enhancements are mainly experimental, for advanced users only, and are intended to enable multiple instances of Shairport Sync (SPS) to run in one computer and provide multiple AirPlay 2 endpoints. At this point, multiple instances of SPS can indeed run on one machine and provide multiple AirPlay 2 endpoints, but unfortunately the presence of multiple services at the same address seems to confuse AirPlay 2 clients. So, it seems that while multiple instances of SPS can now run successfully on one device, they can't be used on the same network.
* Use a uniquely-named named SMI interface to get dedicated access to NQPTP.
* Simplify validation of the mDNS interface setting, if given.
* Exit with a warning if the player is using a name that already exists.
* Stop storing the timing peer list.
* Generate an initial timing list during setup — don’t wait for the first timing peer list message. May gain around 50 ms.
* Fix a bug and clean up generation of device's timing addresses.
* Add some control to the generation of the Device ID:
1. Revise how the Device ID is generated from the device’s MAC addresses
2. Add a setting to add a fixed offset to the automatically-generated Device ID.
3. Add a setting to specify a complete Device ID, replacing the automatically-generated Device ID.
* Be more careful to create default file names based on the name of the app.

Version-4.1-dev-146-g42e508b4
====
#### Enhancements
* Extract several very useful extra pieces of client metadata for AP2. Many thanks to [Cody Cutrer](https://github.com/ccutrer).
* Enable a cleaner restart of the D-Bus daemon in a Docker image. Thanks again to [Charles Omer](https://github.com/charlesomer).

Version-4.1-dev-138-g5617f40e
====
#### Minor Internal Changes
* A small number of internal chages made.

Version-4.1-dev-127-g4d0460b9
====
#### Enhancement
* Update the Docker workflow to automatically do Docker tagging and to use an s6-overlay implementation. Thanks to [Charles Omer](https://github.com/charlesomer) for all this work, and to [Robert Casties](https://github.com/robcast) for his s6-overlay Pull Request [#1349](https://github.com/mikebrady/shairport-sync/pull/1349) which formed the basis of the s6-overlay work.
* Slight documentation update, thanks to [michaeldeborst](https://github.com/michaeldeborst).


Version-4.1-dev-115-g8656149d
====
* Simplify and improve timing when a play session from a new device is started.

Version-4.1-dev-106-ge98111fb
====
#### Enhancement
* Try to improve the reliability of restarting a "silent clock" device.

Version-4.1-dev-104-g84dd2dc4
====
* Begin to explore some more connection types.

Version-4.1-dev-102-g9bf45574
====
#### Enhancement
* Make debug messages in the system log easier to see on macOS Terminal (!) by outputting them at LOG_INFO level rather than LOG_DEBUG level.

Version-4.1-dev-100-g8330f30b
====
#### Enhancement
* Updated information on the statistics provided -- see [MOREINFO.md](https://github.com/mikebrady/shairport-sync/blob/development/MOREINFO.md#statistics). (Note: this has moved to [STATISTICS.md](ADVANCED%20TOPICS/Statistics.md).)

Version 4.1-dev-95-ga7a02083
====
#### Bug Fix
* Fixed a compilation bug that occurs if metadata support is not included in the build configuration. The fix was to add the appropriate conditional code statements. Thanks to [Daniel Gooß](https://github.com/Dangoo) and [winstonsetiawan](https://github.com/winstonsetiawan) for reporting.

Version 4.1-dev-92-g3ab16b5d
====
#### Enhancement
* Thanks to [Anton Lindqvist](https://github.com/mptre) for updates and bug fixes to building for OpenBSD.

#### Other Updates
* Thanks to [tekdude](https://github.com/tecdude) for a documentation update.
* Various documentation updates.

Version 4.1-dev-81-g24a5bbbe
====
This is a pretty big under-the-hood update.

#### Significant Changes
* Change clock base to improve stability. You need to use the updated `nqptp` that has also been changed. If you forget, Shairport Sync will not work properly and will leave a message in the log.
* Avoid skipping the first 100 milliseconds or so of a newly-selected track.
* Clean up the output of statistics — shorter when log verbosity is zero.
* Change behaviour after an unrecoverable error -- if not handled, exit Shairport Sync.
* Add client and server ip metadata for AP2 Buffered Audio Stream.

#### Changes
* Add client and server ip metadata for AP2. Remove timing peer list and DACP code only for a full AP2 teardown.
* Fix a few memory leaks. Add bogus exits to allow SPS to quit at the end of a play session, to make it easier to use `valgrind`.
* Stop checking that the timing peers are within the same subnet.
* Look for at least 0.1 seconds of leadtime, ensure a master clock is at least 0.275 seconds old before use, quieten a few debug messages.
* Add txtAirPlay data to the get info response.
* Don't pause reception of long RTSP messages.
* Don't ask for cover art if not asking for metadata too, duh.
* Skip some types of invalid AAC packets.
* Don't decode outdated frames.
* Wait for timing information to be valid in AP2 Buffered Audio Streams processing.
* Set the timing peer list as soon as a SETPEERS message is received -- don't wait until play is about to begin. Not sure if it works universally yet.
* Tidy up the statistics printout table and stop outputting (non existent) nominal fps for AP2 Realtime Streams.
* Use PKG_CHECK_MODULES to find libavcodec, to make it work with Fedora 35 (?)
* Change behaviour after an unrecoverable error -- if not handled, exit Shairport Sync.
* log statistics headers whenever verbosity goes on or off or whenever statistics are requested
* Clean up statistics.
* Stop watchdog timing out AP2 sessions.
* Remove the commas in the statistics logs -- easier to get into a spreadsheet.
* Don't overwrite service files if they already exist.
* Try finding libplist as libplist-2.0 on Linux too.
* Move to using CLOCK_MONOLITHIC_RAW to avoid NTP effects. Bump interface version.
* Add code to check divisions and mods for a potential divide by zero problem. This may cause SPS to terminate but it will leave a log message.
* Add a 'stats' function to the audio back ends to replace the 'rate_info' function. Zero all except the alsa back end.
* Attempt to increase the precision of the FPS timing
* Update to the latest version of ejurgensen's pair_ap library. With thanks.

#### Bug Fix
- This fixes [#1345](https://github.com/mikebrady/shairport-sync/issues/1345) by checking for the `libplist` library both under the name `libplist` and `libplist-2.0` using the fallback strategy [as described in the autotools docs](https://autotools.io/pkgconfig/pkg_check_modules.html). Since we now try both names, we no longer need the FreeBSD/Linux check. Many thanks to [FW](https://github.com/fwcd) for the bug report, the fix and this documentation!

Version 4.1-dev-21-g1032cad6
===
#### Enhancement
- Update the `pair_ap` library to the [latest version](https://github.com/ejurgensen/pair_ap). Thanks again to [ejurgensen](https://github.com/ejurgensen). Addresses Issue [#1334](https://github.com/mikebrady/shairport-sync/issues/1334). Thanks to [iChUdY](https://github.com/iChUdY) for reporting the issue.

Version 4.1-dev-19-g3f6f0932
===
#### Bug fix
- Fix a bug that would occasionally mute Shairport Sync when using it with AppleTV. Combined with the [recent update](https://github.com/mikebrady/nqptp/blob/development/RELEASE_NOTES.md) of `nqptp`, this makes using Shairport Sync much more useful with an AppleTV 4K.

Version 4.1-dev-15-gf4115c45
====
#### Bug Fix
- Fix the following [buildroot](https://buildroot.org) build failure when the `--with convolution` flag is included. The issue is due to a change in the behaviour of automake 1.16.5 -- see [`https://git.savannah.gnu.org/cgit/automake.git/commit/?id=f4a3a70f69e1dbccb6578f39ef47835098a04624`](https://git.savannah.gnu.org/cgit/automake.git/commit/?id=f4a3a70f69e1dbccb6578f39ef47835098a04624):
```
configure.ac:305: error: AM_INIT_AUTOMAKE expanded multiple times
/home/giuliobenetti/autobuild/run/instance-1/output-1/host/share/aclocal-1.16/init.m4:29: AM_INIT_AUTOMAKE is expanded from...
configure.ac:6: the top level
/home/giuliobenetti/autobuild/run/instance-1/output-1/host/share/aclocal-1.16/init.m4:29: AM_INIT_AUTOMAKE is expanded from...
configure.ac:305: the top level
```
Fixes:
[`http://autobuild.buildroot.org/results/464148bdccb705d8992dc860262bfdeb01b7e2a1`](http://autobuild.buildroot.org/results/464148bdccb705d8992dc860262bfdeb01b7e2a1).

Thanks to [Fabrice Fontaine](https://github.com/ffontaine) for [Pull Request #1314](https://github.com/mikebrady/shairport-sync/pull/1314).


Version 4.1-dev-9-gd2e35fc0
====
#### Bug Fix
- Fix an incompatibility with iOS 15.1 betas and release candidates. The problem was this: if an unknown RTSP command was received, Shairport Sync would return a HTTP error response code of 400 ("Bad Request") but the player would not continue. After some experimentation, it was discovered that a HTTP error response code of 501 ("Not Implemented") would allow the player to continue. Thanks to [corrpel](https://github.com/corrpel) and [bloomkd46](https://github.com/bloomkd46) for reporting the issue.

Version 4.1-dev-4-g0fe21f3a
====
#### Bug Fixes
- Fix a bug whereby the `volume_range_db` setting would be ignored if it was less than the range of the hardware mixer.
- Fix a bug that prevented an AirPlay 2 build if metadata was not enabled, either directly or indirectly, at the `./configure...` step.

Version 4.1-dev
====
#### Enhancement
- **Better Siri, HomePod mini and Home Integration** An AirPlay 2 build of Shairport Sync now provides a visible volume control when used by a HomePod mini. This should work for other devices too -- reports welcome. You can use the Home app and Siri to set and adjust the volume when used to play from the HomePod mini.

Version 4.0-dev-308-g23da206
====
#### Bug Fixes
- Restore logging to `syslog`.

Version 4.0-dev-303-g9074069
====
#### New Features
- An initial [PipeWire](https://pipewire.org) backend, with thanks to [Lukas Rusak](https://github.com/lrusak). Use the `--with-pw` configuration flag at the `./configure...` stage to include support.

#### Bug Fixes
- Fix a bug when using the `tinysvcmdns` mDNS implementation, with thanks to [fesc2000](https://github.com/fesc2000).
- When an unrecognised SETUP message is received, just ignore it and put a warning in the log. Remove a redundant `client_setup_plist` data item from the `conn` data structure.

#### Enhancements
- Shairport Sync no longer needs to be in the same directory as NQPTP during compilation.
- Add Docker automations -- thanks to [Charles Omer](https://github.com/charlesomer).
- Add Issue management automations -- thanks to [Charles Omer](https://github.com/charlesomer).
- Reconnect the DACP scanner for AirPlay 2. (It still isn't useful, unfortunately.)
- Add initial support to allow dynamic modification of Rendezvous advertisements.
- Simplify some UFW suggestions in the Troubleshooting Guide.
- Merge some documentation changes from the `master` branch.
- Add information to the Car Install Guide about improving boot times, thanks to [vasilisvg](https://github.com/vasilisvg)

Version 4.0-dev-185-g0c02ee2
====

#### New Features
- **Home App Integration** An AirPlay 2 build of Shairport Sync now offers limited integration with the Home app. Specifically, a Shairport Sync AirPlay 2 instance can now be added as a speaker to the Home app. Thanks to [ckdo](https://github.com/ckdo) and [ejurgensen](https://github.com/ejurgensen) for figuring this out and for coding it up. It means that some Siri interaction is possible.
- **Automated Docker Builds** Thanks to the work of [Charles Omer](https://github.com/charlesomer), Docker builds on the `development` branch should appear from time to time on the [Docker Hub](https://hub.docker.com/r/mikebrady/shairport-sync).

#### Bug Fixes
- [Crashing bug fix](https://github.com/mikebrady/nqptp/blob/main/RELEASE_NOTES.md) in `nqptp` -- thanks to [ste94pz](https://github.com/ste94pz).
- Honour the `max_volume_db` setting even when `ignore_volume_control` is true. Note that dithering is enabled if software attenuation is needed.

#### Other Changes
- Spell out `Shairport Sync` instead of `SPS` in a few places.
- Clang format some source files.

Version 4.0-dev-153-g9ccde50
====

Big Update
----
Version 4 brings [limited AirPlay 2 functionality](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md) to Shairport Sync.

For information about AirPlay 2, including acknowledgements, please visit [AIRPLAY2.md](https://github.com/mikebrady/shairport-sync/blob/development/AIRPLAY2.md).

#### Known Issues
- In the AirPlay 2 build, a crash can occur occasionally when the player is being selected. If you can get this to happen reliably, please let us know.

#### Warning

1. Big changes have been made to the codebase to enable it to be used to build either as the "classic" AirPlay 1 player or as a new AirPlay 2 player. These changes have probably introduced bugs into the classic AirPlay 1 build.
2. There is lots we don't know about AirPlay 2. This can result in incorrect behaviour. Watch out for very high volume levels, especially if you have a powerful audio system.
3. This is a `development` branch and is probably more buggy than usual. Normal support will not be provided -- instead, we will be interested in bug reports that help improve the program. You need to be adept with Linux or FreeBSD to get involved.

Version 3.3.8d7
====
**Bug Fix**
* Fix a bug in the `alsa` back end. In the interval between checking to see if the alsa device handle was NULL and actually using it, the handle could be set to NULL. The interval between check and usage is now protected.

Version 3.3.8d6
====
**Bug Fix**
* Fix a bug in the `alsa` precision timing code. Thanks to [durwin99](https://github.com/durwin99), [Nicolas Da Mutten](https://github.com/cleverer), [mistakenideas](https://github.com/mistakenideas), [Ben Willmore](https://github.com/ben-willmore) and [giggywithit](https://github.com/giggywithit) for the [report](https://github.com/mikebrady/shairport-sync/issues/1158).

Version 3.3.8d5
====
**Bug Fixes**
* Fix a fault in the configuration script `configure.ac` that caused the `libdaemon` library to be omitted even when requested. Thanks to [aleszczynskig](https://github.com/aleszczynskig) and [xska2](https://github.com/xska2) for the [report](https://github.com/mikebrady/shairport-sync/issues/1137).

Version 3.3.8d4
====
**Bug Fixes**
* Fix a fault in the configuration script `configure.ac`. The fault was that a `--without-*` configuration argument was being misunderstood and interpreted partly as a `--with-*` argument. Thanks to [David Racine](https://github.com/bassdr) for the [report](https://github.com/mikebrady/shairport-sync/issues/1123).

Version 3.3.8d3
====
**Enhancements**
* Documentation for the MQTT interface. Many thanks to [minix1234](https://github.com/minix1234)!

**Bug Fixes**
* Fix a bug that caused Shairport Sync to hang, but not actually crash, if an on-play or any on-*** script failed.
* Fix a crash that occurred if metadata support is enabled during compilation but turned off in the configuration file. Thanks to [Tim Curtis](https://github.com/moodeaudio) for the report.
* Fix a crash that occurred playing from AirPower on Android. Thanks to [Ircama](https://github.com/Ircama) for the report.

Version 3.3.8d2 -- 3.3.8d0
====
There are absent from the repository -- see `3.3.8rc0` and `rc1` in the `master` branch.

Version 3.3.7d19
====
**Bug Fix**
* Ensure the metadata pipe is created, if necessary, when the `--with-metadata` configuration option is chosen without the `--with-dbus-interface`, `--with-mpris-interface` or the `--with-mqtt-client` options. Super thanks to [Philip Howard](https://github.com/Gadgetoid) for finding this and for developing a possible solution.

**Enhancements**
* Add a default name for the `pipe` backend. If you don't specify a name for the `pipe` backend's named pipe, it will be `/tmp/shairport-sync-audio`. Note that Shairport Sync will need to have write permission to `/tmp` to create the pipe.

Version 3.3.7d18
====
**Bug Fix**
* Make the first output backend in the list of backends the default, and make its name the default output_name. Clang-format everything. Thanks again to [kiwi-ed](https://github.com/kiwi-ed) for bringing this issue to light.

Version 3.3.7d17
====
**Bug Fix**
* Include the word `jack` in the version string if support for [Jack Audio](https://jackaudio.org) is included. Thanks to [kiwi-ed](https://github.com/kiwi-ed) for bringing this issue to light.

Version 3.3.7d16
====
**Bug Fix**
* Fix potential inability to restore synchronisation quickly in certain situations.

Version 3.3.7d15
====
**Enhancements**
* Changed permissions when creating the metadata pipe (and the audio pipe in the `pipe` backend) to `rw-rw-rw-` for compatibility with `snapserver`.

Version 3.3.7d14
====
**Enhancements**
* Fixed a situation where a misleading error message was given if a configuration file cound not be located and `get_coverart` was selected as a command line option. Apart from a misleading message, `get_coverart` and `enable_metadata` should have been enabled automatically where metadata support is included in the build configuration. Thanks to [Craig Fletcher](https://github.com/leakypixel) for the bug report.

Version 3.3.7d13
====
This is 3.3.7rc1

**Enhancements**
* Fixed another bug, related to the bug fixed in 3.3.7d4, where leading zeros were not removed from the DACP ID. Thanks again to [julianc1969](https://github.com/juliandc1969).

Version 3.3.7d12
====
This is 3.3.7rc0

**Enhancements**
* Added the the string `-alac` to the the version string in '-V' if Apple ALAC decoder support is included.

Version 3.3.7d11
====
**Bug Fixes**
* Fixed a number of bugs that prevented Shairport Sync from terminating cleanly in response to the `MPRIS` interface's `Quit()` command. Thanks to [João Gabriel](https://github.com/jgabriel98) for reporting this issue.
* Related to the above, the code used to terminate the application after a fatal error has been cleaned up. It now uses the correct `exit()` call rather than the rather hacky `abort()` call, returning the value of the constant `EXIT_FAILURE` (typically `1`) to the caller .

Version 3.3.7d10
====
**Bug Fix**
* Fixed a bug whereby the start and end of active mode tokens `abeg` and `aend` where not generated or published --  `pend` tokens were being generated instead. Thanks to [minix1234](https://github.com/minix1234) for the bug report and [fix](https://github.com/mikebrady/shairport-sync/pull/1023).

**Enhancements**
* Cleaned up and simplified the code that handles `FLUSH` requests coming from the player. (Debug messages are still a little verbose.)

Version 3.3.7d9
====
**Enhancements**
* Improved timing estimation. Shairport Sync has been using linear least-squares regression to estimate timing drift between the (remote) source clock and the local clock. This technique is now extended to provide an estimate of the remote-to-local clock difference itself. (The remote-to-local clock difference is used to remap the timing of audio frames from the remote device's clock to the local clock.)

  Timing drift estimates are now saved when a sessions ends, keyed to the client IP number. When a new session starts from that IP number, the stored estimate is used until a new estimate can be generated from the new session.

  In practice, the timing techniques in use up to now have been very accurate, but this should result in slightly smoother rates of correction.

Version 3.3.7d8
====
**Enhancements**
* Tidied up the creation and initial opening of pipes. Suppress repeated pipe-opening error messages.
* Tidied up warnings and fatal error messages when log verbosity is zero.
* Cleaned up the code that provides a silent lead-in to play on a back end without synchronisation, e.g. a pipe or `stdout`.
* Added in commented-out code to check the timeliness of the release of audio to a back end without synchronisation, e.g. a pipe or `stdout`. TL;DR – so long as the back end does not block, frames will be released to it not more than one packet (352 frames) late.

Version 3.3.7d7
====
**Enhancements**
* Logs and statistics can now be directed to the system log (default), `stdout`, `stderr` or to a file or pipe of your choice using a new setting, `log_output_to` in the `diagnostics` section of the configuration file. This is very useful when the system log is disabled or diverted.
* Audio data from the `pipe` back end and metadata from the metadata pipe are now written using standard blocking `write` commands rather than a slightly complex non-blocking write function. Pipes are now opened in non-blocking mode and changed to blocking mode when successfully opened.
* Separate threads are now used for each metadata subsystem. Until now, all metadata was processed on a single thread. This included writing to the metadata pipe and the multicast stream and supplying metadata for the `mqtt` interface and for the `dbus` and `MPRIS` interfaces. Unfortunately, that meant that a problem with any one of these subsystems could propagate into the others. Now they all run on separate threads. If one thread blocks, it will not interfere with the other subsystems.

Version 3.3.7d6
====
**Bug Fixes and Enhancements**
* Fixed a bug calculating the instantaneous synchronisation error. This bug could occasionally cause Shairport Sync to lose synchronisation and maybe even to mute for a few seconds before resynchronising. It was caused doing modulo arithmetic incorrectly and it's been there for a while.
* Cleaned up and improved the code to synchronise the first frame of audio. This should result in more accurate and reliable initial synchronisation, usually to under a millisecond, and often to within 20 or 30 microseconds. Syncronisation should improve even when the silent lead-in time is as short as 0.3 seconds or when the `audio_backend_latency_offset_in_seconds` is as much as -1.7 seconds, i.e. when only 0.3 seconds of latency are left when the latency would normally be 2.0 seconds.
* Removed a bug which would affect initial synchronisation if a `FLUSH` command was received from the player at an inopportune time.
* Cleaned up some confused uses of modulo arithmetic.
* Cleaned up the allocation of memory for gathering running statistics – the heap is now used instead of the stack.
* Cleaned up the display of statistics for backends that do not implement active synchronisation, e.g. the `pipe` and `STDOUT` back ends.

Version 3.3.7d5
====
**Bug Fixes and Enhancements**
* Added code to do calculations involving the `audio_backend_latency_offset_in_seconds` and `audio_backend_silent_lead_in_time` settings correctly. Many thanks to [Tucker Kern](https://github.com/mill1000) for discovering a number of bugs associated with this and for [proposing a solution](https://github.com/mikebrady/shairport-sync/pull/1001). This prompted a closer investigation and a number of further improvements were made, and a few "hostages to fortune" removed.
* Cleaned up the `audio_backend_silent_lead_in_time` setting by adding an `"auto"` setting.
* Improved synchronisation accuracy with short silence lead-ins.

**New Feature**
* For the PulseAudio backend `pa`, added a new `server` entry to the `pa` section of the configuration file, allowing you to specify a connection to a remote or a local system PulseAudio instance instead of letting PulseAudio choose. Thanks to [Guillaume Revaillot](https://github.com/grevaillot) for this new feature.

Version 3.3.7d4
====
**Bug Fix**
* The DACP ID looks like a 64-bit number expressed in hexadecimal. It is normally quoted with leading zeroes removed, but in the `_dacp._tcp` service string, leading zeros are not removed from the DACP ID. The bug fix removes those leading zeroes. Thanks to [julianc1969](https://github.com/juliandc1969) for tracking down this bug so tenaciously!

Version 3.3.7d3
====
**Enhancement**
* While a player is active, the DACP port number to which to send remote commands should be broadcast over ZEROCONF/Bonjour. However, if that information is not available, Shairport Sync will now check for it every two seconds.

Version 3.3.7d2
====
**Pesky Changes**
* The underlying timing system has been moved from 64-bit fixed-point time representation (like NTP) to a 64-bit unsigned nanoseconds representation. This should make precisely no difference to the functionality of Shairport Sync but the transition might inadvertently have introduced bugs. Problem reports gratefully received.

**Enhancement**
* The timing software in the `sndio` backend does some extra sanity checking on certain time estimates, it may help a little when running on virtual machines.

Version 3.3.7d1
====
This is 3.3.6 with some documentation updates.

Version 3.3.6d11 = Version 3.3.6
====
**Bug Fix**
* Avoid using `strndup` as it seems older versions of OpenWrt do not recognise it.

Version 3.3.6d10
====
**Bug Fix**
* Brown-bag fix -- do what 3.3.6d9 was supposed to do!

Version 3.3.6d9
====
**Bug Fix**
* Fix a bug in the provision of metadata which would on occasion cause metadata from the previous track to be provided. Thanks to [Tuomas Hämäläinen](https://github.com/tuomashamalainen) and [HiFiBerry](https://github.com/hifiberry) for reporting. This bugfix addresses issue [#972](https://github.com/mikebrady/shairport-sync/issues/972).

Version 3.3.6d8
====
**Bug Fixes**
* Tidy up compilation of the `audio-dummy` and `audio-soundio` back ends when the `-fno-common` flag is used.
* Remove a superflous extra definition of a variable which would cause it to be used uninitialised.

Version 3.3.6d7
====
**Bug Fix**
* Treat the `mper` (Persistent ID of a track) metadata attribute as the 64-bit item that it really is rather than a 32-bit item as hithereto. Output it as a hexadecimal number on the MPRIS and D-Bus interfaces to correspond with the format of the persistent id obtained from AppleScript. E.g:

```
tell application "Music"
	get the {persistent ID} of the current track
end tell
```
resulting in:
```
{"FD84B4B40FA33A85"}
```
The MPRIS `trackid` corresponding to this would be `/org/gnome/ShairportSync/FD84B4B40FA33A85`.

Thanks to to [Scott Simon](https://github.com/zutroy97) for finding a related bug in the [shairport-sync-metadata-reader](https://github.com/mikebrady/shairport-sync-metadata-reader).

**Other Changes**
* Include the `-fno-common` flag in the compilation options to detect incorrect definitions of global variables and multiple definitions of `enum` data types.

Version 3.3.6d7
====
**Bug Fix**
* Make Shairport Sync compile in the forthcoming Fedora 32. Fedora 32 uses GCC-10 which [defaults to `-fno-common`](https://gcc.gnu.org/gcc-10/porting_to.html) exposing a number of issues with Shairport Sync – multiple definitions of some `enum`s and failure to define certain variables as `extern`.

  Version 3.3.6d7 is an attempt to clear up all these errors. Many thanks to [Bill Peck](https://github.com/p3ck) for his ongoing support, for bringing this issue to notice and for developing a workaround. Address issue [#973](https://github.com/mikebrady/shairport-sync/issues/973).

Version 3.3.6d6
====
**Bug Fix**
* Add a `SIGCHLD` handler to remove completed processes that were used to perform and program "hooks" without waiting for completion and thus prevent them from becoming zombie processes. Thanks to [patrickjane](https://github.com/patrickjane) for reporting the bug and for suggesting a solution. Addresses issue [#968](https://github.com/mikebrady/shairport-sync/issues/968).

Version 3.3.6d5
====
**Bug Fix**
* Fix an incompatibilty with Forked Daapd that was causing Forked Daapd to lock up. Thanks to [@tomgadow](https://github.com/tomgadow) and [@ejurgensen](https://github.com/ejurgensen) for their help in finding and (hopefully) fixing this issue. Addresses issue [#953](https://github.com/mikebrady/shairport-sync/issues/953) and [Forked Daapd Issue #870](https://github.com/ejurgensen/forked-daapd/issues/870).

**Enhancements**
* Add some extra examples of using the RemoteControl interface's SetAirplayVolume feature to the shairport-sync-dbus-test-client source.

**Pesky Changes You May Not Be Able To Ignore**
* Renamed the `Server` property in the D-Bus interface to `Client`. The rather strange language in use has it that a player like iTunes is a "client" of the AirPlay device like Shairport Sync, which is therefore considered the "server". The newly-renamed "Client" property is the IP number of the player.

Version 3.3.6d4
====
A fix for issue #953 that wouldn't work.

Version 3.3.6d3
====
**New Features**
* Add `SetAirplayVolume` to the native D-Bus `RemoteControl` interface.
* Add `SetVolume` to the `MPRIS` interface.

**Bug Fixes**
* Hook up the `Volume` property in the `MPRIS` interface.

**Enhancements**
* Modify `RemoteCommand` in the D-Bus interface to return the HTTP status and response in hex.
* Add a few sample commands, including `MPRIS` commands, to the [Sample D-Bus Commands](https://github.com/mikebrady/shairport-sync/blob/development/documents/sample%20dbus%20commands) document.

Version 3.3.6d2
====
**New Features**
* Resampling has been added for the [Jack Audio](http://jackaudio.org) backend. This can be used to up-sample to 192kHz. Thanks to [Pieter De Gendt](https://github.com/pdgendt) for this addition.

Version 3.3.6d1
====
This is release version 3.3.5.

Version 3.3.5d1
====
**Pesky Changes You May Not Be Able To Ignore**
* Renamed the `LoudnessFilterActive` property in the D-Bus interface to `Loudness`. Updated [shairport-sync-dbus-test-client.c](https://github.com/mikebrady/shairport-sync/blob/development/shairport-sync-dbus-test-client.c) accordingly.

**New Features**
* Added to the D-Bus interface the ability to turn on and off convolution, to set the convolution gain and to change the convolution impulse response files. See examples in [sample dbus commands](https://github.com/mikebrady/shairport-sync/blob/development/documents/sample%20dbus%20commands). Note: for these commands to make sense, Shairport Sync must be built with support for convolution and the native D-Bus interface (`--with-convolution` and `--with-dbus-interface`) and fully installed.

**Bug Fix**
* Fixed a bug whereby if you enabled the Loudness filter via the D-Bus interface, the output disappeared until you changed the volume.


Version 3.3.5d0
====
**Bug Fix**
* Fixed a crashing bug when output format `"S24"` was chosen. Fixes the issue reported by [artenverho](https://github.com/artenverho) in [#927](https://github.com/mikebrady/shairport-sync/issues/927).

Version 3.3.4
====
This version did not appear on the `development` branch -- it fixed a small compilation error.

Version 3.3.3d11
====
**Bug Fix**
* Fixed a crashing bug -- if a plain HTTP packet (in fact, any packet that didn't have an RTSP-style header) was sent to the TCP session port (usually port 5000), Shairport Sync would crash! Thanks to @[dubo-dubon-duponey](https://github.com/dubo-dubon-duponey) for reporting. Fixes [#921](https://github.com/mikebrady/shairport-sync/issues/921).

**Pesky Change**
* The setting `retain_cover_art` is now part of the `diagnostics` group. Set it to `"yes"` to retain cover art cached by the D-Bus, MPRIS or `mqtt` interfaces. Your directory may fill up if you leave it set!

Version 3.3.3d9
====

**Enhancements**
* Expose a `metadata` setting related to handling cover art:
  * The setting `cover_art_cache_directory` allows you to specify where cover art files will be cached if Shairport Sync has been built with native D-Bus, MPRIS or MQTT support. The default is `/tmp/shairport-sync/.cache/coverart`. If you set it to an empty list: `""`, caching is disabled. This might be useful in, say, an embedded device, or wherever you want to minimise file writing.

Version 3.3.3d8
====

**Bug Fix**
* Fix a bug in the resend request logic.


**Enhancement**
* Expose the settings controlling the resend request logic. The new settings are in the `general` section:
  * `resend_control_first_check_time` is the time allowed to elapse before a packet is considered missing, defaulting to 0.1 sec. UDP packets don't always arrive in order and they don't need to be re-requested just because they arrive out of sequence. Essentially, therefore, this parameter is to prevent needless resend requests for packets that are already in transit.
  * `resend_control_check_interval_time` is the interval between repeated requests for a missing packet, defaulting to 0.25 seconds.
  * `resend_control_last_check_time` is the time by which the last check should be done before the estimated time of a missing packet's transfer to the output buffer, defaulting to 0.1 seconds. In other words, if a packet is still missing 0.1 seconds before it is due to be transferred to the DAC's output buffer, don't bother asking for a resend.


Version 3.3.3d7
====

**Duh**
* Versions 3.3.2d3 to 3.3.2d6 should have been labelled 3.3.3d\<something\>.

**Enhancement**
* Rewrite the logic for identifying missing packets of audio and for asking for resends. It seems more robust -- there was a suspicion of the previous logic that resend requests were not made for some missing packets. In addition, requests for resends of continuous sequences of packets are rolled into one.

Version 3.3.2d6
====
**Enhancements**
* Normalise the `'pvol`' volume outputs so that when both the software and hardware attenuators are in use to extend the overall attenuation range, the maximum output level corresponds to the maximum output level of the hardware mixer.
* Add the option of including the file and line number of each log entry's source. The option is on by default and is settable in the configuration file and in the `dbus` interface.

**Bug Fixes**
* Fixes an error whereby the `'pvol'`volume metadata was no longer sent if Shairport Sync was configured to ignore volume control information coming from the audio source. Addresses issue [#903](https://github.com/mikebrady/shairport-sync/issues/903). Thanks to [Jordan Bass](https://github.com/jorbas) for reporting the regression and for identifying the commit and code in which the regression occurred.
* Fixes a compilation error if including the MQTT client -- thanks to [exoqrtx](https://github.com/exoqrtx) for reporting.
* Fixes a compilation error if including the MPRIS interface.
* Ensure the hardware mixer of an `alsa` device is detected and initialised before responding to the first volume setting.

Version 3.3.2d5
====
**Enhancement**
* Improves the detection of the remote services available when an audio source is playing. If the source is minimally compatible, e.g. iOS, Shairport Sync's `org.gnome.ShairportSync.RemoteControl` native `dbus` interface becomes "`available`". If the source is iTunes, then the `org.gnome.ShairportSync.AdvancedRemoteControl` interface also becomes `available`. Artwork, metadata, status and limited remote control facilities are accessible through these interfaces when they are in the `available` state. Follows on from issues raised in [#890](https://github.com/mikebrady/shairport-sync/issues/890). Thanks again to [exoqrtx](https://github.com/exoqrtx) for bringing these issues to light and for testing.

**Bug Fix**
* Fixes an issue that occured in Ubuntu 14.04 – the `shairport-sync` daemon would silently die after a fairly long period. It typically happened just after a DHCP address was renewed. The problem seemed to be related to having more than one `avahi` threaded polling loop (though this isn't documented anywhere). The fix was to consolidate the `avahi` stuff down to one threaded polling loop. Addresses issue [#895](https://github.com/mikebrady/shairport-sync/issues/895). Thanks to [Hans (the) MCUdude](https://github.com/MCUdude) for reporting and for initial troubleshooting.

Version 3.3.2d4
====
**Bug Fixes and Enhancements**
* Fixes and enhancements to the MPRIS and native D-Bus interfaces. In particular, situations where artwork is absent are better handled, and the remote interface and advanced remote interface `availability` properties should be more resilient in the face of network problems. Addresses issue [#890](https://github.com/mikebrady/shairport-sync/issues/890). Thanks to [exoqrtx](https://github.com/exoqrtx) for bringing these issues to light and for testing.

Version 3.3.2d3
====
**Bug Fix**
* Fixes a potential crash when an incomplete `fmtp` parameter set is sent by the requesting client. Thanks to [Angus71](https://github.com/Angus71) for the fault report and for the repair.

**Enhancement**
* Instead of returning `EXIT_FAILURE`, return `EXIT_WITH_SUCCESS` on early exit with either "version" (`–version` or `-V`) or "help" (`–help` or `-h`) arguments. Thanks to [Henrik Nilsson](https://github.com/henriknil) for the patch.

Version 3.3.2d2
====
**Bug Fix**
* Fix a double-free memory bug. Thanks to [Chris Boot](https://github.com/bootc) for reporting this bug, which came through the Debian BTS: https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=925577. The fix is different from the suggested patch.

Version 3.3.2d1
====
**Enhancements**
* Allow `disable_standby_mode` to operate on any output device, not just real hardware devices. If there are too many underruns while outputting silence, the disable standby mode will be turned off.

The Inevitable Version 3.3.1rc1
====
**Bug Fixes**
* Fix a bug in the MQTT documentation and add sanity checking for the port chosen -- thanks to [David Crook](https://github.com/idcrook).
* Fix a bug that caused manual format and rate setting to be ignored -- thanks to [Jörg Krause](https://github.com/joerg-krause).
* Add missing support for format settings S24_LE, S24_BE, S32_LE and S32_BE.
* Fix a bug that caused dither to be too loud.
* Fix error message for invalid `disable_standby_mode` choice -- thanks to [Tim Curtis](https://github.com/moodeaudio) at [Moode Audio](https://moodeaudio.org).

Version 3.3rc8
====
**Enhancements**
* Add a list of alsa hardware devices found to the alsa backend help section in the `shairport-sync -h` text. (Doesn't work for alsa on FreeBSD/OpenBSD.)
* Always place D-Bus access policy documents in `/etc/dbus-1/system.d` in Linux, but adhere to the standard for FreeBSD/OpenBSD.


Version 3.3rc7
====
**Enhancements**
* Improvements to the `MQTT` client -- extra diagnostic messages at run time and at configuration time.

Version 3.3rc6
====
**Pesky Change You Can't Ignore**
* The `while_active` setting in the new `disable_standby_mode` has been renamed to `auto`.

**Enhancements**
* Automatic bit-depth and speed selection for `alsa` output devices has been added and is the default. The greatest bit depth available is selected, and the lowest multiple of 44,100 frames per second is chosen.
* Improve support for big-endian devices by adding support for `S16_LE`, `S16_BE`, `S24_LE`, `S24_BE`, `S32_LE` and `S32_BE` formats.

Version 3.3rc5
====
* Had a bug, sorry -- please go to 3.3rc6 or later.

Version 3.3rc4
====
**New Feature**
* Support is now included for the automatic selection of the interpolation method. If support for `soxr` interpolation is included at configuration time (using the `--with-soxr` at the `./configuration...` step), then `auto` interpolation is made the default interpolation mode. During startup, the time it takes to do two `soxr` interpolations is calculated -- this, in milliseconds, becomes the `soxr_delay_index`. If the `soxr_delay_index` is less than or equal to a new `general` group setting called `soxr_delay_threshold` (default 30), then `soxr` interpolation will be chosen in `auto` mode; otherwise, `basic` interpolation will be chosen.

Version 3.3rc3
====
**Minor Enhancement**
* Calculate the length of the first sequence of frames of silence preceding the audio at the start of a play session a little more carefully to make it somewhat longer. This should make output devices that operate with a fairly large buffer a little less likely to underrun at the start of a session.

Version 3.3rc2
====

**New Feature**
* Shairport Sync can now accept AirPlay streams containing uncompressed PCM -- specifically, 16-bit network-endian interleaved stereo, 44,100 frames per second. There is a restricton -- the packets of audio must be 352 frames long.

**Bug Fix**
* Compatibility has been restored with virtual ALSA devices. Sometimes, an ALSA output device isn't actually a real hardware device -- for example, if PulseAudio is installed in your system, the "default" ALSA output device may in fact a virtual device that provides a route into the PulseAudio sound server for audio from ALSA-compatible applications. Such virtual devices don't always provide the precise delay timing that Shairport Sync uses. The bug fix is to fall back to the standard calls when precise delay timing is not available.
* If precise delay timing data is not available, the `disable_standby_mode` is turned off, as it relies on high precision timing.

Version 3.3rc1
====

**New Feature**
* If support for `soxr` interpolation is included at configuration time, then `soxr` interpolation is made the default at run time. (This was later changed -- see 3.3rc4.)
* If support for the Apple ALAC decoder is included at configuration time, then the Apple ALAC decoder is made the default at run time.
* If support for metadata is included at configuration, then metadata and cover art are both enabled by default at run time.
(Metadata is enabled if you configure support for metadata, the dbus interface, the MPRIS interface or the MQTT client.)

**Enhancements**
* MQTT publishes `active_start` and `active_end` events when the active state is entered and exited.
* Add correction to fix for tinysvcmdns CVE-2017-12130.
* Partly update man pages.

Version 3.3rc0
====

Version 3.3rc0 is 3.3d56

Version 3.3d56 to Version 3.3d40
====
**New Features**
* Two new external program/script hooks – `run_this_before_entering_active_state` and `run_this_after_exiting_active_state` are provided for when the system goes active or inactive. Background: Many users use the `run_this_before_play_begins` program hook to turn on an amplifier and the `run_this_after_play_ends` hook to turn it off. A big problem is when another play session starts immediately after a play session ends, causing the amplifier to be switched off and then on again very quickly. This happens, for example, when a YouTube clip ends and the next one begins. To get around this, the concept of an *active state* covering a sequence of play sessions is introduced. When a play session starts, the system goes active, i.e. it enters the active state. When the play session ends, the system remains active for a period determined by the `active_state_timeout` setting, 10 seconds by default. If another play session starts before the period elapses, the system stays active; otherwise the system goes inactive -- it leaves the active state. The two new hooks mentioned above can be used to execute programs/scripts when the system goes active or inactive.
* A new `alsa`-only `disable_standby_mode` setting, for controlling the Disable Standby feature, can be set to `always`, `while_active` or `never`. The `always` setting is recommended for systems where the output device is dedicated to Shairport Sync. The "Disable Standby" state iself can be set or cleared via the D-Bus interface `DisableStandby` property.
* A new command-line option, `-u`, directs logging to STDERR rather than the system log. Useful when you compile Shairport Sync without `libdaemon` using the `--without-libdaemon` configuration option.

**Enhancements**
* Changes to the Jack Audio back end. The back end for Jack Audio, `audio_jack.c`,  has been extensively rewritten by [Jörn Nettingsmeier](https://github.com/nettings) in a way that is more in keeping with the Jack Audio style. It uses native Jack Audio lockless buffers and offers autoconnect facilities that the previous version didn't have. Many thanks to him.
* The volume-control software has been completely rewritten. From a user's point of view, the result should be a much smoother response to volume control changes, free from artefacts. It is now also possible to combine the hardware mixer and the software attenuator in two ways -- giving priority to the software mixer or giving priority to the hardware mixer. see the new `volume_range_combined_hardware_priority` setting in the `general` section opf the configuration file.
* The muting/unmuting code has been rewritten to be simpler and more consistent.
* In the `alsa` backend, new `play()` and `delay()` functions minimise the use of `snd_pcm_recover()` to prevent unnecessary resets of the output DACs.
* In the `alsa` backend driver, hardware isn't accessed until the first time it is needed. That is, when Shairport Sync starts up, it no longer needs to access the device momentarily. Instead, it waits for the first use.
* The `libdaemon` library is now an optional build. It is not necessary for `systemd` systems and can be omitted. Use the `--without-libdaemon` configuration option to leave it out.

**Bug Fixes**
* The `mdns-external` method used to advertise the Shairport Sync AirPlay service on ZeroConf is now an optional build and is omitted by default. Previously it was included with `--with-avahi` and could not be included on its own.
* A number of memory leaks associated with the use of OpenSSL have been fixed.
* Use CLOCK_RUNTIME in place of CLOCK_MONOTONIC when doing synchronisation timing using the alsa library prior to 1.0.28. Necessary for compabibility with OpenWRT Barrier Breaker.

Version 3.3d39 to Version 3.3d38
====

**New Feature**

A new feature called *Disable Standby* keeps the output DAC in the play state all the time and helps to remove some annoying clicking / popping noises. It is really pretty impressive, especially combined with fixes to the dithering code described below. If you can enable 24- or 32-bit audio output to your output device, you can get even better results.

This is an ALSA-specific attempt to remove the annoying low-level clicking sounds that some output devices make just when they start processing audio and sometimes when they stop. Typically a faint click might be heard just before a play session starts or just before audio resumes after a pause. Similarly, a faint click can sometimes be heard just after a play session ends.

It is extremely difficult to remove these clicks completely from the hardware, so this new feature ensures that the output device avoids situations where these clicks might be generated by *always* playing audio. To accomplish this, if Shairport Sync isn't actually playing anything, audio frames consisting of silence are sent to the output device, keeping it playing. Apart from the initial startup transition, the output device never stops playing and thus never transitions to and from standby mode, avoiding the possibility of generating associated audio disturbances.

To enable this feature, a new `alsa` group setting with the name `disable_standby_mode` is available. (If you do a full `$ sudo make install`, a new sample configuration file with this setting in it is installed at `/etc/shairport-sync.conf.sample` or `/usr/local/etc/shairport-sync.conf.sample`.)

A downside to this feature is that the output device totally dedicated to Shairport Sync. For this reason, this new feature is disabled by default.

Note -- this setting is likely to change. It will probably be necessary to modify it somehow to enable it to be used in integrated systems. Your feedback would be welcome.

**Enhancements and Bug Fixes**

* At present, Shairport Sync adds dither to the audio (a) if the built-in software-based volume control is used, (b) if the audio is mixed to mono or (3) if there is a reduction in sample size, say from 16- to 8-bit. The code for generating dither has been much improved. Due to a poor pseudo-random number arrangement, the dither noise didn't sound like white noise -- now it does. In addition, if dither is to be added, it is also added to the silence inserted just prior to the start of play, and is also added to the silent frames used to prevent the output device from going into standby mode, if selected.

Version 3.3d37 to Version 3.3d22
====

These updates are about stability.

**Enhancements and Bug Fixes**

With great help from [gibman](https://github.com/gibman) — see [#772](https://github.com/mikebrady/shairport-sync/issues/772) for the gory details — a myriad of issues have been identified and fixed. In particular, [gibman](https://github.com/gibman) shared an automated way of stress-testing Shairport Sync, and this has resulted in the detection of many bugs.

And so, with apologies to Shakespeare, we have taken up arms against a sea of troubles, and by opposing we have ended them. It is hoped that the result is considerably more stable and can better withstand the, uh, slings and arrows of outrageous fortune.

Here is a flavour of some of the issues addressed:
* Replace the existing watchdog, which only offered partial coverage, with a much more robust thread-based watchdog.
* Allow a reported remote processing time of zero.
* Fix a logical error that could cause an endless loop during a flush.
* Ensure a player thread is correctly initialised before allowing it to be cancelled cleanly.
* Ensure the player thread always has a cancellation point so that it should always be possible to ask it to terminate.
* If a play session is interrupted, wait for it to terminate for up to three seconds.
* If a play session is interrupted, ensure the new session uses a different set of UDP ports. This is to ensure that data from the interrupted session – which might be still in transit – doesn't enter the new session.
* Make all ALSA command sequences un-cancellable. This is to ensure that ALSA subsystem will not be left in a partially-initialised state if Shairport Sync terminates abruptly.
* When a connection is terminated abruptly, ensure that all the UDP ports is use are closed properly.
* Impose timeouts on both reading and writing to the supervisory RTSP connection governing a session.
* When closing the RTSP connection due to an error, close it immediately, without waiting for a full TCP handshake, because, if the other end has erred, the handshake may never come.
* Fix a parameter initialisation error in a situation where there is no hardware mixer.
* Fix an MQTT-related crash by ignoring unrecognised commands.
* Fix a compilation error and a warning when using the `--with-convolution` configuration option.

**New Feature**
* A new `run_this_if_an_unfixable_error_is_detected` (in the `sessioncontrol` group of settings) program hook is provided. At the moment, two conditions can trigger this. The first is if the watchdog is unable to terminate a play session. The second is if the output device stalls for a long period. Both conditions can be caused by malfunctioning DACs. The external program could, for example, reboot the device.

Version 3.3d21
====
**Enhancements**
* Use `/dev/urandom` rather than `/dev/random` as a source of some kind of randomness for the cryptographic "nonce" used in AirPlay password exchange, as `/dev/random` blocks occasionally. Please see [here](https://unix.stackexchange.com/questions/324209/when-to-use-dev-random-vs-dev-urandom) for a discussion of the merits of both. The effect of `/dev/random`'s blocking on Shairport Sync was to make the source of randomness somewhat less random. (By the way, you should never use an important password as an AirPlay password for Shairport Sync -- it is stored in Shairport Sync's configuration file in plain text.)

Version 3.3d20
====
**Enhancements**
* The code that deals with packet loss resulting from network problems has been completely rewritten. The reason is that, while the old code worked quite well, it could be overwhelmed if the network problems were very serious. The new code is simpler and more robust in testing so far. This code interacts with the code for flushing audio before and after a play session, so it may have introduced regressions.
* The build instructions have been changed to avoid compiler warnings coming from automatically-generated code produced for the D-Bus-based interfaces.
* Warnings are now logged if the ALSA subsystem fails to recover properly after an error has been cleared.

**Bug Fix**
* A compilation problem in OpenBSD has been fixed by changing the order of some include files.

Version 3.3d16
====
**Enhancement**
* Extend the range of `audio_backend_latency_offset_in_seconds` to ± 1.75 seconds. Note that no sanity checking of any kind is done on this – if it is too large the program may simply crash.

* Pay more attention to errors that may occur when asking for the DAC delay just before play starts. For diagnostic use.

* Formatting of the settings file `shairport-sync.conf` has been fixed, thanks to the work of [roblan](https://github.com/roblan).

Version 3.3d15
====
**Enhancement**
* Adjust the latency calculation to accommodate changes in iOS 12 and AirPlay connections from macOS Mojave. Thanks to [artenverho](https://github.com/artenverho) who first reported the issue.

Version 3.3d14
====
**Bug Fix**
* Fix a problem when calling the program to be run when the volume control is changed. Thanks to [shaven](https://github.com/shaven) for the report.

Version 3.3d13
====
Reverted to 3.3d11 -- the modification in 3.3d12 was not needed and has been removed.

Version 3.3d11
====
**Enhancement**
* Add a configuration option to specify the PulseAudio output sink. An extra option for the `pa` backend to allow the specification of the audio sink. Thanks to [Maciej Wilczyński](https://github.com/mLupine) for his work on this.

**Bug Fix**
* Remove unrecognised options from FreeBSD and OpenBSD compilation instructions.

Version 3.3d10
====
Modify the `jack` interface to ignore the latencies of any ports connected to the `jack` end. This is because it does not seem appropriate to try to compensate for the delays in the `jack` system. Not sure if this is the correct approach... Maybe it's better to allow the user to select no compensation, compensate for the lowest latency or compensate for the highest latency; it seems unnecessarily complex, not really the business of Shairport Sync.

Version 3.3d9
====
Many changes to compilation and linking flags. Stop using `HAVE_*` flags except where necessary, use `CONFIG_*` for optional stuff, use `HAS_*` for immediate definitions to be used during configuration, use `USE_*` for Automake definitions. Probably introduced bugs, sigh.

Version 3.3d8
====
**Bug Fix**
* Ensure the compiler flag `HAVE_LIBSOXR` is defined if the `--with-soxr` configuration flag is used and `pkg-config` is in use.

Version 3.3d7
====
* Remove the "rolling" requirement for Jack output to be sent.
* Set the default buffer size for Jack to 0.5 seconds. This may be temporary!

Version 3.3d6
====
An improved [Jack Audio](http://jackaudio.org) backend. Testing notes:
* At the `./configure` step, include `--with-jack`. This should not require any extra packages or libraries when it is being compiled on an AVLinux machine. No need for any other backend to be included, so you can omit `--with-alsa`, etc.
* Tested on the latest AVLinux only.
* Jack Audio will not run correctly on a virtual machine; it will only work properly on a real device.
* A new set of configuration options is present in the configuration file.
* The Shairport Sync backend will attempt, every second, to open a client on the Jack server until it succeeds. This behaviour can be stopped or the interval extended with the `jack` `auto_client_open_interval` configuration option.
* Once a Jack client has been opened by the Shairport Sync backend, it will remain open. This behaviour can be changed with the `jack` `auto_client_disconnect` configuration option.
* Other Jack congfiguration options include the ability to change the name of the client – `client_name` – and the names of the channels – `left_channel_name` and `right_channel_name`.

Testing needed -- these arrangements may not conform to the norms of the Jack community, so all feedback gratefully received.

Some information, e.g. the true output rate, not implemented yet.

Version 3.3d5
====
* Introducing a very rough implementation of a Jack Audio backend. The JACK Audio Connection Kit "provides a basic infrastructure for audio applications to communicate with each other and with audio hardware. Through JACK, users are enabled to build powerful systems for signal processing and music production."

Version 3.3d4
====
* Sometimes `libsoxr` is built to rely on `libavutil`. With the present changes to `configure.ac`, if `libavutil` is present, link to it when linking to `libsoxr`, but if it's not present, don't link to it when linking to `libsoxr`.
* Add `pgk-config` support to `libsoxr` selection.

Thanks to [Jörg Krause](https://github.com/joerg-krause) for identifying these issues and proposing fixes.

Version 3.3d3
====
* Modify the code that synchronises the Shairport Sync system's clock with the source clock to try to take account of the sources's nominal rate, which (oddly, e.g. iTunes on a Mac) might not be exactly 44,100 fps. There may be a 32-bit unsigned overflow error here somewhere!

* Try to interpolate for the measured drift between the standard three-second timing snapshots. These changes make a very slight difference from time to time, of the order of microseconds, and it's not clear yet how reliable the drift interpolation is.

* Add some extra fields to the `statistics` output, including nominal source rate, actual input rate, actual output rate, source clock drift, calculated correction needed in ppm. All these numbers agree to a high degree, but the generation of them is fragile and susceptible to errors when there are problems like underrun, and they are not actually running averages, which would be genuinely useful. But they show promise!

**Bug Fixes**
* Fix a bug in the `dbus` native interface which would silently switch `soxr` interpolation to `basic`.
* Fix a mutex lock bug in the metadata hub. No known effects.

Version 3.3d1
====
Internal changes are being made for version 3.3 to avoid using `SIGUSR1` and `pthread_kill` to stop threads; the standard `pthread_cancel` and friends are being used instead. This should lead to more reliable and orderly cancellation of sessions and threads. However, it is quite a complex change, so bugs may have been introduced or reactivated. Lots of testing needed.

**Bug Fix**
* Fix an arithmetic overflow in frame rate calculations that could occur after 2^32 frames – approximately 27 hours at 44,100 frames per second.

Version 3.3d0
====

**New Features**
* Frame rates! Exact input and output frame rates are now included in the `statistics` output. The figures generated are averaged over the entire play session since the last pause/resume, if any, so they should settle down and become more accurate over a long play session, say a couple of hours. Timing is relative to `CLOCK_MONOTONIC`. When your system is connected for an appreciable period to network time, e.g. using an NTP client, `CLOCK_MONOTONIC` is adjusted ("conditioned") to keep time extremely accurately. The calculated output frame rate should be very accurate. The calculated input frame rate will vary considerably over short intervals due to network conditions, but over a long play session it should also become very accurate.
Timing is done from the start of the play session, or from the resumption of play following a pause. Note that while some audio sources pause and resume between tracks, both iTunes on the Mac and the Music app on iOS play all the tracks on a playlist without pause (so long as the tracks are downloaded and present in the device in time).
* Add the command `quit` to the MPRIS and the D-Bus interfaces. The main motivation for this is that it makes it easier to search for memory leaks.

**Bug Fixes**
* A number of memory leaks have been identified and removed.
* A bogus warning about using the deprecated `general` `statistics` options has been fixed.

Version 3.2d67
====
This is equivalent to 3.2.1.

**Bug Fix**
* Fix a bug causing a crash when `soxr` interpolation was enabled and a play session was stopped. This bug overwrote incorrect memory locations, so depending on what it overwrote, may have been responsible for other inexplicable crashes. Thanks are due to [hanaguro](https://github.com/hanaguro), [FnasBas](https://github.com/FnasBas), [priitohlo](https://github.com/priitohlo), [David Krmpotić](https://github.com/davidhq) and [artenverho](https://github.com/artenverho).

Version 3.2.d66
====
This is equivalent to 3.2.

Version 3.2.d65
====
**Modification**
* The algorithm for generating resend requests has been made somewhat more gentle -- a little less insistent.


**Bug Chasing**
* Some very elementary bounds checking on the ANNOUNCE packet is performed: that it is 1024 bytes or less in size and that line lengths remaining after parsing do not turn negative when they should remain positive or be exactly zero.

Version 3.2.d64
====
**Enhancement**
* An extra property has been added to the RemoteControl section of the native Shairport Sync D-Bus interface. It is the "progress" metadata emitted from time to time by the AirPlay source and consisting of RTP timestamps for the start of the current play sequence, the current play point and the end of the play sequence.

**Bug Fix**
* Code to monitor the amount of time a mutex lock request took is improved.

Version 3.2.d63
====
**Enhancement**
* An [MQTT](https://en.wikipedia.org/wiki/MQTT) client interface is introduced, with support for metadata and for some remote control commands, thanks to the work of [Till Zimmermann](https://github.com/tillz). MQTT is a fairly often used protocol in FOSS home-automation projects (as well as in commercial ones), and as Shairport Sync is often used in these setups, this adds client support for this protocol.

Version 3.2d62
====
**Bug Fix**
* Restore compatibility with Synology AudioStation/5.2. Thanks to [Jörg Krause](https://github.com/joerg-krause) for identifying both the issue itself and the likely location of the fix needed. It's not clear if the problem lies with Shairport Sync or Synology – the fix was to ensure that an RTSP reply was sent in one `SEND` call, which shouldn't be important.

Version 3.2d61
====
**Bug Fix**
* Add a pthread_cancel cleanup routine to put_packet to unlock the player rw mutex when cancelled. Sincere thanks to [David Krmpotić](https://github.com/davidhq) for help investigating this.

Version 3.2d57, 3.2d58, 3.2d59, 3.2d60
====
**Bug Chasing**
* Debug messaging has been enhanced when a `TEARDOWN` message is received.
* Check both timed and untimed mutex locks and give estimates of how long to wait. Generate debug messages when appropriate.
* Unlock the `ab_mutex` when sending a resend request.

Version 3.2d56
====
**Enhancement**
* The `mute_using_playback_switch` setting in the `alsa` group has been replaced by a `use_hardware_mute_if_available`, which defaults to `"no"`. This new setting controls both the playback switch method and the magic volume level method for doing hardware-supported muting.
If hardware muting is used, the output device will be muted when Shairport Sync releases it, potentially causing problems for other audio programs using the device. To avoid this, `use_hardware_mute_if_available` is disabled by default.

**Bug Chasing**
* A debug message has been enhanced when a `TEARDOWN` message is received.

Version 3.2d55
====
**Bug Fix**
* Flush requests that are received while the player is being initialised or deleted are now ignored. This should prevents two potential (but never seen) lock possibilities.

Version 3.2d54
====
**Bug Fix**
* Prevent packets of audio being added when the play session has ended and the player is being deleted. They are now simply dropped.

Version 3.2d53
====
**Bug Fix**
* Fix an incompatibility with Cygwin 64. It seems that the handling of signals in Cygwin is different to regular "native" Unix/Linux platforms. So, this is an attempt to use `pthread_cancel` calls where possible. In principle it should simplify thread management considerably, so let's see how it goes.
* Fix an off-by-one error that was generating excessive resend requests.

Version 3.2d51
====
* Fix a small bug locking and unlocking the read-write mutex. It might (and in principle, it could) cause a crash.

Version 3.2d50
====
* Add a read-write mutex lock to the player thread so that it can't be accessed while it's being created and initialised or while it's being torn down and destroyed. Make external accessor functions acquire a read lock for access. It's "A Good Thing" and it might help the seg fault problem, which I'm afraid still can't reproduce...

Version 3.2d49
====
* A few extra debugging messages to try to locate where a segfault is occurring.

Version 3.2d48
====
**Bug Fix**
* A seg fault possibly caused by a dangling pointer has been addressed by making the UDP sockets blocking rather than non-blocking. A two millisecond limit has been put on the sendto calls.

Version 3.2d47
====
**Bug Fix**
* A very subtle bug that would occasionally cause a loud 80 millisecond buzz when a new track was selected has been located and fixed. Thanks to [mistipen](https://github.com/mistepien) and [artenverho](https://github.com/artenverho) for help with this.

Version 3.2d46
====

Update [TROUBLESHOOTING.md](https://github.com/mikebrady/shairport-sync/blob/development/TROUBLESHOOTING.md) to correspond to 3.2RC6.

Version 3.2d45
====
**Enhancement**
* Restore the old method for calculating latency for older AirPlay sources: an AirPlay source displaying an AirPlay User Agent string version of 353 or older -- corresponding to iOS 11.1.2 or older -- will add an extra 0.25 seconds to the latency requested. This seems to be right.

Version 3.2d44
====
**Bug Fixes**
* When errors occur sending resend requests, back off for 0.5 seconds, rather than 10 seconds, and make the code actually work.
* Make the configuration options for including the dbus interface and the mpris interface be `--with-dbus-interface` and `--with-mpris-interface` as expected, not `--with-dbus` and `--with-mpris` as they actually were.

Version 3.2d42
====
This version just brings some small changes made to the Release Candidates back into the `development` branch.

Version 3.2d41
====
**Bug Fixes**
* Fixed a hitherto-silent bug that prevented Shairport Sync working properly with AirAudio. According to the [unofficial standard](https://nto.github.io/AirPlay.html), requests for resending missing packets come from the control port; however, up until now, Shairport Sync sent them on the audio port. All AirPlay sources responded to these improperly-delivered requests except AirAudio, which discards requests that do not come from the correct source. Fixed now, with many thanks to [funtax](https://github.com/funtax) and to [Janusz Kowalczyk](https://github.com/kowalcj0) for bringing the issue to the fore.

Version 3.2d40
====
**Bug Fixes**
* A number of serious and long-standing bugs have been identified and fixed in the threads that handle audio, control and timing packets. Specifically, if UDP reception or transmission errors occurred (a rare occurrence on a good network, but possible on noisy or congested networks), the threads would quit. In this way, an error on the reception of the first control packet could mute an entire play session.

**Enhancements**
* The code used to request the retransmission of missing audio packets has been significantly improved.

Version 3.2d39
====
**Enhancements**
* An extra diagnostic to artificially drop UDP packets to simulate a noisy network has been added. Set the proportion of packets to be dropped in the `diagnostics` section of the configuration file using the tag `drop_this_fraction_of_audio_packets`. The value should be between 0.0 and 1.0. E.g. a value of 0.001 would mean one packet in a thousand would be dropped, on average. Look in the sample configuration file `/etc/shairport-sync.conf.sample` for the added entry.
* Minimal compatibility with AirAudio has been added / restored. There remains a question about error correction for handling UDP packet loss. Thanks to [Janusz Kowalczyk](https://github.com/kowalcj0) for reporting the issues.

Version 3.2d35
====
**Enhancements**
* More cleaning up of the D-Bus and MPRIS interface messages -- they are quieter now.

**Bug Fix**
* Thanks are due to [yejun](https://github.com/yejun) for noticing and proposing a fix for the bug that dithering is left on when the volume control is ignored. Audio samples should pass through without alteration. (The fix was already in the `development` branch though.)

Version 3.2d34
====
**Enhancements**
* Better compatibility with TuneBlade -- Shairport Sync honours the latency settings properly now.
* Big cleanup of D-Bus and MPRIS interface messages -- it's a lot less noisy. More to be done here.
* The brokey YouTube iOS app, which generates a great deal of invalid metadata (do they even know?), is handled a bit better. If valid metadata is there, Shairport Sync can process it.
* `clip` and `svip` messages are now only emitted for a play connection, not for all connections (e.g. connections that just enquire if the service is present).
* `pfls` and `prsm` messages are less frequent, especially when a play session starts.

**Other Developments**
* Shairport Sync now uses about an extra half megabyte of RAM for compatibility with TuneBlade's option to have a very long latency -- up to five seconds.

Version 3.2d33
====
A new metadata token -- `'pffr'` -- is emitted when the First Frame of a play session has been Received. Not sure we'll keep it...

Version 3.2d30
====
**Enhancements**
* A "native" D-Bus Remote Control permits remote control of the current AirPlay or iTunes client. It includes status information about whether the remote control connection is viable or not, i.e. whether it can still be used to control the client.
A remote control connection to the audio client becomes valid when the client starts AirPlaying to Shairport Sync. The connections remains valid until the audio source deselects Shairport Sync for AirPlay, or until the client disappears, or until another client starts AirPlaying to Shairport Sync. It is likely that a time limit will be put on this, so that after, say, 30 minutes of inactivity, the remote control connection will be dropped.

Version 3.2d29
====
**Enhancements**
* CAR INSTALL and OPENBSD notes added.
* A barebones "native" D-Bus interface for Shairport Sync permitting control of some diagnostic settings. To be expanded...
* A partly implemented MPRIS control interface including  play/pause/next/previous/volume and some metadata.
* Remote control of an iTunes source including play/pause/next/previous/volume.
* Remote control of an AirPlay source including play/pause/next/previous.

Version 3.2d28
====
Continuing the experiments with D-Bus and related DACP support. In this revision, an attempt is made to control the amount of scanning the system does to maintain up-to-date information about a DACP source. As before, please note that the implementation is likely to change greatly or be removed at any time.

**Enhancements**
* Barebones support added for OpenBSD compilation.
* Only ask for missing packets to be resent once, and if any error occurs making the request, stop for 10 seconds.
* Include the `-pthread` flag -- including the pthread library with `-lpthread` isn't always enough.

Version 3.2d26
====

**Enhancements**
* Improvements in the documentation relating to scripts -- thanks to [Niklas Janz](https://github.com/Alphakilo).
* Add optional timing annotations to debug messages -- see the new settings in the diagnostic stanza of the configuration file.

**Bug Fixes**
* Ensure the TEARDOWN of a play session is not delayed by a long sleep timer.
* Allow more than one ANNOUNCE packet for the same play session. Honour the settings in the most recent one.
* Move the creation and calling of a player thread from the SETUP handler to the RECORD handler.
* When closing an ALSA sound device, don't wait for any remaining audio to be output with `snd_pcm_drain`; instead, just drop all remaining frame using `snd_pcm_drop`.
* TEARDOWN should complete in less than 50 ms.

Version 3.2d25
====
**Enhancement**
* Better handling of missing timing packets.
* Improved resnchronisation logic should improve performance with slow-to-download YouTube videos.
* Shairport Sync will now log an unexpectedy dropped or faulty RTSP connection. This might be useful on noisy networks.

Version 3.2d24
====
**New Feature**
* A new `general` option `volume_control_profile`, for advanced use only, with two options: `"standard"` which uses the standard volume control profile -- this has a higher transfer profile at low volumes and a lower transfer profile at high volumes --  or `"flat"` which uses a uniform transfer profile to linearly scale the output mixer's dB according to the AirPlay volume.

Version 3.2d23
====
Continuing the experiments with D-Bus and related DACP support.

**Enhancement**
* Some DACs have a feature that the lowest permissible "attenuation" value that the built-in hardware mixer can be set to is not an attenuation value at all – it is in fact a command to mute the output completely. Shairport Sync has always checked for this feature, basically in order to ignore it when getting the true range of attenuation values offered by the mixer.
However, with this enhancement, Shairport Sync can actually use this feature to mute the output where appropriate.

Version 3.2d22
====
Continuing the experiments with D-Bus and related DACP support.

**Bug Fix**
* Fix timing error when using Airfoil as a source.

Version 3.2d21
====
Continuing the experiments with D-Bus and related DACP support. In this revision, [tinyhttp](https://github.com/mendsley/tinyhttp) is now used for sending and retrieving DACP information. Many thanks to [Matthew Endsley](https://github.com/mendsley) for this work on tinyhttp.
As before, please note that the implementation is likely to change greatly or be removed at any time.

Version 3.2d20
====
**Bug Fix**
* Fix silly seg-fault bug in 3.2d19, activated when a DACP record was withdrawn.

Version 3.2d19
====
**Enhancement**
* Add compatibility with [Swinsian](https://swinsian.com), a Mac music player.

Version 3.2d18
====
**Bug Fix**
* In recent versions of iOS (11.2) and mac OS (10.13.2), when play is resumed after a pause, the volume level is not always restored, and, if software volume control is being used, Shairport Sync plays at full volume. This issue has been addressed by storing the last airplay volume setting when a play session ends and using it as a default when a new play session begins. (This is a more generalised solution than in 3.2d16.)
* Better AirPlay synchronisation. Older versions of Shairport Sync added an 11,025 frame (0.25 seconds) offset to all the latencies agreed with the sender. This seems now only to be correct for iTunes and ForkedDaapd sources, but incorrect for AirPlay sources. Accordingly, the offset is only added for iTunes and ForkedDaapd. The result is better sync with videos, e.g, YouTube, etc. while iTunes and ForkedDaapd synchronisation is unaffected.

Version 3.2d16
====
**Bug Fix**
* In recent versions of iOS (11.2) and mac OS (10.13.2), when play is resumed after a pause, the volume level is not always restored, and, if software volume control is being used, Shairport Sync plays at full volume. This issue has been addressed by storing the last software volume setting when a play session ends and using it as a default when a new play session begins.

Version 3.2d15
====
**Bug Fix, kind of...**
* Shairport Sync crashes on Arch Linux with with pulseaudio backend enabled. The cause appears to be a pulseaudio configuration issue, but of course, Shairport Sync shouldn't crash. For the present, the bug fix merely adds an error message before terminating Shairport Sync.

**Enhancements**
* Still lots of changes and experiments with D-Bus and DACP. As before, please note that the implementation is likely to change greatly or to be removed at any time.

Version 3.2d13
====
**Security Update**

The version of `tinysvcmdns` bundled in Shairport Sync has a buffer overflow bug: *"An exploitable heap overflow vulnerability exists in the tinysvcmdns library version 2016-07-18. A specially crafted packet can make the library overwrite an arbitrary amount of data on the heap with attacker controlled values. An attacker needs send a dns packet to trigger this vulnerability."* The vulnerability is addressed  by additional checking on packet sizes. See also [CVE-2017-12087](https://bugs.launchpad.net/bugs/cve/2017-12087) and [Vulnerability in tinysvcmdns](https://bugs.launchpad.net/ubuntu/+source/shairport-sync/+bug/1729668).
Thanks and [Chris Boot](https://github.com/bootc) for fixing this bug.

Version 3.2d12
====
Experimenting with an [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/)-compatible D-Bus interface. A very small number of features have a tentative implementation. As with the Shairport Sync D-Bus interface, please note that the implementation is likely to change greatly or be removed at any time.

Continuing the experiments with D-Bus support, Shairport Sync can now be compiled to have a D-Bus presence on the D-Bus system bus. It presents a small number of properties and can execute a method call which sends a command string to the audio source's DACP port. As before, please note that the implementation is likely to change greatly or be removed at any time.

**Enhancement**
* The metadata output stream can include a `dapo` message carrying the DACP port number to be used when communicating with the DACP remote control. This might be useful because the port number is actually pretty hard to find and requires the use of asynchronous mdns operations. You must be using the Avahi mdns back end.

**Bug Fix**
* A bug in the hardware volume control affects output devices that have hardware mixers but that do not allow the volume to be set in dB. One example is the Softvol plugin in ALSA. Shairport Sync fails silently when presented with such a device when hardware volume control is enabled: the volume events have no effect. The bug has been fixed by adding two missing lines of code to the `init()` function in `audio_alsa.c`. Thanks to [Jakub Nabaglo](https://github.com/nbgl) for finding and fixing the bug.
* A number of bug fixes due to [belboj](https://github.com/belboj). Many thanks for these!
* Enhancements to the handling of quit requests by threads, thanks(again) to [belboj](https://github.com/belboj)!

**Other Stuff**
* The directory structure has been rearranged somewhat. Probably will change again...
* Typo fix! Thanks to [corbinsantin](https://github.com/corbinsantin).

Version 3.2d8
====

**Bug Fix**
* Fixed a bug that prevented Shairport Sync from starting automatically on systems using the System V startup system (e.g. Ubuntu 14.04). The problem was that the directory to be used – `/var/run/shairport-sync/` – was deleted on power down and needed to be recreated on startup. In it's absence, Shairport Sync would not start and would report a mysterious daemon error \#2.

Version 3.2d7
====
This introduces a very experimental [D-Bus](https://en.wikipedia.org/wiki/D-Bus) interface to Shairport Sync. At present, in a very ad-hoc trial arrangement, Shairport Sync provides a system bus D-Bus service enabling a program to get and set Volume, to enable and disable the Loundness Filter and to get and set the Loundness Filter threshold (remember, BTW, the Loudness filter only works with software-based volume control). The implementation is likely to change greatly or be removed at any time. Tested on Ubuntu 16.04 and on Raspbian Stretch.
Two extra configuration options are provided: `--with-dbus` and `--with-dbus-test-client`. (BTW, the test client is never installed, merely compiled.)

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
* Updates to `man` page and  [README](https://github.com/mikebrady/shairport-sync/blob/development/README.md). Reports of typos or suggestions for improvement are welcome!

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
* The dither code was broken in Shairport Sync and also less than ideal anyway. Fixed and improved. Dither is added whenever you use the software volume control at less than full volume. See http://www.ece.rochester.edu/courses/ECE472/resources/Papers/Lipshitz_1992.pdf for a very influential paper by Lipshitz, Wannamaker and Vanderkooy, 1992. The dither code in Shairport Sync was inherited from Shairport and does not conform to the recommendations in the paper -- specifically the implementation would give one bit of dither where the paper recommends two bits peak-to-peak. The other thing is that the inherited dither code was actually broken in Shairport Sync. So, the new dither code gives a two bit peak-to-peak dither based on a Triangular Probability Distribution Function (TPDF). It sounds like a very low-level white noise, unmodulated by the audio material. It would be nice if it was even lower, but it's better than listening to the artifacts present when dithering is disabled.

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
