# Block Audio Output

The `blockaudio` backend writes audio to a named pipe, like the `pipe` backend, but in records: each block of audio is preceded by a header that gives its format, its RTP timestamp and the time at which its first frame should be played. This means that the program reading the pipe can schedule the audio itself – for example, to keep its own outputs in sync with the other AirPlay speakers the source is playing to – and it will see every change of format as it happens.

To use it, build Shairport Sync with the `--with-blockaudio` configuration flag (see [here](../CONFIGURATION%20FLAGS.md)). To check that it is built in, make sure that the string `blockaudio` is included in the version string.

Then select the backend and, optionally, the pipe in the configuration file:

```
general =
{
	output_backend = "blockaudio";
};

blockaudio =
{
	name = "/tmp/shairport-sync-blockaudio"; // this is the default
};
```

The `output_rate`, `output_format` and `output_channels` settings work just as they do for the `pipe` backend.

## Records

The pipe carries a sequence of records. Each record is a header, followed by `payload_length` bytes of audio. Header-only records have no audio.

Header fields are in the host's native byte order. The play time is only meaningful on the same machine anyway (see [Timing](#timing)).

| Offset | Size | Field | Description |
| ---: | ---: | :--- | :--- |
| 0 | 4 | `magic` | The bytes `SSBA`. Written as bytes, so the same on any host. |
| 4 | 2 | `version` | `1`. |
| 6 | 2 | `header_length` | Length of the header in bytes: `40` in version 1. |
| 8 | 4 | `payload_length` | Length of the audio that follows, in bytes. `0` for header-only records. |
| 12 | 4 | `sequence` | One more than in the record before, including header-only records. Wraps around. |
| 16 | 4 | `header_crc32` | CRC-32 of the first `header_length` bytes, computed with this field set to `0`. |
| 20 | 2 | `flags` | See below. |
| 22 | 2 | reserved | `0`. |
| 24 | 4 | `format` | Format of the audio, encoded as below. |
| 28 | 4 | `rtp` | RTP timestamp of the first frame, at the source's rate. Only meaningful if `timed` is set. |
| 32 | 8 | `play_time` | Time at which the first frame should be played, in nanoseconds. Only meaningful if `timed` is set. |

The CRC-32 is the same as zlib's `crc32()` (reflected polynomial `0xEDB88320`), so you can check it with zlib, which most languages have: in Python, `zlib.crc32()`.

The flags are:

| Bit | Name | Meaning |
| ---: | :--- | :--- |
| 0 | `timed` | `rtp` and `play_time` are valid. Records that are not timed are silence inserted by Shairport Sync. |
| 1 | `flush` | Discard any audio received but not yet played, for example after the source seeks or pauses. |
| 2 | `stop` | The play session has ended. |

Other bits are reserved and are `0`. Flush and stop are sent in header-only records, and both may be set in the same record.

### Format

`format` is Shairport Sync's encoded output format:

| Bits | Content |
| :--- | :--- |
| 0–5 | Sample format, see below. |
| 6–24 | Frames per second, divided by 2. |
| 25–31 | Number of channels. |

The audio is interleaved. The sample formats are:

| Value | Format | Bytes per sample |
| ---: | :--- | ---: |
| 1 | `S8` | 1 |
| 2 | `U8` | 1 |
| 3 | `S16_LE` | 2 |
| 4 | `S16_BE` | 2 |
| 5 | `S24_LE` (in 32 bits) | 4 |
| 6 | `S24_BE` (in 32 bits) | 4 |
| 7 | `S24_3LE` | 3 |
| 8 | `S24_3BE` | 3 |
| 9 | `S32_LE` | 4 |
| 10 | `S32_BE` | 4 |

Header-only records carry the current format, or `0` if no format has been set yet.

Later versions will only append fields after the ones above, which keep their offsets and meanings.
A reader that knows version 1 can thus skip future records using `header_length`.

## Timing

Frame `n` of a timed record should be played at `play_time + n × 1,000,000,000 / rate` nanoseconds.

- **Clock:** Shairport Sync's internal clock, `CLOCK_MONOTONIC_RAW` (or `CLOCK_MONOTONIC`) on Linux and the BSDs, `mach_absolute_time()` on macOS. The reader must use the same clock, so it has to run on the same machine (or in a container on the same kernel).
- **Drift:** not corrected by this backend, as Shairport Sync can't tell how far ahead of the output the reader is. Play times slowly move away from what the frame counts suggest, so take each timed record's time from its own `play_time`, and resample, or drop and insert frames, to follow it.
- **Lead time:** records are written about `audio_backend_buffer_desired_length_in_seconds` (1 second by default) ahead of their play time, so the reader has to buffer that much audio.
- **Untimed records:** play straight after the record before them. With no timed record since the last flush or stop, as with the silence at the start of a play session, they have no defined time and can be dropped.
- **RTP timestamp:** matches the RTP timestamps in the [metadata](Metadata.md), for example to find out which track a block belongs to.

## Writing a Reader

Read the pipe all the time, from start-up, whether or not anything is playing. When the pipe is full, Shairport Sync waits for room, and the player waits with it, just as with the `pipe` backend.

Flush and stop are never held up like that, so they can arrive a little late: straight after the record that was being written when they happened or, if the pipe was full, just before the next record. A late flush and stop can arrive together in one record.

A record can also be cut short, if a write fails or the play session ends in the middle of one. Shairport Sync then closes the pipe, without sending a stop, and opens it again for the next block of audio. Your reader sees this as an end of file or, if it also keeps the pipe open for writing, as a new header (`SSBA` with a matching `header_crc32`) inside a payload. Either way, drop the partial record and treat it as a flush and a stop. Then open the pipe again, or carry on from the header you found.

If you start reading a pipe that already has data in it, after a restart say, you may land in the middle of a record. Look for `SSBA` with a matching `header_crc32`: the magic alone isn't enough, as audio can contain any bytes. To be sure, check that the next header follows `header_length + payload_length` bytes later. A gap in `sequence` means records were lost.
