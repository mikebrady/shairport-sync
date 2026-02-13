# Statistics

If you set the `statistics` setting in the `diagnostics` section of the configuration file to `"YES"`, some statistics will be logged at regular intervals. The items logged will depend on the type of stream being processed: classic AirPlay, AirPlay 2 Buffered Audio or AirPlay 2 Realtime Audio. If the `log_verbosity` is set to 1, 2 or 3, additional items will be logged.

From an audio enthusiast's point of view, the most important figure is possibly the `All Sync PPM` figure. This is the total amount of interpolation needed by Shairport Sync to keep the audio stream in sync in the last interval, i.e. the number of frames added plus the number of frames removed from the audio stream, relative to the total number of frames output, expressed in parts per million (PPM). For reference, adding or removing one frame per second into a 44,100 frames per second stream is ± 22.68 PPM. The lower this number number is, the higher the fidelity of the audio signal passed to the output device. On a well sorted system, this figure can be 0.0 for considerable periods, but it can't really be zero forever unless the output device is adapting to the true data rate (some very high-end streamers seem to do so). You may also find that the number might be higher at the start while the system settles down.

The second most important figure is possibly the `Sync Error ms`. This is the average synchronisation error in milliseconds in the last interval. Ideally it should be 0.0. By default, Shairport Sync has a tolerance of a sync error of ± 2.0 milliseconds without triggering interpolation. 

Two other interesting measurements of the output rate may be available – `Output FPS (r)` and `Output FPS (c)`, where `(r)` means "raw" and the `(c)` means "corrected".

The "raw" figure is the rate at which the output device (typically a DAC) accepts data measured relative to the computer's own system clock (specifically the `CLOCK_MONOTONIC_RAW` clock). The accuracy of the number depends on the accuracy of the clock, which will typically be accurate to within anything from 20 to 100 ppm.

The "corrected" figure is the rate at which the output device accepts data relative to the computer's network-time-disciplined clock (specifically the `CLOCK_MONOTONIC` clock). This clock is normally adjusted ("disciplined") to keep time with network time and should be accurate to with a few tens of milliseconds over a _long_ period. So (1) if you could run a play session for a long period – say a day – and (2) if network time synchronisation is enabled and (3) if the network connection to the network time service is fast and stable, then you should get an accurate absolute measure of exact frame rate of the output device. If your internet connection is not good, the corrected figure will be very inaccurate indeed.

Here is a brief description of the figures that might be provided.
##### Sync Error ms
Average playback synchronisation error in milliseconds in the last interval. By default, Shairport Sync will allow a sync error of ± 2.0 milliseconds without any interpolation. Positive means late, negative means early.
##### Net Sync PPM
This is the net amount of interpolation done by Shairport Sync to keep the audio stream in sync in the last interval, i.e. the number of frames added **minus** the number of frames removed from the audio stream, relative to the total number of frames output, expressed in parts per million (PPM). For reference, adding or removing one frame per second into a 44,100 frames per second stream is 22.68 ppm.
##### All Sync PPM
This is the total amount of interpolation done by Shairport Sync to keep the audio stream in sync in the last interval, i.e. the number of frames added **plus** the number of frames removed from the audio stream, relative to the total number of frames output, expressed in parts per million (PPM). The magnitude of this should be the same as the `Net Sync PPM`. If it is much larger it means that Shairport Sync is overcorrecting for sync errors – try increasing the drift tolerance to reduce it.
##### Av Sync Window (ms)
This is an estimate, in milliseconds, of how often the audio system updates the latency data it provides.
##### Packets
This is the number of packets of audio frames received since the start of the session. A packet normally contains 352 ± 1 audio frames.
##### Missing
This is the number of packets of audio frames that were expected but not received in the last interval. It should be zero, and if not it usually indicates a significant problem with the network. classic AirPlay and AirPlay 2 Realtime Streams only.
##### Late
This is the number of packets of audio frames that were received late – but still in time to be used – in the last interval. Classic AirPlay and AirPlay 2 Realtime Streams only.
##### Too Late
This is the number of packets of audio frames that were received too late to be used in the last interval. It is possible that these packets were already received, so those frames might not actually be missing when the time comes to play them. Classic AirPlay and AirPlay 2 Realtime Streams only.
##### Resend Reqs
This is the number of times Shairport Sync requests the resending of missing frames. Requests can be for one or more frames. Classic AirPlay and AirPlay 2 Realtime Streams only.
##### Min DAC Queue
The is the smallest number of frames of audio in the DAC's hardware queue. If it goes too low, the DAC may begin to underrun.
##### Min Buffers
The is the smallest number of packets of audio in the queue to be processed in the last interval. It is related to the overall latency in Classic AirPlay and AirPlay 2 Realtime Streams. If it comes close to zero it's often a sign that the network is poor.
##### Max Buffers
The is the largest number of packets of audio in the queue to be processed in the last interval. 
##### Min Buffer Size
The is smallest remaining number of bytes in the Buffered Audio buffer in the last interval. It can legitimately be zero when a track ends or begins. If it reaches zero while a track is playing, it means that audio data is not arriving at Shairport Sync quickly enough and may indicate network problems. AirPlay 2 Buffered Audio streams only.
##### Nominal FPS
This is the rate specified in the AirPlay stream itself. Classic AirPlay only.
##### Received FPS
This is the rate at which frames are received from the network averaged since the start of the play session. Classic AirPlay only.
##### Output FPS (r)
Output rate measured relative to the computer system's clock since the start of the play session. See above for a discussion.
##### Output FPS (c)
Output rate measured relative to the network-clock-disciplined computer system's clock since the start of the play session. See above for a discussion.
##### Source Drift PPM
This is a measure of the difference between the source clock and Shairport Sync's clock expressed in parts per million. Only valid when 10 or more drift samples have been received. Classic AirPlay only.
##### Drift Samples
This is the number drift samples have been accepted for calculating the source drift. Classic AirPlay only.
#### Example
The following example is of an AirPlay 2 Buffered Audio Stream from a HomePod mini to a WiFi-connected Raspberry Pi 3 equipped with a Pimoroni "Audio DAC SHIM (Line-Out)" with `log_verbosity` set to `1` after 3 hours and 37 minutes of operation. The audio is from a radio station accessed through Apple Music:

