use std::{
    collections::BTreeMap,
    net::SocketAddr,
    sync::Arc,
};

use anyhow::Context;
use plist::{Dictionary, Value};
use serde::{Deserialize, Serialize};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
    task::JoinHandle,
};
use tracing::{debug, info, trace, warn};

use crate::{
    airplay::crypto::{IdentityKey, PairCipher},
    airplay::pairing::{PairingEndpoint, PairingService, PairingSession},
    airplay::sdp::parse_sdp,
    config::AirplayConfig,
    decoder,
    player::SharedPlayer,
    state::{AppState, PlayerState},
};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct RtspRequest {
    pub method: String,
    pub uri: String,
    pub version: String,
    pub headers: BTreeMap<String, String>,
    pub body: Vec<u8>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct RtspResponse {
    pub code: u16,
    pub reason: &'static str,
    pub headers: BTreeMap<String, String>,
    pub body: Vec<u8>,
}

pub async fn spawn_rtsp_server(
    config: AirplayConfig,
    state: AppState,
    player: SharedPlayer,
) -> anyhow::Result<JoinHandle<()>> {
    let bind: SocketAddr = config
        .bind
        .parse()
        .with_context(|| format!("invalid AirPlay bind address {}", config.bind))?;
    let listener = TcpListener::bind(bind)
        .await
        .with_context(|| format!("failed to bind AirPlay RTSP listener {bind}"))?;
    let identity_key = IdentityKey::load_or_generate(
        config.identity_key_path.as_ref().map(std::path::Path::new),
    );
    let pairing = Arc::new(PairingService::new(
        identity_key,
        config.device_id.clone(),
        config.pin.clone(),
        config.pairing_db_path.as_ref().map(std::path::PathBuf::from),
    ));

    Ok(tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, peer)) => {
                    let state = state.clone();
                    let config = config.clone();
                    let pairing = pairing.clone();
                    let player = player.clone();
                    tokio::spawn(async move {
                        if let Err(err) =
                            handle_connection(stream, peer, config, state, pairing, player).await
                        {
                            warn!(%peer, %err, "RTSP connection failed");
                        }
                    });
                }
                Err(err) => warn!(%err, "RTSP accept failed"),
            }
        }
    }))
}

async fn handle_connection(
    mut stream: TcpStream,
    peer: SocketAddr,
    config: AirplayConfig,
    state: AppState,
    pairing: Arc<PairingService>,
    player: SharedPlayer,
) -> anyhow::Result<()> {
    let mut buf = Vec::with_capacity(8192);
    let mut session = RtspSession::default();
    loop {
        let mut chunk = [0u8; 4096];
        let read = stream.read(&mut chunk).await?;
        if read == 0 {
            return Ok(());
        }
        buf.extend_from_slice(&chunk[..read]);
        while let Some((request, consumed)) = parse_request(&buf) {
            log_request(peer, &request);
            let response = route_request(&config, &state, &pairing, &mut session, &request, &player);
            log_response(peer, &request, &response);
            stream.write_all(&response.to_bytes()).await?;
            buf.drain(..consumed);
        }
    }
}

