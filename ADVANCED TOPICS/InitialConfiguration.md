# Finish Setting Up
When you complete the instructions in [BUILD.md](../BUILD.md), you have a basic functioning Shairport Sync installation. If you want more control – for example, if you want to use a specific DAC, or if you want AirPlay to control the DAC's volume control – you can use settings in the configuration file (recommended) or you can use command-line options.

## The Configuration File
Shairport Sync reads settings from a configuration file at `/etc/shairport-sync.conf` (in FreeBSD it will be at `/usr/local/etc/shairport-sync.conf`). When you run `$sudo make install`, a sample configuration file  called `shairport-sync.conf.sample` is always installed or updated. This contains all the setting groups and all the settings available, but they all are commented out (comments begin with `//`) so that default values are used. The file contains explanations of the settings, useful hints and suggestions.

## Specifying the Output Device and Mixer Control
If you have followed the [BUILD.md](../BUILD.md) instructions, audio received by Shairport Sync will be sent to the `default` device. Depending on the configuration of your system, you may be able to specify a specific hardware output DAC and use its built-in mixer to control volume levels. This would be desirable because (1) the `default` device may be doing further processing on the audio before sending it to the hardware output device, degrading its fidelity, and (2) using the real device's hardware mixer to control volume would give Shairport Sync complete control of the volume range.

To get a list of the hardware DACs on your system, refer to the output of `$ shairport-sync -h`. Here is a sample from a Raspberry Pi 3B system:
```
Usage: shairport-sync [options...]
  or:  shairport-sync [options...] -- [audio output-specific options]
...
[snip]
...
Settings and options for the audio backend "alsa":
    -d output-device    set the output device, default is "default".
    -c mixer-control    set the mixer control name, default is to use no mixer.
    -m mixer-device     set the mixer device, default is the output device.
    -i mixer-index      set the mixer index, default is 0.
    hardware output devices:
      "hw:Headphones"
      "hw:sndrpihifiberry"
      "hw:vc4hdmi"
...
[snip]
...
```
In this system, you can see that there are three hardware output devices, `"hw:Headphones"`, `"hw:sndrpihifiberry"` and `"hw:vc4hdmi"`.

