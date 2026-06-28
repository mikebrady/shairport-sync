use std::{net::SocketAddr, sync::Arc};

use anyhow::Context;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use tokio::{net::UdpSocket, task::JoinHandle};
use tracing::{debug, info, warn};

use crate::{
    audio::AudioEngine, codec, config::AirplayConfig, decoder, player::SharedPlayer,
    state::AppState,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum RtpChannel {
    Audio,
    Control,
    Timing,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct RtpPacket {
    pub version: u8,
    pub marker: bool,
    pub payload_type: u8,
    pub sequence_number: u16,
    pub timestamp: u32,
    pub ssrc: u32,
    pub payload_len: usize,
}

pub async fn spawn_rtp_receivers(
    config: AirplayConfig,
    state: AppState,
    audio_engine: AudioEngine,
    player: SharedPlayer,
) -> anyhow::Result<Vec<JoinHandle<()>>> {
    let audio = bind_audio_channel(
        config.audio_port,
        state.clone(),
        audio_engine.clone(),
        player,
    )
    .await?;
    let control = bind_channel(RtpChannel::Control, config.control_port, state.clone()).await?;
    let timing = bind_channel(RtpChannel::Timing, config.timing_port, state).await?;
    Ok(vec![audio, control, timing])
}

async fn bind_audio_channel(
    port: u16,
    state: AppState,
    audio_engine: AudioEngine,
    player: SharedPlayer,
) -> anyhow::Result<JoinHandle<()>> {
    let bind = SocketAddr::from(([0, 0, 0, 0], port));
    let socket = UdpSocket::bind(bind)
        .await
        .with_context(|| format!("failed to bind RTP Audio socket on {bind}"))?;
    Ok(tokio::spawn(async move {
        let mut buf = [0u8; 2048];
        // AES-CBC uses chaining IV: start with aesiv from SDP, update per packet
        let iv: Arc<RwLock<Option<[u8; 16]>>> = Arc::new(RwLock::new(None));
        let mut audio_decoder: Option<codec::AudioDecoder> = None;
        let mut decoder_epoch = state.track_transition_epoch();

        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, _)) => {
                    if len < 12 {
                        continue;
                    }
                    let payload_type = buf[1] & 0x7f;
                    // Classic AP1 audio type: 0x60 (audio data) or 0x56 (resend)
                    if payload_type != 0x60 && payload_type != 0x56 {
                        continue;
                    }
                    let current_epoch = state.track_transition_epoch();
                    if current_epoch != decoder_epoch {
                        audio_decoder = None;
                        *iv.write() = None;
                        decoder_epoch = current_epoch;
                        info!(
                            epoch = decoder_epoch,
                            "RTP audio decoder reset for track transition"
                        );
                    }

                    let rtp_payload_len = len - 12;
                    if rtp_payload_len < 16 {
                        continue;
                    }

                    let payload = &buf[12..len];

                    // Wait until session key is available
                    let session_key = *state.session_key.read();
                    let Some(key) = session_key else {
                        debug!("no session key yet — buffering");
                        continue;
                    };

                    // Initialize IV from session state if not set
                    {
                        let mut iv_guard = iv.write();
                        if iv_guard.is_none() {
                            // For classic AP, the first IV comes from the SDP `a=aesiv`
                            // but we need to start with the last ciphertext block as chaining.
                            // If no SDP IV is set, use a zero IV.
                            *iv_guard = Some(
                                state
                                    .alac_magic_cookie
                                    .read()
                                    .as_ref()
                                    .and_then(|_| {
                                        // The actual initial IV is stored in the SDP aesiv field
                                        // which isn't in alac_magic_cookie. Use zero as fallback.
                                        None
                                    })
                                    .unwrap_or([0u8; 16]),
                            );
                        }
                    }

                    // Decrypt the AES-CBC payload in place
                    let mut decrypted = payload.to_vec();
                    if decrypted.len() < 16 {
                        continue;
                    }

                    let aes_len = decrypted.len() & !0xf;
                    if aes_len == 0 {
                        continue;
                    }

                    // Get current IV (will be updated in-place by AES-CBC)
                    let current_iv = {
                        let iv_guard = iv.read();
                        iv_guard.unwrap_or([0u8; 16])
                    };

                    if let Err(e) = decoder::aes_cbc_decrypt_in_place(
                        &key,
                        &current_iv,
                        &mut decrypted[..aes_len],
                    ) {
                        warn!(%e, "AES-CBC decrypt failed");
                        continue;
                    }

                    // Update chaining IV from last ciphertext block
                    let last_block_start = aes_len.saturating_sub(16);
                    if last_block_start + 16 <= payload.len() {
                        let mut new_iv = [0u8; 16];
                        new_iv.copy_from_slice(&payload[last_block_start..last_block_start + 16]);
                        *iv.write() = Some(new_iv);
                    }
                    if state.is_waiting_for_track_title() {
                        debug!("RTP audio packet drained while waiting for new title");
                        continue;
                    }

                    if audio_decoder.is_none() {
                        let cookie = state.alac_magic_cookie.read().clone();
                        if let Some(ref cookie) = cookie {
                            let sample_size = state.alac_sample_size.read().unwrap_or(16);
                            let channels = state.alac_channels.read().unwrap_or(2);
                            let rate = state.alac_sample_rate.read().unwrap_or(44_100);
                            let frames_per_packet =
                                state.frames_per_packet.read().unwrap_or(352) as usize;
                            match codec::AudioDecoder::new_alac(
                                sample_size,
                                channels,
                                rate,
                                frames_per_packet,
                                cookie,
                            ) {
                                Ok(d) => {
                                    audio_decoder = Some(d);
                                    info!(sample_size, channels, rate, "ALAC decoder initialized");
                                }
                                Err(e) => {
                                    warn!(%e, "ALAC decoder init failed");
                                    continue;
                                }
                            }
                        }
                    }

                    if let Some(ref mut dec) = audio_decoder {
                        match dec.decode(&decrypted) {
                            Ok(decoded) => {
                                if !decoded.samples.is_empty() {
                                    let ts = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);

                                    player.push_frame(
                                        ts,
                                        decoded.samples.clone(),
                                        decoded.sample_rate,
                                        decoded.channels,
                                    );

                                    let (enqueued, total_samples) = audio_engine
                                        .enqueue_interleaved_for_output(
                                            &decoded.samples,
                                            decoded.sample_rate,
                                            decoded.channels,
                                        );
                                    if enqueued < total_samples {
                                        debug!(
                                            "audio ring buffer full, dropped {} samples",
                                            total_samples - enqueued
                                        );
                                    }
                                }
                                state.record_rtp_packet(
                                    RtpChannel::Audio,
                                    parse_rtp_packet_inner(&buf[..len]),
                                );
                            }
                            Err(e) => {
                                warn!(%e, "ALAC decode failed");
                            }
                        }
                    }
                }
                Err(err) => warn!(?err, "RTP audio receive failed"),
            }
        }
    }))
}

