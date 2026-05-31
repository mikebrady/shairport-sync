use std::{collections::BTreeMap, net::SocketAddr, sync::Arc};

use anyhow::Context;
use plist::{Dictionary, Value};
use serde::{Deserialize, Serialize};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
    task::JoinHandle,
};
use tracing::{debug, warn};

use crate::{
    airplay::crypto::PairCipher,
    airplay::pairing::{PairingEndpoint, PairingService, PairingSession},
    airplay::sdp::parse_sdp,
    config::AirplayConfig,
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
) -> anyhow::Result<JoinHandle<()>> {
    let bind: SocketAddr = config
        .bind
        .parse()
        .with_context(|| format!("invalid AirPlay bind address {}", config.bind))?;
    let listener = TcpListener::bind(bind)
        .await
        .with_context(|| format!("failed to bind AirPlay RTSP listener {bind}"))?;
    let pairing = Arc::new(PairingService::new(config.device_id.clone()));

    Ok(tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, peer)) => {
                    let state = state.clone();
                    let config = config.clone();
                    let pairing = pairing.clone();
                    tokio::spawn(async move {
                        if let Err(err) =
                            handle_connection(stream, peer, config, state, pairing).await
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
            debug!(%peer, method = %request.method, uri = %request.uri, "RTSP request");
            let response = route_request(&config, &state, &pairing, &mut session, &request);
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
) -> RtspResponse {
    if let Some(client_name) = request.headers.get("X-Apple-Client-Name") {
        state.set_client_name(client_name.clone());
    }

    match (request.method.as_str(), request.uri.as_str()) {
        ("OPTIONS", _) => response(200, "OK")
            .header(
                "Public",
                "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER",
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
            response(200, "OK").with_cseq(request)
        }
        ("POST", "/pair-setup") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Setup, &request.body);
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
        ("SET_PARAMETER", _) => {
            apply_set_parameter(state, request);
            response(200, "OK").with_cseq(request)
        }
        ("SETUP", _) => {
            session.session_id = Some("1".to_string());
            state.set_active(true);
            response(200, "OK")
                .header("Session", "1")
                .header(
                    "Transport",
                    format!(
                        "RTP/AVP/UDP;unicast;mode=record;server_port={};control_port={};timing_port={}",
                        config.audio_port, config.control_port, config.timing_port
                    ),
                )
                .with_cseq(request)
        }
        ("RECORD", _) => {
            state.set_player_state(PlayerState::Playing);
            response(200, "OK")
                .header("Audio-Latency", "11025")
                .with_cseq(request)
        }
        ("FLUSH", _) => {
            state.set_player_state(PlayerState::Paused);
            response(200, "OK").with_cseq(request)
        }
        ("TEARDOWN", _) => {
            state.set_active(false);
            state.set_player_state(PlayerState::Stopped);
            response(200, "OK").with_cseq(request)
        }
        _ => response(404, "Not Found").with_cseq(request),
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
    dict.insert("features".into(), Value::Integer(1_643_489_111.into()));
    dict.insert("statusFlags".into(), Value::Integer(4.into()));
    dict.insert("sourceVersion".into(), Value::String("220.68".to_string()));
    dict.insert("name".into(), Value::String("Shairport RS".to_string()));
    dict.insert("model".into(), Value::String("ShairportRS1,1".to_string()));
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
}
