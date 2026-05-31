use std::net::SocketAddr;

use anyhow::Context;
use serde::{Deserialize, Serialize};
use tokio::{net::UdpSocket, task::JoinHandle};
use tracing::{debug, warn};

use crate::{config::AirplayConfig, state::AppState};

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
) -> anyhow::Result<Vec<JoinHandle<()>>> {
    let audio = bind_channel(RtpChannel::Audio, config.audio_port, state.clone()).await?;
    let control = bind_channel(RtpChannel::Control, config.control_port, state.clone()).await?;
    let timing = bind_channel(RtpChannel::Timing, config.timing_port, state).await?;
    Ok(vec![audio, control, timing])
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
                Ok((len, peer)) => {
                    if let Some(packet) = parse_rtp_packet(&buf[..len]) {
                        debug!(?channel, %peer, seq = packet.sequence_number, "RTP packet received");
                        state.record_rtp_packet(channel, packet);
                    }
                }
                Err(err) => warn!(?channel, %err, "RTP receive failed"),
            }
        }
    }))
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
