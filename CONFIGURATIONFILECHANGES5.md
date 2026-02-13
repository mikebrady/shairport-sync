# Configuration File Changes in Version 5.0

This document summarizes the important changes to the `shairport-sync.conf` configuration file for normal users upgrading to Version 5.0.

## Multi-Channel Audio Support (NEW!)

Version 5.0 adds support for multi-channel audio (up to 8 channels), including surround sound formats like 5.1 and 7.1.

**New Settings in `general` section (showing defaults):**

* `eight_channel_mode = "on"` - Enable 8-channel (7.1 surround) audio reception.
* `six_channel_mode = "on"` - Enable 6-channel (5.1 surround) audio reception.  
* `mixdown = "auto"` - Control how multi-channel audio is mixed down to fewer channels.
* `output_channel_mapping = "auto"` - Control how audio channels map to your output device.

**What this means for you:** If you have a surround sound system, you can now receive and play multi-channel audio directly. Most users can leave these at their defaults.

## Audio Format and Rate Settings

**Enhanced Interpolation Options:**

The `interpolation` setting now supports a new `"vernier"` mode especially intended for low-power devices:
* `"auto"` (default, recommended) - Automatically chooses the best method for your processor.
* `"vernier"` (new!) - Optimized for low-power devices like Raspberry Pi.
* `"soxr"` - High quality, needs fast processor.
* `"basic"` - No longer recommended.

**New FFmpeg Decoder:**

The `alac_decoder` setting has changed:
* Default is now `"ffmpeg"` (if built with FFmpeg support).
* Old `"hammerton"` and `"apple"` decoders are deprecated for security reasons.

**New Buffer Setting:**

* `audio_decoded_buffer_desired_length_in_seconds = 1.0` - (Advanced.) Controls the internal audio buffer size (AirPlay 2 only).

## Backend-Specific Changes

### ALSA Backend

**New format/rate/channel settings** - You can now specify multiple options and let Shairport Sync auto-select:

* `output_rate = "auto"` - Can be "auto", a single rate like `48000`, or a list like `(44100, 48000)`.
* `output_format = "auto"` - Can be "auto", a format like `"S32_LE"`, or a list.
* `output_channels = "auto"` - Can be "auto", a number like `2`, or a list like `(2, 6, 8)`.

**What this means:** Shairport Sync can now automatically switch between different audio formats and rates to match your source, or you can lock it to specific settings.

**Other ALSA changes:**

* `use_mmap_if_available` default changed from `"yes"` to `"no"`.
* `disable_standby_mode_silence_scan_interval` default changed from `0.004` to `0.030`.
* New: `disable_standby_mode_default_channels = 2` - Initial channel setting when standby mode is disabled.
* New: `disable_standby_mode_default_rate` - Initial sample rate when standby mode is disabled.

### PipeWire Backend

**Renamed section:** The `pw` section is now called `pipewire`.

**New settings:**
* `output_rate = "auto"`.
* `output_format = "auto"`.  
* `output_channels = "auto"`.

**What this means:** PipeWire now has the same flexible format/rate/channel options as ALSA.

### PulseAudio Backend

**Renamed section:** The `pa` section is now called `pulseaudio`.

**New settings:**
* `output_rate = "auto"`.
* `output_format = "auto"`.
* `output_channels = "auto"`.
* `default_channel_layouts = "alsa"` - Use ALSA-compatible channel layouts (default) or PulseAudio's own layouts.

### Other Backends

`sndio`, `pipe`, `stdout`, and `ao` backends have all gained the new `output_rate`, `output_format`, and `output_channels` settings with similar functionality.

## DSP (Convolution and Loudness) Changes

**Convolution Filter:**

Settings have been renamed for clarity:
* `convolution` → `convolution_enabled`.
* `convolution_ir_file` → `convolution_ir_files`.
* `convolution_max_length` → `convolution_max_length_in_seconds`.

**New convolution setting:**
* `convolution_thread_pool_size = 1` - Number of CPU threads for convolution processing.

**What this means:** 
- You can now specify multiple impulse response files for different sample rates.
- The convolution filter works with both stereo and multi-channel audio.
- You can use multiple CPU cores for faster processing (but core management by the OS may cause power supply noise on some systems).

**Loudness Filter:**

* `loudness` → `loudness_enabled`.
* The loudness filter now works with stereo and multi-channel audio at both 44.1k and 48k.

## MQTT Changes

**New setting:**

* `publish_retain = "no"` - Set to `"yes"` to make the MQTT broker store the last message for each topic.

**What this means:** When enabled, new MQTT subscribers will immediately receive the most recent values instead of having to wait for the next update.

## Session Control Changes

**Default timeout changed:**

* `session_timeout` default changed from `120` seconds to `60` seconds.

**What this means:** Shairport Sync will now become available again 60 seconds (instead of 120) after a source disappears.

## Removed Settings

The following old settings have been removed:

* `resync_recovery_time_in_seconds` - No longer needed with improved synchronization.

## What Should You Do?

**For most users:** Your existing configuration file will continue to work, though you might have to make some minimal changes.

**Must Do**

* If you use PipeWire or PulseAudio, please change over to the new configuration file settings and backend names immediately.

**Should Do**

* If you are using ALSA plugins, e.g. `plughw:1` to transcode from 44.1k to 48k, consider outputing directly to the underlying hardware device -- `hw:1` in this example -- allowing Shairport Sync to transcode if needed.

**If you want to use new features:**

1. **Multi-channel audio:** Leave `eight_channel_mode` and `six_channel_mode` settings at default, or set them to `"on"` if you have a surround sound system.

2. **Better performance on low-power devices:** Leave the `interpolation` setting at default, or set  it to `"vernier"`.

3. **MQTT retain:** Set `publish_retain = "yes"` if you want MQTT clients to receive the last known state immediately.

4. **Convolution improvements:** Update your `convolution_ir_file` to `convolution_ir_files` and add multiple impulse response files for different sample rates.

5. **Backend-specific formats:** Specify exact rates/formats/channels if you want to lock Shairport Sync to specific settings, or use "auto" to let it adapt.

**To prepare for the future:** Consider updating deprecated setting names to their new equivalents:
- `convolution` → `convolution_enabled`.
- `convolution_ir_file` → `convolution_ir_files`.  
- `convolution_max_length` → `convolution_max_length_in_seconds`.
- `loudness` → `loudness_enabled`.