fn route_request(
    config: &AirplayConfig,
    state: &AppState,
    pairing: &PairingService,
    session: &mut RtspSession,
    request: &RtspRequest,
    player: &SharedPlayer,
) -> RtspResponse {
    if let Some(client_name) = request.headers.get("X-Apple-Client-Name") {
        state.set_client_name(client_name.clone());
    }

    match (request.method.as_str(), request.uri.as_str()) {
        ("OPTIONS", _) => response(200, "OK")
            .header(
                "Public",
                "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, FLUSHBUFFERED, TEARDOWN, OPTIONS, POST, GET, PUT",
            )
            .with_cseq(request),
        ("GET", "/info") | ("GET", "info") => {
            response(200, "OK")
                .header("Content-Type", "application/x-apple-binary-plist")
                .body(get_info_body(config))
                .with_cseq(request)
        }
        ("ANNOUNCE", _) => {
            let sdp = String::from_utf8_lossy(&request.body);
            let parsed = parse_sdp(&sdp);
            state.set_source_format(parsed.source_format_description());
            let params = parsed.classic_params();

            if let Some(encrypted_key) = &params.rsaaeskey {
                // Try to generate an RSA key on-the-fly for key exchange.
                // For Phase 1 this is best-effort: if it fails, the session
                // will still proceed but decryption will not work.
                let rsa_key = decoder::generate_rsa_key();
                match decoder::rsa_oaep_decrypt(&rsa_key, encrypted_key) {
                    Ok(mut aes_key_bytes) => {
                        if aes_key_bytes.len() >= 16 {
                            aes_key_bytes.truncate(16);
                            let mut key = [0u8; 16];
                            key.copy_from_slice(&aes_key_bytes);
                            *state.session_key.write() = Some(key);
                            info!("classic AirPlay AES session key derived");
                        }
                    }
                    Err(e) => {
                        warn!(%e, "RSA-OAEP decryption of AES key failed");
                    }
                }
            }

            if let Some(iv) = &params.aesiv {
                if iv.len() == 16 {
                    state.set_diagnostic("aes_iv_set", "yes");
                }
            }

            if let Some(asc) = &params.alac_specific_config {
                state.alac_magic_cookie.write().clone_from(&Some(asc.clone()));
            }
            if let Some(rate) = params.alac_sample_rate {
                state.alac_sample_rate.write().clone_from(&Some(rate));
            }
            if let Some(fpp) = params.frames_per_packet {
                state.frames_per_packet.write().clone_from(&Some(fpp));
            }
            // channels default to 2 for classic AP
            state.alac_channels.write().clone_from(&Some(2));

            response(200, "OK").with_cseq(request)
        }
        ("POST", "/pair-setup") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Setup, &request.body);
            response(reply.status_code, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(reply.body)
                .with_cseq(request)
        }
        ("POST", "/pair-pin-start") => response(200, "OK").with_cseq(request),
        ("POST", "/pair-add") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Add, &request.body);
            response(reply.status_code, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(reply.body)
                .with_cseq(request)
        }
        ("POST", "/pair-remove") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Remove, &request.body);
            response(reply.status_code, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(reply.body)
                .with_cseq(request)
        }
        ("POST", "/pair-list") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::List, &request.body);
            response(reply.status_code, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(reply.body)
                .with_cseq(request)
        }
        ("POST", "/pair-verify") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Verify, &request.body);
            if session.control_cipher.is_none()
                && let Some(shared_secret) = session.pairing.shared_secret()
            {
                session.control_cipher = Some(PairCipher::control_for_server(shared_secret));
            }
            response(reply.status_code, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(reply.body)
                .with_cseq(request)
        }
        ("POST", "/fp-setup") => {
            let body = fairplay_setup_reply(&request.body).unwrap_or_default();
            response(200, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(body)
                .with_cseq(request)
        }
        ("POST", "/command") | ("POST", "/feedback") => response(200, "OK").with_cseq(request),
        ("GET_PARAMETER", _) => response(200, "OK").with_cseq(request),
        ("SET_PARAMETER", _) => {
            apply_set_parameter(state, request);
            response(200, "OK").with_cseq(request)
        }
        ("SETUP", _) => {
            session.session_id = Some("1".to_string());
            state.set_active(true);
            let server_port = config.audio_port;
            let control_port = config.control_port;
            let timing_port = config.timing_port;
            info!(server_port, control_port, timing_port, "AP1 SETUP");

            response(200, "OK")
                .header("Session", "1")
                .header(
                    "Transport",
                    format!(
                        "RTP/AVP/UDP;unicast;mode=record;server_port={};control_port={};timing_port={}",
                        server_port, control_port, timing_port
                    ),
                )
                .with_cseq(request)
        }
        ("RECORD", _) => {
            let latency = request
                .headers
                .get("X-Apple-Latency")
                .and_then(|v| v.parse::<u32>().ok())
                .unwrap_or(11025);
            // Derive sample rate from the alac_sample_rate state
            let rate = state.alac_sample_rate.read().unwrap_or(44100);
            player.set_sample_rate(rate);
            player.start(latency);
            state.set_player_state(PlayerState::Playing);
            state.set_diagnostic("ap1_latency", latency.to_string());
            info!(latency, "AP1 RECORD");

            response(200, "OK")
                .header("Audio-Latency", latency.to_string())
                .header("Audio-Jack-Status", "connected; type=analog")
                .with_cseq(request)
        }
        ("FLUSH", _) => {
            player.flush();
            state.set_player_state(PlayerState::Paused);
            info!("AP1 FLUSH");
            response(200, "OK").with_cseq(request)
        }
        ("FLUSHBUFFERED", _) => {
            state.set_player_state(PlayerState::Paused);
            warn!("FLUSHBUFFERED called in AP1 mode — ignoring");
            response(200, "OK").with_cseq(request)
        }
        ("TEARDOWN", _) => {
            player.stop();
            state.set_active(false);
            state.set_player_state(PlayerState::Stopped);
            *state.session_key.write() = None;
            info!("AP1 TEARDOWN");
            response(200, "OK").with_cseq(request)
        }
        _ => response(404, "Not Found").with_cseq(request),
    }
}

