# Metadata

Shairport Sync can deliver metadata supplied by the source, such as Album Name, Artist Name, Cover Art, etc.
through a pipe or UDP socket to a recipient application program — see https://github.com/mikebrady/shairport-sync-metadata-reader for a sample recipient.
Sources that supply metadata include iTunes and the Music app in macOS and iOS.


## Metadata over UDP

As an alternative to sending metadata to a pipe, the `socket_address` and `socket_port` tags may be set in the metadata group to cause Shairport Sync
to broadcast UDP packets containing the track metadata.

The advantage of UDP is that packets can be sent to a single listener or, if a multicast address is used, to multiple listeners.
It also allows metadata to be routed to a different host. However UDP has a maximum packet size of about 65000 bytes; while large enough for most data, Cover Art will often exceed this value. Any metadata exceeding this limit will not be sent over the socket interface. The maximum packet size may be set with the `socket_msglength` tag to any value between 500 and 65000 to control this - lower values may be used to ensure that each UDP packet is sent in a single network frame. The default is 500. Other than this restriction, metadata sent over the socket interface is identical to metadata sent over the pipe interface.

The UDP metadata format is very simple - the first four bytes are the metadata *type*, and the next four bytes are the metadata *code*
(both are sent in network byte order - see https://github.com/mikebrady/shairport-sync-metadata-reader for a definition of those terms).
The remaining bytes of the packet, if any, make up the raw value of the metadata.

## Receiver measurements

With metadata enabled, Shairport Sync sends these `ssnc` events. Each value is
three decimal integers separated by `/`.

### `abrt`: buffered AAC bitrate

The value is `payload_bytes/input_sample_frames/sample_rate`. Bytes exclude
transport and encryption overhead and the synthetic ADTS header used for decoding.
Sample frames are counted before output resampling; each frame contains all channels.
The supported input sample rates are 44100 and 48000 Hz.

Calculate the average bitrate in bits per second as
`payload_bytes * 8 * sample_rate / input_sample_frames`.
For example, `64000/96000/48000` gives 256000 bits per second.

The counts accumulate since the last reset. The first report follows at least
two seconds of decoded input audio; later reports follow about one more second
of input audio. Buffered audio can arrive ahead of playback, so a report may
include audio that has not yet played. This is not a sliding average or a
guaranteed average for one track.

The meter resets on pause or stop, input format changes, immediate flush,
activation of a deferred flush, timestamp discontinuities, sample-rate changes,
and counter overflow. A reset sends `0/0/0` if the meter held data; consumers
should clear the previous measurement without dividing by zero. Consecutive
duplicate sequence/timestamp pairs do not contribute to the counts.
This event measures buffered AAC only, not ALAC or Classic AirPlay bitrate.

### `arst`: receiver playback counters

The value is `missing_audio_blocks/too_late_audio_blocks/retry_requests`.
These are the existing player counters, reported on the first available audio
frame and about once per elapsed second during playback, across codecs.
They are totals for the receiver session and are not reset for each report.

The missing count records unavailable audio blocks that need silence substitution.
The late count records blocks arriving too late for playback. The retry count
records Shairport resend requests, not TCP retransmissions. These counters do
not measure all network loss, decoder failures, or audible interruptions.

Both events use the existing metadata queue without waiting for space. A full
queue can drop a report, so consumers should allow missing or delayed updates.
