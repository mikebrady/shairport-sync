# Shairport Sync Architecture

Shairport Sync is an AirPlay audio receiver for Linux, FreeBSD, and OpenBSD. It supports both "classic" AirPlay (AirPlay 1) and AirPlay 2 protocols. This document describes the internal architecture, protocols, and data flow.

AirPlay is Apple's proprietary protocol suite for wireless media streaming, built on top of standard protocols (mDNS, RTSP, RTP, NTP/PTP) with custom extensions. The audio streaming subset was originally called **AirTunes** and later renamed to AirPlay. The underlying audio protocol is formally known as **RAOP** (Remote Audio Output Protocol), which is based on RTSP/RTP. The protocol has never been officially published and all implementations are based on reverse engineering.

**Key references for protocol details:**
- [Unofficial AirPlay Protocol Specification](https://openairplay.github.io/airplay-spec/) (covers AirPlay 1, based on iOS 5.1 / tvOS 5.0)
- [airplay2-receiver](https://github.com/openairplay/airplay2-receiver) (Python reference implementation of AirPlay 2)
- [pyatv protocol docs](https://pyatv.dev/documentation/protocols/) (comprehensive reverse engineering documentation)
- [AirPlay 2 Internals](https://emanuelecozzi.net/docs/airplay2) (work-in-progress protocol analysis)<｜｜DSML｜｜parameter name="replaceAll" string="false">false

---

## Table of Contents

1. [Protocol Overview](#1-protocol-overview)
2. [Network Discovery (mDNS/Bonjour)](#2-network-discovery-mdnsbonjour)
3. [AirPlay 1 (Classic) Protocol](#3-airplay-1-classic-protocol)
4. [AirPlay 2 Protocol](#4-airplay-2-protocol)
5. [RTSP Session Lifecycle](#5-rtsp-session-lifecycle)
6. [Audio Pipeline](#6-audio-pipeline)
7. [Clock Synchronization](#7-clock-synchronization)
8. [Pairing & Encryption](#8-pairing--encryption)
9. [Auxiliary Services](#9-auxiliary-services)
10. [Threading Model](#10-threading-model)
11. [Build System & Dependencies](#11-build-system--dependencies)
12. [Source File Map](#12-source-file-map)

---

## 1. Protocol Overview

```
                        Apple Device (iOS/macOS/Apple TV)
                                  |
                   +--------------+--------------+
                   |              |              |
               mDNS/Bonjour    RTSP (TCP)    RTP (UDP)
              (discovery)    (session ctrl)  (audio data)
                   |              |              |
                   v              v              v
            +-------------------------------------------+
            |         Shairport Sync (Linux/BSD)         |
            |                                           |
            |  mdns.c  ->  rtsp.c  ->  rtp.c            |
            |                 |            |             |
            |                 v            v             |
            |           player.c (decode, DSP, sync)     |
            |                 |                          |
            |                 v                          |
            |         audio_alsa.c (DAC output)          |
            +-------------------------------------------+
```

AirPlay operates over three main transports:

| Layer | Protocol | Transport | Port | Purpose |
|-------|----------|-----------|------|---------|
| Discovery | mDNS/DNS-SD | UDP | 5353 | Advertise the receiver on the local network |
| Session Control | RTSP (Apple variant) | TCP | 7000 | Negotiate session, set up streams, manage playback |
| Audio Data | RTP (Apple variant) | UDP | ephemeral | Deliver encrypted, timestamped audio packets |

### AirPlay 1 vs AirPlay 2

| Aspect | AirPlay 1 (Classic) | AirPlay 2 |
|--------|---------------------|-----------|
| Audio Format | ALAC/S16/44100/stereo only | ALAC + AAC; S16, S24, F24; 44100 + 48000; stereo + 5.1 + 7.1 |
| Stream Type | Realtime only | Realtime + Buffered |
| Latency | ~2.0-2.25 seconds | ~0.5 seconds (buffered), ~2 seconds (realtime) |
| Clock Sync | NTP variant | PTP via NQPTP daemon |
| Encryption | RSA + AES (AirPlay auth) | HomeKit pairing + libsodium (Curve25519, Ed25519) |
| mDNS Service | `_raop._tcp` | `_raop._tcp` + `_airplay._tcp` |

---

## 2. Network Discovery (mDNS/Bonjour)

Shairport Sync advertises itself on the local network using multicast DNS, allowing Apple devices to discover it.

### Architecture

```
shairport.c (main)
    |
    v
mdns.c (dispatcher)
    |
    +---> mdns_avahi.c        (Avahi, recommended on Linux)
    +---> mdns_dns_sd.c       (Apple dns-sd API)
    +---> mdns_tinysvcmdns.c  (Built-in lightweight mDNS)
    +---> mdns_external.c     (Shells out to external tool)
    |
    v
bonjour_strings.c  (generates TXT record strings)
```

### mDNS Service Records

**Classic AirPlay** advertises one service:
- `_raop._tcp` — remote audio output protocol

**AirPlay 2** advertises two services:
- `_raop._tcp` — carries classic-compatible TXT records (`ft`, `sf`, `fv`, `am`, `pk`, etc.)
- `_airplay._tcp` — carries AirPlay 2 TXT records (`features`, `flags`, `deviceid`, `pi`, `psi`, `gid`, `protovers`, etc.)

### Key TXT Record Fields

| Field | Meaning |
|-------|---------|
| `ft` | Feature flags (codec support, metadata, etc.) |
| `sf` | Status flags (device state) |
| `fv` | Firmware version string |
| `am` | Model name |
| `pk` | Public key (base64, for encryption) |
| `deviceid` | AirPlay 2 device identifier (MAC-style) |
| `features` | AirPlay 2 feature bitmask (hex) |
| `pi` | Permanent identifier |
| `gid` | Group identifier (for multi-room sync) |
| `protovers` | Protocol version ("1.1") |

The TXT records are built by `bonjour_strings.c:build_bonjour_strings()` and can be dynamically updated (e.g., when group membership changes).

---

## 3. AirPlay 1 (Classic) Protocol

### Session Flow

```
Source (iOS/Mac)                              Shairport Sync
     |                                              |
     |---- OPTIONS --------------------------------->|  (capability query)
     |<--- 200 OK (Apple-Response header) ----------|
     |                                              |
     |---- ANNOUNCE (SDP body, AES key) ----------->|  (stream description)
     |<--- 200 OK ----------------------------------|
     |                                              |
     |---- SETUP (client/server UDP ports) -------->|  (RTP port negotiation)
     |<--- 200 OK (server UDP ports) --------------|
     |                                              |
     |==== RTP Audio Stream (UDP, AES encrypted) ==>|
     |                                              |
     |---- RECORD (start playback) ---------------->|  (can include start time)
     |<--- 200 OK ----------------------------------|
     |                                              |
     |---- GET /info ------------------------------>|  (capability query)
     |<--- 200 OK (Apple plist XML) ----------------|
     |                                              |
     |---- SET_PARAMETER (volume, metadata) ------->|
     |<--- 200 OK ----------------------------------|
     |                                              |
     |---- TEARDOWN ------------------------------->|
     |<--- 200 OK ----------------------------------|
```

### Complete RTSP Method Set

| Method | Direction | Purpose |
|--------|-----------|---------|
| `OPTIONS` | Client -> Server | Query supported methods; server responds with `Apple-Response` header |
| `GET /info` | Client -> Server | Fetch device capabilities (binary plist: device ID, features, supported formats) |
| `ANNOUNCE` | Client -> Server | Declare stream via SDP body; includes AES key encrypted with RSA public key |
| `SETUP` | Client -> Server | Initialize RTP transport; negotiate UDP ports (control port, timing port, data ports) |
| `RECORD` | Client -> Server | Begin audio streaming; specifies initial RTP sequence number and timestamp in `RTP-Info` |
| `FLUSH` | Client -> Server | Stop/pause playback |
| `TEARDOWN` | Client -> Server | Terminate the RTSP session |
| `SET_PARAMETER` | Client -> Server | Volume control, metadata (DAAP tags), cover art (JPEG) |
| `GET_PARAMETER` | Client -> Server | Query a parameter value |
| `POST /pair-setup` | Bidirectional | HomeKit Secure Remote Password (SRP) pairing, 6-step exchange (M1-M6) |
| `POST /pair-verify` | Bidirectional | Verify a previously-paired session, 4-step exchange (M1-M4) |
| `POST /auth-setup` | Bidirectional | MFi authentication (Curve25519 ECDH + RSA-1024 signature) |
| `POST /fp-setup` | Bidirectional | FairPlay DRM key exchange for encrypted content from iOS devices |
| `POST /pair-pin-start` | Bidirectional | Start PIN code pairing |
| `POST /pair-add` | Bidirectional | Add a new paired client (called by Home app) |
| `POST /pair-remove` | Bidirectional | Remove a paired client |
| `POST /pair-list` | Bidirectional | List currently paired clients |
| `SETPEERS` | Client -> Server | Advertise PTP timing peer addresses (AirPlay 2) |
| `FLUSHBUFFERED` | Client -> Server | Flush buffered audio with binary plist containing `flushUntilSeq` and `flushUntilTS` |
| `POST /command` | Client -> Server | Event channel commands for remote control (AirPlay 2) |
| `POST /feedback` | Client -> Server | Keep-alive message every 2 seconds (AirPlay 2) |

### Session Lifecycle (Full)

```
OPTIONS -> GET /info -> [POST /fp-setup (FairPlay)] -> POST /auth-setup (MFi)
    -> ANNOUNCE (SDP + AES key) -> SETUP (ports) -> RECORD (start)
    -> [FLUSH -> RECORD]* -> TEARDOWN
```

### Key RTSP Headers

| Header | Purpose |
|--------|---------|
| `Apple-Response` | AES session key encrypted with receiver's public RSA key (base64) |
| `CSeq` | Command sequence number |
| `Session` | Session identifier (from SETUP response) |
| `DACP-ID` | 64-bit DACP server identifier for metadata/remote control |
| `Active-Remote` | Authentication token for DACP remote control commands |
| `Client-Instance` | Instance identifier for the client |
| `Audio-Latency` | Target audio latency in frames (e.g. 2205 = ~50ms at 44.1kHz) |
| `Audio-Jack-Status` | Headphone/speaker jack status (in SETUP response) |
| `X-Apple-ProtocolVersion` | Protocol version negotiation |
| `X-Apple-Client-Name` | Client device name |
| `Transport` | RTP port and channel specification, e.g.: `RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=X;timing_port=Y` |
| `RTP-Info` | Initial stream parameters: `seq=<16bit>;rtptime=<32bit>` (randomized start values) |

### RTP Audio Format

- **Codec**: ALAC (Apple Lossless Audio Codec)
- **Sample Rate**: 44,100 Hz
- **Bit Depth**: 16-bit signed integer
- **Channels**: Stereo
- **Encryption**: AES-128-CBC with per-packet initialization vector (IV derived from RTP timestamp + sequence number)
- **Packet Size**: 352 frames per packet
- **Timestamp**: 44,100 Hz clock, wraps at 32 bits
- **ALAC Cookie**: 36-byte magic cookie (`alac` tag, version 0, ALACSpecificConfig) carried in SDP `fmtp` attribute

### The RSA/AES Key Exchange

```
1. Server generates an RSA-1024 or RSA-2048 keypair at startup
2. Public key is advertised in mDNS TXT record ("pk" field, base64-encoded)
3. Client generates a random 128-bit AES session key
4. Client encrypts AES key with server's RSA public key (PKCS#1 OAEP padding)
5. Client includes encrypted key in ANNOUNCE SDP body: a=rsaaeskey:<base64 ciphertext>
6. Server decrypts with its private key to obtain the AES session key
7. All subsequent RTP payloads are encrypted with AES-128-CBC using this key
8. Initialization vector for each packet derived from RTP timestamp and sequence number:
   - AES-CBC key = session key
   - IV = MD5(session_key || audio_fixed_iv)[0..15] XOR'd with packet sequence/timestamp
```

### Timing (NTP-based)

Classic AirPlay uses a variant of NTP for clock synchronization:
1. The source sends time query messages on the timing port (announced in SETUP)
2. The receiver responds with its local time
3. The receiver computes `local_to_remote_time_offset` to map source timestamps to local DAC timing
4. Packets are played at: `local_play_time = rtp_timestamp + local_to_remote_time_offset + audio_latency`

---

## 4. AirPlay 2 Protocol

### AirPlay 2 RTSP Methods (in addition to AirPlay 1 set)

| Method | Purpose |
|--------|---------|
| `SETPEERS` | Advertises PTP timing peer IP addresses to the receiver; receiver forwards to NQPTP |
| `FLUSHBUFFERED` | Flush buffered audio pipeline with binary plist specifying `flushUntilSeq` and `flushUntilTS` |
| `POST /command` | Event channel commands (remote control operations) |
| `POST /feedback` | Keep-alive sent by the source every 2 seconds; receiver echoes back |

### AirPlay 2 Session Flow

```
1. OPTIONS (standard)
2. GET /info (binary plist response with device capabilities)
3. POST /pair-setup (HomeKit SRP, M1-M6 exchange; PIN displayed on receiver's screen)
4. POST /pair-verify (HomeKit verification, M1-M4 exchange; establishes shared secret)
5. SETUP (optional isRemoteControlOnly first, then main stream SETUP)
   - Negotiates PTP timing peers
   - Provides event channel TCP port (for protobuf events)
   - Provides remote control channel TCP port
6. SETPEERS (source sends PTP peer list; forwarded to NQPTP)
7. ANNOUNCE (SDP with FairPlay-encrypted AES key: a=fpaeskey:)
8. RECORD (start playback with initial seq/rtptime)
9. RTP audio streams begin (realtime and/or buffered)
10. POST /feedback keep-alive every 2 seconds
11. FLUSHBUFFERED or FLUSH to stop
12. TEARDOWN to end session
```

### Session Architecture

```
                                NQPTP Daemon
                                (separate process,
                                 ports 319/320)
                                     |
                              shared memory (/nqptp)
                                     |
Source ---RTSP (TCP:7000)----> rtsp.c
           |                        |
           +-- /pair-setup -------->|  pair_ap library (HomeKit SRP)
           +-- /pair-verify ------->|  Curve25519, Ed25519
           +-- SETUP --------------->|  PTP time, event/RC ports
           +-- ANNOUNCE (SDP)------>|  AAC/ALAC, multi-channel
                                     |
            --RTP UDP packets------> rtp.c
                                     |
            --TCP (event port)-----> ap2_event_receiver.c  (protobuf events)
            --TCP (RC port)--------> ap2_rc_event_receiver.c (remote control)
                                     |
                                     v
                            ap2_buffered_audio_processor.c
                            (decrypt, add ADTS for AAC)
                                     |
                                     v
                               player.c
```

### Stream Types

**Realtime Audio** (also called "Classic AirPlay" inside AP2):
- ALAC/S16/44100/2 only
- Same packet format as AirPlay 1, but encrypted with AP2 session keys
- ~2 seconds latency

**Buffered Audio**:
- AAC/F24/44100/2 or 48000/2 (standard)
- AAC/F24/48000/5.1 or 7.1 (surround)
- ALAC/S24/48000/2 (lossless)
- Encrypted with libsodium (ChaCha20-Poly1305)
- ~0.5 seconds latency
- Data arrives faster than playback, buffered locally

### RTP SSRC Values

Each audio format is identified by a distinct SSRC value in buffered audio RTP packets:

| SSRC | Format |
|------|--------|
| `0x15000000` | ALAC 48000/S24/2 |
| `0x16000000` | AAC 44100/F24/2 |
| `0x17000000` | AAC 48000/F24/2 |
| `0x27000000` | AAC 48000/F24/5.1 |
| `0x28000000` | AAC 48000/F24/7.1 |

### Buffered Audio Processing

The `ap2_buffered_audio_processor.c` module:
1. Receives encrypted buffered audio packets
2. Decrypts using libsodium (`crypto_aead_chacha20poly1305_ietf_decrypt`)
3. For AAC format: adds ADTS headers to make raw AAC data playable by FFmpeg
4. For ALAC format: passes through FFmpeg's ALAC decoder
5. Uses 23-bit modular arithmetic (`utilities/mod23.c`) for packet sequence tracking (wraps at `2^23`)

### Event Streams

AirPlay 2 uses Protocol Buffers for two auxiliary TCP streams:

**Event Receiver** (`ap2_event_receiver.c`):
- Receives timing and control events from the source
- Group membership info (group ID, group leader status)
- Client name identification
- Updates mDNS TXT records dynamically

**Remote Control Event Receiver** (`ap2_rc_event_receiver.c`):
- Receives remote control commands (play, pause, skip, volume)
- Separate TCP connection from the source

### PTP Clock Synchronization

AirPlay 2 uses Precision Time Protocol (IEEE 1588) and possibly IEEE 802.1AS ("Timing and Synchronization for Time-Sensitive Applications") via the external **NQPTP** ("Not Quite PTP") daemon.

**Design rationale** (from the NQPTP author): AirPlay 2's synchronization mechanism is not fully understood. Apple devices seem to use IEEE 1588 PTP with custom extensions, possibly including power-management messages for sleeping clocks. NQPTP takes a pragmatic "end-run" approach:
- It passively monitors PTP signals coming from the AirPlay source on ports 319/320
- It uses these signals to sync the local clock to the player's clock
- It does **not** actively participate in PTP interactions — it does not originate PTP messages, respond to PTP signals, or participate in clock mastership elections
- This approach works on any network connection without requiring special timing hardware

**Interface between NQPTP and Shairport Sync:**
- NQPTP runs as a separate process, owning UDP ports 319 and 320 (typically requires root or `CAP_NET_BIND_SERVICE`)
- Communicates with Shairport Sync through POSIX shared memory at `/nqptp`
- Provides `local_to_master_time_offset` for converting source timestamps to local time
- Uses `__sync_synchronize()` memory barriers for lock-free concurrent access (dual-record consistency protocol)
- Control protocol via UDP port 9000 for commands:
  - `T` — set timing peers (IP list; first IP is the clock to follow)
  - `B` — begin play (alerts NQPTP that the master clock is active and won't sleep)
  - `E` — end play (clock may sleep)
  - `P` — paused (buffered audio only; clock keeps running)

**Clock smoothing behavior:**
- When the clock is **active**, NQPTP smooths the offset by clamping decreases to a small value (following clock drift, ignoring network delays)
- When the clock goes from inactive to active, NQPTP resets smoothing to the new offset (avoids treating a clock restart as a network delay)

See `nqptp-shm-structures.h` and `ptp-utilities.c`.

---

## 5. RTSP Session Lifecycle

The session lifecycle is managed by `rtsp.c` (5589 lines), the core protocol handler.

### Main Loop

```
shairport.c: main()
    |
    v
rtsp_listen_loop()          // accepts TCP connections on port 7000
    |
    +--- pthread_create ---> rtsp_connection_thread()
                                |
                                +-- handle_request()  // parse RTSP method/URL
                                |     |
                                |     +-- handle_options()
                                |     +-- handle_announce()    // SDP, AES key
                                |     +-- handle_setup()       // UDP port setup
                                |     +-- handle_record()      // start playback
                                |     +-- handle_flush()       // flush buffers
                                |     +-- handle_teardown()    // end session
                                |     +-- handle_set_parameter()
                                |     +-- handle_get_parameter()
                                |     +-- handle_get_info()    // plist response
                                |     +-- handle_pair_setup()  // AP2 only
                                |     +-- handle_pair_verify() // AP2 only
                                |
                                +-- player thread  (spawned on RECORD)
                                +-- rtp thread     (spawned on SETUP)
                                +-- AP2 event threads (spawned on AP2 SETUP)
```

### Connection State Machine

The `rtsp_conn_info` structure (defined in `common.h`) tracks all per-connection state including:
- `connection_number` — unique per-connection identifier
- `airplay_stream_category` — type of stream (ptp, buffered, realtime)
- `client_ip_string` — source IP address
- `latency` — negotiated audio latency
- Encryption keys (AES, RSA)
- Audio format parameters (samplerate, bit depth, channels)
- Player thread and RTP thread IDs
- DACP server connection info

### AirPlay 2 SETUP Differences

In AirPlay 2, the initial SETUP:
1. Sets the connection as the `principal_conn` (master connection that controls mDNS)
2. Negotiates PTP timing peer addresses
3. Opens event receiver and remote control receiver TCP ports
4. Sends timing peer list to NQPTP daemon via UDP control port

### ANNOUNCE (SDP Parsing)

The ANNOUNCE carries a Session Description Protocol (SDP) body with:
- `a=rsaaeskey:` — encrypted AES session key (AirPlay 1 style)
- `a=fpaeskey:` — encrypted AES key using FairPlay (AirPlay 2)
- `a=fmtp:` — ALAC format parameters (cookie, frame length)
- `a=rtpmap:` — RTP payload type mapping
- Audio codec, sample rate, channel count

---

## 6. Audio Pipeline

### Complete Data Flow

```
RTP UDP Packets
      |
      v
rtp.c  (receive, buffer, sequence-check)
      |
      |  abuf_t ring buffer  (BUFFER_FRAMES = 1024 entries)
      |  each entry: 352 frames of decoded audio
      |
      v
player.c  (main decoding/playback loop)
      |
      +-- 1. ALAC Decode
      |      alac.c (Hammerton)  OR  apple_alac.cpp  OR  FFmpeg (libavcodec)
      |
      +-- 2. AES Decrypt         (AirPlay 1: AES-128-CBC)
      |    ap2_buffered_audio_processor.c (AirPlay 2: libsodium ChaCha20)
      |
      +-- 3. Resampling / Interpolation
      |      ST_basic:  insert/drop frames in 352-frame packets
      |      ST_vernier: interpolate 352/1024 <-> 353/1025 or 351/1023
      |      ST_soxr:    high-quality libsoxr resampling
      |
      +-- 4. Volume Control
      |      Attenuation model (tangentsoft.net/audio/atten.html)
      |      Hardware mixer  OR  software attenuation
      |      Active in decoder or in backend
      |
      +-- 5. Loudness Filter  (optional)
      |      loudness.c: biquad filter, bass boost at low volumes
      |      Based on Fletcher-Munson equal-loudness contours
      |
      +-- 6. Convolution DSP  (optional)
      |      FFTConvolver/: partitioned FFT convolution
      |      FIR filters loaded from WAV/audio files
      |      Multi-threaded per channel (ConvolverThreadPool)
      |
      +-- 7. Format Conversion
      |      FFmpeg swresample: rate, format, channel layout conversion
      |      Transcoding: 44100<->48000, S16<->S24<->F24
      |      Mixdown: 5.1/7.1 to stereo, channel remapping
      |
      +-- 8. Timing Correction
      |      Master clock: local DAC frame counter
      |      Computes sync_error = desired vs actual play position
      |      Applies correction via resampling ratio
      |
      v
audio.c  (backend dispatcher)
      |
      +---> audio_alsa.c    (ALSA direct DAC, best timing)
      +---> audio_pw.c      (PipeWire async, good timing)
      +---> audio_pa.c      (PulseAudio async, moderate timing)
      +---> audio_jack.c    (JACK, professional audio)
      +---> audio_sndio.c   (sndio, BSD systems, good timing)
      +---> audio_pipe.c    (Unix named pipe, partial sync)
      +---> audio_stdout.c  (STDOUT, partial sync)
      +---> audio_dummy.c   (discards audio, testing only)
```

### Backend Interface

All audio backends implement the `audio_output` struct (`audio.h`):

```c
typedef struct {
    void (*init)(int argc, char **argv);
    void (*deinit)(void);
    int  (*prepare)(void);
    void (*start)(int sample_rate, int sample_format);
    void (*stop)(void);
    int  (*play)(void *buf, int samples);
    void (*delay)(long *the_delay);        // DAC delay in frames
    void (*volume)(double vol);            // hardware volume control
    void (*parameters)(audio_parameters *info);
    void (*mute)(int do_mute);
    // ...
} audio_output;
```

### ALSA Synchronization

ALSA provides the most precise timing:
- `snd_pcm_delay()` returns exact number of frames currently in the DAC hardware buffer
- Shairport Sync computes a `sync_error` comparing the expected frame position to the actual DAC position
- Correction is applied by adjusting the resampling rate (slightly faster or slower)
- The correction loop runs continuously, tracking drift at the parts-per-million level

### Decoders

| Decoder | Source | Use Case |
|---------|--------|----------|
| Hammerton ALAC | `alac.c` | Pure-C ALAC decoder (no dependencies); used when FFmpeg is unavailable |
| Apple ALAC | `apple_alac.cpp` | Apple's proprietary library wrapper (macOS only; deprecated) |
| FFmpeg ALAC | libavcodec | ALAC decoding via FFmpeg (primary choice when available) |
| FFmpeg AAC | libavcodec | AAC decoding for AirPlay 2 buffered audio (requires `fltp` — floating planar — support) |

### ALAC Magic Cookie

The ALAC decoder is configured via a 36-byte "magic cookie" (ALACSpecificConfig):
- `cookie_size` (4 bytes): 36 (0x24)
- `cookie_tag` (4 bytes): `'alac'` (0x616C6163)
- `cookie_version` (4 bytes): 0
- `frameLength` (4 bytes): frames per packet (default 352)
- `compatibleVersion` (1 byte): 0
- `bitDepth` (1 byte): source bit depth (16 for AirPlay 1, 24 for AP2 lossless)
- `numChannels` (1 byte): 1 = mono, 2 = stereo
- `sampleRate` (4 bytes): 44100 or 48000

---

## 7. Clock Synchronization

```
+------------------+          +------------------+          +------------------+
|  Apple Source    |          | NQPTP Daemon     |          | Shairport Sync   |
| (Grandmaster)    |          | (ports 319/320)  |          |                  |
+--------+---------+          +--------+---------+          +--------+---------+
         |                            |                             |
         |<==== PTP Sync ============>|                             |
         |                            |                             |
         |                            |=== shared memory (/nqptp)==>|
         |                            |    local_to_master_offset   |
         |                            |                             |
         |==== audio RTP (timestamps in master time) ==============>|
         |                            |                             |
         |                            |        convert to local time|
         |                            |        schedule playback    |
         |                            |        at DAC frame #       |
```

### AirPlay 1 Timing

Uses an NTP-like protocol:
1. Source sends `nqptp` time query
2. Receiver responds with local time
3. Receiver computes offset: `local_to_remote_time_offset`
4. RTP packets carry timestamps in source clock
5. Receiver converts: `local_play_time = rtp_timestamp + local_to_remote_time_offset + latency`

### AirPlay 2 Timing (PTP)

Uses Precision Time Protocol via NQPTP:
1. NQPTP daemon runs as a separate process on ports 319/320
2. Synchronizes with the Apple source (PTP grandmaster)
3. Writes clock offset data to POSIX shared memory at `/nqptp`
4. Shairport Sync reads this via `ptp-utilities.c:get_nqptp_data()`
5. Dual-record structure with `__sync_synchronize()` ensures consistent reads
6. Converts remote (master) timestamps on RTP packets to local DAC time

### Shared Memory Structure

```c
struct shm_structure {
    uint16_t version;               // NQPTP_SHM_STRUCTURES_VERSION (10)
    shm_structure_set main;         // primary record
    shm_structure_set secondary;    // backup record for consistency checking
};

typedef struct {
    uint64_t master_clock_id;
    uint64_t local_time;
    uint64_t local_to_master_time_offset;  // ADD to local to get master time
    uint64_t master_clock_start_time;
} shm_structure_set;
```

### Resampling

To maintain synchronization with source clock drift:
- `ST_basic`: Insert or drop one frame from a 352-frame packet (coarse, ~0.3%)
- `ST_vernier`: Smooth interpolation using ratio 352/1024, 353/1025, or 351/1023 (fine)
- `ST_soxr`: High-quality resampling using libsoxr (best quality, higher CPU)

---

## 8. Pairing & Encryption

### AirPlay 1 Encryption

- **Key Exchange**: Source encrypts a random AES session key with the receiver's public RSA key
- **AES Key**: Delivered in the `Apple-Response` header of the ANNOUNCE response
- **Audio Encryption**: AES-128-CBC on RTP audio payloads
- **HMAC**: Uses a 64-bit per-packet initialization vector derived from the RTP timestamp

### AirPlay 2 Pairing (HomeKit)

Uses the `pair_ap` library (adapted from ejurgensen/pair_ap):

```
Source                           Shairport Sync
  |                                    |
  |---- POST /pair-setup (M1) -------->|  step 1: SRP start request
  |<---- (M2) SRP salt + B ------------|
  |---- (M3) SRP proof --------------->|
  |<---- (M4) SRP proof + verify ------|
  |                                    |  (keys stored for future /pair-verify)
  |                                    |
  |---- POST /pair-verify (M1) ------->|  step 1: Curve25519 public key
  |<---- (M2) public key + signature --|
  |---- (M3) signature --------------->|
  |<---- (M4) verified ----------------|
  |                                    |  (session shared secret established)
```

**Crypto algorithms** (all via libsodium):
- **SRP** (Secure Remote Password): Password-authenticated key exchange
- **Curve25519**: Elliptic curve Diffie-Hellman for session key
- **Ed25519**: Edwards-curve digital signatures for authentication
- **ChaCha20-Poly1305**: Authenticated encryption for audio data

**Pairing types** (`pair_ap/pair.h`):
- `PAIR_CLIENT_HOMEKIT_NORMAL` — full PIN-based setup + verification
- `PAIR_CLIENT_HOMEKIT_TRANSIENT` — fixed PIN (3939), shorter exchange, session key only
- `PAIR_CLIENT_FRUIT` — Apple TV device verification (tvOS 10.2+)
- `PAIR_SERVER_HOMEKIT` — server-side implementation

### AirPlay 2 Audio Encryption

- **Buffered audio**: ChaCha20-Poly1305 IETF variant, per-packet nonce derived from RTP sequence and SSRC
- **Realtime audio**: Same AES as AirPlay 1, but keys derived from AP2 session
- Key material: 32-byte shared secret from `/pair-verify`

### FairPlay Encryption (iOS Content)

AirPlay 2 also supports FairPlay DRM-decrypted content:
- `a=fpaeskey:` SDP attribute carries FairPlay-encrypted AES key
- Requires decryption before standard AES/ChaCha20 processing
- Enables streaming of DRM-protected content

---

## 9. Auxiliary Services

### DACP (Digital Audio Control Protocol)

`dacp.c` communicates with the source (iTunes/Music app) over HTTP to:
- Fetch track metadata (artist, album, title, artwork URL)
- Control playback remotely (play, pause, skip, volume)
- Uses the `tinyhttp` library for HTTP parsing (chunked encoding, headers)
- Runs as a client making requests to the DACP server on the source device

### Metadata Hub

`metadata_hub.c` manages metadata distribution:
- Receives metadata from DACP or from inline RTSP metadata (AP2 SET_PARAMETER)
- Stores current track info (artist, album, title, genre, artwork)
- Distributes via:
  - Unix pipe (`/tmp/shairport-sync-metadata`) — fixed-format binary messages
  - UDP socket — broadcast to configured port
  - MQTT — publish to broker
  - D-Bus / MPRIS — inter-process communication

### MQTT

`mqtt.c` (using libmosquitto):
- Publishes metadata, artwork, and status to MQTT topics
- Subscribes to control topics for remote playback commands
- Configurable topic prefix, broker address, authentication

### D-Bus Interfaces

**Native D-Bus** (`dbus-service.c`):
- `org.gnome.ShairportSync` interface
- Advanced remote control, diagnostics, system settings
- Generated from `org.gnome.ShairportSync.xml` introspection XML

**MPRIS** (`mpris-service.c`):
- `org.mpris.MediaPlayer2` standard interface
- Metadata, playback status, basic remote control
- Compatible with Linux desktop media player controllers

### Activity Monitor

`activity_monitor.c` implements a three-state machine:
- **idle** → no active playback
- **active** → playing audio
- **timing out** → playback ended, waiting before returning to idle

Can trigger external scripts or D-Bus signals on state transitions. Useful for power management or display control.

---

## 10. Threading Model

Shairport Sync is highly multithreaded. Key threads:

| Thread | Source | Purpose |
|--------|--------|---------|
| Main thread | `shairport.c:main()` | Daemon init, signal handling, control loop |
| RTSP listener | `rtsp.c:rtsp_listen_loop()` | Accepts new TCP connections on port 7000 |
| RTSP connection (per client) | `rtsp.c:rtsp_connection_thread()` | Handles one RTSP session |
| RTP receiver (per stream) | `rtp.c` | Receives and buffers audio UDP packets |
| Player | `player.c` | Decodes, processes, and outputs audio |
| AP2 event receiver | `ap2_event_receiver.c` | Receives protobuf events over TCP |
| AP2 RC event receiver | `ap2_rc_event_receiver.c` | Receives remote control events over TCP |
| Activity monitor | `activity_monitor.c` | Idle/active/timing-out state machine |
| Audio backend (per backend) | `audio_alsa.c` etc. | Hardware-specific I/O callbacks |
| DACP client | `dacp.c` | Fetches metadata from source |
| Metadata writer | `metadata_hub.c` | Writes metadata to pipe/socket |
| MQTT client | `mqtt.c` | Communicates with MQTT broker |

### Locks and Synchronization

- `conn_lock`: per-connection mutex protecting RTSP state
- `playing_conn_lock`: protects the playing/active connection reference
- `principal_conn_lock`: RW lock protecting the principal connection (AP2 mDNS control)
- `pc_queue_lock`: producer-consumer queue locks for metadata
- Memory barriers (`__sync_synchronize()`): lock-free PTP shared memory reads

### Ring Buffer

The audio ring buffer (`abuf_t BUFFER_FRAMES = 1024`) bridges the RTP receiver and player threads:
- Each entry holds one decoded frame packet (352 frames typical)
- BUFIDX(seqno) macro maps sequence numbers to ring positions
- Producer: RTP receiver thread fills entries
- Consumer: Player thread reads and clears entries

---

## 11. Build System & Dependencies

### Configuration (GNU Autotools)

`configure.ac` controls all build-time feature selection via `--with-*` flags:

```
./configure --with-airplay-2 --with-alsa --with-avahi --with-ffmpeg \
            --with-soxr --with-convolution --with-dbus-interface \
            --with-mpris-interface --with-mqtt --with-metadata
```

### Key Dependency Categories

| Category | Libraries | Purpose |
|----------|-----------|---------|
| **Audio Backend** | alsa, pipewire-0.3, pulse, jack, sndio, ao | Hardware audio output |
| **Codec** | FFmpeg (libavcodec, libavformat, libavutil, libswresample) | AAC/ALAC decode, format conversion |
| **Encryption** | OpenSSL, mbedTLS, or PolarSSL | AES, RSA, MD5 |
| **AirPlay 2** | libsodium, libgcrypt, libplist (≥2.0), libuuid | ChaCha20, Ed25519, plist parsing |
| **mDNS** | Avahi (libavahi-client), dns-sd (libdns_sd) | Network discovery |
| **DSP** | libsndfile, libsoxr, FFTW (or Accelerate/Ooura) | FIR filters, resampling, FFT |
| **D-Bus/MPRIS** | glib-2.0, gio-unix-2.0 | Inter-process communication |
| **MQTT** | libmosquitto | Home automation integration |
| **General** | libpopt, libconfig, libdaemon, pthread | CLI parsing, config files, daemonization |

### Build Artifacts

- `Makefile.am` defines conditional compilation per backend
- Static libraries: `lib_pair_ap.a`, `lib_tinyhttp.a`
- Code generation: D-Bus/MPRIS interface code from XML, plist XML → C via `plistutil`/`xxd`
- Rust rewrite (`shairport-rs/`) is a work-in-progress built with Cargo

---

## 12. Source File Map

### Core Protocol Stack

```
shairport.c              Main entry, daemon init, config loading
common.h / common.c      Global types, config parsing, utilities
definitions.h            Platform portability (OS detection, socket types)
config.h                 Auto-generated feature flags from configure

rtsp.c / rtsp.h          RTSP protocol handler (session control)
rtp.c / rtp.h            Apple RTP audio receiver (packet handling)
player.c / player.h      Slave-clocked ALAC stream player (decode pipeline)

alac.c / alac.h          Hammerton's pure-C ALAC decoder (no FFmpeg)
apple_alac.cpp / .h      Apple ALAC library wrapper (deprecated, macOS)
```

### AirPlay 2 Modules

```
ap2_buffered_audio_processor.c/h   Buffered audio decrypt + AAC ADTS headers
ap2_event_receiver.c/h            Protobuf event stream (group, timing, metadata)
ap2_rc_event_receiver.c/h         Remote control event stream
ptp-utilities.c/h                  PTP clock read from NQPTP shared memory
nqptp-shm-structures.h            Shared memory interface definition
```

### Audio Backends

```
audio.c / audio.h          Dispatcher + backend interface
audio_alsa.c               ALSA (Linux, best timing)
audio_pw.c                 PipeWire (async, native)
audio_pa.c                 PulseAudio (async, compatibility)
audio_jack.c               JACK (professional, ring buffer)
audio_sndio.c              sndio (BSD, good timing)
audio_ao.c                 libao (generic, limited sync)
audio_soundio.c            libsoundio (deprecated)
audio_pipe.c               Unix pipe output
audio_stdout.c             STDOUT output
audio_dummy.c              Null output (testing)
```

### Network Discovery

```
mdns.c / mdns.h            mDNS backend dispatcher
mdns_avahi.c               Avahi backend (Linux, recommended)
mdns_dns_sd.c              dns-sd backend (Bonjour API)
mdns_tinysvcmdns.c         Built-in lightweight mDNS
mdns_external.c            External command-line mDNS
tinysvcmdns.c / .h         TinySVCmDNS library
bonjour_strings.c / .h     TXT record generation (AirPlay feature flags)
```

### Pairing & Encryption

```
pair_ap/pair.c / .h            Core pairing protocol (HomeKit, Fruit)
pair_ap/pair_homekit.c         HomeKit SRP pairing implementation
pair_ap/pair_fruit.c           Apple TV device verification
pair_ap/pair-tlv.c / .h        TLV message encoding/decoding
pair_ap/pair-internal.h        Internal pairing structures
pair_ap/client-example.c       Pairing client example
pair_ap/server-example.c       Pairing server example
pair_ap/evrtsp/rtsp.c / .h     Lightweight embedded RTSP server (libevent)
```

### Metadata & Control Services

```
metadata_hub.c / .h        Metadata storage and distribution
dacp.c / dacp.h            DACP protocol client (iTunes/Music communication)
dbus-service.c / .h        Native D-Bus interface (org.gnome.ShairportSync)
mpris-service.c / .h       MPRIS D-Bus interface (org.mpris.MediaPlayer2)
mqtt.c / mqtt.h            MQTT client (metadata + remote control)
```

### Audio DSP

```
loudness.c / .h            Biquad loudness filter (Fletcher-Munson)
FFTConvolver/convolver.c/h     C wrapper for FFT convolution
FFTConvolver/FFTConvolver.cpp  Partitioned FFT convolution engine
FFTConvolver/AudioFFT.cpp      FFT backend (FFTW/Accelerate/Ooura)
FFTConvolver/ConvolverThreadPool.cpp  Per-channel thread pool
```

### Libraries (Vendored)

```
tinyhttp/http.c / .h          HTTP response parser (for DACP)
tinyhttp/header.c / .h        HTTP header parser
tinyhttp/chunk.c / .h         HTTP chunked transfer decoder
utilities/debug.c / .h        Thread-safe debug logging
utilities/network_utilities.c/h  EINTR-safe accept, address formatting
utilities/buffered_read.c/h   TCP buffered line reader (for AP2 events)
utilities/structured_buffer.c/h  Growable byte buffer (for protobuf parsing)
utilities/mod23.c / .h        23-bit modular arithmetic (AP2 markers)
```

### Rust Rewrite (Work in Progress)

```
shairport-rs/src/main.rs              Rust daemon entry point
shairport-rs/src/airplay/rtsp.rs      Rust RTSP implementation
shairport-rs/src/airplay/rtp.rs       Rust RTP handler
shairport-rs/src/airplay/crypto.rs    Encryption (Rust)
shairport-rs/src/airplay/pairing.rs   HomeKit pairing (Rust)
shairport-rs/src/airplay/tlv.rs       TLV encoding (Rust)
shairport-rs/src/airplay/buffered_audio.rs  AP2 buffered audio (Rust)
shairport-rs/src/airplay/sdp.rs       SDP parsing
shairport-rs/src/airplay/txt_records.rs  mDNS TXT records
shairport-rs/src/ptp/mod.rs           PTP service skeleton
shairport-rs/src/mdns/mod.rs          mDNS advertisement (Rust)
shairport-rs/src/player/mod.rs        Player coordination
shairport-rs/src/audio/mod.rs         CPAL audio device backend
shairport-rs/src/codec/mod.rs         Codec abstraction
shairport-rs/src/decoder/mod.rs       Decoder abstraction
shairport-rs/src/api/mod.rs           HTTP control API
shairport-rs/src/web/mod.rs           Web UI shell
shairport-rs/src/config.rs            TOML configuration
shairport-rs/src/state.rs             Application state
```

---

## 13. Protocol References

The AirPlay protocol is undocumented by Apple and all implementations are reverse-engineered. These are the key community resources:

### Specifications & Documentation

| Resource | Coverage | URL |
|----------|----------|-----|
| Unofficial AirPlay Protocol Specification | AirPlay 1: RAOP, mDNS, RTSP, RTP, encryption, mirroring | [openairplay.github.io/airplay-spec](https://openairplay.github.io/airplay-spec/) |
| airplay2-receiver (Python) | AirPlay 2 reference implementation: pairing, buffered audio, FairPlay | [github.com/openairplay/airplay2-receiver](https://github.com/openairplay/airplay2-receiver) |
| pyatv protocol docs | AirPlay 1 + 2, RAOP, Companion, DACP, MRP | [pyatv.dev/documentation/protocols](https://pyatv.dev/documentation/protocols/) |
| AirPlay 2 Internals | Work-in-progress AP2 reverse engineering notes | [emanuelecozzi.net/docs/airplay2](https://emanuelecozzi.net/docs/airplay2) |
| goplay2 (Go) | Buffered audio AirPlay 2 receiver | [github.com/openairplay/goplay2](https://github.com/openairplay/goplay2) |
| airplay2-rs (Rust) | AirPlay 2 receiver with PTP implementation | [github.com/lmcgartland/airplay2-rs](https://github.com/lmcgartland/airplay2-rs) |
| Shairport Sync NQPTP | PTP companion daemon for AP2 clock sync | [github.com/mikebrady/nqptp](https://github.com/mikebrady/nqptp) |

### Key Community Contributors

- **James Laird** (abrasive) — Original Shairport author, pioneer of AirPlay reverse engineering
- **Mike Brady** — Shairport Sync maintainer, NQPTP author, AP2 implementation
- **ejurgensen** — pair_ap library (HomeKit pairing for AirPlay 2)
- **ckdo** — airplay2-receiver Python implementation (pairing + encryption protocols)
- **invano** — Early AirPlay 2 Python development
- **openairplay** organization — Central repository for AirPlay reverse engineering community

### Historical Context

- **2004**: AirTunes introduced (audio streaming to AirPort Express)
- **2010**: Renamed to AirPlay, Apple TV video support added
- **2011**: Shairport 0.x created (first open-source AirPlay receiver) by James Laird et al.
- **2013**: Shairport (1.0) released
- **2014**: Shairport Sync forked from Shairport 1.0 by Mike Brady (audio synchronization focus)
- **2017**: Apple announces AirPlay 2
- **2018**: iOS 11.4 ships AirPlay 2 (multi-room, buffered audio, PTP)
- **2020**: Shairport Sync v4 adds AirPlay 2 support
- **2025**: Shairport Sync v5.x (current, with multichannel and lossless support)

---

*Document generated from Shairport Sync v5.0.4 source code analysis and community research.*