fn log_request(peer: SocketAddr, request: &RtspRequest) {
    debug!(
        %peer,
        method = %request.method,
        uri = %request.uri,
        cseq = request.headers.get("CSeq").map(String::as_str).unwrap_or(""),
        content_type = request
            .headers
            .get("Content-Type")
            .map(String::as_str)
            .unwrap_or(""),
        content_length = request.body.len(),
        "RTSP request"
    );
    trace!(
        %peer,
        headers = ?request.headers,
        body_preview = %body_preview(&request.body),
        "RTSP request details"
    );
}

fn log_response(peer: SocketAddr, request: &RtspRequest, response: &RtspResponse) {
    debug!(
        %peer,
        method = %request.method,
        uri = %request.uri,
        cseq = request.headers.get("CSeq").map(String::as_str).unwrap_or(""),
        status = response.code,
        content_length = response.body.len(),
        "RTSP response"
    );
    trace!(
        %peer,
        headers = ?response.headers,
        body_preview = %body_preview(&response.body),
        "RTSP response details"
    );
}

fn body_preview(body: &[u8]) -> String {
    const LIMIT: usize = 96;
    if body.is_empty() {
        return String::new();
    }
    let mut preview = body
        .iter()
        .take(LIMIT)
        .map(|byte| format!("{byte:02x}"))
        .collect::<Vec<_>>()
        .join(" ");
    if body.len() > LIMIT {
        preview.push_str(" ...");
    }
    preview
}

