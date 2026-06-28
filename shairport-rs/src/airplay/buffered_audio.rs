use std::net::SocketAddr;

use anyhow::Context;
use chacha20poly1305::{
    ChaCha20Poly1305, KeyInit,
    aead::{Aead, Payload},
};
use tokio::{
    io::AsyncReadExt,
    net::{TcpListener, TcpStream},
    task::JoinHandle,
    time::{self, Instant},
};
use tracing::{debug, info, warn};

use crate::{audio::AudioEngine, codec, config::AirplayConfig, state::AppState};

/// SSRC constants for AP2 audio formats
const SSRC_ALAC_44100_S16_2: u32 = 0x0000_FACE;
const SSRC_ALAC_48000_S24_2: u32 = 0x1500_0000;
const SSRC_AAC_44100_F24_2: u32 = 0x1600_0000;
const SSRC_AAC_48000_F24_2: u32 = 0x1700_0000;

/// Spawn a TCP listener for AP2 buffered audio on the configured audio port.
pub async fn spawn_buffered_audio_receiver(
    config: AirplayConfig,
    state: AppState,
    audio_engine: AudioEngine,
) -> anyhow::Result<JoinHandle<()>> {
    let bind = SocketAddr::from(([0, 0, 0, 0], config.audio_port));
    let listener = TcpListener::bind(bind)
        .await
        .with_context(|| format!("failed to bind buffered audio TCP on {bind}"))?;
    info!(port = config.audio_port, "AP2 buffered audio TCP listener");
    Ok(spawn_buffered_accept_loop(listener, state, audio_engine))
}

/// Spawn an accept loop for buffered audio on an already-bound TcpListener.
pub fn spawn_buffered_accept_loop(
    listener: TcpListener,
    state: AppState,
    audio_engine: AudioEngine,
) -> JoinHandle<()> {
    tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, peer)) => {
                    let state = state.clone();
                    let engine = audio_engine.clone();
                    tokio::spawn(async move {
                        if let Err(e) = handle_buffered_stream(stream, peer, state, engine).await {
                            warn!(%peer, %e, "buffered audio stream error");
                        }
                    });
                }
                Err(e) => warn!(%e, "buffered audio accept error"),
            }
        }
    })
}

