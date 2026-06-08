use std::{
    collections::BTreeMap,
    net::{IpAddr, Ipv4Addr, SocketAddr},
    sync::Arc,
};

use anyhow::Context;
use plist::{Dictionary, Value};
use serde::{Deserialize, Serialize};
use socket2::{Domain, Protocol, Socket, Type};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream, UdpSocket},
    task::JoinHandle,
};
use tracing::{debug, info, trace, warn};

use crate::{
    airplay::crypto::{IdentityKey, PairCipher},
    airplay::pairing::{PairingEndpoint, PairingService, PairingSession},
    airplay::sdp::parse_sdp,
    audio::AudioEngine,
    config::{AdvertisedFormatPolicy, AirplayConfig},
    decoder,
    player::SharedPlayer,
    ptp,
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
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

pub async fn spawn_rtsp_server(
    config: AirplayConfig,
    state: AppState,
    audio_engine: AudioEngine,
    player: SharedPlayer,
) -> anyhow::Result<JoinHandle<()>> {
    let bind: SocketAddr = config
        .bind
        .parse()
        .with_context(|| format!("invalid AirPlay bind address {}", config.bind))?;
    let listener = bind_rtsp_listener(bind)
        .await
        .with_context(|| format!("failed to bind AirPlay RTSP listener {bind}"))?;
    let identity_key = IdentityKey::load_or_generate(
        config.identity_key_path.as_ref().map(std::path::Path::new),
        &config.device_id,
    );
    let pairing = Arc::new(PairingService::new(
        identity_key,
        config.device_id.clone(),
        config.pin.clone(),
        config
            .pairing_db_path
            .as_ref()
            .map(std::path::PathBuf::from),
    ));

    Ok(tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, peer)) => {
                    let state = state.clone();
                    let config = config.clone();
                    let pairing = pairing.clone();
                    let audio_engine = audio_engine.clone();
                    let player = player.clone();
                    tokio::spawn(async move {
                        if let Err(err) = handle_connection(
                            stream,
                            peer,
                            config,
                            state,
                            pairing,
                            audio_engine,
                            player,
                        )
                        .await
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

async fn bind_rtsp_listener(bind: SocketAddr) -> anyhow::Result<TcpListener> {
    if bind.ip().is_ipv4() && bind.ip().is_unspecified() {
        let dual_stack_bind = SocketAddr::from((std::net::Ipv6Addr::UNSPECIFIED, bind.port()));
        let socket = Socket::new(Domain::IPV6, Type::STREAM, Some(Protocol::TCP))?;
        socket.set_only_v6(false)?;
        socket.set_reuse_address(true)?;
        socket.bind(&dual_stack_bind.into())?;
        socket.listen(128)?;
        socket.set_nonblocking(true)?;
        let listener: std::net::TcpListener = socket.into();
        return TcpListener::from_std(listener).context("failed to create Tokio RTSP listener");
    }

    TcpListener::bind(bind).await.map_err(Into::into)
}

fn spawn_tcp_drain_listener(
    bind_addr: SocketAddr,
    label: &'static str,
) -> anyhow::Result<(u16, JoinHandle<()>)> {
    let socket = match bind_addr {
        SocketAddr::V4(_) => Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?,
        SocketAddr::V6(_) => {
            let socket = Socket::new(Domain::IPV6, Type::STREAM, Some(Protocol::TCP))?;
            socket.set_only_v6(false)?;
            socket
        }
    };
    socket.set_reuse_address(true)?;
    socket.bind(&bind_addr.into())?;
    socket.listen(128)?;
    socket.set_nonblocking(true)?;
    let std_listener: std::net::TcpListener = socket.into();
    let port = std_listener.local_addr()?.port();
    let listener = TcpListener::from_std(std_listener)?;

    let handle = tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((mut stream, peer)) => {
                    info!(%peer, %label, "AP2 TCP connection opened");
                    tokio::spawn(async move {
                        let mut buf = [0u8; 2048];
                        loop {
                            match stream.read(&mut buf).await {
                                Ok(0) => {
                                    debug!(%peer, %label, "AP2 TCP connection closed");
                                    break;
                                }
                                Ok(read) => {
                                    trace!(%peer, %label, bytes_read = read, "AP2 TCP data received");
                                }
                                Err(e) => {
                                    warn!(%peer, %e, %label, "AP2 TCP connection failed");
                                    break;
                                }
                            }
                        }
                    });
                }
                Err(e) => warn!(%e, %label, "AP2 TCP accept failed"),
            }
        }
    });

    Ok((port, handle))
}

fn spawn_ap2_event_listener(
    bind_addr: SocketAddr,
    config: AirplayConfig,
    peer_addr: Option<SocketAddr>,
    shared_secret: Vec<u8>,
    group_uuid: Option<String>,
    group_contains_group_leader: Option<bool>,
) -> anyhow::Result<(u16, JoinHandle<()>)> {
    let socket = match bind_addr {
        SocketAddr::V4(_) => Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?,
        SocketAddr::V6(_) => {
            let socket = Socket::new(Domain::IPV6, Type::STREAM, Some(Protocol::TCP))?;
            socket.set_only_v6(false)?;
            socket
        }
    };
    socket.set_reuse_address(true)?;
    socket.bind(&bind_addr.into())?;
    socket.listen(5)?;
    socket.set_nonblocking(true)?;
    let std_listener: std::net::TcpListener = socket.into();
    let port = std_listener.local_addr()?.port();
    let listener = TcpListener::from_std(std_listener)?;

    let handle = tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((mut stream, peer)) => {
                    info!(%peer, "AP2 event connection opened");
                    let config = config.clone();
                    let shared_secret = shared_secret.clone();
                    let group_uuid = group_uuid.clone();
                    tokio::spawn(async move {
                        let mut cipher = PairCipher::events_for_server(&shared_secret);
                        match build_ap2_update_info_event(
                            &config,
                            peer_addr,
                            group_uuid.as_deref(),
                            group_contains_group_leader,
                        ) {
                            Ok(wire) => match cipher.encrypt_blocks(&wire) {
                                Ok(encrypted) => {
                                    if let Err(e) = stream.write_all(&encrypted).await {
                                        warn!(%peer, %e, "failed to send AP2 event updateInfo");
                                        return;
                                    }
                                    debug!(
                                        %peer,
                                        plaintext_len = wire.len(),
                                        encrypted_len = encrypted.len(),
                                        "AP2 event updateInfo sent"
                                    );
                                }
                                Err(e) => {
                                    warn!(%peer, %e, "failed to encrypt AP2 event updateInfo");
                                    return;
                                }
                            },
                            Err(e) => {
                                warn!(%peer, %e, "failed to build AP2 event updateInfo");
                                return;
                            }
                        }

                        let mut encrypted_buf = Vec::new();
                        let mut read_buf = [0u8; 2048];
                        loop {
                            match stream.read(&mut read_buf).await {
                                Ok(0) => {
                                    debug!(%peer, "AP2 event connection closed");
                                    break;
                                }
                                Ok(read) => {
                                    encrypted_buf.extend_from_slice(&read_buf[..read]);
                                    match cipher.decrypt_blocks(&encrypted_buf) {
                                        Ok((plain, consumed)) => {
                                            if consumed > 0 {
                                                encrypted_buf.drain(..consumed);
                                            }
                                            if !plain.is_empty() {
                                                debug!(
                                                    %peer,
                                                    plaintext_len = plain.len(),
                                                    plaintext = %String::from_utf8_lossy(&plain),
                                                    "AP2 event payload received"
                                                );
                                            }
                                        }
                                        Err(e) => {
                                            warn!(%peer, %e, bytes_read = read, "failed to decrypt AP2 event payload");
                                            break;
                                        }
                                    }
                                }
                                Err(e) => {
                                    warn!(%peer, %e, "AP2 event connection failed");
                                    break;
                                }
                            }
                        }
                    });
                }
                Err(e) => warn!(%e, "AP2 event accept failed"),
            }
        }
    });

    Ok((port, handle))
}

