Version 2.7.3 -- Development Version
----
**Bug Fix**
* The dither code was broken in Shairport Sync and also less than ideal anyway. Fixed and improved. Dither is added whenever you use the software volume control at less than full volume. See http://www.ece.rochester.edu/courses/ECE472/resources/Papers/Lipshitz_1992.pdf for a very influential paper by Lipshitz, Wannamaker and Vanderkooy, 1992. The dither code in Shairport Sync was inherited from Shairport and does not conform to the recommendations in the paper -- specifically the implementation would give one bit of dither where the paper recommends two bits peak-to-peak. The other thing is that the inherited dither code was actually broken in Shairport Sync. So, the new dither code gives a two bit peak-to-peak dither based on a Triangular Propability Distributing Function (TPDF). It sounds like a very low-level white noise, unmodulated by the audio material. It would be nice if it was even lower, but it's better than listening to the artifacts present when dithering is disabled.

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
* Extend the volume range for some DACs. Background: some of the cheaper DACS have a very small volume range (that is, the ratio of the highest to the lowest volume, expressed in decibels). In some really cheap DACs it's only around 30 dB. That means that the difference betweeen the lowest and highest volume settings isn't large enough. With the new feature, if you set the `general` `volume_range_db` to more than the hardware mixer's range, Shairport Sync will combine the hardware mixer's range with a software attenuator to give the desired range. For example, suppose you want a volume range of 70 dB and the hardware mixer offers only 30 dB, then Shairport Sync will make up the other 40 dB with a software attenuator. One drawback is that, when the volume is being changed, there may be a slight delay (0.15 seconds by default) as the audio, whose volume may have been adjusted in software, propagates through the system. Another slight possible drawback is a slightly heavier load on the processor.
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
This release has important enhancements, bug fixes and documentation updates. It also appears to bring compatiblity with Synology NAS devices.


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
* An experimental handler for a GET_PARAMETER rtsp request has been added. It does nothing except log the occurence.
* The RTSP request dispatcher now logs an event whenever an unrecognised rtsp has been made.


Version 2.4.1
----
This release has three small bug fixes and some small documentation updates.

**Bug Fixes**

Changes from the previous stable version -- 2.4 -- are summarised here:
 * The USE_CUSTOM_LOCAL_STATE_DIR macro was still being used when it should have been USE_CUSTOM_PID_DIR. This could affect users using a custom location for the PID directory.
 * A compiler error has been fixed that occured if metadata was enabled and tinysvcmdns was included.
 * A crash has been fixed that occured if metadata was enabled and a metadata pipename was not specified.
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
* Larger range of interpolation. Shairport Sync was previously constrained not to make interpolations ("corrections") of more than about 1 per 1000 frames. This contraint has been relaxed, and it is now able to make corrections of up to 1 in 352 frames. This might result in a faster and undesirably sudden correction early during a play session, so a number of further changes have been made. The full set of these changes is as follows:
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
  * If a reader disappears while writing is occuring, the write will time out after five seconds.
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
This gives more flexibility in adding features gives better compatability across different versions of Linux.
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
 * New tolerance option: `--tolerance=FRAMES`. Use this option to specify the largest synchronisation error to allow before making corrections. The default is 88 frames, i.e. 2 milliseconds. The default tolerance is fine for streaming over wired ethernet; however, if some of the stream's path is via WiFi, or if the source is a third-party product, it may lead to much overcorrection -- i.e. the difference between "corrections" and "net correction" in the `--statistics` option. Increasing the tolerence may reduce the amount of overcorrection.

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
	* Option `--with-localstatedir` has been replaced with `--with-piddir`. This compilation option allows you to specify the directory in which the PID file will be written. The directory must exist and be writable. Supercedes the `--with-localstatedir` and describes the intended functionality a little more accurately.

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