/// Handle one buffered audio TCP connection.
pub async fn handle_buffered_stream(
    mut stream: TcpStream,
    peer: SocketAddr,
    state: AppState,
    audio_engine: AudioEngine,
) -> anyhow::Result<()> {
    info!(%peer, "buffered audio connection opened");

    // Wait until a session key is available (poll without holding guard across await)
    info!("buffered audio: waiting for session key...");
    let session_key = loop {
        {
            let key = state.ap2_media_key.read();
            if let Some(k) = *key {
                info!("buffered audio: session key obtained");
                break k;
            }
        }
        tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;
    };

    // Derive the data stream cipher
    let mut cipher = BufferedCipher::new(&session_key);
    info!("buffered audio: cipher initialized, reading first block");
    let mut audio_decoder: Option<(codec::AudioFormat, codec::AudioDecoder)> = None;
    let mut decoder_epoch = state.track_transition_epoch();
    let mut stale_for_transition = false;
    let mut block_count: u64 = 0;
    let mut first_block_logged = false;

    let mut word_buf = [0u8; 4];

    loop {
        // Read block length prefix (2 bytes, big-endian — matches C code's ntohs)
        // Use timeout to prevent blocking forever if client stops sending
        let len_raw = match time::timeout(time::Duration::from_secs(5), stream.read_u16()).await {
            Ok(Ok(len)) => {
                if !first_block_logged {
                    info!(
                        block_len = len,
                        "buffered audio: first block length received, reading header..."
                    );
                    first_block_logged = true;
                }
                len
            }
            Ok(Err(e)) => {
                info!(%e, "buffered audio stream closed by client, blocks_processed={}", block_count);
                break;
            }
            Err(_) => {
                // Timeout — no data for 5 seconds, the stream may be dead
                info!("buffered audio: read timeout (5s), stream may be idle");
                break;
            }
        };
        let block_len = len_raw as usize;
        // C code: data_len = ntohs(raw); then reads data_len - 2 for body.
        // read_u16() gives the same value as ntohs, so block_len includes the 2-byte prefix.
        let body_len = block_len.saturating_sub(2);
        if body_len < 12 || block_len > 65535 {
            warn!(
                block_len,
                blocks_processed = block_count,
                "invalid block length, breaking..."
            );
            break;
        }

        // Read block header: 4-byte seq (23-bit), 4-byte timestamp, 4-byte SSRC
        if stream.read_exact(&mut word_buf).await.is_err() {
            warn!("buffered audio: read seq failed");
            break;
        }
        let seq_23 = u32::from_be_bytes(word_buf) & 0x7FFFFF;

        if stream.read_exact(&mut word_buf).await.is_err() {
            warn!("buffered audio: read timestamp failed");
            break;
        }
        let timestamp = u32::from_be_bytes(word_buf);

        if stream.read_exact(&mut word_buf).await.is_err() {
            warn!("buffered audio: read SSRC failed");
            break;
        }
        let ssrc = u32::from_be_bytes(word_buf);

        // body_len - 12 header bytes = payload (ciphertext+tag+nonce)
        let payload_len = body_len.saturating_sub(12);
        let mut payload = vec![0u8; payload_len];
        if payload_len > 0 && stream.read_exact(&mut payload).await.is_err() {
            warn!("buffered audio: read payload failed");
            break;
        }

        block_count += 1;
        let current_epoch = state.track_transition_epoch();
        if current_epoch != decoder_epoch {
            audio_decoder = None;
            decoder_epoch = current_epoch;
            stale_for_transition = true;
            info!(
                epoch = decoder_epoch,
                "buffered audio decoder reset for track transition"
            );
        }
        if stale_for_transition {
            debug!(
                seq = seq_23,
                ssrc,
                epoch = decoder_epoch,
                "stale buffered audio packet drained after track transition"
            );
            continue;
        }
        // AAD = timestamp(4) + SSRC(4) from the block header
        let mut aad_buf = [0u8; 8];
        aad_buf[..4].copy_from_slice(&timestamp.to_be_bytes());
        aad_buf[4..].copy_from_slice(&ssrc.to_be_bytes());
        // Decrypt
        let plaintext = match cipher.decrypt_block(&payload, &aad_buf) {
            Ok(p) => {
                if block_count <= 3 {
                    let hex_first16: String = payload
                        .iter()
                        .take(16)
                        .map(|b| format!("{b:02x}"))
                        .collect::<Vec<_>>()
                        .join(" ");
                    info!(seq = seq_23, ssrc, blocks_processed = block_count, plaintext_len = p.len(), payload_first16 = %hex_first16, "buffered audio: block decrypted successfully");
                }
                p
            }
            Err(e) => {
                warn!(%e, seq = seq_23, ssrc, blocks_processed = block_count, block_len, payload_len, "block decrypt failed");
                continue;
            }
        };

        debug!(
            seq = seq_23,
            ts = timestamp,
            ssrc,
            payload_len = plaintext.len(),
            "buffered audio block"
        );

        let format =
            match codec::AudioFormat::from_ssrc(ssrc).or_else(|| *state.ap2_audio_format.read()) {
                Some(f) => f,
                None => {
                    warn!(
                        ssrc,
                        ssrc_hex = format_args!("{ssrc:#010x}"),
                        "unknown AP2 audio format"
                    );
                    continue;
                }
            };

        if !format.is_playable() {
            warn!(
                format = format.description(),
                "unsupported AP2 audio format"
            );
            continue;
        }

        let decoder = match audio_decoder.as_mut() {
            Some((decoder_format, decoder)) if *decoder_format == format => decoder,
            _ => {
                let cookie = if format.is_alac() {
                    Some(alac_specific_config(
                        format.sample_rate(),
                        format.bits_per_sample(),
                    ))
                } else {
                    None
                };
                match codec::AudioDecoder::new_for_format(
                    format,
                    cookie.as_ref().map(|cookie| cookie.as_slice()),
                ) {
                    Ok(d) => {
                        info!(format = format.description(), "audio decoder initialized");
                        audio_decoder = Some((format, d));
                        &mut audio_decoder.as_mut().unwrap().1
                    }
                    Err(e) => {
                        warn!(%e, ssrc, format = format.description(), "AP2 decoder init failed");
                        continue;
                    }
                }
            }
        };

        match decoder.decode(&plaintext) {
            Ok(decoded) => {
                if block_count <= 3 {
                    info!(
                        seq = seq_23,
                        ssrc,
                        sample_count = decoded.samples.len(),
                        format = format.description(),
                        "audio decode OK"
                    );
                }
                let waiting_for_title = state.is_waiting_for_track_title();
                let (enqueued, total_samples) = if waiting_for_title {
                    let result = enqueue_decoded_frame_for_later(
                        &audio_engine,
                        &decoded.samples,
                        decoded.sample_rate,
                        decoded.channels,
                    )
                    .await;
                    if result.0 > 0 {
                        state.release_track_transition_wait();
                        state.set_diagnostic("audio_waiting_for_track_title", "false");
                        audio_engine.set_playback_enabled(true);
                        info!(
                            seq = seq_23,
                            ssrc, "track transition released by decoded audio"
                        );
                    }
                    result
                } else {
                    enqueue_decoded_frame(
                        &audio_engine,
                        &decoded.samples,
                        decoded.sample_rate,
                        decoded.channels,
                    )
                    .await
                };
                if block_count <= 3 {
                    info!(
                        enqueued,
                        total = total_samples,
                        waiting_for_title,
                        "samples enqueued to audio engine"
                    );
                }
                if enqueued < total_samples {
                    debug!("audio buffer full, dropped {}", total_samples - enqueued);
                }
            }
            Err(e) => {
                warn!(%e, ssrc, blocks_processed = block_count, format = format.description(), "AP2 audio decode failed");
                let spf = format.frames_per_packet() as usize * format.channels() as usize;
                enqueue_decoded_frame(
                    &audio_engine,
                    &vec![0.0f32; spf],
                    format.sample_rate(),
                    format.channels(),
                )
                .await;
                continue;
            }
        }

        state.record_rtp_packet(
            crate::airplay::rtp::RtpChannel::Audio,
            crate::airplay::rtp::RtpPacket {
                version: 2,
                marker: false,
                payload_type: 96,
                sequence_number: seq_23 as u16,
                timestamp,
                ssrc,
                payload_len: plaintext.len(),
            },
        );
    }

    info!(%peer, "buffered audio connection closed");
    Ok(())
}