async fn bind_channel(
    channel: RtpChannel,
    port: u16,
    state: AppState,
) -> anyhow::Result<JoinHandle<()>> {
    let bind = SocketAddr::from(([0, 0, 0, 0], port));
    let socket = UdpSocket::bind(bind)
        .await
        .with_context(|| format!("failed to bind RTP {channel:?} socket on {bind}"))?;
    Ok(tokio::spawn(async move {
        let mut buf = [0u8; 65_536];
        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, _)) => {
                    if let Some(packet) = parse_rtp_packet(&buf[..len]) {
                        debug!(
                            ?channel,
                            seq = packet.sequence_number,
                            "RTP packet received"
                        );
                        state.record_rtp_packet(channel, packet);
                    }
                }
                Err(err) => warn!(?channel, %err, "RTP receive failed"),
            }
        }
    }))
}

fn parse_rtp_packet_inner(buf: &[u8]) -> RtpPacket {
    let csrc_count = (buf[0] & 0x0f) as usize;
    let header_len = 12 + csrc_count * 4;
    RtpPacket {
        version: buf[0] >> 6,
        marker: buf[1] & 0x80 != 0,
        payload_type: buf[1] & 0x7f,
        sequence_number: u16::from_be_bytes([buf[2], buf[3]]),
        timestamp: u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]),
        ssrc: u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]),
        payload_len: buf.len() - header_len,
    }
}

pub fn parse_rtp_packet(packet: &[u8]) -> Option<RtpPacket> {
    if packet.len() < 12 {
        return None;
    }
    let version = packet[0] >> 6;
    if version != 2 {
        return None;
    }
    let csrc_count = (packet[0] & 0x0f) as usize;
    let header_len = 12 + csrc_count * 4;
    if packet.len() < header_len {
        return None;
    }
    Some(RtpPacket {
        version,
        marker: packet[1] & 0x80 != 0,
        payload_type: packet[1] & 0x7f,
        sequence_number: u16::from_be_bytes([packet[2], packet[3]]),
        timestamp: u32::from_be_bytes([packet[4], packet[5], packet[6], packet[7]]),
        ssrc: u32::from_be_bytes([packet[8], packet[9], packet[10], packet[11]]),
        payload_len: packet.len() - header_len,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_rtp_header() {
        let mut packet = vec![0x80, 0xe0, 0, 7, 0, 0, 0, 9, 1, 2, 3, 4];
        packet.extend_from_slice(&[1, 2, 3]);
        let parsed = parse_rtp_packet(&packet).unwrap();
        assert_eq!(parsed.version, 2);
        assert!(parsed.marker);
        assert_eq!(parsed.payload_type, 0x60);
        assert_eq!(parsed.sequence_number, 7);
        assert_eq!(parsed.timestamp, 9);
        assert_eq!(parsed.ssrc, 0x01020304);
        assert_eq!(parsed.payload_len, 3);
    }
}