const FAIRPLAY_REPLY_MODE_0: &[u8] = b"\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x00\x0f\x9f\x3f\x9e\x0a\x25\x21\xdb\xdf\x31\x2a\xb2\xbf\xb2\x9e\x8d\x23\x2b\x63\x76\xa8\xc8\x18\x70\x1d\x22\xae\x93\xd8\x27\x37\xfe\xaf\x9d\xb4\xfd\xf4\x1c\x2d\xba\x9d\x1f\x49\xca\xaa\xbf\x65\x91\xac\x1f\x7b\xc6\xf7\xe0\x66\x3d\x21\xaf\xe0\x15\x65\x95\x3e\xab\x81\xf4\x18\xce\xed\x09\x5a\xdb\x7c\x3d\x0e\x25\x49\x09\xa7\x98\x31\xd4\x9c\x39\x82\x97\x34\x34\xfa\xcb\x42\xc6\x3a\x1c\xd9\x11\xa6\xfe\x94\x1a\x8a\x6d\x4a\x74\x3b\x46\xc3\xa7\x64\x9e\x44\xc7\x89\x55\xe4\x9d\x81\x55\x00\x95\x49\xc4\xe2\xf7\xa3\xf6\xd5\xba";
const FAIRPLAY_REPLY_MODE_1: &[u8] = b"\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x01\xcf\x32\xa2\x57\x14\xb2\x52\x4f\x8a\xa0\xad\x7a\xf1\x64\xe3\x7b\xcf\x44\x24\xe2\x00\x04\x7e\xfc\x0a\xd6\x7a\xfc\xd9\x5d\xed\x1c\x27\x30\xbb\x59\x1b\x96\x2e\xd6\x3a\x9c\x4d\xed\x88\xba\x8f\xc7\x8d\xe6\x4d\x91\xcc\xfd\x5c\x7b\x56\xda\x88\xe3\x1f\x5c\xce\xaf\xc7\x43\x19\x95\xa0\x16\x65\xa5\x4e\x19\x39\xd2\x5b\x94\xdb\x64\xb9\xe4\x5d\x8d\x06\x3e\x1e\x6a\xf0\x7e\x96\x56\x16\x2b\x0e\xfa\x40\x42\x75\xea\x5a\x44\xd9\x59\x1c\x72\x56\xb9\xfb\xe6\x51\x38\x98\xb8\x02\x27\x72\x19\x88\x57\x16\x50\x94\x2a\xd9\x46\x68\x8a";
const FAIRPLAY_REPLY_MODE_2: &[u8] = b"\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x02\xc1\x69\xa3\x52\xee\xed\x35\xb1\x8c\xdd\x9c\x58\xd6\x4f\x16\xc1\x51\x9a\x89\xeb\x53\x17\xbd\x0d\x43\x36\xcd\x68\xf6\x38\xff\x9d\x01\x6a\x5b\x52\xb7\xfa\x92\x16\xb2\xb6\x54\x82\xc7\x84\x44\x11\x81\x21\xa2\xc7\xfe\xd8\x3d\xb7\x11\x9e\x91\x82\xaa\xd7\xd1\x8c\x70\x63\xe2\xa4\x57\x55\x59\x10\xaf\x9e\x0e\xfc\x76\x34\x7d\x16\x40\x43\x80\x7f\x58\x1e\xe4\xfb\xe4\x2c\xa9\xde\xdc\x1b\x5e\xb2\xa3\xaa\x3d\x2e\xcd\x59\xe7\xee\xe7\x0b\x36\x29\xf2\x2a\xfd\x16\x1d\x87\x73\x53\xdd\xb9\x9a\xdc\x8e\x07\x00\x6e\x56\xf8\x50\xce";
const FAIRPLAY_REPLY_MODE_3: &[u8] = b"\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x03\x90\x01\xe1\x72\x7e\x0f\x57\xf9\xf5\x88\x0d\xb1\x04\xa6\x25\x7a\x23\xf5\xcf\xff\x1a\xbb\xe1\xe9\x30\x45\x25\x1a\xfb\x97\xeb\x9f\xc0\x01\x1e\xbe\x0f\x3a\x81\xdf\x5b\x69\x1d\x76\xac\xb2\xf7\xa5\xc7\x08\xe3\xd3\x28\xf5\x6b\xb3\x9d\xbd\xe5\xf2\x9c\x8a\x17\xf4\x81\x48\x7e\x3a\xe8\x63\xc6\x78\x32\x54\x22\xe6\xf7\x8e\x16\x6d\x18\xaa\x7f\xd6\x36\x25\x8b\xce\x28\x72\x6f\x66\x1f\x73\x88\x93\xce\x44\x31\x1e\x4b\xe6\xc0\x53\x51\x93\xe5\xef\x72\xe8\x68\x62\x33\x72\x9c\x22\x7d\x82\x0c\x99\x94\x45\xd8\x92\x46\xc8\xc3\x59";
const FAIRPLAY_SETUP2_HEADER: &[u8] = b"\x46\x50\x4c\x59\x03\x01\x04\x00\x00\x00\x00\x14";