async fn enqueue_decoded_frame_for_later(
    audio_engine: &AudioEngine,
    samples: &[f32],
    sample_rate: u32,
    channels: u16,
) -> (usize, usize) {
    let converted = audio_engine.convert_interleaved_for_output(samples, sample_rate, channels);
    let total = converted.len();
    let pushed = audio_engine.enqueue_output_samples_unchecked(&converted);
    (pushed, total)
}

async fn enqueue_decoded_frame(
    audio_engine: &AudioEngine,
    samples: &[f32],
    sample_rate: u32,
    channels: u16,
) -> (usize, usize) {
    let converted = audio_engine.convert_interleaved_for_output(samples, sample_rate, channels);
    let total = converted.len();
    if !audio_engine.is_playback_enabled() {
        return (0, total);
    }
    let mut pushed = audio_engine.enqueue_output_samples(&converted);

    if pushed < total {
        let deadline = Instant::now() + time::Duration::from_secs(2);
        let remaining = &converted[pushed..];
        let mut offset = 0;
        while offset < remaining.len() {
            if Instant::now() >= deadline {
                warn!(
                    needed = remaining.len() - offset,
                    total, pushed, "buffered audio ring buffer full, dropping remaining samples"
                );
                break;
            }
            time::sleep(time::Duration::from_millis(5)).await;
            let batch = &remaining[offset..];
            let n = audio_engine.enqueue_output_samples(batch);
            pushed += n;
            offset += n;
            if n > 0 {
                continue;
            }
        }
    }

    (pushed, total)
}