```
         5.292055362 "rtsp.c:3112" Connection 1. AP2 Buffered Audio Stream.
         1.382815677 "player.c:2650" Connection 1: Playback Started -- AirPlay 2 Buffered.
         7.899808955 "player.c:2491" Sync Error ms | Net Sync PPM | All Sync PPM | Min DAC Queue | Min Buffer Size | Output FPS (r) | Output FPS (c)
...         
         8.014025361 "player.c:2491"         -1.69            2.8            2.8            7341              198k         44100.04         44100.35
         7.992082237 "player.c:2491"         -1.70            2.8            2.8            7332              188k         44100.04         44100.34
         8.015987549 "player.c:2491"         -1.68            0.0            0.0            7334              186k         44100.04         44100.35
         8.010537393 "player.c:2491"         -1.68            2.8            2.8            7335              187k         44100.04         44100.36
         7.993940934 "player.c:2491"         -1.85            0.0            0.0            7329              198k         44100.04         44100.37
         8.015796195 "player.c:2491"         -1.87            5.7            5.7            7325              194k         44100.04         44100.37
         8.021665934 "player.c:2491"         -1.92            5.7            5.7            7329              190k         44100.04         44100.37
         7.990409477 "player.c:2491"         -1.85            2.8            2.8            7347              201k         44100.04         44100.37
         8.001858278 "player.c:2491"         -1.96           17.0           17.0            7332              204k         44100.04         44100.37
         8.007150986 "player.c:2491"         -1.89            2.8            2.8            7327              195k         44100.04         44100.36
         7.998612862 "player.c:2491"         -1.87            2.8            2.8            7317              199k         44100.04         44100.36
         8.020592653 "player.c:2491"         -1.89           11.3           11.3            7323              203k         44100.04         44100.35
         8.003245674 "player.c:2491"         -1.89            8.5            8.5            7325              198k         44100.04         44100.35
         7.990874789 "player.c:2491"         -1.98           19.8           19.8            7103              197k         44100.04         44100.34
         8.010600257 "player.c:2491"         -1.85            2.8            2.8            7327              187k         44100.04         44100.33
         8.004573122 "player.c:2491"         -1.89            5.7            5.7            7327              187k         44100.04         44100.33
         8.012853278 "player.c:2491"         -1.89            5.7            5.7            7326              190k         44100.04         44100.32
         8.005596664 "player.c:2491"         -1.95           22.7           22.7            7322              165k         44100.04         44100.31
         8.019198695 "player.c:2491"         -1.85            5.7            5.7            7333              166k         44100.04         44100.31
         7.999363382 "player.c:2491"         -1.79            2.8            2.8            7349              153k         44100.04         44100.30
         7.990332549 "player.c:2491"         -1.87            5.7            5.7            7328              155k         44100.04         44100.29
         8.012287445 "player.c:2491"         -1.84            2.8            2.8            7352              185k         44100.04         44100.28
         7.998929528 "player.c:2491"         -1.94           14.2           14.2            7331              171k         44100.04         44100.28
         8.008170622 "player.c:2491"         -1.88            5.7            5.7            7327              188k         44100.04         44100.27
         8.012935257 "player.c:2491"         -1.87           19.8           19.8            7270              205k         44100.04         44100.26
         8.006337966 "player.c:2491"         -1.86            0.0            0.0            7334              199k         44100.04         44100.25
         8.004206612 "player.c:2491"         -1.76            2.8            2.8            7331              204k         44100.04         44100.25
         7.998495257 "player.c:2491"         -1.82            2.8            2.8            7329              204k         44100.04         44100.24
         8.016859059 "player.c:2491"         -1.88            2.8            2.8            7335              201k         44100.04         44100.23
         7.993529320 "player.c:2491"         -1.92           14.2           14.2            7329              205k         44100.04         44100.22
         8.021956820 "player.c:2491"         -1.96           11.3           11.3            7322              206k         44100.04         44100.21
         8.006401820 "player.c:2491"         -1.93           11.3           11.3            7322              199k         44100.04         44100.21
         8.001469111 "player.c:2491"         -1.89            5.7            5.7            7336              204k         44100.04         44100.20
         7.995194320 "player.c:2491"         -1.86            5.7            5.7            7330              202k         44100.04         44100.19
         8.017805206 "player.c:2491"         -1.99           22.7           22.7            7322              201k         44100.04         44100.18
         7.995541038 "player.c:2491"         -1.95           22.7           22.7            7331              203k         44100.04         44100.18
         8.011463643 "player.c:2491"         -1.87            0.0            0.0            7331              207k         44100.04         44100.17
         7.995941403 "player.c:2491"         -1.85            2.8            2.8            7331              206k         44100.04         44100.16
         8.013330570 "player.c:2491"         -1.71            2.8            2.8            7344              212k         44100.04         44100.15
         8.012214580 "player.c:2491"         -1.82            0.0            0.0            7333              203k         44100.04         44100.15
         7.998585049 "player.c:2491"         -1.85            8.5            8.5            7332              207k         44100.04         44100.14
         8.023448799 "player.c:2491"         -1.86            5.7            5.7            7328              206k         44100.04         44100.13
         7.984586612 "player.c:2491"         -1.87            8.5            8.5            7277              205k         44100.04         44100.12
         8.017339372 "player.c:2491"         -1.91           17.0           17.0            7327              207k         44100.04         44100.12
         8.007090309 "player.c:2491"         -1.94           17.0           17.0            7286              199k         44100.04         44100.11
         8.007894164 "player.c:2491"         -1.87            2.8            2.8            7334              201k         44100.04         44100.10
         7.991787497 "player.c:2491"         -1.89            2.8            2.8            7327              194k         44100.04         44100.09
         8.019011611 "player.c:2491"         -1.94            5.7            5.7            7323              185k         44100.04         44100.09
         7.994695362 "player.c:2491"         -1.99           14.2           14.2            7321              199k         44100.04         44100.08
         8.010284632 "player.c:2491"         -1.93           11.3           11.3            7325              191k         44100.04         44100.07
         8.011451612 "player.c:2491"         -1.87            8.5            8.5            7329              156k         44100.04         44100.09
         7.993112549 "player.c:2491"         -1.84            2.8            2.8            7330              150k         44100.04         44100.13
         8.004971455 "player.c:2491"         -1.77           11.3           11.3            7325              157k         44100.04         44100.16
         8.010517133 "player.c:2491"         -1.85            0.0            0.0            7322              182k         44100.04         44100.19
         8.005598851 "player.c:2491"         -2.00           39.7           39.7            7207              180k         44100.04         44100.22
         8.006777965 "player.c:2491"         -1.99           39.7           39.7            7320              183k         44100.04         44100.24
         7.115508696 "player.c:1643" Connection 1: Playback Stopped. Total playing time 03:37:20. Output: 44100.04 (raw), 44100.24 (corrected) frames per second.
```
For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time.
