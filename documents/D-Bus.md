## Shairport Sync D-Bus Interface

Shairport Sync can have a D-Bus interface, which can be used to control aspects of its operation and get status information from it.

To include the D-Bus interface at build time, add the `--with-dbus-interface` flag at the `./configure…` stage. When the D-Bus interface is included in Shairport Sync, its Version String will include the term `dbus`, for instance:
```
$ shairport-sync -V
5.0.0-1-gc956d763-AirPlay2-smi10-OpenSSL-Avahi-ALSA-soxr-metadata-dbus-sysconfdir:/etc

```
Once Shairport Sync has been built with the D-Bus interface, it must then be installed correctly to provide a Shairport Sync service, on either the D-Bus system bus or the D Bus session bus.
To become available as a system-wide service on the D-Bus system bus, it must be installed as a system service using the `# make install` step of the build process. This installs the appropriate service files and sets required permissions.
Remember to enable Shairport Sync to start as a system service. You may also need to restart the entire system to allow the service to be seen.

If Shairport Sync is installed as a user service, it will not be able to become a service on the D-Bus system bus, but it can be added to the D-Bus session bus. Edit the configuration file to select the session bus, or add the option `--dbus_default_message_bus=session` to the command line.

### D-Feet
On desktop Linuxes with a GUI, e.g. Ubuntu, D-Feet is a great tool for examining a D-Bus service:

![Shairport Sync and DFeet](https://github.com/user-attachments/assets/613dab83-9b02-4312-9ad1-c94db1d0ef49)

### Sample Test Client

A simple test client can be built when you are building Shairport Sync itself. To build it, simply add the `--with-dbus-test-client` flag at the `./configure…` stage. Along with the `shairport-sync` executable application, you'll get another executable called `shairport-sync-dbus-test-client` which you can run from the command line.

After attempting to send some commands, it will listen for property changes on the D-Bus interface and report them on the console.

### Command Line Examples

The examples below are based on Shairport Sync running as a system service and use the standard CLI tool `dbus-send`:
* Get `Active` Status -- `true` when Shairport Sync is playing (and for a short time later); `false` otherwise.
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:Active
```
* Get Log Verbosity
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.Diagnostics string:Verbosity
```
* Return Log Verbosity
```
  echo `dbus-send --print-reply=literal --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.Diagnostics string:Verbosity`
```
* Set Log Verbosity to 2
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync.Diagnostics string:Verbosity variant:int32:2
```
* Get Statistics-Requested Status
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.Diagnostics string:Statistics
```
* Set Statistics-Requested Status to `true`
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync.Diagnostics string:Statistics variant:boolean:true
```
* Are File Name and Line Number included in Log Entries?
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.Diagnostics string:FileAndLine
```
* Include File Name and Line Number in Log Entries
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync.Diagnostics string:FileAndLine variant:boolean:true
```
* Is Elapsed Time included in Log Entries?
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.Diagnostics string:ElapsedTime
```
* Include Elapsed Time in Log Entries
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync.Diagnostics string:ElapsedTime variant:boolean:true
```
* Get Drift Tolerance
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:DriftTolerance
```
* Set Drift Tolerance to 1 millisecond
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:DriftTolerance variant:double:0.001
```
* Is Loudness enabled:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:LoudnessEnabled
```
* Enable Loudness Filter
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:LoudnessEnabled variant:boolean:true
```
* Get Loudness Threshold
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:LoudnessThreshold
```
* Set Loudness Threshold
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:LoudnessThreshold variant:double:-15.0
```
* Is Convolution enabled:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:ConvolutionEnabled
```
* Enable Convolution
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:ConvolutionEnabled variant:boolean:true
```
* Get Convolution Gain:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:ConvolutionGain
```
* Set Convolution Gain -- the gain applied after convolution is applied -- to -10.0 dB
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:ConvolutionGain variant:double:-10
```
* Get Convolution Impulse Response Files:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:ConvolutionImpulseResponseFiles
```
* Set Convolution Impulse Response Files:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:ConvolutionImpulseResponseFiles variant:string:"'/home/pi/filters/Sennheiser HD 205 minimum phase 48000Hz.wav', '/home/pi/filters/Sennheiser HD 205 minimum phase 44100Hz.wav'"
```
* Get Convolution Impulse Response File Maximum Length:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:ConvolutionMaximumLengthInSeconds
```
* Set Convolution Impulse Response File Maximum Length:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:ConvolutionMaximumLengthInSeconds variant:double:1
```
* Get the Protocol that Shairport Sync was built for -- AirPlay or AirPlay 2:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:Protocol
```
* Get the current volume level. This returns a value between -30.0 and 0.0, which is linear on the UI volume control. It can also return -144.0 to indicate mute.
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync string:Volume
```
* Set the current volume level. This should be a value between -30.0 and 0.0. Set a value of -144.0 to mute. Note that all this is done locally on the Shairport Sync device itself. The volume control of audio source (e.g. iTunes / macOS Music / iOS) is not updated to reflect any changes.
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Set string:org.gnome.ShairportSync string:Volume variant:double:-10.0
```
* Drop the current session immediately. Shairport Sync will immediately stop playing and drop the connection from the audio source. This may show up as an error at the source.
```
  dbus-send --system --print-reply --type=method_call --dest=org.gnome.ShairportSync '/org/gnome/ShairportSync' org.gnome.ShairportSync.DropSession
```
#### Remote Control
Remote Control commands are sent as requests to the player (iOS, iTunes, macOS Music, etc.). Different versions of the players implement different subsets of the following commands.

**Note:** Unfortunately, at this time -- early 2026 -- these requests are ignored, so remote control doesn't work.

* Check if Remote Control is available:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.RemoteControl string:Available
```
* Send the `play` command to the player:
```
  dbus-send --system --print-reply --type=method_call --dest=org.gnome.ShairportSync '/org/gnome/ShairportSync' org.gnome.ShairportSync.RemoteControl.Play
```
  Remote Control commands include: `Play`, `Pause`, `PlayPause`, `Resume`, `Stop`, `Next`, `Previous`, `VolumeUp`, `VolumeDown`, `ToggleMute`, `FastForward`, `Rewind`, `ShuffleSongs`.

* Set Airplay Volume using Remote Control. Airplay Volume is between -30.0 and 0.0 and maps linearly onto the slider, with -30.0 being lowest and 0.0 being highest.
```
  dbus-send --system --print-reply --type=method_call --dest=org.gnome.ShairportSync '/org/gnome/ShairportSync' org.gnome.ShairportSync.RemoteControl.SetAirplayVolume double:-10.0
```
#### Advanced Remote Control
Some commands and properties are accessible only through the `AdvancedRemoteControl` interface.

**Note:** Unfortunately, at this time -- early 2026 -- these requests are ignored, so advance remote control doesn't work.

* Check if Advanced Remote Control is available:
```
  dbus-send --print-reply --system --dest=org.gnome.ShairportSync /org/gnome/ShairportSync org.freedesktop.DBus.Properties.Get string:org.gnome.ShairportSync.AdvancedRemoteControl string:Available
```
* Set Volume using Advanced Remote Control:
```
  dbus-send --system --print-reply --type=method_call --dest=org.gnome.ShairportSync '/org/gnome/ShairportSync' org.gnome.ShairportSync.AdvancedRemoteControl.SetVolume int32:50
```