fn fairplay_setup_reply(body: &[u8]) -> Option<Vec<u8>> {
    if body.len() <= 14 {
        warn!(length = body.len(), "FairPlay setup request too short");
        return None;
    }
    if body[4] != 3 || body[5] != 1 {
        warn!(
            version = body[4],
            message_type = body[5],
            "unsupported FairPlay setup request"
        );
    }

    match body[6] {
        1 => match body[14] {
            0 => Some(FAIRPLAY_REPLY_MODE_0.to_vec()),
            1 => Some(FAIRPLAY_REPLY_MODE_1.to_vec()),
            2 => Some(FAIRPLAY_REPLY_MODE_2.to_vec()),
            3 => Some(FAIRPLAY_REPLY_MODE_3.to_vec()),
            mode => {
                warn!(mode, "unsupported FairPlay setup mode");
                None
            }
        },
        3 if body.len() >= 20 => {
            let mut reply = Vec::with_capacity(FAIRPLAY_SETUP2_HEADER.len() + 20);
            reply.extend_from_slice(FAIRPLAY_SETUP2_HEADER);
            reply.extend_from_slice(&body[body.len() - 20..]);
            Some(reply)
        }
        sequence => {
            warn!(sequence, "unsupported FairPlay setup sequence");
            None
        }
    }
}

#[derive(Default)]
struct RtspSession {
    session_id: Option<String>,
    pairing: PairingSession,
    control_cipher: Option<PairCipher>,
}

fn apply_set_parameter(state: &AppState, request: &RtspRequest) {
    let content_type = request
        .headers
        .get("Content-Type")
        .map(String::as_str)
        .unwrap_or_default();
    if content_type.contains("text/parameters") {
        let body = String::from_utf8_lossy(&request.body);
        let mut title = None;
        let mut artist = None;
        let mut album = None;
        for line in body.lines() {
            if let Some(value) = line.strip_prefix("volume:") {
                if let Ok(db) = value.trim().parse::<f64>() {
                    state.set_airplay_volume(db);
                }
            } else if let Some(value) = line.strip_prefix("title:") {
                title = Some(value.trim().to_string());
            } else if let Some(value) = line.strip_prefix("artist:") {
                artist = Some(value.trim().to_string());
            } else if let Some(value) = line.strip_prefix("album:") {
                album = Some(value.trim().to_string());
            }
        }
        state.set_track_metadata(title, artist, album);
    }
}

fn get_info_body(config: &AirplayConfig) -> Vec<u8> {
    let mut dict = Dictionary::new();
    dict.insert("deviceID".into(), Value::String(config.device_id.clone()));
    dict.insert(
        "features".into(),
        Value::Integer(496_155_701_824_000i64.into()),
    );
    dict.insert("statusFlags".into(), Value::Integer(4.into()));
    dict.insert("sourceVersion".into(), Value::String("366.0".to_string()));
    dict.insert("name".into(), Value::String("Shairport RS".to_string()));
    dict.insert("model".into(), Value::String("ShairportSync".to_string()));
    dict.insert(
        "manufacturer".into(),
        Value::String("Shairport RS".to_string()),
    );
    dict.insert("protovers".into(), Value::String("1.1".to_string()));

    let mut out = Vec::new();
    plist::to_writer_binary(&mut out, &Value::Dictionary(dict))
        .expect("serializing in-memory plist should not fail");
    out
}