Using a tool like [`dacquery`](https://github.com/mikebrady/dacquery) or `alsamixer`, an output device can be checked to find the name of the volume control mixer. For the first device, the name of the mixer is `"Headphone"`.

These setting can be entered into the configuration file, in the `alsa` section, as follows:
```
...
alsa =
{
	output_device = "hw:Headphones"; // the name of the alsa output device. Use "shairport-sync -h" to discover the names of ALSA hardware devices. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
	mixer_control_name = "PCM"; // the name of the mixer to use to adjust output volume. If not specified, volume in adjusted in software.
...
}
```
Make sure to uncomment entries you wish to make active, and restart Shairport Sync after you have made changes.

If you make a syntax error, Shairport Sync will not start and will leave a message in the log.  See more details below and in the comments in the configuration file.

Please note that if your system has a sound server such as PipeWire or PulseAudio (most desktop Linuxes have one of these), you may not be able to access the sound hardware directly, so you may only be able to use the `default` output.

## More about Configuration Settings
Settings in the configuration file are grouped. For instance, there is a `general` group within which you can use the `name` tag to set the service name. Suppose you wanted to set the name of the service to `Front Room` and give the service the password `secret`, then you should do the following:

```
general =
{
  name = "Front Room";
  password = "secret";
  // ... other general settings
};

```
The password setting is only valid for classic Shairport Sync.

(Remember, anything preceded by `//` is a comment and will have no effect on the setting of Shairport Sync.)

**Important:** You should *never* use an important password as the AirPlay password for a Shairport Sync player – the password is stored in Shairport Sync's configuration file in plain text and is thus completely vulnerable.

No backend is specified here, so it will default to the `alsa` backend if more than one back end has been compiled. To route the output to PipeWire, set:
```
  output_backend = "pipewire";
```
in the `general` group.

The `alsa` group is used to specify properties of the output device. The most obvious setting is the name of the output device which you can set using the `output_device` tag.

The following `alsa` group settings are very important for maximum performance. If your audio device has a mixer that can be use to control the volume, then Shairport Sync can use it to give instant response to volume and mute commands and it can offload some work from the processor.
* The `mixer_control_name` tag allows you to specify the name of the mixer volume control.
* The `mixer_device` tag allows you specify where the mixer is. By default, the mixer is on the `output_device`, so you only need to use the `mixer_device` tag if the mixer is elsewhere. This can happen if you specify a *device* rather than a *card* with the `output_device` tag, because normally a mixer is associated with a *card* rather than a device. Suppose you wish to use the output device `5` of card `hw:0` and the mixer volume-control named `PCM`:

```
alsa =
{
  output_device = "hw:0,5";
  mixer_device = "hw:0";
  mixer_control_name = "PCM";
  // ... other alsa settings
};
```

The `pipewire` group is used to specify settings relevant to the PipeWire backend. You can set the "Application Name" that will appear in the "Sound" control panel.

Note: Shairport Sync can take configuration settings from command line options. This is mainly for backward compatibility, but sometimes still useful. Where possible, it is recommended that you use the configuration file method.

### Raspberry Pi

To make Shairport Sync output to the built-in audio DAC and use its hardware mixer, in the `alsa` section of the configuration file, set the output device and mixer as follows:
```
alsa =
{
  output_device = "hw:Headphones"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
  mixer_control_name = "PCM"; // the name of the mixer to use to adjust output volume. If not specified, volume in adjusted in software.
  // ... other alsa settings
```
(Remember to uncomment the lines by removing the `//` at the start of each.) When these changes have been made, restart Shairport Sync or simply reboot the system.

A problem with the built-in DAC that it declares itself to have a very large mixer volume control range – all the way from -102.38dB up to +4dB, a range of 106.38 dB. In reality, only the top 60 dB or so is in any way usable. To help get the most from it, consider using the `volume_range_db` setting in the `general` group to instruct Shairport Sync to use the top of the DAC mixer's declared range. For example, if you set the `volume_range_db` figure to 60, the top 60 dB of the range will the used. With this setting on the Raspberry Pi, maximum volume will be +4dB and minimum volume will be -56dB, below which muting will occur.

From a user's point of view, the effect of using this setting is to move the minimum usable volume all the way down to the bottom of the user's volume control, rather than have the minimum usable volume concentrated very close to the maximum volume.

### Examples

Here are some examples of complete configuration files.

```
general = {
  name = "Joe's Stereo";
};

alsa = {
  output_device = "hw:0";
};
```

This gives the service the name "Joe's Stereo" and specifies that audio device `hw:0` be used.

For best results with the `alsa` backend — including getting true mute and instant response to volume control and pause commands — you should access the hardware volume controls. Use [`dacquery`](https://github.com/mikebrady/dacquery)`amixer` or `alsamixer` or similar to discover the name of the mixer control to be used as the `mixer_control_name`.

Here is an example for for a Raspberry Pi using its internal soundcard — device hw:0 — that drives the headphone jack:
```
general = {
  name = "Mike's Boombox";
};

alsa = {
  output_device = "hw:0";
  mixer_control_name = "Headphone";
};
```

Here is an example of driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC and which is connected as audio device `hw:1`:
```
general = {
  name = "Kitchen";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "PCM";
};
```

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:
```
general = {
  name = "Front Room";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "Speaker";
};
```

For a first generation Griffin iMic on a Raspberry Pi:
```
general = {
  name = "Attic";
};

alsa = {
  output_device = "hw:1";
  mixer_control_name = "PCM";
};
```

On an NSLU2, to drive a first generation Griffin iMic:
```
general = {
  name = "Den";
};

alsa = {
  mixer_control_name = "PCM";
};
```

On an NSLU2, to drive the "3D Sound" USB card:
```
general = {
  name = "TV Room";
};

alsa = {
  mixer_control_name = "Speaker";
};
```

Finally, here is an example of using the PulseAudio backend:
```
general = {
  name = "Zoe's Computer";
  output_backend = "pa";
};

```


### Latency
Latency is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the audio output device, usually a Digital to Audio Converter (DAC), irrespective of any internal delays, processing times, etc. in the computer.

Shairport Sync uses latencies supplied by the source, typically either 2 seconds or just over 2.25 seconds. You shouldn't need to change them.

Problems can arise when you are trying to synchronise with speaker systems — typically surround-sound home theatre systems — that have their own inherent delays. You can compensate for an inherent delay using the appropriate backend (typically `alsa`) `audio_backend_latency_offset_in_seconds`. Set this offset (in frames) to compensate for a fixed delay in the audio back end; for example, if the output device delays by 100 ms, set this to -0.1.

### Resynchronisation

Shairport Sync actively maintains synchronisation with the source.
If synchronisation is lost — say due to a busy source or a congested network — Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 0.050 seconds — i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
* You can vary the resync threshold, or turn resync off completely, with the `general` `resync_threshold_in_seconds` setting.

### Tolerance
Playback synchronisation is allowed to wander — to "drift" — a small amount before attempting to correct it. The default is 0.002 seconds, i.e. 2 ms. The smaller the tolerance, the  more likely it is that overcorrection will occur. Overcorrection is when more corrections (insertions and deletions) are made than are strictly necessary to keep the stream in sync. Use the `statistics` setting to monitor correction levels. Corrections should not greatly exceed net corrections.
* You can vary the tolerance with the `general` `drift_tolerance_in_seconds` setting.

## Command Line Arguments

You can use command line arguments to provide settings to Shairport Sync, though newer settings will only be available via the configuration file. For full information, please read the Shairport Sync `man` page, also available at  http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/master/man/shairport-sync.html.

Apart from the following options, all command line options can be replaced by settings in the configuration file. Here is a brief description of command line options that are not replicated by settings in the settings file.

* The `-c` option allows you to specify the location of the configuration file.
* The `-V` option gives you version information about  Shairport Sync and then quits.
* The `-d` option causes Shairport Sync to properly daemonise itself, that is, to run in the background. You may need sudo privileges for this.
* The `-k` option causes Shairport Sync to kill an existing Shairport Sync daemon. You may need to have sudo privileges for this.

The System V init script at `/etc/init.d/shairport-sync` has a bare minimum :
`-d`. Basically all it does is put the program in daemon mode. The program will read its settings from the configuration file.

