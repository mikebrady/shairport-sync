use std::{
    collections::VecDeque,
    sync::Arc,
    time::{Duration, Instant},
};

use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tracing::{debug, info, warn};

const DEFAULT_LATENCY_FRAMES: u32 = 11025; // ~250ms at 44100Hz
const DEFAULT_SAMPLE_RATE: u32 = 44100;
const MAX_BUFFER_FRAMES: usize = 512; // max decoded audio frames in buffer

/// A decoded audio frame with its RTP timestamp.
#[derive(Clone, Debug)]
pub struct AudioFrame {
    /// RTP timestamp from the audio source
    pub timestamp: u32,
    /// Decoded interleaved PCM samples (f32)
    pub samples: Vec<f32>,
    /// Sample rate of this frame
    pub sample_rate: u32,
    /// Number of channels
    pub channels: u16,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PlayerStatus {
    pub buffered_frames: usize,
    pub total_frames_played: u64,
    pub underruns: u64,
    pub late_frames_dropped: u64,
    pub playing: bool,
    pub timestamp_offset: Option<u32>,
    pub latency_frames: u32,
}

pub struct Player {
    /// Ring buffer of decoded audio frames
    frame_buffer: VecDeque<AudioFrame>,
    /// Reference timestamp: the first RTP timestamp received
    base_rtp_timestamp: Option<u32>,
    /// Local time when base_rtp_timestamp was received
    base_local_time: Option<Instant>,
    /// Current latency in frames
    latency_frames: u32,
    /// Sample rate of the current stream
    sample_rate: u32,
    /// Number of frames played so far
    total_frames_played: u64,
    /// Undercount
    underruns: u64,
    /// Late frames dropped
    late_frames_dropped: u64,
    /// Whether playback is active
    playing: bool,
}

impl Player {
    pub fn new() -> Self {
        Self {
            frame_buffer: VecDeque::with_capacity(MAX_BUFFER_FRAMES),
            base_rtp_timestamp: None,
            base_local_time: None,
            latency_frames: DEFAULT_LATENCY_FRAMES,
            sample_rate: DEFAULT_SAMPLE_RATE,
            total_frames_played: 0,
            underruns: 0,
            late_frames_dropped: 0,
            playing: false,
        }
    }

    /// Start or resume playback.
    pub fn start(&mut self, latency_frames: u32) {
        self.playing = true;
        self.latency_frames = if latency_frames > 0 {
            latency_frames
        } else {
            DEFAULT_LATENCY_FRAMES
        };
        info!(latency = self.latency_frames, "player started");
    }

    /// Stop playback and reset state.
    pub fn stop(&mut self) {
        self.playing = false;
        self.frame_buffer.clear();
        self.base_rtp_timestamp = None;
        self.base_local_time = None;
        self.total_frames_played = 0;
        info!("player stopped");
    }

    /// Flush the buffer but keep timing state.
    pub fn flush(&mut self) {
        self.frame_buffer.clear();
        debug!("player buffer flushed");
    }

    /// Set sample rate for current stream.
    pub fn set_sample_rate(&mut self, rate: u32) {
        self.sample_rate = if rate > 0 { rate } else { DEFAULT_SAMPLE_RATE };
    }

    /// Push a decoded audio frame into the buffer.
    /// Returns true if the frame was accepted, false if dropped.
    pub fn push_frame(
        &mut self,
        timestamp: u32,
        samples: Vec<f32>,
        sample_rate: u32,
        channels: u16,
    ) -> bool {
        if !self.playing {
            return false;
        }

        if self.frame_buffer.len() >= MAX_BUFFER_FRAMES {
            warn!("player buffer full, dropping frame");
            return false;
        }

        // Set base timestamp on first frame
        if self.base_rtp_timestamp.is_none() {
            self.base_rtp_timestamp = Some(timestamp);
            self.base_local_time = Some(Instant::now());
            debug!(timestamp, "player base timestamp set");
        }

        self.frame_buffer.push_back(AudioFrame {
            timestamp,
            samples,
            sample_rate,
            channels,
        });

        true
    }