pub fn parse_request(buf: &[u8]) -> Option<(RtspRequest, usize)> {
    let header_end = find_header_end(buf)?;
    let headers_raw = std::str::from_utf8(&buf[..header_end]).ok()?;
    let mut lines = headers_raw.split("\r\n");
    let request_line = lines.next()?;
    let mut parts = request_line.split_whitespace();
    let method = parts.next()?.to_string();
    let uri = parts.next()?.to_string();
    let version = parts.next()?.to_string();
    let mut headers = BTreeMap::new();
    for line in lines {
        if let Some((name, value)) = line.split_once(':') {
            headers.insert(name.trim().to_string(), value.trim().to_string());
        }
    }
    let content_length = headers
        .get("Content-Length")
        .and_then(|v| v.parse::<usize>().ok())
        .unwrap_or(0);
    let body_start = header_end + 4;
    let consumed = body_start + content_length;
    if buf.len() < consumed {
        return None;
    }
    Some((
        RtspRequest {
            method,
            uri,
            version,
            headers,
            body: buf[body_start..consumed].to_vec(),
        },
        consumed,
    ))
}

fn find_header_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|window| window == b"\r\n\r\n")
}

fn response(code: u16, reason: &'static str) -> RtspResponse {
    RtspResponse {
        code,
        reason,
        headers: BTreeMap::new(),
        body: Vec::new(),
    }
}

impl RtspResponse {
    fn header(mut self, name: impl Into<String>, value: impl Into<String>) -> Self {
        self.headers.insert(name.into(), value.into());
        self
    }

    fn body(mut self, body: Vec<u8>) -> Self {
        self.headers
            .insert("Content-Length".to_string(), body.len().to_string());
        self.body = body;
        self
    }

    fn with_cseq(mut self, request: &RtspRequest) -> Self {
        if let Some(cseq) = request.headers.get("CSeq") {
            self.headers.insert("CSeq".to_string(), cseq.clone());
        }
        self
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut out = format!("RTSP/1.0 {} {}\r\n", self.code, self.reason).into_bytes();
        for (name, value) in &self.headers {
            out.extend_from_slice(format!("{name}: {value}\r\n").as_bytes());
        }
        out.extend_from_slice(b"\r\n");
        out.extend_from_slice(&self.body);
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_rtsp_request_with_body() {
        let raw = b"SET_PARAMETER rtsp://x RTSP/1.0\r\nCSeq: 4\r\nContent-Length: 15\r\n\r\nvolume: -10.0\r\n";
        let (request, consumed) = parse_request(raw).unwrap();
        assert_eq!(consumed, raw.len());
        assert_eq!(request.method, "SET_PARAMETER");
        assert_eq!(request.headers.get("CSeq").unwrap(), "4");
        assert_eq!(request.body, b"volume: -10.0\r\n");
    }

    #[test]
    fn serializes_cseq_response() {
        let (request, _) = parse_request(b"OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n").unwrap();
        let bytes = response(200, "OK").with_cseq(&request).to_bytes();
        let text = String::from_utf8(bytes).unwrap();
        assert!(text.contains("RTSP/1.0 200 OK"));
        assert!(text.contains("CSeq: 1"));
    }

    #[test]
    fn fairplay_setup1_returns_mode_reply() {
        let mut request = vec![0; 16];
        request[4] = 3;
        request[5] = 1;
        request[6] = 1;
        request[14] = 2;

        let reply = fairplay_setup_reply(&request).unwrap();

        assert_eq!(reply, FAIRPLAY_REPLY_MODE_2);
    }

    #[test]
    fn fairplay_setup2_echoes_suffix_with_header() {
        let mut request = vec![0; 40];
        request[4] = 3;
        request[5] = 1;
        request[6] = 3;
        for (index, byte) in request[20..].iter_mut().enumerate() {
            *byte = index as u8;
        }

        let reply = fairplay_setup_reply(&request).unwrap();

        assert_eq!(
            &reply[..FAIRPLAY_SETUP2_HEADER.len()],
            FAIRPLAY_SETUP2_HEADER
        );
        assert_eq!(&reply[FAIRPLAY_SETUP2_HEADER.len()..], &request[20..]);
    }
}