fn build_ap2_update_info_event(
    config: &AirplayConfig,
    peer_addr: Option<SocketAddr>,
    group_uuid: Option<&str>,
    group_contains_group_leader: Option<bool>,
) -> anyhow::Result<Vec<u8>> {
    let info_body = get_info_body_with_group(
        config,
        peer_addr,
        group_uuid,
        group_contains_group_leader.unwrap_or(false),
    );
    let info_value: Value =
        plist::from_bytes(&info_body).context("failed to parse generated /info plist")?;
    debug_ap2_info_payload("AP2 updateInfo value built", &info_value, group_uuid);
    let mut update_info = Dictionary::new();
    update_info.insert("type".to_string(), Value::String("updateInfo".to_string()));
    update_info.insert("value".to_string(), info_value);

    let mut body = Vec::new();
    plist::to_writer_binary(&mut body, &Value::Dictionary(update_info))
        .context("failed to serialize AP2 updateInfo plist")?;

    let mut wire = format!(
        "POST /command RTSP/1.0\r\nContent-Length: {}\r\nContent-Type: application/x-apple-binary-plist\r\n\r\n",
        body.len()
    )
    .into_bytes();
    wire.extend_from_slice(&body);
    Ok(wire)
}

fn spawn_udp_drain_socket(
    bind_addr: SocketAddr,
    label: &'static str,
) -> anyhow::Result<(u16, JoinHandle<()>)> {
    let std_socket = match bind_addr {
        SocketAddr::V4(_) => Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?,
        SocketAddr::V6(_) => {
            let socket = Socket::new(Domain::IPV6, Type::DGRAM, Some(Protocol::UDP))?;
            socket.set_only_v6(false)?;
            socket
        }
    };
    std_socket.set_reuse_address(true)?;
    std_socket.bind(&bind_addr.into())?;
    std_socket.set_nonblocking(true)?;
    let udp = UdpSocket::from_std(std_socket.into())?;
    let port = udp.local_addr()?.port();
    let handle = tokio::spawn(async move {
        let mut buf = [0u8; 2048];
        loop {
            match udp.recv_from(&mut buf).await {
                Ok((read, peer)) => {
                    trace!(%peer, %label, bytes_read = read, "AP2 UDP packet received")
                }
                Err(e) => {
                    warn!(%e, %label, "AP2 UDP socket failed");
                    break;
                }
            }
        }
    });
    Ok((port, handle))
}

fn receiver_timing_addresses(primary: IpAddr) -> Vec<String> {
    let mut addresses = Vec::new();
    push_unique_ip(&mut addresses, primary);
    for ip in local_non_loopback_interface_addresses() {
        push_unique_ip(&mut addresses, ip);
    }
    addresses
}

fn receiver_primary_ip(socket_ip: Option<IpAddr>, override_ip: Option<&str>) -> IpAddr {
    if let Some(override_ip) = override_ip {
        match override_ip.parse::<IpAddr>() {
            Ok(ip) if !ip.is_unspecified() => return ip,
            Ok(ip) => warn!(%ip, "ignoring AP2 bind IP override because it is not usable"),
            Err(e) => warn!(%override_ip, %e, "ignoring invalid AP2 bind IP override"),
        }
    }

    if let Some(ip) = socket_ip
        && !ip.is_unspecified()
    {
        return ip;
    }

    let interface_ips = local_non_loopback_interface_addresses();
    interface_ips
        .into_iter()
        .find(|ip| ip.is_ipv4())
        .or_else(|| local_non_loopback_interface_addresses().into_iter().next())
        .unwrap_or(IpAddr::V4(Ipv4Addr::LOCALHOST))
}

fn event_bind_addr(local_addr: Option<SocketAddr>, primary_ip: IpAddr) -> SocketAddr {
    match local_addr {
        Some(SocketAddr::V4(addr))
            if IpAddr::V4(*addr.ip()) == primary_ip
                && !addr.ip().is_unspecified()
                && !addr.ip().is_loopback() =>
        {
            SocketAddr::from((*addr.ip(), 0))
        }
        Some(SocketAddr::V6(addr))
            if IpAddr::V6(*addr.ip()) == primary_ip
                && !addr.ip().is_unspecified()
                && !addr.ip().is_loopback() =>
        {
            SocketAddr::from(std::net::SocketAddrV6::new(
                *addr.ip(),
                0,
                0,
                addr.scope_id(),
            ))
        }
        _ => SocketAddr::new(primary_ip, 0),
    }
}

fn push_unique_ip(addresses: &mut Vec<String>, ip: IpAddr) {
    if ip.is_unspecified() {
        return;
    }
    let address = ip.to_string();
    if !addresses.iter().any(|candidate| candidate == &address) {
        addresses.push(address);
    }
}

#[cfg(unix)]
fn local_non_loopback_interface_addresses() -> Vec<IpAddr> {
    let mut addrs: *mut libc::ifaddrs = std::ptr::null_mut();
    let mut ips = Vec::new();

    // SAFETY: getifaddrs initializes a linked list owned by libc. We only read
    // sockaddr fields while the list is alive and always release it with
    // freeifaddrs before returning.
    unsafe {
        if libc::getifaddrs(&mut addrs) != 0 {
            return ips;
        }

        let mut cursor = addrs;
        while !cursor.is_null() {
            let ifa = &*cursor;
            let flags = ifa.ifa_flags as libc::c_uint;
            let is_up = flags & libc::IFF_UP as libc::c_uint != 0;
            let is_loopback = flags & libc::IFF_LOOPBACK as libc::c_uint != 0;
            if !ifa.ifa_addr.is_null() && !ifa.ifa_netmask.is_null() && is_up && !is_loopback {
                let family = (*ifa.ifa_addr).sa_family as libc::c_int;
                match family {
                    libc::AF_INET => {
                        let sockaddr = &*(ifa.ifa_addr as *const libc::sockaddr_in);
                        let ip = IpAddr::V4(Ipv4Addr::from(sockaddr.sin_addr.s_addr.to_ne_bytes()));
                        if !ip.is_loopback() && !ip.is_unspecified() {
                            ips.push(ip);
                        }
                    }
                    libc::AF_INET6 => {
                        let sockaddr = &*(ifa.ifa_addr as *const libc::sockaddr_in6);
                        let ip = IpAddr::V6(std::net::Ipv6Addr::from(sockaddr.sin6_addr.s6_addr));
                        if !ip.is_loopback() && !ip.is_unspecified() {
                            ips.push(ip);
                        }
                    }
                    _ => {}
                }
            }
            cursor = ifa.ifa_next;
        }

        libc::freeifaddrs(addrs);
    }

    ips
}

#[cfg(not(unix))]
fn local_non_loopback_interface_addresses() -> Vec<IpAddr> {
    Vec::new()
}

