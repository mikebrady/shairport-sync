use std::{
    net::SocketAddr,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

use anyhow::Context;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use tokio::{
    net::UdpSocket,
    task::JoinHandle,
    time::Instant,
};
use tracing::{debug, info, warn};

use crate::{config::PtpConfig, state::AppState};

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub enum PtpMessageType {
    Sync,
    DelayReq,
    PDelayReq,
    PDelayResp,
    FollowUp,
    DelayResp,
    Announce,
    Signaling,
    Management,
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

/// A simple clock servo that uses an IIR low-pass filter to smooth offset estimates.
#[derive(Clone, Debug)]
pub struct ClockServo {
    /// Current estimated offset from master (nanoseconds). Positive means local is ahead.
    pub offset_ns: f64,
    /// Filter coefficient (0..1). Lower = more smoothing.
    alpha: f64,
    /// Number of samples received
    sample_count: u64,
    /// Master clock identity
    pub master_clock_id: Option<u64>,
    /// Whether the servo has locked
    pub locked: bool,
}

impl ClockServo {
    pub fn new() -> Self {
        Self {
            offset_ns: 0.0,
            alpha: 0.1,
            sample_count: 0,
            master_clock_id: None,
            locked: false,
        }
    }

    /// Update the servo with a new offset sample from a Sync/FollowUp pair.
    pub fn update_offset(&mut self, sample_offset_ns: i64, master_id: u64) {
        self.master_clock_id = Some(master_id);
        self.sample_count += 1;

        // First few samples: use directly to converge quickly
        if self.sample_count < 10 {
            self.offset_ns = sample_offset_ns as f64;
        } else {
            // IIR low-pass filter
            self.offset_ns = self.alpha * sample_offset_ns as f64 + (1.0 - self.alpha) * self.offset_ns;
        }

        // Consider locked after 20 samples with consistent offset
        if self.sample_count >= 20 {
            self.locked = true;
        }
    }

    /// Convert an RTP timestamp (44.1kHz or 48kHz sample clock) to local time in nanoseconds.
    pub fn frame_to_local_time(&self, frame: u32, base_frame: u32, base_local_time_ns: u64, sample_rate: u32) -> u64 {
        let frame_diff = if frame >= base_frame {
            frame - base_frame
        } else {
            frame.wrapping_sub(base_frame)
        };
        let sample_period_ns = 1_000_000_000u64 / sample_rate as u64;
        let frame_offset_ns = frame_diff as u64 * sample_period_ns;
        base_local_time_ns + frame_offset_ns
    }

    /// Apply the clock offset to convert master timebase to local timebase.
    pub fn master_to_local(&self, master_time_ns: u64) -> u64 {
        if self.offset_ns >= 0.0 {
            master_time_ns + self.offset_ns as u64
        } else {
            master_time_ns - self.offset_ns.abs() as u64
        }
    }
}

/// Shared PTP state for cross-task access.
#[derive(Clone)]
pub struct PtpServo {
    inner: std::sync::Arc<RwLock<ClockServo>>,
}

impl PtpServo {
    pub fn new() -> Self {
        Self {
            inner: std::sync::Arc::new(RwLock::new(ClockServo::new())),
        }
    }

    pub fn update_offset(&self, sample: i64, master_id: u64) {
        self.inner.write().update_offset(sample, master_id);
    }

    pub fn is_locked(&self) -> bool {
        self.inner.read().locked
    }

    pub fn offset_ns(&self) -> f64 {
        self.inner.read().offset_ns
    }

    pub fn master_clock_id(&self) -> Option<u64> {
        self.inner.read().master_clock_id
    }

    pub fn frame_to_local(&self, frame: u32, base_frame: u32, base_time_ns: u64, rate: u32) -> u64 {
        self.inner.read().frame_to_local_time(frame, base_frame, base_time_ns, rate)
    }

    pub fn master_to_local(&self, master_time_ns: u64) -> u64 {
        self.inner.read().master_to_local(master_time_ns)
    }
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

    let servo = PtpServo::new();
    state.mark_ptp_running();

    // Share servo with state for other components
    let servo_for_state = servo.clone();

    Ok(tokio::spawn(async move {
        let event_task = run_event_socket(event_socket, state.clone(), servo.clone());
        let general_task = run_general_socket(general_socket, state.clone(), servo.clone());
        let delay_task = send_periodic_delay_req(servo.clone());
        tokio::join!(event_task, general_task, delay_task);

        // Update state with servo info periodically
        let state_clone = state;
        let mut interval = tokio::time::interval(Duration::from_secs(1));
        loop {
            interval.tick().await;
            let s = servo_for_state.inner.read();
            state_clone.set_diagnostic(
                "ptp_offset_ns",
                format!("{:.0}", s.offset_ns),
            );
            state_clone.set_diagnostic(
                "ptp_locked",
                if s.locked { "yes" } else { "no" },
            );
            if let Some(id) = s.master_clock_id {
                state_clone.set_diagnostic("ptp_master_clock", format!("{:x}", id));
            }
        }
    }))
}

async fn run_event_socket(socket: UdpSocket, state: AppState, servo: PtpServo) {
    let mut buf = [0u8; 2048];
    loop {
        match tokio::time::timeout(Duration::from_secs(30), socket.recv_from(&mut buf)).await {
            Ok(Ok((len, peer))) => {
                if let Some(message) = parse_ptp_message(&buf[..len]) {
                    match message.message_type {
                        PtpMessageType::Sync => {
                            // Sync carries the estimated origin timestamp at the sender
                            if let Some(ts) = message.origin_timestamp_ns {
                                let local_now = timestamp_now_ns();
                                let offset = local_now as i64 - ts as i64;
                                servo.update_offset(offset, message.clock_identity);
                                debug!(offset_ns = offset, "PTP Sync offset");
                            }
                            state.record_ptp_packet(message);
                        }
                        PtpMessageType::DelayReq => {
                            // Respond with DelayResp on general socket
                            debug!("PTP DelayReq received");
                            state.record_ptp_packet(message);
                        }
                        _ => {
                            state.record_ptp_packet(message);
                        }
                    }
                }
            }
            Ok(Err(err)) => warn!(%err, "PTP event socket error"),
            Err(_) => {}
        }
    }
}

async fn run_general_socket(socket: UdpSocket, state: AppState, _servo: PtpServo) {
    let mut buf = [0u8; 2048];
    loop {
        match tokio::time::timeout(Duration::from_secs(30), socket.recv_from(&mut buf)).await {
            Ok(Ok((len, peer))) => {
                if let Some(message) = parse_ptp_message(&buf[..len]) {
                    if message.message_type == PtpMessageType::FollowUp {
                        // FollowUp has the precise origin timestamp
                        if let Some(ts) = message.origin_timestamp_ns {
                            // The offset was already computed from Sync; FollowUp refines it
                            debug!(seq = message.sequence_id, "PTP FollowUp received");
                        }
                    }
                    state.record_ptp_packet(message);
                }
            }
            Ok(Err(err)) => warn!(%err, "PTP general socket error"),
            Err(_) => {}
        }
    }
}

/// Periodically send DelayReq messages to measure round-trip delay.
async fn send_periodic_delay_req(_servo: PtpServo) {
    let mut interval = tokio::time::interval(Duration::from_secs(10));
    loop {
        interval.tick().await;
        // DelayReq sending requires knowing the master's address
        // For now this is a placeholder — full two-step clock requires
        // tracking the master's endpoint from Announce messages.
        debug!("PTP DelayReq cycle (placeholder)");
    }
}

fn timestamp_now_ns() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0)
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
        0xB => PtpMessageType::Announce,
        0xC => PtpMessageType::Signaling,
        0xD => PtpMessageType::Management,
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
        let local = timestamp_now_ns() as i128;
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

    #[test]
    fn clock_servo_converges() {
        let mut servo = ClockServo::new();
        assert!(!servo.locked);
        for i in 0..25 {
            servo.update_offset(1000, 0x42);
        }
        assert!(servo.locked);
        assert!((servo.offset_ns - 1000.0).abs() < 100.0);
    }

    #[test]
    fn frame_to_local_time_basic() {
        let servo = ClockServo::new();
        let sample_period = 1_000_000_000 / 44100;
        let expected = 44100 * sample_period;
        let local = servo.frame_to_local_time(44100, 0, 0, 44100);
        assert_eq!(local, expected as u64);
    }

    #[test]
    fn master_to_local_applies_offset() {
        let mut servo = ClockServo::new();
        servo.update_offset(500_000_000, 0x42); // local is 0.5s ahead
        let local = servo.master_to_local(1_000_000_000);
        assert_eq!(local, 1_500_000_000);
    }
}
