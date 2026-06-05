use std::{
    collections::BTreeMap,
    sync::Arc,
    time::SystemTime,
};

use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use tokio::sync::broadcast;

use crate::{audio::AudioDevice, config::Config};

#[derive(Clone)]
pub struct AppState {
    inner: Arc<RwLock<StateSnapshot>>,
    events: broadcast::Sender<StateSnapshot>,
    pub session_key: Arc<RwLock<Option<[u8; 16]>>>,
    pub alac_magic_cookie: Arc<RwLock<Option<Vec<u8>>>>,
    pub alac_sample_rate: Arc<RwLock<Option<u32>>>,
    pub alac_channels: Arc<RwLock<Option<u16>>>,
    pub frames_per_packet: Arc<RwLock<Option<u32>>>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct StateSnapshot {
    pub active: bool,
    pub player_state: PlayerState,
    pub stream_type: Option<String>,
    pub track: TrackInfo,
    pub volume: VolumeState,
    pub audio: AudioState,
    pub rtp: RtpState,
    pub mdns: MdnsState,
    pub ptp: PtpState,
    pub diagnostics: BTreeMap<String, String>,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum PlayerState {
    Stopped,
    Playing,
    Paused,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct TrackInfo {
    pub title: Option<String>,
    pub artist: Option<String>,
    pub album: Option<String>,
    pub artwork_url: Option<String>,
    pub progress_ms: Option<u64>,
    pub duration_ms: Option<u64>,
    pub client_name: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct VolumeState {
    pub airplay_db: f64,
    pub local_db: f64,
    pub muted: bool,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct AudioState {
    pub backend: String,
    pub host: String,
    pub selected_device: Option<String>,
    pub source_format: Option<String>,
    pub output_format: Option<String>,
    pub devices: Vec<AudioDevice>,
    pub underruns: u64,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct RtpState {
    pub audio_port: u16,
    pub control_port: u16,
    pub timing_port: u16,
    pub audio_packets: u64,
    pub control_packets: u64,
    pub timing_packets: u64,
    pub last_audio_sequence: Option<u16>,
    pub last_audio_timestamp: Option<u32>,
    pub last_audio_ssrc: Option<u32>,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct MdnsState {
    pub backend: String,
    pub running: bool,
    pub error: Option<String>,
    pub services: Vec<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct PtpState {
    pub enabled: bool,
    pub running: bool,
    pub master_clock_id: Option<String>,
    pub offset_ns: Option<i64>,
    pub last_message_at: Option<SystemTime>,
    pub sync_quality: SyncQuality,
    pub packets_seen: u64,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum SyncQuality {
    Unknown,
    Searching,
    Locked,
    Stale,
    Error,
}

impl AppState {
    pub fn new(config: Config) -> Self {
        let (events, _) = broadcast::channel(64);
        let snapshot = StateSnapshot {
            active: false,
            player_state: PlayerState::Stopped,
            stream_type: None,
            track: TrackInfo::default(),
            volume: VolumeState {
                airplay_db: -30.0,
                local_db: -30.0,
                muted: false,
            },
            audio: AudioState {
                backend: "cpal".to_string(),
                host: config.audio.host.to_string(),
                selected_device: config.audio.device,
                ..AudioState::default()
            },
            rtp: RtpState {
                audio_port: config.airplay.audio_port,
                control_port: config.airplay.control_port,
                timing_port: config.airplay.timing_port,
                ..RtpState::default()
            },
            mdns: MdnsState {
                backend: config.mdns.backend.to_string(),
                ..MdnsState::default()
            },
            ptp: PtpState {
                enabled: config.ptp.enabled,
                running: false,
                master_clock_id: None,
                offset_ns: None,
                last_message_at: None,
                sync_quality: SyncQuality::Searching,
                packets_seen: 0,
            },
            diagnostics: BTreeMap::new(),
        };
        Self {
            inner: Arc::new(RwLock::new(snapshot)),
            events,
            session_key: Arc::new(RwLock::new(None)),
            alac_magic_cookie: Arc::new(RwLock::new(None)),
            alac_sample_rate: Arc::new(RwLock::new(None)),
            alac_channels: Arc::new(RwLock::new(None)),
            frames_per_packet: Arc::new(RwLock::new(None)),
        }
    }

    pub fn snapshot(&self) -> StateSnapshot {
        self.inner.read().clone()
    }

    pub fn subscribe(&self) -> broadcast::Receiver<StateSnapshot> {
        self.events.subscribe()
    }

    pub fn update_audio_devices(&self, devices: Vec<AudioDevice>) {
        self.mutate(|state| state.audio.devices = devices);
    }

    pub fn select_audio_device(&self, device: Option<String>) {
        self.mutate(|state| state.audio.selected_device = device);
    }

    pub fn set_source_format(&self, source_format: Option<String>) {
        self.mutate(|state| state.audio.source_format = source_format);
    }

    pub fn set_volume(&self, local_db: f64) {
        self.mutate(|state| {
            state.volume.local_db = local_db.clamp(-144.0, 0.0);
            state.volume.muted = state.volume.local_db <= -144.0;
        });
    }

    pub fn set_airplay_volume(&self, airplay_db: f64) {
        self.mutate(|state| {
            state.volume.airplay_db = airplay_db.clamp(-144.0, 0.0);
            state.volume.muted = state.volume.airplay_db <= -144.0;
        });
    }

    pub fn set_active(&self, active: bool) {
        self.mutate(|state| {
            state.active = active;
            if !active {
                state.player_state = PlayerState::Stopped;
            }
        });
    }

    pub fn set_player_state(&self, player_state: PlayerState) {
        self.mutate(|state| state.player_state = player_state);
    }

    pub fn set_client_name(&self, client_name: String) {
        self.mutate(|state| state.track.client_name = Some(client_name));
    }

    pub fn set_track_metadata(
        &self,
        title: Option<String>,
        artist: Option<String>,
        album: Option<String>,
    ) {
        self.mutate(|state| {
            if title.is_some() {
                state.track.title = title;
            }
            if artist.is_some() {
                state.track.artist = artist;
            }
            if album.is_some() {
                state.track.album = album;
            }
        });
    }

    pub fn set_mdns_running(&self, backend: String, service_types: Vec<String>) {
        self.mutate(|state| {
            state.mdns.backend = backend;
            state.mdns.running = true;
            state.mdns.error = None;
            state.mdns.services = service_types;
        });
    }

    pub fn set_mdns_error(&self, error: String) {
        self.mutate(|state| {
            state.mdns.running = false;
            state.mdns.error = Some(error);
        });
    }

    pub fn mark_ptp_running(&self) {
        self.mutate(|state| {
            state.ptp.running = true;
            state.ptp.sync_quality = SyncQuality::Searching;
        });
    }

    pub fn record_ptp_packet(&self, message: crate::ptp::PtpMessage) {
        self.mutate(|state| {
            state.ptp.running = true;
            state.ptp.packets_seen += 1;
            state.ptp.last_message_at = Some(SystemTime::now());
            state.ptp.master_clock_id = Some(format!("{:016x}", message.clock_identity));
            state.ptp.offset_ns = message.estimated_offset_ns;
            state.ptp.sync_quality = SyncQuality::Locked;
        });
    }

    pub fn record_rtp_packet(
        &self,
        channel: crate::airplay::rtp::RtpChannel,
        packet: crate::airplay::rtp::RtpPacket,
    ) {
        self.mutate(|state| match channel {
            crate::airplay::rtp::RtpChannel::Audio => {
                state.rtp.audio_packets += 1;
                state.rtp.last_audio_sequence = Some(packet.sequence_number);
                state.rtp.last_audio_timestamp = Some(packet.timestamp);
                state.rtp.last_audio_ssrc = Some(packet.ssrc);
            }
            crate::airplay::rtp::RtpChannel::Control => state.rtp.control_packets += 1,
            crate::airplay::rtp::RtpChannel::Timing => state.rtp.timing_packets += 1,
        });
    }

    pub fn set_diagnostic(&self, key: impl Into<String>, value: impl Into<String>) {
        self.mutate(|state| {
            state.diagnostics.insert(key.into(), value.into());
        });
    }

    fn mutate(&self, update: impl FnOnce(&mut StateSnapshot)) {
        let snapshot = {
            let mut state = self.inner.write();
            update(&mut state);
            state.clone()
        };
        let _ = self.events.send(snapshot);
    }
}
