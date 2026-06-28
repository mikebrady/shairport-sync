use std::{
    collections::HashMap,
    net::{Ipv4Addr, Ipv6Addr, SocketAddr},
    sync::OnceLock,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

use anyhow::Context;
use parking_lot::{Mutex, RwLock};
use serde::{Deserialize, Serialize};
use socket2::{Domain, Protocol, Socket, Type};
use tokio::{net::UdpSocket, task::JoinHandle};
use tracing::{debug, trace, warn};

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

static PTP_EPOCH: OnceLock<Instant> = OnceLock::new();
static LOCAL_CLOCK_ID: OnceLock<u64> = OnceLock::new();

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PtpMessage {
    pub message_type: PtpMessageType,
    pub transport_specific: u8,
    pub version: u8,
    pub message_length: u16,
    pub domain: u8,
    pub sequence_id: u16,
    pub clock_identity: u64,
    pub origin_timestamp_ns: Option<u64>,
    pub estimated_offset_ns: Option<i64>,
}

impl PtpMessage {
    fn transport_specific(&self) -> u8 {
        self.transport_specific
    }
}

#[derive(Clone, Copy, Debug)]
struct PtpRuntimeState {
    transport_specific: u8,
    domain: u8,
    master_clock_id: Option<u64>,
    master_event_addr: Option<SocketAddr>,
    master_general_addr: Option<SocketAddr>,
    path_delay_ns: i64,
}

impl Default for PtpRuntimeState {
    fn default() -> Self {
        Self {
            transport_specific: 1,
            domain: 0,
            master_clock_id: None,
            master_event_addr: None,
            master_general_addr: None,
            path_delay_ns: 0,
        }
    }
}

#[derive(Clone, Debug, Default)]
struct SyncSample {
    local_receipt_ns: u64,
    origin_ns: Option<u64>,
    master_clock_id: u64,
}

#[derive(Clone, Debug)]
struct DelaySample {
    sequence_id: u16,
    send_local_ns: u64,
}

#[derive(Clone, Debug)]
struct PtpRuntime {
    state: std::sync::Arc<Mutex<PtpRuntimeState>>,
    sync_samples: std::sync::Arc<Mutex<HashMap<u16, SyncSample>>>,
    last_sync: std::sync::Arc<Mutex<Option<SyncSample>>>,
    delay_samples: std::sync::Arc<Mutex<HashMap<u16, DelaySample>>>,
    next_delay_sequence: std::sync::Arc<Mutex<u16>>,
    local_clock_id: u64,
}

impl PtpRuntime {
    fn new() -> Self {
        Self {
            state: Default::default(),
            sync_samples: Default::default(),
            last_sync: Default::default(),
            delay_samples: Default::default(),
            next_delay_sequence: Default::default(),
            local_clock_id: local_clock_identity(),
        }
    }

    fn note_master(&self, message: &PtpMessage, peer: SocketAddr, is_event: bool) {
        let mut state = self.state.lock();
        state.transport_specific = message.transport_specific();
        state.domain = message.domain;
        state.master_clock_id = Some(message.clock_identity);
        if is_event {
            state.master_event_addr = Some(SocketAddr::new(peer.ip(), 319));
        } else {
            state.master_general_addr = Some(SocketAddr::new(peer.ip(), 320));
        }
    }

    fn next_delay_sequence(&self) -> u16 {
        let mut seq = self.next_delay_sequence.lock();
        *seq = seq.wrapping_add(1);
        if *seq == 0 {
            *seq = 1;
        }
        *seq
    }
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
    /// Estimated mean path delay to the master clock, in nanoseconds.
    pub path_delay_ns: f64,
}

impl ClockServo {
    pub fn new() -> Self {
        Self {
            offset_ns: 0.0,
            alpha: 0.1,
            sample_count: 0,
            master_clock_id: None,
            locked: false,
            path_delay_ns: 0.0,
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
            self.offset_ns =
                self.alpha * sample_offset_ns as f64 + (1.0 - self.alpha) * self.offset_ns;
        }

        // Consider locked after 20 samples with consistent offset
        if self.sample_count >= 20 {
            self.locked = true;
        }
    }

    pub fn update_path_delay(&mut self, sample_path_delay_ns: i64) {
        let sample = sample_path_delay_ns.max(0) as f64;
        if self.sample_count < 10 {
            self.path_delay_ns = sample;
        } else {
            self.path_delay_ns = self.alpha * sample + (1.0 - self.alpha) * self.path_delay_ns;
        }
    }

    /// Convert an RTP timestamp (44.1kHz or 48kHz sample clock) to local time in nanoseconds.
    pub fn frame_to_local_time(
        &self,
        frame: u32,
        base_frame: u32,
        base_local_time_ns: u64,
        sample_rate: u32,
    ) -> u64 {
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
        self.inner
            .read()
            .frame_to_local_time(frame, base_frame, base_time_ns, rate)
    }

    pub fn master_to_local(&self, master_time_ns: u64) -> u64 {
        self.inner.read().master_to_local(master_time_ns)
    }
}

pub async fn spawn_ptp_service(
    config: PtpConfig,
    state: AppState,
) -> anyhow::Result<JoinHandle<()>> {
    let event_addr_v4 = SocketAddr::from(([0, 0, 0, 0], config.event_port));
    let general_addr_v4 = SocketAddr::from(([0, 0, 0, 0], config.general_port));
    let event_socket_v4 = bind_ptp_socket(event_addr_v4, "event-v4")?;
    let general_socket_v4 = bind_ptp_socket(general_addr_v4, "general-v4")?;

    let event_socket_v6 = match bind_ptp_socket_v6(config.event_port, "event-v6") {
        Ok(socket) => Some(socket),
        Err(err) => {
            warn!(%err, "IPv6 PTP event socket not started");
            None
        }
    };
    let general_socket_v6 = match bind_ptp_socket_v6(config.general_port, "general-v6") {
        Ok(socket) => Some(socket),
        Err(err) => {
            warn!(%err, "IPv6 PTP general socket not started");
            None
        }
    };

    let servo = PtpServo::new();
    let runtime = PtpRuntime::new();
    state.mark_ptp_running();

    Ok(tokio::spawn(async move {
        let event_socket_v4 = std::sync::Arc::new(event_socket_v4);
        let general_socket_v4 = std::sync::Arc::new(general_socket_v4);
        let event_socket_v6 = event_socket_v6.map(std::sync::Arc::new);
        let general_socket_v6 = general_socket_v6.map(std::sync::Arc::new);

        let event_task_v4 = run_event_socket(
            event_socket_v4.clone(),
            state.clone(),
            servo.clone(),
            runtime.clone(),
            "v4",
        );
        let general_task_v4 = run_general_socket(
            general_socket_v4.clone(),
            state.clone(),
            servo.clone(),
            runtime.clone(),
            "v4",
        );
        let event_task_v6 = run_optional_event_socket(
            event_socket_v6.clone(),
            state.clone(),
            servo.clone(),
            runtime.clone(),
        );
        let general_task_v6 = run_optional_general_socket(
            general_socket_v6.clone(),
            state.clone(),
            servo.clone(),
            runtime.clone(),
        );
        let delay_task = send_periodic_delay_req(
            event_socket_v4,
            event_socket_v6,
            state.clone(),
            servo.clone(),
            runtime,
        );
        let diagnostics_task = publish_ptp_diagnostics(state, servo);
        tokio::join!(
            event_task_v4,
            general_task_v4,
            event_task_v6,
            general_task_v6,
            delay_task,
            diagnostics_task
        );
    }))
}

fn bind_ptp_socket(addr: SocketAddr, name: &str) -> anyhow::Result<UdpSocket> {
    let socket = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))
        .with_context(|| format!("failed to create PTP {name} UDP socket for {addr}"))?;
    socket
        .set_reuse_address(true)
        .with_context(|| format!("failed to set SO_REUSEADDR on PTP {name} socket {addr}"))?;

    #[cfg(unix)]
    socket
        .set_reuse_port(true)
        .with_context(|| format!("failed to set SO_REUSEPORT on PTP {name} socket {addr}"))?;

    socket
        .join_multicast_v4(&Ipv4Addr::new(224, 0, 1, 129), &Ipv4Addr::UNSPECIFIED)
        .with_context(|| format!("failed to join PTP multicast group on {name} socket {addr}"))?;
    socket
        .join_multicast_v4(&Ipv4Addr::new(224, 0, 0, 107), &Ipv4Addr::UNSPECIFIED)
        .with_context(|| format!("failed to join gPTP multicast group on {name} socket {addr}"))?;
    socket
        .set_multicast_loop_v4(false)
        .with_context(|| format!("failed to disable PTP multicast loop on {name} socket {addr}"))?;

    socket
        .bind(&addr.into())
        .with_context(|| format!("failed to bind PTP {name} UDP socket on {addr}"))?;
    socket
        .set_nonblocking(true)
        .with_context(|| format!("failed to set PTP {name} socket nonblocking on {addr}"))?;

    let std_socket: std::net::UdpSocket = socket.into();
    UdpSocket::from_std(std_socket)
        .with_context(|| format!("failed to attach PTP {name} socket {addr} to Tokio"))
}

