use std::{
    net::SocketAddr,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

use anyhow::Context;
use serde::{Deserialize, Serialize};
use tokio::{net::UdpSocket, task::JoinHandle};
use tracing::{debug, warn};

use crate::{config::PtpConfig, state::AppState};

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub enum PtpMessageType {
    Sync,
    DelayReq,
    PDelayReq,
    PDelayResp,
    FollowUp,
    DelayResp,
    Other(u8),
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PtpMessage {
    pub message_type: PtpMessageType,
    pub version: u8,
    pub message_length: u16,
    pub domain: u8,
    pub sequence_id: u16,
    pub clock_identity: u64,
    pub origin_timestamp_ns: Option<u64>,
    pub estimated_offset_ns: Option<i64>,
}

pub async fn spawn_ptp_service(
    config: PtpConfig,
    state: AppState,
) -> anyhow::Result<JoinHandle<()>> {
    let event_addr = SocketAddr::from(([0, 0, 0, 0], config.event_port));
    let general_addr = SocketAddr::from(([0, 0, 0, 0], config.general_port));
    let event_socket = UdpSocket::bind(event_addr)
        .await
        .with_context(|| format!("failed to bind PTP event port {event_addr}"))?;
    let general_socket = UdpSocket::bind(general_addr)
        .await
        .with_context(|| format!("failed to bind PTP general port {general_addr}"))?;

    state.mark_ptp_running();
    Ok(tokio::spawn(async move {
        let event_task = run_socket("event", event_socket, state.clone());
        let general_task = run_socket("general", general_socket, state);
        tokio::join!(event_task, general_task);
    }))
}

async fn run_socket(name: &'static str, socket: UdpSocket, state: AppState) {
    let mut buf = [0u8; 2048];
    loop {
        match tokio::time::timeout(Duration::from_secs(30), socket.recv_from(&mut buf)).await {
            Ok(Ok((len, peer))) => {
                if let Some(message) = parse_ptp_message(&buf[..len]) {
                    debug!(name, %peer, ?message, "PTP packet received");
                    state.record_ptp_packet(message);
                }
            }
            Ok(Err(err)) => warn!(name, %err, "PTP socket receive failed"),
            Err(_) => {}
        }
    }
}

pub fn parse_ptp_message(packet: &[u8]) -> Option<PtpMessage> {
    if packet.len() < 44 {
        return None;
    }
    let message_type = match packet[0] & 0x0f {
        0x0 => PtpMessageType::Sync,
        0x1 => PtpMessageType::DelayReq,
        0x2 => PtpMessageType::PDelayReq,
        0x3 => PtpMessageType::PDelayResp,
        0x8 => PtpMessageType::FollowUp,
        0x9 => PtpMessageType::DelayResp,
        other => PtpMessageType::Other(other),
    };
    let version = packet[1] & 0x0f;
    let message_length = u16::from_be_bytes([packet[2], packet[3]]);
    if message_length as usize > packet.len() {
        return None;
    }
    let domain = packet[4];
    let sequence_id = u16::from_be_bytes([packet[30], packet[31]]);
    let clock_identity = u64::from_be_bytes(packet[20..28].try_into().ok()?);
    let origin_timestamp_ns = parse_timestamp_ns(packet.get(34..44)?);
    let estimated_offset_ns = origin_timestamp_ns.and_then(|origin| {
        let local = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .ok()?
            .as_nanos() as i128;
        Some((origin as i128 - local).clamp(i64::MIN as i128, i64::MAX as i128) as i64)
    });
    Some(PtpMessage {
        message_type,
        version,
        message_length,
        domain,
        sequence_id,
        clock_identity,
        origin_timestamp_ns,
        estimated_offset_ns,
    })
}

fn parse_timestamp_ns(raw: &[u8]) -> Option<u64> {
    if raw.len() != 10 {
        return None;
    }
    let seconds = ((raw[0] as u64) << 40)
        | ((raw[1] as u64) << 32)
        | ((raw[2] as u64) << 24)
        | ((raw[3] as u64) << 16)
        | ((raw[4] as u64) << 8)
        | raw[5] as u64;
    let nanos = u32::from_be_bytes(raw[6..10].try_into().ok()?) as u64;
    Some(seconds.saturating_mul(1_000_000_000).saturating_add(nanos))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ptp_header_fields() {
        let mut packet = [0u8; 44];
        packet[0] = 0x08;
        packet[1] = 0x02;
        packet[2..4].copy_from_slice(&44u16.to_be_bytes());
        packet[4] = 7;
        packet[20..28].copy_from_slice(&0x1122334455667788u64.to_be_bytes());
        packet[30..32].copy_from_slice(&42u16.to_be_bytes());
        packet[34..40].copy_from_slice(&[0, 0, 0, 0, 0, 9]);
        packet[40..44].copy_from_slice(&123u32.to_be_bytes());

        let parsed = parse_ptp_message(&packet).unwrap();
        assert_eq!(parsed.message_type, PtpMessageType::FollowUp);
        assert_eq!(parsed.version, 2);
        assert_eq!(parsed.domain, 7);
        assert_eq!(parsed.sequence_id, 42);
        assert_eq!(parsed.clock_identity, 0x1122334455667788);
        assert_eq!(parsed.origin_timestamp_ns, Some(9_000_000_123));
    }
}