fn alac_format_for_ssrc(ssrc: u32) -> Option<(u32, u32)> {
    match ssrc {
        SSRC_ALAC_44100_S16_2 => Some((44_100, 16)),
        SSRC_ALAC_48000_S24_2 => Some((48_000, 24)),
        _ => None,
    }
}

fn alac_specific_config(sample_rate: u32, sample_size: u32) -> [u8; 24] {
    let mut config = [0u8; 24];
    config[0..4].copy_from_slice(&352u32.to_be_bytes());
    config[4] = 0;
    config[5] = sample_size as u8;
    config[6] = 40;
    config[7] = 10;
    config[8] = 14;
    config[9] = 2;
    config[10..12].copy_from_slice(&255u16.to_be_bytes());
    config[12..16].copy_from_slice(&0u32.to_be_bytes());
    config[16..20].copy_from_slice(&0u32.to_be_bytes());
    config[20..24].copy_from_slice(&sample_rate.to_be_bytes());
    config
}

/// Chacha20-Poly1305 cipher for buffered audio decryption.
struct BufferedCipher {
    key: [u8; 32],
    counter: u64,
}

impl BufferedCipher {
    fn new(session_key: &[u8; 32]) -> Self {
        Self {
            key: *session_key,
            counter: 0,
        }
    }

    fn decrypt_block(&mut self, ciphertext: &[u8], aad: &[u8]) -> Result<Vec<u8>, &'static str> {
        // AP2 buffered audio: last 8 bytes of packet are the nonce (front-padded to 12)
        // AAD: timestamp(4) + SSRC(4) from the block header, passed in from caller
        if ciphertext.len() < 16 + 8 {
            return Err("block too short");
        }

        let nonce_len = 8;
        let clen = ciphertext.len() - nonce_len; // ciphertext + tag
        let nonce_raw = &ciphertext[clen..]; // last 8 bytes

        let mut nonce = [0u8; 12];
        nonce[4..].copy_from_slice(nonce_raw);

        let key = chacha20poly1305::Key::from_slice(&self.key);
        let cipher = ChaCha20Poly1305::new(key);

        let payload = Payload {
            msg: &ciphertext[..clen], // ciphertext + 16-byte tag
            aad,
        };

        cipher
            .decrypt(&nonce.into(), payload)
            .map_err(|_| "chacha20-poly1305 decrypt failed")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ssrc_values_match_known_formats() {
        assert_eq!(SSRC_ALAC_44100_S16_2, 0x0000_FACE);
        assert_eq!(SSRC_ALAC_48000_S24_2, 0x1500_0000);
        assert_eq!(SSRC_AAC_44100_F24_2, 0x1600_0000);
        assert_eq!(SSRC_AAC_48000_F24_2, 0x1700_0000);
    }

    #[test]
    fn cipher_decrypt_fails_on_short_input() {
        let key = [0u8; 32];
        let mut cipher = BufferedCipher::new(&key);
        assert!(cipher.decrypt_block(&[0u8; 10], &[0u8; 8]).is_err());
    }

    #[tokio::test]
    async fn enqueue_decoded_frame_does_not_wait_when_playback_disabled() {
        let audio_engine = AudioEngine::new(8);
        audio_engine.set_playback_enabled(false);
        let samples = vec![0.25; 4096];

        let result = time::timeout(
            time::Duration::from_millis(50),
            enqueue_decoded_frame(&audio_engine, &samples, 44_100, 2),
        )
        .await
        .expect("enqueue should return immediately while playback is disabled");

        assert_eq!(result.0, 0);
        assert!(result.1 > 0);
    }
}