fn bind_ptp_socket_v6(port: u16, name: &str) -> anyhow::Result<UdpSocket> {
    let addr = SocketAddr::from((Ipv6Addr::UNSPECIFIED, port));
    let socket = Socket::new(Domain::IPV6, Type::DGRAM, Some(Protocol::UDP))
        .with_context(|| format!("failed to create PTP {name} UDP socket for {addr}"))?;
    socket
        .set_only_v6(true)
        .with_context(|| format!("failed to set IPV6_V6ONLY on PTP {name} socket {addr}"))?;
    socket
        .set_reuse_address(true)
        .with_context(|| format!("failed to set SO_REUSEADDR on PTP {name} socket {addr}"))?;

    #[cfg(unix)]
    socket
        .set_reuse_port(true)
        .with_context(|| format!("failed to set SO_REUSEPORT on PTP {name} socket {addr}"))?;

    socket
        .bind(&addr.into())
        .with_context(|| format!("failed to bind PTP {name} UDP socket on {addr}"))?;

    // PTP over IPv6 commonly uses ff02::181; peer-delay/gPTP traffic uses ff02::6b.
    // Interface 0 asks the OS to join on the default multicast-capable interfaces.
    for group in [
        Ipv6Addr::from(0xff02_0000_0000_0000_0000_0000_0000_0181u128),
        Ipv6Addr::from(0xff02_0000_0000_0000_0000_0000_0000_006bu128),
    ] {
        if let Err(err) = socket.join_multicast_v6(&group, 0) {
            warn!(%err, %group, %name, "failed to join IPv6 PTP multicast group");
        }
    }

    socket
        .set_nonblocking(true)
        .with_context(|| format!("failed to set PTP {name} socket nonblocking on {addr}"))?;

    let std_socket: std::net::UdpSocket = socket.into();
    UdpSocket::from_std(std_socket)
        .with_context(|| format!("failed to attach PTP {name} socket {addr} to Tokio"))
}

