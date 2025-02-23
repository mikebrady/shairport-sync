# Adjusting Synchronisation with Shairport Sync

Sometimes, a timing difference can be heard, where the audio coming from the Shairport-Sync-powered device is slightly ahead or slightly behind another device playing in synchrony. This can sometimes be heard as an irritating "echo".

This is usually due to audio amplifier delays:

* If your audio output device (including the amplifier in a TV) includes any digital processing component, it probably delays audio while amplifying it.

* If your output device is a HDMI-connected device such as a TV or an AV Receiver (AVR), it will almost certainly delay audio by anything up to several hundred milliseconds.

In these circumstances, if the output from the Shairport Sync device is amplified by a conventional analog-only HiFi amplifier – which has almost no delay – it will be early by comparison with audio coming from the other device.

Conversely, if the output from the Shairport Sync device is passed through an AVR, then it could be late by comparison with audio amplified by a conventional audio amplifier.

The fix for this is to get Shairport Sync to compensate for delays by providing audio to the output device _slightly late_ or _slightly early_, so that when audio emerges from the amplifier, it is in exact synchrony with audio from the other devices.

The setting to look for is in the `general` section of the Shairport Sync configuration file and is called `audio_backend_latency_offset_in_seconds`. By default it is `0.0` seconds.

For example, to delay the output from the Shairport Sync device by 100 milliseconds (0.1 seconds), set the `audio_backend_latency_offset_in_seconds` to `0.1`, so that audio is provided to your output device 100 milliseconds later than nominal synchronisation time.

Similarly, to get the output from the Shairport Sync device 50 milliseconds (0.05 seconds) early, set the `audio_backend_latency_offset_in_seconds` to `-0.05`, so that audio is provided to your output device 50 milliseconds earlier than nominal synchronisation time.

Latency adjustments should be small, not more than about ± 250 milliseconds.

Remember to uncomment the line by removing the initial `//` and then restart Shairport Sync (or reboot the device) for the changed setting to take effect.
