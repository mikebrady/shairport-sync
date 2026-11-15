# Pipe Output with Timing Headers

The `pipe` backend writes the audio as raw PCM to a named pipe. A program reading the pipe gets
the audio, but not the time at which each frame should be played, so it can only play frames as
they arrive.

With timing headers turned on, every block of audio is preceded by a 32-byte header that gives
its RTP timestamp and the time at which its first frame should be played. A reader can then
schedule the audio itself, for example to keep its own outputs in sync with the source's other
AirPlay speakers.

Turn them on in the `pipe` section of the configuration file:

```
pipe =
{
	name = "/tmp/shairport-sync-audio";
	timing_headers = "yes";
};
```

Without the setting, the pipe carries plain PCM, as before.

## Records

The pipe carries a sequence of records. Each record is a header, followed by
`frames × bytes per frame` bytes of audio in the configured output format. Header-only records
have no audio.

The header is little-endian:

| Offset | Size | Field |
| :--- | :--- | :--- |
| 0 | 4 | Magic: `0x48545053` (the bytes `SPTH`). |
| 4 | 2 | Version: `1`. |
| 6 | 2 | Flags (see below). |
| 8 | 4 | Number of frames of audio that follow. |
| 12 | 4 | RTP timestamp of the first frame. |
| 16 | 8 | Time at which the first frame should be played, in nanoseconds (see below). `0` if the record is not timed. |
| 24 | 4 | Output rate, in frames per second. |
| 28 | 4 | Bytes per frame. |

The flags are:

| Flag | Meaning |
| :--- | :--- |
| `1` timed | The play time is valid. Untimed records are silence inserted by Shairport Sync. |
| `2` flush | Discard any audio received but not yet played, e.g. after the source seeks or pauses. |
| `4` stop | The play session has ended. |

Flush and stop are sent in header-only records, and both may be set in the same record.

## Timing

The frames of each record follow on directly from those of the previous record, so frame `n` of
a timed record should be played at `play time + n × 1,000,000,000 / output rate` nanoseconds. An
untimed record plays straight after the record before it.

The play time is taken from the clock Shairport Sync uses internally: `CLOCK_MONOTONIC_RAW` (or
`CLOCK_MONOTONIC` if that is not available) on Linux and the BSDs, and `mach_absolute_time()` on
macOS. A reader must compare it with the same clock, so it has to run on the same machine (or in a
container on the same kernel).

## Reading the Pipe

* **Keep reading.** Read the pipe continuously from start-up, whether or not anything is playing.
  A record is written in one go so that it can't be cut short, so while the pipe is full, the
  write waits, and that also delays the end of a play session until the reader reads again.
* **Flush and stop can arrive late.** Shairport Sync never waits to write them: if the pipe is
  full, or a record is being written at that moment, the marker is written before the next record
  instead. A late flush and stop may arrive together in one record.
* **When the reader goes away,** Shairport Sync closes its end of the pipe and reopens it when the
  next block of audio is ready. With nothing left holding the pipe open, the kernel discards any
  half-written record, so a new reader starts at the beginning of a record.
* **To resynchronise** after any other failure, search for the magic and check the version and
  flags before trusting a header.
