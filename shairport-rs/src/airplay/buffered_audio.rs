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
};
use tracing::{debug, info, warn};

use crate::{audio::AudioEngine, config::AirplayConfig, state::AppState};

/// SSRC constants for AP2 audio formats
const SSRC_ALAC_44100_S16_2: u32 = 0x00000001;
const SSRC_ALAC_48000_S24_2: u32 = 0x00000007;
const SSRC_AAC_44100_F24_2: u32 = 0x00000004;
const SSRC_AAC_48000_F24_2: u32 = 0x00000005;

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

    Ok(tokio::spawn(async move {
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
    }))
}

/// Handle one buffered audio TCP connection.
async fn handle_buffered_stream(
    mut stream: TcpStream,
    peer: SocketAddr,
    state: AppState,
    audio_engine: AudioEngine,
) -> anyhow::Result<()> {
    info!(%peer, "buffered audio connection opened");

    // Wait until a session key is available (poll without holding guard across await)
    let session_key = loop {
        {
            let key = state.session_key.read();
            if let Some(k) = *key {
                break k;
            }
        }
        tokio::time::sleep(tokio::time::Duration::from_millis(10)).await;
    };

    // Derive the data stream cipher
    let mut cipher = BufferedCipher::new(&session_key);

    let mut word_buf = [0u8; 4];

    loop {
        // Read block length prefix (2 bytes, big-endian)
        let len_raw = match stream.read_u16().await {
            Ok(len) => len,
            Err(e) => {
                debug!(%e, "buffered audio stream closed");
                break;
            }
        };
        let block_len = len_raw as usize;
        if block_len < 12 || block_len > 65535 {
            warn!(block_len, "invalid block length");
            break;
        }

        // Read block header: 4-byte seq (23-bit), 4-byte timestamp, 4-byte SSRC
        if stream.read_exact(&mut word_buf).await.is_err() {
            break;
        }
        let seq_23 = u32::from_be_bytes(word_buf) & 0x7FFFFF;

        if stream.read_exact(&mut word_buf).await.is_err() {
            break;
        }
        let timestamp = u32::from_be_bytes(word_buf);

        if stream.read_exact(&mut word_buf).await.is_err() {
            break;
        }
        let ssrc = u32::from_be_bytes(word_buf);

        // Read the audio payload: block_len - 12 header bytes
        let payload_len = block_len.saturating_sub(12);
        let mut payload = vec![0u8; payload_len];
        if payload_len > 0 && stream.read_exact(&mut payload).await.is_err() {
            break;
        }

        // Decrypt
        let plaintext = match cipher.decrypt_block(&payload) {
            Ok(p) => p,
            Err(e) => {
                warn!(%e, "block decrypt failed");
                continue;
            }
        };

        // Detect format from SSRC and pass to audio engine
        let frames_per_packet = match ssrc {
            SSRC_ALAC_44100_S16_2 | SSRC_ALAC_48000_S24_2 => 352,
            _ => 1024, // AAC
        };
        let _sample_rate = match ssrc {
            SSRC_ALAC_44100_S16_2 | SSRC_AAC_44100_F24_2 => 44100,
            _ => 48000,
        };

        debug!(
            seq = seq_23,
            ts = timestamp,
            ssrc,
            payload_len = plaintext.len(),
            "buffered audio block"
        );

        // For now, push raw bytes as f32 samples (placeholder for actual decode)
        // Proper decode via ALAC/AAC will be added in Phase 8
        let sample_count = frames_per_packet * 2; // stereo
        let mut float_samples = Vec::with_capacity(sample_count);
        for i in 0..sample_count.min(plaintext.len() / 2) {
            let byte_idx = i * 2;
            if byte_idx + 1 < plaintext.len() {
                let sample = i16::from_be_bytes([plaintext[byte_idx], plaintext[byte_idx + 1]]);
                float_samples.push((sample as f32) / 32768.0);
            }
        }
        let enqueued = audio_engine.enqueue_interleaved(&float_samples);
        if enqueued < float_samples.len() {
            debug!(
                "audio buffer full, dropped {}",
                float_samples.len() - enqueued
            );
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

/// Chacha20-Poly1305 cipher for buffered audio decryption.
struct BufferedCipher {
    key: [u8; 16],
    counter: u64,
}

impl BufferedCipher {
    fn new(session_key: &[u8; 16]) -> Self {
        Self {
            key: *session_key,
            counter: 0,
        }
    }

    fn decrypt_block(&mut self, ciphertext: &[u8]) -> Result<Vec<u8>, &'static str> {
        // AP2 buffered audio: last 8 bytes of packet are the nonce (front-padded to 12)
        // AAD: first 8 bytes of the block (seq + timestamp)
        if ciphertext.len() < 8 + 16 + 8 {
            return Err("block too short");
        }

        let payload_len = ciphertext.len() - 8; // last 8 = nonce
        let aad = &ciphertext[..8]; // first 8 bytes are AAD
        let encrypted = &ciphertext[8..payload_len];
        let nonce_raw = &ciphertext[payload_len..];

        let mut nonce = [0u8; 12];
        nonce[4..].copy_from_slice(nonce_raw);

        let key = chacha20poly1305::Key::from_slice(&self.key);
        let cipher = ChaCha20Poly1305::new(key);

        let payload = Payload {
            msg: encrypted,
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
        assert_eq!(SSRC_ALAC_44100_S16_2, 1);
        assert_eq!(SSRC_ALAC_48000_S24_2, 7);
        assert_eq!(SSRC_AAC_44100_F24_2, 4);
        assert_eq!(SSRC_AAC_48000_F24_2, 5);
    }

    #[test]
    fn cipher_decrypt_fails_on_short_input() {
        let key = [0u8; 16];
        let mut cipher = BufferedCipher::new(&key);
        assert!(cipher.decrypt_block(&[0u8; 10]).is_err());
    }
}