    /// Called from the audio output callback. Returns the next batch of
    /// samples that should be played immediately, or silence if none ready.
    pub fn pull_samples(&mut self, num_samples: usize) -> Vec<f32> {
        if !self.playing || self.frame_buffer.is_empty() {
            if self.playing && self.total_frames_played > 0 {
                self.underruns += 1;
                debug!(underruns = self.underruns, "audio underrun");
            }
            return vec![0.0; num_samples];
        }

        let mut output = Vec::with_capacity(num_samples);
        let mut samples_needed = num_samples;

        while samples_needed > 0 && !self.frame_buffer.is_empty() {
            let frame = self.frame_buffer.front().unwrap();

            // Calculate when this frame should be played
            let play_time = self.frame_play_time(frame.timestamp);

            if let Some(play_time) = play_time {
                let now = Instant::now();
                if now < play_time {
                    // Frame is not due yet — output silence for now
                    break;
                }

                let elapsed = now.saturating_duration_since(play_time);
                if elapsed > Duration::from_millis(200) {
                    // Frame is more than 200ms late, drop it
                    let dropped = self.frame_buffer.pop_front().unwrap();
                    self.late_frames_dropped += 1;
                    debug!(
                        timestamp = dropped.timestamp,
                        late_frames = self.late_frames_dropped,
                        "dropped late frame"
                    );
                    continue;
                }
            }

            // Take samples from the front frame
            let frame = self.frame_buffer.front_mut().unwrap();
            let take = samples_needed.min(frame.samples.len());
            let drained: Vec<f32> = frame.samples.drain(..take).collect();
            output.extend_from_slice(&drained);
            samples_needed -= take;

            if frame.samples.is_empty() {
                let _ = self.frame_buffer.pop_front();
                self.total_frames_played += 1;
            }
        }

        // Pad with silence if we didn't get enough samples
        output.resize(num_samples, 0.0);
        output
    }

    /// Calculate the local time when a frame with the given RTP timestamp should be played.
    fn frame_play_time(&self, timestamp: u32) -> Option<Instant> {
        let base_ts = self.base_rtp_timestamp?;
        let base_time = self.base_local_time?;

        let frame_diff = if timestamp >= base_ts {
            timestamp - base_ts
        } else {
            timestamp.wrapping_sub(base_ts)
        };

        let sample_period_ns = 1_000_000_000u64 / self.sample_rate as u64;
        let latency_ns = self.latency_frames as u64 * sample_period_ns;
        let frame_offset_ns = frame_diff as u64 * sample_period_ns;

        Some(base_time + Duration::from_nanos(latency_ns + frame_offset_ns))
    }

    pub fn status(&self) -> PlayerStatus {
        PlayerStatus {
            buffered_frames: self.frame_buffer.len(),
            total_frames_played: self.total_frames_played,
            underruns: self.underruns,
            late_frames_dropped: self.late_frames_dropped,
            playing: self.playing,
            timestamp_offset: self.base_rtp_timestamp,
            latency_frames: self.latency_frames,
        }
    }
}

/// Shared player state for use across async tasks.
#[derive(Clone)]
pub struct SharedPlayer {
    inner: Arc<Mutex<Player>>,
}

impl SharedPlayer {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(Player::new())),
        }
    }

    pub fn start(&self, latency_frames: u32) {
        self.inner.lock().start(latency_frames);
    }

    pub fn stop(&self) {
        self.inner.lock().stop();
    }

    pub fn flush(&self) {
        self.inner.lock().flush();
    }

    pub fn set_sample_rate(&self, rate: u32) {
        self.inner.lock().set_sample_rate(rate);
    }

    pub fn push_frame(
        &self,
        timestamp: u32,
        samples: Vec<f32>,
        sample_rate: u32,
        channels: u16,
    ) -> bool {
        self.inner
            .lock()
            .push_frame(timestamp, samples, sample_rate, channels)
    }

    pub fn pull_samples(&self, num_samples: usize) -> Vec<f32> {
        self.inner.lock().pull_samples(num_samples)
    }

    pub fn status(&self) -> PlayerStatus {
        self.inner.lock().status()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn player_starts_stops_cleanly() {
        let mut p = Player::new();
        assert!(!p.playing);
        p.start(11025);
        assert!(p.playing);
        assert_eq!(p.latency_frames, 11025);
        p.stop();
        assert!(!p.playing);
    }

    #[test]
    fn player_rejects_frames_when_stopped() {
        let mut p = Player::new();
        let accepted = p.push_frame(100, vec![0.0; 100], 44100, 2);
        assert!(!accepted);
    }

    #[test]
    fn player_accepts_frames_when_playing() {
        let mut p = Player::new();
        p.start(11025);
        let accepted = p.push_frame(100, vec![0.0; 100], 44100, 2);
        assert!(accepted);
    }

    #[test]
    fn player_returns_silence_when_buffer_empty() {
        let mut p = Player::new();
        p.start(11025);
        let samples = p.pull_samples(64);
        assert_eq!(samples.len(), 64);
        assert!(samples.iter().all(|&s| s == 0.0));
    }

    #[test]
    fn player_flush_clears_buffer() {
        let mut p = Player::new();
        p.start(11025);
        p.push_frame(100, vec![0.0; 100], 44100, 2);
        assert_eq!(p.frame_buffer.len(), 1);
        p.flush();
        assert_eq!(p.frame_buffer.len(), 0);
    }

    #[test]
    fn player_tracks_underruns_after_frames_consumed() {
        let mut p = Player::new();
        p.start(11025);
        // Manually simulate having played frames
        p.total_frames_played = 10;
        // Pull with empty buffer should increment underruns
        p.pull_samples(64);
        assert_eq!(p.underruns, 1);
    }
}