async fn handle_connection(
    mut stream: TcpStream,
    peer: SocketAddr,
    config: AirplayConfig,
    state: AppState,
    pairing: Arc<PairingService>,
    audio_engine: AudioEngine,
    player: SharedPlayer,
) -> anyhow::Result<()> {
    debug!(%peer, "RTSP connection opened");
    let mut buf = Vec::with_capacity(8192);
    let mut encrypted_buf = Vec::with_capacity(8192);
    let mut session = RtspSession::default();
    session.local_addr = stream.local_addr().ok();
    session.peer_addr = Some(peer);
    loop {
        let mut chunk = [0u8; 4096];
        let read = stream.read(&mut chunk).await?;
        if read == 0 {
            debug!(%peer, "RTSP connection closed by client (read EOF)");
            return Ok(());
        }
        trace!(%peer, bytes_read = read, "RTSP raw read");
        if let Some(ref mut cipher) = session.control_cipher {
            encrypted_buf.extend_from_slice(&chunk[..read]);
            match cipher.decrypt_blocks(&encrypted_buf) {
                Ok((plaintext, consumed)) => {
                    trace!(
                        %peer,
                        encrypted_read = read,
                        encrypted_pending = encrypted_buf.len(),
                        encrypted_consumed = consumed,
                        plaintext_len = plaintext.len(),
                        "RTSP control stream decrypted"
                    );
                    if consumed > 0 {
                        encrypted_buf.drain(..consumed);
                    }
                    if !plaintext.is_empty() {
                        buf.extend_from_slice(&plaintext);
                    }
                }
                Err(e) => {
                    warn!(
                        %peer,
                        %e,
                        encrypted_pending = encrypted_buf.len(),
                        "RTSP control stream decryption failed"
                    );
                    return Err(e);
                }
            }
        } else {
            buf.extend_from_slice(&chunk[..read]);
        }
        while let Some((request, consumed)) = parse_request(&buf) {
            log_request(peer, &request);

            // If this request arrived after the control stream cipher was active,
            // the entire RTSP response must be encrypted too. Capture this before
            // routing, because /pair-setup M3 activates the cipher for later
            // requests while its own M4 response is still plaintext.
            let encrypt_response = session.control_cipher.is_some();

            let response = route_request(
                &config,
                &state,
                &pairing,
                &mut session,
                &request,
                &audio_engine,
                &player,
            );

            log_response(peer, &request, &response);
            let mut wire = response.to_bytes();
            if encrypt_response && let Some(ref mut cipher) = session.control_cipher {
                match cipher.encrypt_blocks(&wire) {
                    Ok(encrypted) => {
                        trace!(
                            %peer,
                            plaintext_len = wire.len(),
                            encrypted_len = encrypted.len(),
                            "RTSP control stream encrypted"
                        );
                        wire = encrypted;
                    }
                    Err(e) => {
                        warn!(%peer, %e, "RTSP control stream encryption failed");
                        return Err(e);
                    }
                }
            }
            trace!(%peer, wire_len = wire.len(), wire = %String::from_utf8_lossy(&wire), encrypted = encrypt_response, "RTSP response wire");
            stream.write_all(&wire).await?;
            buf.drain(..consumed);
        }
        // If we have residual data but no complete message, log at trace
        if !buf.is_empty() {
            trace!(%peer, pending = buf.len(), pending_hex = ?buf, "RTSP incomplete frame waiting for more data");
        }
        if !encrypted_buf.is_empty() {
            trace!(%peer, encrypted_pending = encrypted_buf.len(), encrypted_pending_hex = ?encrypted_buf, "RTSP encrypted frame waiting for more data");
        }
    }
}
fn route_request(
    config: &AirplayConfig,
    state: &AppState,
    pairing: &PairingService,
    session: &mut RtspSession,
    request: &RtspRequest,
    audio_engine: &AudioEngine,
    player: &SharedPlayer,
) -> RtspResponse {
    if let Some(client_name) = request.headers.get("X-Apple-Client-Name") {
        state.set_client_name(client_name.clone());
    }

    // Log any request body at debug level for non-OPTIONS methods
    if request.method != "OPTIONS" && request.method != "GET_PARAMETER" {
        debug!(
            method = %request.method,
            uri = %request.uri,
            content_type = request.headers.get("Content-Type").map(|s| s.as_str()).unwrap_or(""),
            body_len = request.body.len(),
            body_hex = %body_preview(&request.body),
            "RTSP handler"
        );
    }

    match (request.method.as_str(), request.uri.as_str()) {
        ("OPTIONS", _) => {
            let public = if config.airplay2_enabled {
                "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, FLUSHBUFFERED, TEARDOWN, OPTIONS, POST, GET, PUT, SETPEERS"
            } else {
                "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER"
            };
            response(200, "OK")
                .header("Public", public)
                .with_cseq(request)
        }
        ("GET", "/info") | ("GET", "info") => response(200, "OK")
            .header("Content-Type", "application/x-apple-binary-plist")
            .body(get_info_body(config, session.peer_addr))
            .with_cseq(request),
        ("ANNOUNCE", _) => {
            info!("AP1 ANNOUNCE received ({} bytes body)", request.body.len());
            let sdp = String::from_utf8_lossy(&request.body);
            let parsed = parse_sdp(&sdp);
            state.set_source_format(parsed.source_format_description());
            let params = parsed.classic_params();

            if let Some(encrypted_key) = &params.rsaaeskey {
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
                state
                    .alac_magic_cookie
                    .write()
                    .clone_from(&Some(asc.clone()));
            }
            if let Some(rate) = params.alac_sample_rate {
                state.alac_sample_rate.write().clone_from(&Some(rate));
            }
            if let Some(fpp) = params.frames_per_packet {
                state.frames_per_packet.write().clone_from(&Some(fpp));
            }
            state.alac_channels.write().clone_from(&Some(2));

            response(200, "OK").with_cseq(request)
        }
        ("POST", "/pair-setup") => {
            let reply = pairing.handle(&mut session.pairing, PairingEndpoint::Setup, &request.body);
            // After pair-setup M4, activate control cipher if SRP session key is available
            if session.control_cipher.is_none()
                && let Some(sk) = session.pairing.session_key()
            {
                session.control_cipher = Some(PairCipher::control_for_server(sk));
                session.event_cipher = Some(PairCipher::events_for_server(sk));
                info!("AP2 control cipher activated from pair-setup session key");
            }
            info!(
                status = reply.status_code,
                body_len = reply.body.len(),
                has_shared_secret = session.pairing.shared_secret().is_some(),
                "pair-setup step"
            );
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
            let reply =
                pairing.handle(&mut session.pairing, PairingEndpoint::Remove, &request.body);
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
            let reply =
                pairing.handle(&mut session.pairing, PairingEndpoint::Verify, &request.body);
            if session.control_cipher.is_none()
                && let Some(shared_secret) = session.pairing.shared_secret()
            {
                let ss: &[u8] = shared_secret;
                session.control_cipher = Some(PairCipher::control_for_server(ss));
                session.event_cipher = Some(PairCipher::events_for_server(ss));
                info!("AP2 control and event ciphers activated");
            } else {
                info!(
                    status = reply.status_code,
                    has_cipher = session.control_cipher.is_some(),
                    has_shared = session.pairing.shared_secret().is_some(),
                    "pair-verify"
                );
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
        ("POST", "/command") => {
            // Command endpoint receives encrypted plist commands
            apply_ap2_command(state, request);
            info!("received /command ({} bytes)", request.body.len());
            response(200, "OK")
                .header("Content-Type", "application/octet-stream")
                .body(Vec::new())
                .with_cseq(request)
        }
        ("POST", "/feedback") => {
            // Feedback endpoint for AP2 event/status updates
            info!("received /feedback ({} bytes)", request.body.len());
            response(200, "OK").with_cseq(request)
        }
        ("POST", "/audioMode") => {
            apply_audio_mode(state, request);
            response(200, "OK").with_cseq(request)
        }
        ("POST", "/configure") => response(200, "OK").with_cseq(request),
        ("SETPEERS", _) => {
            state.set_diagnostic("ap2_setpeers_len", request.body.len().to_string());
            response(200, "OK").with_cseq(request)
        }
        ("SETPEERSX", _) => {
            state.set_diagnostic("ap2_setpeersx_len", request.body.len().to_string());
            response(200, "OK").with_cseq(request)
        }
        ("SETRATEANCHORTI", _) | ("SETRATEANCHORTIME", _) => {
            apply_setrateanchortime(state, player, request);
            response(200, "OK").with_cseq(request)
        }
        ("GET_PARAMETER", _) => response(200, "OK").with_cseq(request),
        ("SET_PARAMETER", _) => {
            apply_set_parameter(state, request);
            response(200, "OK").with_cseq(request)
        }
        ("SETUP", _) => {
            session.session_id = Some("1".to_string());
            state.set_active(true);

            // Detect AP2 SETUP from Content-Type
            let is_ap2 = request
                .headers
                .get("Content-Type")
                .map(|ct| ct.contains("application/x-apple-binary-plist"))
                .unwrap_or(false);

            if is_ap2 && config.airplay2_enabled {
                handle_ap2_setup(config, state, session, request, audio_engine).with_cseq(request)
            } else if is_ap2 {
                // AP2 plist but AP2 is disabled - respond with error
                warn!("AP2 SETUP received but airplay2_enabled is false");
                response(501, "Not Implemented").with_cseq(request)
            } else {
                // Classic AP1 SETUP
                info!(
                    "AP1 SETUP — requesting audio on ports {} {} {}",
                    config.audio_port, config.control_port, config.timing_port
                );
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
        }
        ("RECORD", _) => {
            if session.ap2_category != Ap2ConnectionCategory::Unknown
                || session.control_cipher.is_some()
                || session.ap2_timing_protocol.is_some()
            {
                info!(
                    category = ?session.ap2_category,
                    timing_protocol = session.ap2_timing_protocol.as_deref().unwrap_or(""),
                    "AP2 RECORD"
                );
                state.set_diagnostic("ap2_phase", "record");
                return response(200, "OK")
                    .header("Audio-Latency", "0")
                    .with_cseq(request);
            }

            let latency = request
                .headers
                .get("X-Apple-Latency")
                .and_then(|v| v.parse::<u32>().ok())
                .unwrap_or(11025);
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
            apply_flushbuffered(state, player, request);
            response(200, "OK").with_cseq(request)
        }
        ("TEARDOWN", _) => {
            player.stop();
            state.set_active(false);
            state.set_player_state(PlayerState::Stopped);
            *state.session_key.write() = None;
            session.abort_ap2_listeners();
            info!("TEARDOWN");
            response(200, "OK").with_cseq(request)
        }
        _ => {
            warn!(
                method = %request.method,
                uri = %request.uri,
                "unhandled RTSP method — responding 404"
            );
            response(404, "Not Found").with_cseq(request)
        }
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

/// Log the raw wire bytes of a request (called from handle_connection at trace level)
fn log_raw_request(peer: SocketAddr, raw: &[u8], consumed: usize) {
    if consumed > 0 {
        trace!(
            %peer,
            consumed,
            raw = %std::str::from_utf8(&raw[..consumed]).unwrap_or("<non-utf8>"),
            "RTSP raw request consumed"
        );
    }
}

fn log_response(peer: SocketAddr, request: &RtspRequest, response: &RtspResponse) {
    debug!(
        %peer,
        method = %request.method,
        uri = %request.uri,
        cseq = request.headers.get("CSeq").map(String::as_str).unwrap_or(""),
        status = response.code,
        reason = response.reason,
        content_length = response.body.len(),
        response_headers = ?response.headers,
        "RTSP response"
    );
    trace!(
        %peer,
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

fn plist_xml_preview(value: &plist::Value) -> String {
    let mut out = Vec::new();
    if plist::to_writer_xml(&mut out, value).is_err() {
        return "<plist serialization failed>".to_string();
    }
    String::from_utf8_lossy(&out).replace('\n', "")
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

/// AP2 stream types
#[derive(Clone, Copy, Debug)]
enum Ap2StreamType {
    RealtimeAudio = 96,
    BufferedAudio = 103,
    DataStream = 130,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum Ap2ConnectionCategory {
    #[default]
    Unknown,
    Ptp,
    RemoteControl,
}

#[derive(Default)]
struct RtspSession {
    peer_addr: Option<SocketAddr>,
    local_addr: Option<SocketAddr>,
    receiver_ip_override: Option<IpAddr>,
    session_id: Option<String>,
    pairing: PairingSession,
    control_cipher: Option<PairCipher>,
    event_cipher: Option<PairCipher>,
    data_cipher: Option<PairCipher>,
    event_listener: Option<JoinHandle<()>>,
    ap2_control_listener: Option<JoinHandle<()>>,
    buffered_audio_listener: Option<JoinHandle<()>>,
    realtime_audio_listener: Option<JoinHandle<()>>,
    data_listener: Option<JoinHandle<()>>,
    event_port: Option<u16>,
    ap2_control_port: Option<u16>,
    buffered_audio_port: Option<u16>,
    realtime_audio_port: Option<u16>,
    data_port: Option<u16>,
    ap2_category: Ap2ConnectionCategory,
    ap2_timing_protocol: Option<String>,
    ap2_streams: Vec<Ap2StreamType>,
    ap2_group_uuid: Option<String>,
    group_contains_group_leader: Option<bool>,
    dacp_id: Option<String>,
    active_remote: Option<String>,
    session_key: Option<Vec<u8>>,
}

impl RtspSession {
    fn receiver_ip(&self) -> IpAddr {
        self.receiver_ip_override
            .unwrap_or_else(|| receiver_primary_ip(self.local_addr.map(|addr| addr.ip()), None))
    }

    fn receiver_bind_addr(&self) -> SocketAddr {
        event_bind_addr(self.local_addr, self.receiver_ip())
    }

    fn abort_ap2_listeners(&mut self) {
        for handle in [
            self.event_listener.take(),
            self.ap2_control_listener.take(),
            self.buffered_audio_listener.take(),
            self.realtime_audio_listener.take(),
            self.data_listener.take(),
        ]
        .into_iter()
        .flatten()
        {
            handle.abort();
        }
    }

    fn ensure_ap2_control_socket(&mut self) -> anyhow::Result<u16> {
        if let Some(port) = self.ap2_control_port {
            return Ok(port);
        }
        let bind = self.receiver_bind_addr();
        let (port, handle) = spawn_udp_drain_socket(bind, "ap2-control")?;
        self.ap2_control_port = Some(port);
        self.ap2_control_listener = Some(handle);
        Ok(port)
    }

    fn ensure_buffered_audio_listener(
        &mut self,
        state: &AppState,
        audio_engine: &AudioEngine,
    ) -> anyhow::Result<u16> {
        if let Some(port) = self.buffered_audio_port {
            return Ok(port);
        }
        let bind = self.receiver_bind_addr();
        let std_socket = match bind {
            SocketAddr::V4(_) => Socket::new(Domain::IPV4, Type::STREAM, Some(Protocol::TCP))?,
            SocketAddr::V6(_) => {
                let socket = Socket::new(Domain::IPV6, Type::STREAM, Some(Protocol::TCP))?;
                socket.set_only_v6(false)?;
                socket
            }
        };
        std_socket.set_reuse_address(true)?;
        std_socket
            .bind(&bind.into())
            .with_context(|| format!("failed to bind buffered audio TCP on {bind}"))?;
        std_socket.listen(128)?;
        std_socket.set_nonblocking(true)?;
        let std_listener: std::net::TcpListener = std_socket.into();
        let port = std_listener.local_addr()?.port();
        let listener = TcpListener::from_std(std_listener)?;
        let handle = crate::airplay::buffered_audio::spawn_buffered_accept_loop(
            listener,
            state.clone(),
            audio_engine.clone(),
        );
        self.buffered_audio_port = Some(port);
        self.buffered_audio_listener = Some(handle);
        Ok(port)
    }

    fn ensure_realtime_audio_listener(&mut self) -> anyhow::Result<u16> {
        if let Some(port) = self.realtime_audio_port {
            return Ok(port);
        }
        let bind = self.receiver_bind_addr();
        let (port, handle) = spawn_udp_drain_socket(bind, "ap2-realtime-audio")?;
        self.realtime_audio_port = Some(port);
        self.realtime_audio_listener = Some(handle);
        Ok(port)
    }

    fn ensure_data_listener(&mut self) -> anyhow::Result<u16> {
        if let Some(port) = self.data_port {
            return Ok(port);
        }
        let bind = self.receiver_bind_addr();
        let (port, handle) = spawn_tcp_drain_listener(bind, "ap2-data")?;
        self.data_port = Some(port);
        self.data_listener = Some(handle);
        Ok(port)
    }
}

impl Drop for RtspSession {
    fn drop(&mut self) {
        self.abort_ap2_listeners();
    }
}

/// Handle an AirPlay 2 SETUP with binary plist body.
fn handle_ap2_setup(
    config: &AirplayConfig,
    state: &AppState,
    session: &mut RtspSession,
    request: &RtspRequest,
    audio_engine: &AudioEngine,
) -> RtspResponse {
    let plist_body = &request.body;
    let setup = match plist::from_bytes::<plist::Dictionary>(plist_body) {
        Ok(dict) => dict,
        Err(e) => {
            warn!(%e, "failed to parse AP2 SETUP plist");
            return response(400, "Bad Request");
        }
    };
    debug!(
        keys = %setup.keys().cloned().collect::<Vec<_>>().join(","),
        plist = %plist_xml_preview(&plist::Value::Dictionary(setup.clone())),
        "AP2 SETUP plist parsed"
    );

    // Check for streams array
    if let Some(plist::Value::Array(streams)) = setup.get("streams") {
        state.set_diagnostic("ap2_phase", "stream-setup");
        if let Some(plist::Value::String(active_remote)) = setup.get("activeRemote") {
            session.active_remote = Some(active_remote.clone());
            state.set_diagnostic("active_remote", active_remote.clone());
        }
        if let Some(plist::Value::String(dacp_id)) = setup.get("dacpID") {
            session.dacp_id = Some(dacp_id.clone());
            state.set_diagnostic("dacp_id", dacp_id.clone());
        }
        let control_port = match session.ensure_ap2_control_socket() {
            Ok(port) => {
                state.set_diagnostic("ap2_control_port", port.to_string());
                port
            }
            Err(e) => {
                warn!(%e, "failed to open AP2 control UDP socket");
                return response(503, "Service Unavailable");
            }
        };
        let mut response_dict = plist::Dictionary::new();
        let mut response_streams: Vec<plist::Value> = Vec::new();
        for stream_val in streams {
            let plist::Value::Dictionary(stream) = stream_val else {
                continue;
            };
            let type_val = stream
                .get("type")
                .and_then(plist_uint)
                .and_then(|v| u32::try_from(v).ok())
                .unwrap_or(0);
            let mut stream_dict = plist::Dictionary::new();
            match type_val {
                96 => {
                    session.ap2_streams.push(Ap2StreamType::RealtimeAudio);
                    let data_port = match session.ensure_realtime_audio_listener() {
                        Ok(port) => port,
                        Err(e) => {
                            warn!(%e, "failed to open AP2 realtime audio UDP socket");
                            return response(503, "Service Unavailable");
                        }
                    };
                    stream_dict.insert("type".to_string(), plist_uint_value(96u64));
                    stream_dict.insert("dataPort".to_string(), plist_uint_value(data_port));
                    stream_dict.insert("controlPort".to_string(), plist_uint_value(control_port));
                    state.set_active(true);
                    state.set_player_state(PlayerState::Playing);
                    state.set_diagnostic("ap2_stream_type", "realtime");
                    state.set_diagnostic("ap2_realtime_audio_port", data_port.to_string());
                    info!(
                        data_port,
                        control_port, "AP2 realtime audio stream requested"
                    );
                }
                103 => {
                    session.ap2_streams.push(Ap2StreamType::BufferedAudio);
                    let data_port =
                        match session.ensure_buffered_audio_listener(state, audio_engine) {
                            Ok(port) => port,
                            Err(e) => {
                                warn!(%e, "failed to open AP2 buffered audio TCP socket");
                                return response(503, "Service Unavailable");
                            }
                        };
                    if let Some(plist::Value::Data(shk)) = stream.get("shk")
                        && shk.len() >= 16
                    {
                        session.session_key = Some(shk.clone());
                        let mut key = [0u8; 16];
                        key.copy_from_slice(&shk[..16]);
                        *state.session_key.write() = Some(key);
                        state.set_diagnostic("ap2_shk_len", shk.len().to_string());
                    }
                    if let Some(ct) = stream.get("ct").and_then(plist_uint) {
                        state.set_diagnostic("ap2_compression_type", ct.to_string());
                    }
                    if let Some(sr) = stream.get("sr").and_then(plist_uint) {
                        state.alac_sample_rate.write().clone_from(&Some(sr as u32));
                    }
                    if let Some(spf) = stream.get("spf").and_then(plist_uint) {
                        state
                            .frames_per_packet
                            .write()
                            .clone_from(&Some(spf as u32));
                    }
                    stream_dict.insert("type".to_string(), plist_uint_value(103u64));
                    stream_dict.insert("dataPort".to_string(), plist_uint_value(data_port));
                    stream_dict.insert("controlPort".to_string(), plist_uint_value(control_port));
                    stream_dict.insert(
                        "audioBufferSize".to_string(),
                        plist_uint_value(8 * 1024 * 1024u64),
                    );
                    state.set_active(true);
                    state.set_player_state(PlayerState::Playing);
                    state.set_diagnostic("ap2_stream_type", "buffered");
                    state.set_diagnostic("ap2_buffered_audio_port", data_port.to_string());
                    info!(
                        data_port,
                        control_port, "AP2 buffered audio stream requested"
                    );
                }
                130 => {
                    session.ap2_streams.push(Ap2StreamType::DataStream);
                    let data_port = match session.ensure_data_listener() {
                        Ok(port) => port,
                        Err(e) => {
                            warn!(%e, "failed to open AP2 data TCP socket");
                            return response(503, "Service Unavailable");
                        }
                    };
                    if let Some(seed) = stream.get("seed").and_then(plist_uint)
                        && let Some(sk) = session.pairing.session_key()
                    {
                        session.data_cipher =
                            Some(PairCipher::data_for_server(sk, seed.to_string().as_str()));
                        state.set_diagnostic("ap2_data_seed", seed.to_string());
                    }
                    stream_dict.insert("type".to_string(), plist_uint_value(130u64));
                    stream_dict.insert("streamID".to_string(), plist_uint_value(1u64));
                    stream_dict.insert("dataPort".to_string(), plist_uint_value(data_port));
                    state.set_diagnostic("ap2_data_port", data_port.to_string());
                    info!("AP2 data/event stream requested");
                }
                other => {
                    warn!(type = other, "unknown AP2 stream type");
                    stream_dict.insert("type".to_string(), plist_uint_value(other));
                    stream_dict.insert("status".to_string(), plist_uint_value(1u64));
                }
            }
            response_streams.push(plist::Value::Dictionary(stream_dict));
        }
        response_dict.insert("streams".to_string(), plist::Value::Array(response_streams));
        debug!(
            plist = %plist_xml_preview(&plist::Value::Dictionary(response_dict.clone())),
            "AP2 stream SETUP response plist"
        );

        let mut body = Vec::new();
        if plist::to_writer_binary(&mut body, &plist::Value::Dictionary(response_dict)).is_ok() {
            return response(200, "OK")
                .header("Content-Type", "application/x-apple-binary-plist")
                .body(body);
        }
        return response(200, "OK").with_cseq(request);
    }

    // Initial SETUP (no streams) - handle timing protocol and event channel.
    if let Some(tp) = setup.get("timingProtocol").and_then(|v| v.as_string()) {
        info!(protocol = %tp, "AP2 initial SETUP with timing protocol");
        state.set_diagnostic("ap2_timing_protocol", tp.to_string());
        session.ap2_timing_protocol = Some(tp.to_string());

        // For PTP, we need to signal the PTP service
        if tp == "PTP" {
            session.ap2_category = Ap2ConnectionCategory::Ptp;
            state.set_diagnostic("ap2_phase", "initial-ptp-setup");
            // Check for groupUUID
            if let Some(plist::Value::String(gid)) = setup.get("groupUUID") {
                session.ap2_group_uuid = Some(gid.clone());
                state.set_diagnostic("ap2_group_uuid", gid.clone());
            }
            if let Some(group_leader) = setup.get("groupContainsGroupLeader").and_then(plist_bool) {
                session.group_contains_group_leader = Some(group_leader);
                state.set_diagnostic("ap2_group_contains_group_leader", group_leader.to_string());
            }

            let mut response_dict = plist::Dictionary::new();
            let local_ip = receiver_primary_ip(
                session.local_addr.map(|addr| addr.ip()),
                config.ap2_bind_ip.as_deref(),
            );
            session.receiver_ip_override = Some(local_ip);
            let local_ip_string = local_ip.to_string();
            let clock_id = ptp::local_clock_identity();
            let timing_addresses = receiver_timing_addresses(local_ip)
                .into_iter()
                .map(plist::Value::String)
                .collect::<Vec<_>>();
            debug!(
                addresses = ?timing_addresses
                    .iter()
                    .filter_map(plist::Value::as_string)
                    .collect::<Vec<_>>(),
                id = %local_ip_string,
                primary_ip = %local_ip_string,
                clock_id = format_args!("{clock_id:016x}"),
                "AP2 SETUP timing peer response"
            );

            let mut timing_peer_info = plist::Dictionary::new();
            timing_peer_info.insert(
                "Addresses".to_string(),
                plist::Value::Array(timing_addresses),
            );
            timing_peer_info.insert("ID".to_string(), plist::Value::String(local_ip_string));
            timing_peer_info.insert(
                "ClockID".to_string(),
                plist::Value::Integer(plist::Integer::from(clock_id)),
            );
            timing_peer_info.insert(
                "DeviceType".to_string(),
                plist::Value::Integer(plist::Integer::from(0u64)),
            );
            timing_peer_info.insert(
                "SupportsClockPortMatchingOverride".to_string(),
                plist::Value::Boolean(true),
            );
            response_dict.insert(
                "timingPeerInfo".to_string(),
                plist::Value::Dictionary(timing_peer_info),
            );

            if session.event_port.is_none() {
                let event_bind = event_bind_addr(session.local_addr, local_ip);
                match session.pairing.session_key() {
                    Some(shared_secret) => {
                        match spawn_ap2_event_listener(
                            event_bind,
                            config.clone(),
                            session.peer_addr,
                            shared_secret.to_vec(),
                            session.ap2_group_uuid.clone(),
                            session.group_contains_group_leader,
                        ) {
                            Ok((port, handle)) => {
                                session.event_port = Some(port);
                                session.event_listener = Some(handle);
                                info!(port, bind = %event_bind, "AP2 event listener opened");
                            }
                            Err(e) => {
                                warn!(%e, "failed to open AP2 event listener");
                                return response(503, "Service Unavailable");
                            }
                        }
                    }
                    None => {
                        warn!("cannot open AP2 event listener before pair-setup session key");
                        return response(503, "Service Unavailable");
                    }
                }
            }
            if let Some(port) = session.event_port {
                response_dict.insert(
                    "eventPort".to_string(),
                    plist::Value::Integer(plist::Integer::from(port as u64)),
                );
            }
            response_dict.insert(
                "timingPort".to_string(),
                plist::Value::Integer(plist::Integer::from(0u64)),
            );
            debug!(
                plist = %plist_xml_preview(&plist::Value::Dictionary(response_dict.clone())),
                "AP2 initial SETUP response plist"
            );
            let mut body = Vec::new();
            if plist::to_writer_binary(&mut body, &plist::Value::Dictionary(response_dict)).is_ok()
            {
                return response(200, "OK")
                    .header("Content-Type", "application/x-apple-binary-plist")
                    .body(body);
            }
        }
    }

    response(200, "OK").with_cseq(request)
}

fn apply_set_parameter(state: &AppState, request: &RtspRequest) {
    let content_type = request
        .headers
        .get("Content-Type")
        .map(String::as_str)
        .unwrap_or_default();

    if content_type.contains("application/x-apple-binary-plist") {
        // AP2 metadata as binary plist
        if let Ok(dict) = plist::from_bytes::<plist::Dictionary>(&request.body) {
            if let Some(plist::Value::String(title)) = dict.get("title") {
                state.set_track_metadata(Some(title.clone()), None, None);
            }
            if let Some(plist::Value::String(artist)) = dict.get("artist") {
                state.set_track_metadata(None, Some(artist.clone()), None);
            }
            if let Some(plist::Value::String(album)) = dict.get("album") {
                state.set_track_metadata(None, None, Some(album.clone()));
            }
            if let Some(plist::Value::Data(artwork)) = dict.get("artwork") {
                state.set_diagnostic("artwork_size", artwork.len().to_string());
            }
            if let Some(plist::Value::Real(progress)) = dict.get("progress") {
                state.set_progress_ms((progress * 1000.0) as u64);
            }
            if let Some(plist::Value::Real(duration)) = dict.get("duration") {
                state.set_duration_ms((duration * 1000.0) as u64);
            }
            // DACP / remote control identifiers
            if let Some(plist::Value::String(dacp_id)) = dict.get("dacpID") {
                state.set_diagnostic("dacp_id", dacp_id.clone());
            }
            if let Some(plist::Value::String(active_remote)) = dict.get("activeRemote") {
                state.set_diagnostic("active_remote", active_remote.clone());
            }
        }
    } else if content_type.contains("text/parameters") {
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
            } else if let Some(value) = line.strip_prefix("Progress:") {
                // Format: "Progress: position/duration"
                if let Some(progress_str) = value.split('/').next() {
                    if let Ok(pos) = progress_str.trim().parse::<f64>() {
                        state.set_progress_ms((pos * 1000.0) as u64);
                    }
                }
            }
        }
        state.set_track_metadata(title, artist, album);
    }
}

fn apply_ap2_command(state: &AppState, request: &RtspRequest) {
    state.set_diagnostic("ap2_last_command_len", request.body.len().to_string());
    if let Ok(dict) = plist::from_bytes::<plist::Dictionary>(&request.body) {
        let keys = dict.keys().cloned().collect::<Vec<_>>().join(",");
        state.set_diagnostic("ap2_last_command_keys", keys);
        let command = dict
            .get("command")
            .and_then(plist::Value::as_string)
            .or_else(|| dict.get("type").and_then(plist::Value::as_string));
        if let Some(command) = command {
            state.set_diagnostic("ap2_last_command", command.to_string());
        }
        let params_keys = dict
            .get("params")
            .and_then(plist::Value::as_dictionary)
            .map(|params| params.keys().cloned().collect::<Vec<_>>().join(","))
            .unwrap_or_default();
        debug!(
            command = command.unwrap_or(""),
            top_keys = ?dict.keys().collect::<Vec<_>>(),
            params_keys,
            body_len = request.body.len(),
            "AP2 /command plist parsed"
        );
        debug_ap2_mr_supported_commands(&dict);
    }
}

fn debug_ap2_mr_supported_commands(dict: &plist::Dictionary) {
    let Some("updateMRSupportedCommands") = dict
        .get("type")
        .and_then(plist::Value::as_string)
        .or_else(|| dict.get("command").and_then(plist::Value::as_string))
    else {
        return;
    };
    let Some(plist::Value::Array(commands)) = dict
        .get("params")
        .and_then(plist::Value::as_dictionary)
        .and_then(|params| params.get("mrSupportedCommandsFromSender"))
    else {
        debug!("AP2 updateMRSupportedCommands missing mrSupportedCommandsFromSender array");
        return;
    };

    let summaries = commands
        .iter()
        .take(12)
        .enumerate()
        .map(|(idx, command)| match command {
            plist::Value::Data(data) => debug_ap2_embedded_command_summary(idx, data),
            other => format!("{idx}:{}", plist_value_kind(other)),
        })
        .collect::<Vec<_>>();
    debug!(
        count = commands.len(),
        first = ?summaries,
        "AP2 MR supported commands from sender"
    );
}

fn debug_ap2_embedded_command_summary(idx: usize, data: &[u8]) -> String {
    if let Ok(value) = plist::from_bytes::<plist::Value>(data) {
        if let Some(dict) = value.as_dictionary() {
            let command = dict
                .get("command")
                .and_then(plist::Value::as_string)
                .or_else(|| dict.get("type").and_then(plist::Value::as_string))
                .or_else(|| dict.get("name").and_then(plist::Value::as_string))
                .unwrap_or("");
            let enabled = dict
                .get("enabled")
                .and_then(plist_bool)
                .map(|v| v.to_string())
                .unwrap_or_else(|| "-".to_string());
            let keys = dict.keys().cloned().collect::<Vec<_>>().join("|");
            return format!("{idx}:plist command={command} enabled={enabled} keys={keys}");
        }
        return format!("{idx}:plist {}", plist_value_kind(&value));
    }
    format!("{idx}:data len={}", data.len())
}

fn apply_flushbuffered(state: &AppState, player: &SharedPlayer, request: &RtspRequest) {
    state.set_diagnostic("ap2_phase", "flushbuffered");
    if let Ok(dict) = plist::from_bytes::<plist::Dictionary>(&request.body) {
        for key in [
            "flushFromSeq",
            "flushFromTS",
            "flushUntilSeq",
            "flushUntilTS",
        ] {
            if let Some(value) = dict.get(key).and_then(plist_uint) {
                state.set_diagnostic(format!("ap2_{key}"), value.to_string());
            }
        }
    }
    player.flush();
    state.set_player_state(PlayerState::Paused);
}

fn apply_audio_mode(state: &AppState, request: &RtspRequest) {
    state.set_diagnostic("ap2_phase", "audio_mode");
    state.set_diagnostic("ap2_audio_mode_len", request.body.len().to_string());

    let Ok(dict) = plist::from_bytes::<plist::Dictionary>(&request.body) else {
        warn!("POST /audioMode missing or invalid plist body");
        return;
    };

    let keys = dict.keys().cloned().collect::<Vec<_>>().join(",");
    state.set_diagnostic("ap2_audio_mode_keys", keys.clone());
    if let Some(mode) = dict.get("audioMode").and_then(plist::Value::as_string) {
        state.set_diagnostic("ap2_audio_mode", mode.to_string());
        debug!(mode, keys, "AP2 /audioMode plist parsed");
    } else {
        debug!(keys, "AP2 /audioMode plist parsed without audioMode key");
    }
}

fn apply_setrateanchortime(state: &AppState, player: &SharedPlayer, request: &RtspRequest) {
    state.set_diagnostic("ap2_phase", "setrateanchortime");
    let Ok(dict) = plist::from_bytes::<plist::Dictionary>(&request.body) else {
        warn!("SETRATEANCHORTIME missing or invalid plist body");
        return;
    };

    if let Some(timeline_id) = dict.get("networkTimeTimelineID").and_then(plist_uint) {
        state.set_diagnostic("ap2_network_time_timeline_id", format!("{timeline_id:x}"));
    }
    if let Some(secs) = dict.get("networkTimeSecs").and_then(plist_uint) {
        state.set_diagnostic("ap2_network_time_secs", secs.to_string());
    }
    if let Some(frac) = dict.get("networkTimeFrac").and_then(plist_uint) {
        state.set_diagnostic("ap2_network_time_frac", frac.to_string());
    }
    if let Some(rtp_time) = dict.get("rtpTime").and_then(plist_uint) {
        state.set_diagnostic("ap2_anchor_rtp_time", rtp_time.to_string());
    }
    let rate = dict.get("rate").and_then(plist_uint).unwrap_or(0);
    state.set_diagnostic("ap2_rate", format!("{rate:#x}"));

    if rate & 1 != 0 {
        let sample_rate = state.alac_sample_rate.read().unwrap_or(44_100);
        player.set_sample_rate(sample_rate);
        player.start(0);
        state.set_player_state(PlayerState::Playing);
        state.set_diagnostic("ap2_play_enabled", "true");
    } else {
        player.flush();
        state.set_player_state(PlayerState::Paused);
        state.set_diagnostic("ap2_play_enabled", "false");
    }
}

fn plist_uint(value: &plist::Value) -> Option<u64> {
    match value {
        plist::Value::Integer(i) => i.as_unsigned(),
        _ => None,
    }
}

fn plist_bool(value: &plist::Value) -> Option<bool> {
    match value {
        plist::Value::Boolean(v) => Some(*v),
        _ => None,
    }
}

fn plist_value_kind(value: &plist::Value) -> &'static str {
    match value {
        plist::Value::Array(_) => "array",
        plist::Value::Dictionary(_) => "dict",
        plist::Value::Boolean(_) => "bool",
        plist::Value::Data(_) => "data",
        plist::Value::Date(_) => "date",
        plist::Value::Integer(_) => "int",
        plist::Value::Real(_) => "real",
        plist::Value::String(_) => "string",
        plist::Value::Uid(_) => "uid",
        _ => "unknown",
    }
}

fn debug_ap2_info_payload(message: &'static str, value: &plist::Value, group_uuid: Option<&str>) {
    let Some(dict) = value.as_dictionary() else {
        return;
    };
    let (audio_stream, buffer_stream) = dict
        .get("supportedFormats")
        .and_then(plist::Value::as_dictionary)
        .map(|formats| {
            (
                formats
                    .get("audioStream")
                    .and_then(plist_uint)
                    .unwrap_or_default(),
                formats
                    .get("bufferStream")
                    .and_then(plist_uint)
                    .unwrap_or_default(),
            )
        })
        .unwrap_or_default();
    let txt_selected = dict
        .get("txtAirPlay")
        .and_then(|value| match value {
            plist::Value::Data(data) => Some(txt_airplay_entries(data)),
            _ => None,
        })
        .unwrap_or_default()
        .into_iter()
        .filter(|entry| {
            entry.starts_with("features=")
                || entry.starts_with("fex=")
                || entry.starts_with("gid=")
                || entry.starts_with("gcgl=")
                || entry.starts_with("pgid=")
                || entry.starts_with("pgcgl=")
                || entry.starts_with("pi=")
                || entry.starts_with("psi=")
                || entry.starts_with("protovers=")
        })
        .collect::<Vec<_>>();
    debug!(
        keys = ?dict.keys().collect::<Vec<_>>(),
        group_uuid,
        audio_stream,
        audio_stream_hex = format_args!("{audio_stream:#x}"),
        buffer_stream,
        buffer_stream_hex = format_args!("{buffer_stream:#x}"),
        txt = ?txt_selected,
        body_len = dict.len(),
        message
    );
}

fn txt_airplay_entries(data: &[u8]) -> Vec<String> {
    let mut entries = Vec::new();
    let mut idx = 0;
    while idx < data.len() {
        let len = data[idx] as usize;
        idx += 1;
        if idx + len > data.len() {
            entries.push(format!("truncated(len={len})"));
            break;
        }
        entries.push(String::from_utf8_lossy(&data[idx..idx + len]).into_owned());
        idx += len;
    }
    entries
}

fn plist_uint_value(value: impl Into<u64>) -> plist::Value {
    plist::Value::Integer(plist::Integer::from(value.into()))
}

fn get_info_body(config: &AirplayConfig, peer_addr: Option<SocketAddr>) -> Vec<u8> {
    get_info_body_with_group(config, peer_addr, None, false)
}

fn get_info_body_with_group(
    config: &AirplayConfig,
    peer_addr: Option<SocketAddr>,
    group_uuid: Option<&str>,
    group_contains_group_leader: bool,
) -> Vec<u8> {
    use crate::airplay::crypto::accessory_public_key_for_device_id;
    use crate::airplay::txt_records::{AP2_FEATURES, AP2_STATUS_FLAGS, stable_uuid};
    let mut dict = Dictionary::new();
    dict.insert("vv".into(), Value::Integer(plist::Integer::from(2u64)));
    let mut playback_capabilities = Dictionary::new();
    playback_capabilities.insert("supportsInterstitials".into(), Value::Boolean(false));
    playback_capabilities.insert("supportsFPSSecureStop".into(), Value::Boolean(false));
    playback_capabilities.insert(
        "supportsUIForAudioOnlyContent".into(),
        Value::Boolean(false),
    );
    playback_capabilities.insert("canRecordScreenStream".into(), Value::Boolean(false));
    playback_capabilities.insert("keepAliveSendStatsAsBody".into(), Value::Boolean(false));
    playback_capabilities.insert("protocolVersion".into(), Value::String("1.1".to_string()));
    playback_capabilities.insert(
        "volumeControlType".into(),
        Value::Integer(plist::Integer::from(3u64)),
    );
    playback_capabilities.insert("screenDemoMode".into(), Value::Boolean(false));
    dict.insert(
        "playbackCapabilities".into(),
        Value::Dictionary(playback_capabilities),
    );
    dict.insert("deviceID".into(), Value::String(config.device_id.clone()));
    dict.insert(
        "features".into(),
        Value::Integer(plist::Integer::from(AP2_FEATURES)),
    );
    dict.insert(
        "featuresEx".into(),
        Value::String(features_ex(AP2_FEATURES)),
    );
    dict.insert(
        "statusFlags".into(),
        Value::Integer(plist::Integer::from(AP2_STATUS_FLAGS as u64)),
    );
    dict.insert("sourceVersion".into(), Value::String("366.0".to_string()));
    dict.insert("name".into(), Value::String("Shairport RS".to_string()));
    dict.insert("model".into(), Value::String("ShairportSync".to_string()));
    // Permanent receiver identity. The AP2 TXT data carries psi/protovers.
    let pi = stable_uuid("pi", &config.device_id).to_string();
    dict.insert("pi".into(), Value::String(pi));

    // Public key as raw 32-byte data
    let pk = accessory_public_key_for_device_id(&config.device_id);
    dict.insert("pk".into(), Value::Data(pk.to_vec()));
    if let Some(peer_addr) = peer_addr {
        dict.insert(
            "senderAddress".into(),
            Value::String(format!("{}:{}", peer_addr.ip(), peer_addr.port())),
        );
    }

    // Initial volume (0.0 = mute, 1.0 = full)
    dict.insert("initialVolume".into(), Value::Real(0.0));
    let mut supported_formats = Dictionary::new();
    let advertised_formats = advertised_ap2_formats(config.advertised_format_policy);
    supported_formats.insert(
        "audioStream".into(),
        Value::Integer(plist::Integer::from(advertised_formats.audio_stream)),
    );
    supported_formats.insert(
        "bufferStream".into(),
        Value::Integer(plist::Integer::from(advertised_formats.buffer_stream)),
    );
    dict.insert(
        "supportedFormats".into(),
        Value::Dictionary(supported_formats),
    );
    dict.insert(
        "receiverHDRCapability".into(),
        Value::String("4k60".to_string()),
    );

    // Generate txtAirPlay binary data (DNS-SD TXT format)
    let txt_data = build_txt_airplay_data(config, group_uuid, group_contains_group_leader);
    dict.insert("txtAirPlay".into(), Value::Data(txt_data));

    let mut out = Vec::new();
    plist::to_writer_binary(&mut out, &Value::Dictionary(dict))
        .expect("serializing in-memory plist should not fail");
    out
}

struct AdvertisedAp2Formats {
    audio_stream: u64,
    buffer_stream: u64,
}

fn advertised_ap2_formats(policy: AdvertisedFormatPolicy) -> AdvertisedAp2Formats {
    const AUDIO_STREAM_PARENT_MASK: u64 = 21_235_712;
    const BUFFER_ALAC_44100_S16_2: u64 = 0x0004_0000;
    const BUFFER_ALAC_48000_F24_2: u64 = 0x0020_0000;
    const BUFFER_AAC_44100_F24_2: u64 = 0x0040_0000;
    const BUFFER_AAC_48000_F24_2: u64 = 0x0080_0000;

    let mut buffer_stream = BUFFER_ALAC_44100_S16_2 | BUFFER_ALAC_48000_F24_2;
    if policy == AdvertisedFormatPolicy::AacIfAvailable {
        if aac_decoder_available() {
            buffer_stream |= BUFFER_AAC_44100_F24_2 | BUFFER_AAC_48000_F24_2;
        } else {
            debug!("not advertising AP2 AAC buffer formats because no AAC decoder is available");
        }
    }

    AdvertisedAp2Formats {
        audio_stream: AUDIO_STREAM_PARENT_MASK,
        buffer_stream,
    }
}

fn aac_decoder_available() -> bool {
    false
}

fn build_txt_airplay_data(
    config: &AirplayConfig,
    group_uuid: Option<&str>,
    group_contains_group_leader: bool,
) -> Vec<u8> {
    use crate::airplay::crypto::accessory_public_key_for_device_id;
    use crate::airplay::txt_records::stable_uuid;
    let features = crate::airplay::txt_records::AP2_FEATURES;
    let features_lo = (features & 0xffff_ffff) as u32;
    let features_hi = (features >> 32) as u32;
    let pk_raw = accessory_public_key_for_device_id(&config.device_id);
    let pk_hex = pk_raw
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect::<String>();
    let fex = features_ex(features);
    let pi = stable_uuid("pi", &config.device_id).to_string();
    let psi = stable_uuid("psi", &config.device_id).to_string();
    let gid = group_uuid.unwrap_or(&pi);
    let gcgl = u8::from(group_contains_group_leader);

    let entries: Vec<String> = vec![
        "acl=0".to_string(),
        "btaddr=00:00:00:00:00:00".to_string(),
        format!("deviceid={}", config.device_id),
        format!("fex={fex}"),
        format!("features=0x{features_lo:X},0x{features_hi:X}"),
        format!(
            "flags=0x{:x}",
            crate::airplay::txt_records::AP2_STATUS_FLAGS
        ),
        format!("gid={gid}"),
        "igl=0".to_string(),
        format!("gcgl={gcgl}"),
        format!("pgid={pi}"),
        format!("pgcgl={gcgl}"),
        "model=ShairportSync".to_string(),
        "protovers=1.1".to_string(),
        format!("pi={pi}"),
        format!("psi={psi}"),
        format!("pk={pk_hex}"),
        "srcvers=366.0".to_string(),
        "osvers=15.0".to_string(),
        "vv=2".to_string(),
    ];

    let mut out = Vec::new();
    for entry in &entries {
        let len = entry.len().min(255) as u8;
        out.push(len);
        out.extend_from_slice(entry.as_bytes());
    }
    out
}

fn features_ex(features: u64) -> String {
    use base64::Engine;
    let bytes = features.to_le_bytes();
    base64::engine::general_purpose::STANDARD_NO_PAD.encode(bytes)
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
        headers: vec![("Server".to_string(), "AirTunes/366.0".to_string())],
        body: Vec::new(),
    }
}

impl RtspResponse {
    fn header(mut self, name: impl Into<String>, value: impl Into<String>) -> Self {
        self.set_header(name.into(), value.into());
        self
    }

    fn body(mut self, body: Vec<u8>) -> Self {
        self.body = body;
        self
    }

    fn with_cseq(mut self, request: &RtspRequest) -> Self {
        if let Some(cseq) = request.headers.get("CSeq") {
            self.set_header("CSeq".to_string(), cseq.clone());
        }
        self
    }

    fn set_header(&mut self, name: String, value: String) {
        if let Some((_, existing)) = self
            .headers
            .iter_mut()
            .find(|(existing_name, _)| existing_name.eq_ignore_ascii_case(&name))
        {
            *existing = value;
        } else if name.eq_ignore_ascii_case("CSeq") {
            self.headers.insert(0, (name, value));
        } else {
            self.headers.push((name, value));
        }
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut out = format!("RTSP/1.0 {} {}\r\n", self.code, self.reason).into_bytes();
        for (name, value) in &self.headers {
            out.extend_from_slice(format!("{name}: {value}\r\n").as_bytes());
        }
        out.extend_from_slice(format!("Content-Length: {}\r\n", self.body.len()).as_bytes());
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

    #[tokio::test]
    async fn ap2_buffered_setup_returns_ports_and_stores_session_key() {
        let mut config = crate::config::Config::default();
        config.airplay.airplay2_enabled = true;
        config.airplay.audio_port = 6000;
        config.airplay.control_port = 6001;
        let state = AppState::new(config.clone());
        let mut session = RtspSession::default();

        let mut stream = plist::Dictionary::new();
        stream.insert("type".to_string(), plist_uint_value(103u64));
        stream.insert("shk".to_string(), plist::Value::Data(vec![7u8; 16]));
        stream.insert("sr".to_string(), plist_uint_value(44_100u64));
        stream.insert("spf".to_string(), plist_uint_value(352u64));
        let mut setup = plist::Dictionary::new();
        setup.insert(
            "streams".to_string(),
            plist::Value::Array(vec![plist::Value::Dictionary(stream)]),
        );
        let mut body = Vec::new();
        plist::to_writer_binary(&mut body, &plist::Value::Dictionary(setup)).unwrap();
        let request = RtspRequest {
            method: "SETUP".to_string(),
            uri: "rtsp://example/session".to_string(),
            version: "RTSP/1.0".to_string(),
            headers: BTreeMap::new(),
            body,
        };

        let audio_engine = AudioEngine::new(1024);
        let response = handle_ap2_setup(
            &config.airplay,
            &state,
            &mut session,
            &request,
            &audio_engine,
        );
        let parsed: plist::Dictionary = plist::from_bytes(&response.body).unwrap();
        let streams = parsed
            .get("streams")
            .and_then(plist::Value::as_array)
            .unwrap();
        let first = streams[0].as_dictionary().unwrap();

        assert_eq!(first.get("type").and_then(plist_uint), Some(103));
        let data_port = first.get("dataPort").and_then(plist_uint).unwrap();
        let control_port = first.get("controlPort").and_then(plist_uint).unwrap();
        assert!(data_port > 0);
        assert!(control_port > 0);
        assert_ne!(data_port, control_port);
        assert_eq!(*state.session_key.read(), Some([7u8; 16]));
    }

    #[test]
    fn ap2_alac_only_does_not_advertise_aac_buffer_formats() {
        let formats = advertised_ap2_formats(AdvertisedFormatPolicy::AlacOnly);

        assert_ne!(formats.buffer_stream & 0x0004_0000, 0);
        assert_ne!(formats.buffer_stream & 0x0020_0000, 0);
        assert_eq!(formats.buffer_stream & 0x0040_0000, 0);
        assert_eq!(formats.buffer_stream & 0x0080_0000, 0);
    }
}