async fn run_optional_event_socket(
    socket: Option<std::sync::Arc<UdpSocket>>,
    state: AppState,
    servo: PtpServo,
    runtime: PtpRuntime,
) {
    if let Some(socket) = socket {
        run_event_socket(socket, state, servo, runtime, "v6").await;
    } else {
        std::future::pending::<()>().await;
    }
}

async fn run_optional_general_socket(
    socket: Option<std::sync::Arc<UdpSocket>>,
    state: AppState,
    servo: PtpServo,
    runtime: PtpRuntime,
) {
    if let Some(socket) = socket {
        run_general_socket(socket, state, servo, runtime, "v6").await;
    } else {
        std::future::pending::<()>().await;
    }
}

async fn run_event_socket(
    socket: std::sync::Arc<UdpSocket>,
    state: AppState,
    servo: PtpServo,
    runtime: PtpRuntime,
    family: &'static str,
) {
    let mut buf = [0u8; 2048];
    loop {
        match tokio::time::timeout(Duration::from_secs(30), socket.recv_from(&mut buf)).await {
            Ok(Ok((len, peer))) => {
                if let Some(message) = parse_ptp_message(&buf[..len]) {
                    runtime.note_master(&message, peer, true);
                    trace!(%peer, family, message_type = ?message.message_type, seq = message.sequence_id, "PTP event packet received");
                    match message.message_type {
                        PtpMessageType::Sync => {
                            let local_receipt_ns = timestamp_now_ns();
                            let sample = SyncSample {
                                local_receipt_ns,
                                origin_ns: message.origin_timestamp_ns,
                                master_clock_id: message.clock_identity,
                            };
                            runtime
                                .sync_samples
                                .lock()
                                .insert(message.sequence_id, sample.clone());
                            if let Some(origin_ns) =
                                sample.origin_ns.filter(|origin_ns| *origin_ns != 0)
                            {
                                update_servo_from_sync(&servo, &runtime, &sample, origin_ns);
                            }
                            state.record_ptp_packet(message);
                        }
                        PtpMessageType::DelayReq => {
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

async fn run_general_socket(
    socket: std::sync::Arc<UdpSocket>,
    state: AppState,
    servo: PtpServo,
    runtime: PtpRuntime,
    family: &'static str,
) {
    let mut buf = [0u8; 2048];
    loop {
        match tokio::time::timeout(Duration::from_secs(30), socket.recv_from(&mut buf)).await {
            Ok(Ok((len, peer))) => {
                if let Some(message) = parse_ptp_message(&buf[..len]) {
                    runtime.note_master(&message, peer, false);
                    trace!(%peer, family, message_type = ?message.message_type, seq = message.sequence_id, "PTP general packet received");
                    match message.message_type {
                        PtpMessageType::FollowUp => {
                            if let Some(origin_ns) = message.origin_timestamp_ns {
                                let sample = runtime
                                    .sync_samples
                                    .lock()
                                    .remove(&message.sequence_id)
                                    .unwrap_or(SyncSample {
                                        local_receipt_ns: timestamp_now_ns(),
                                        origin_ns: Some(origin_ns),
                                        master_clock_id: message.clock_identity,
                                    });
                                update_servo_from_sync(&servo, &runtime, &sample, origin_ns);
                                debug!(
                                    seq = message.sequence_id,
                                    origin_ns, "PTP FollowUp received"
                                );
                            }
                        }
                        PtpMessageType::DelayResp => {
                            if let Some(receive_ns) = message.origin_timestamp_ns {
                                update_servo_from_delay_resp(
                                    &servo,
                                    &runtime,
                                    message.sequence_id,
                                    receive_ns,
                                );
                            }
                        }
                        PtpMessageType::Announce => {
                            debug!(
                                clock = format_args!("{:016x}", message.clock_identity),
                                domain = message.domain,
                                "PTP master announced"
                            );
                        }
                        _ => {
                            trace!(message_type = ?message.message_type, "PTP general message");
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
async fn send_periodic_delay_req(
    socket_v4: std::sync::Arc<UdpSocket>,
    socket_v6: Option<std::sync::Arc<UdpSocket>>,
    state: AppState,
    _servo: PtpServo,
    runtime: PtpRuntime,
) {
    let mut interval = tokio::time::interval(Duration::from_millis(750));
    loop {
        interval.tick().await;
        let snapshot = *runtime.state.lock();
        let Some(master_addr) = snapshot.master_event_addr else {
            trace!("PTP DelayReq skipped without master endpoint");
            continue;
        };
        let sequence_id = runtime.next_delay_sequence();
        let send_local_ns = timestamp_now_ns();
        runtime.delay_samples.lock().insert(
            sequence_id,
            DelaySample {
                sequence_id,
                send_local_ns,
            },
        );
        let packet = build_delay_req(
            snapshot.transport_specific,
            snapshot.domain,
            runtime.local_clock_id,
            sequence_id,
            send_local_ns,
        );
        let socket = if master_addr.is_ipv6() {
            socket_v6.as_ref().unwrap_or(&socket_v4)
        } else {
            &socket_v4
        };
        match socket.send_to(&packet, master_addr).await {
            Ok(sent) => {
                trace!(sequence_id, sent, %master_addr, "PTP DelayReq sent");
                state.set_diagnostic("ptp_last_delay_req", sequence_id.to_string());
            }
            Err(e) => warn!(%e, %master_addr, "PTP DelayReq send failed"),
        }
    }
}

async fn publish_ptp_diagnostics(state: AppState, servo: PtpServo) {
    let mut interval = tokio::time::interval(Duration::from_secs(1));
    loop {
        interval.tick().await;
        let s = servo.inner.read();
        state.set_diagnostic("ptp_offset_ns", format!("{:.0}", s.offset_ns));
        state.set_diagnostic("ptp_path_delay_ns", format!("{:.0}", s.path_delay_ns));
        state.set_diagnostic("ptp_locked", if s.locked { "yes" } else { "no" });
        if let Some(id) = s.master_clock_id {
            state.set_diagnostic("ptp_master_clock", format!("{id:016x}"));
        }
    }
}

fn update_servo_from_sync(
    servo: &PtpServo,
    runtime: &PtpRuntime,
    sample: &SyncSample,
    origin_ns: u64,
) {
    let path_delay_ns = runtime.state.lock().path_delay_ns;
    let offset = (sample.local_receipt_ns as i128 - origin_ns as i128 - path_delay_ns as i128)
        .clamp(i64::MIN as i128, i64::MAX as i128) as i64;
    servo.update_offset(offset, sample.master_clock_id);
    *runtime.last_sync.lock() = Some(SyncSample {
        origin_ns: Some(origin_ns),
        ..sample.clone()
    });
    debug!(
        offset_ns = offset,
        path_delay_ns,
        master = format_args!("{:016x}", sample.master_clock_id),
        "PTP Sync offset updated"
    );
}

fn update_servo_from_delay_resp(
    servo: &PtpServo,
    runtime: &PtpRuntime,
    sequence_id: u16,
    delay_req_receive_ns: u64,
) {
    let Some(delay) = runtime.delay_samples.lock().remove(&sequence_id) else {
        trace!(sequence_id, "PTP DelayResp without local DelayReq sample");
        return;
    };
    let Some(sync) = runtime.last_sync.lock().clone() else {
        trace!(sequence_id, "PTP DelayResp without Sync sample");
        return;
    };
    let Some(sync_origin_ns) = sync.origin_ns else {
        return;
    };

    let t2_minus_t1 = sync.local_receipt_ns as i128 - sync_origin_ns as i128;
    let t4_minus_t3 = delay_req_receive_ns as i128 - delay.send_local_ns as i128;
    let path_delay = ((t2_minus_t1 + t4_minus_t3) / 2)
        .max(0)
        .clamp(i64::MIN as i128, i64::MAX as i128) as i64;
    let offset = ((t2_minus_t1 - t4_minus_t3) / 2).clamp(i64::MIN as i128, i64::MAX as i128) as i64;

    runtime.state.lock().path_delay_ns = path_delay;
    {
        let mut inner = servo.inner.write();
        inner.update_path_delay(path_delay);
        inner.update_offset(offset, sync.master_clock_id);
    }

    debug!(
        sequence_id = delay.sequence_id,
        offset_ns = offset,
        path_delay_ns = path_delay,
        "PTP DelayResp updated servo"
    );
}

fn timestamp_now_ns() -> u64 {
    PTP_EPOCH
        .get_or_init(Instant::now)
        .elapsed()
        .as_nanos()
        .try_into()
        .unwrap_or(u64::MAX)
}

fn wall_clock_now_ns() -> u64 {
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
        transport_specific: packet[0] >> 4,
        version,
        message_length,
        domain,
        sequence_id,
        clock_identity,
        origin_timestamp_ns,
        estimated_offset_ns,
    })
}

fn build_delay_req(
    transport_specific: u8,
    domain: u8,
    clock_identity: u64,
    sequence_id: u16,
    origin_ns: u64,
) -> [u8; 44] {
    let mut packet = [0u8; 44];
    packet[0] = ((transport_specific & 0x0f) << 4) | 0x01;
    packet[1] = 0x02;
    packet[2..4].copy_from_slice(&44u16.to_be_bytes());
    packet[4] = domain;
    packet[6..8].copy_from_slice(&0u16.to_be_bytes());
    packet[20..28].copy_from_slice(&clock_identity.to_be_bytes());
    packet[28..30].copy_from_slice(&1u16.to_be_bytes());
    packet[30..32].copy_from_slice(&sequence_id.to_be_bytes());
    packet[32] = 1;
    packet[33] = 0x7f;
    write_timestamp_ns(&mut packet[34..44], origin_ns);
    packet
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

fn write_timestamp_ns(raw: &mut [u8], timestamp_ns: u64) {
    if raw.len() != 10 {
        return;
    }
    let seconds = timestamp_ns / 1_000_000_000;
    let nanos = (timestamp_ns % 1_000_000_000) as u32;
    raw[0] = ((seconds >> 40) & 0xff) as u8;
    raw[1] = ((seconds >> 32) & 0xff) as u8;
    raw[2] = ((seconds >> 24) & 0xff) as u8;
    raw[3] = ((seconds >> 16) & 0xff) as u8;
    raw[4] = ((seconds >> 8) & 0xff) as u8;
    raw[5] = (seconds & 0xff) as u8;
    raw[6..10].copy_from_slice(&nanos.to_be_bytes());
}

pub(crate) fn local_clock_identity() -> u64 {
    *LOCAL_CLOCK_ID.get_or_init(|| {
        let now = wall_clock_now_ns();
        let pid = std::process::id() as u64;
        0x5253_0000_0000_0000u64 ^ now.rotate_left(17) ^ pid
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ptp_header_fields() {
        let mut packet = [0u8; 44];
        packet[0] = 0x18;
        packet[1] = 0x02;
        packet[2..4].copy_from_slice(&44u16.to_be_bytes());
        packet[4] = 7;
        packet[20..28].copy_from_slice(&0x1122334455667788u64.to_be_bytes());
        packet[30..32].copy_from_slice(&42u16.to_be_bytes());
        packet[34..40].copy_from_slice(&[0, 0, 0, 0, 0, 9]);
        packet[40..44].copy_from_slice(&123u32.to_be_bytes());

        let parsed = parse_ptp_message(&packet).unwrap();
        assert_eq!(parsed.message_type, PtpMessageType::FollowUp);
        assert_eq!(parsed.transport_specific, 1);
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
        for _ in 0..25 {
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

    #[test]
    fn builds_delay_req_packet() {
        let packet = build_delay_req(1, 0, 0x1122334455667788, 7, 9_000_000_123);
        let parsed = parse_ptp_message(&packet).unwrap();
        assert_eq!(parsed.message_type, PtpMessageType::DelayReq);
        assert_eq!(parsed.transport_specific, 1);
        assert_eq!(parsed.sequence_id, 7);
        assert_eq!(parsed.clock_identity, 0x1122334455667788);
        assert_eq!(parsed.origin_timestamp_ns, Some(9_000_000_123));
    }

    #[test]
    fn delay_resp_updates_path_delay_and_offset() {
        let servo = PtpServo::new();
        let runtime = PtpRuntime::new();
        *runtime.last_sync.lock() = Some(SyncSample {
            local_receipt_ns: 1_100,
            origin_ns: Some(1_000),
            master_clock_id: 0x42,
        });
        runtime.delay_samples.lock().insert(
            9,
            DelaySample {
                sequence_id: 9,
                send_local_ns: 1_200,
            },
        );

        update_servo_from_delay_resp(&servo, &runtime, 9, 1_260);

        assert_eq!(runtime.state.lock().path_delay_ns, 80);
        assert_eq!(servo.inner.read().offset_ns as i64, 20);
        assert_eq!(servo.inner.read().master_clock_id, Some(0x42));
    }
}
