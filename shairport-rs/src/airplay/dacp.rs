use std::{
    net::{IpAddr, SocketAddr},
    sync::Arc,
    time::Duration,
};

use mdns_sd::{ResolvedService, ServiceDaemon, ServiceEvent};
use tokio::{
    io::AsyncWriteExt,
    net::TcpStream,
    time::{Instant, timeout},
};
use tracing::{debug, warn};

use crate::state::AppState;

const DACP_SERVICE_TYPE: &str = "_dacp._tcp.local.";
const DACP_DISCOVERY_TIMEOUT: Duration = Duration::from_millis(1_000);
const DACP_IO_TIMEOUT: Duration = Duration::from_millis(300);

#[derive(Clone)]
pub struct DacpController {
    state: AppState,
    daemon: Arc<Option<ServiceDaemon>>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DacpSendResult {
    pub command: &'static str,
    pub endpoint: SocketAddr,
    pub status_line: String,
}

#[derive(Debug, thiserror::Error)]
pub enum DacpError {
    #[error("unsupported remote command")]
    UnsupportedCommand,
    #[error("remote control unavailable: missing dacpID or activeRemote")]
    MissingSession,
    #[error("mDNS DACP discovery unavailable: {0}")]
    MdnsUnavailable(String),
    #[error("DACP service not found for dacpID {0}")]
    DiscoveryTimeout(String),
    #[error("DACP request timed out")]
    Timeout,
    #[error("DACP request failed: {0}")]
    Io(#[from] std::io::Error),
    #[error("DACP sender returned {0}")]
    HttpStatus(String),
}

impl DacpController {
    pub fn new(state: AppState) -> Self {
        let daemon = match ServiceDaemon::new() {
            Ok(daemon) => Some(daemon),
            Err(err) => {
                let message = err.to_string();
                warn!(%message, "DACP mDNS browser not started");
                state.set_dacp_error(message);
                None
            }
        };
        Self {
            state,
            daemon: Arc::new(daemon),
        }
    }

    #[cfg(test)]
    pub fn disabled(state: AppState) -> Self {
        Self {
            state,
            daemon: Arc::new(None),
        }
    }

    pub fn update_session(
        &self,
        dacp_id: Option<String>,
        active_remote: Option<String>,
        peer_addr: Option<SocketAddr>,
    ) {
        self.state
            .set_remote_control_session(dacp_id, active_remote, peer_addr);
        self.prewarm_endpoint();
    }

    pub fn clear_session(&self) {
        self.state.clear_remote_control_session();
    }

    pub async fn send_alias(&self, alias: &str) -> Result<DacpSendResult, DacpError> {
        let command = dacp_command_for_alias(alias).ok_or(DacpError::UnsupportedCommand)?;
        self.send(command).await
    }

    pub async fn send(&self, command: &'static str) -> Result<DacpSendResult, DacpError> {
        let snapshot = self.state.snapshot().remote_control;
        let dacp_id = snapshot.dacp_id.ok_or(DacpError::MissingSession)?;
        let active_remote = snapshot.active_remote.ok_or(DacpError::MissingSession)?;
        let peer_addr = snapshot
            .peer_addr
            .as_deref()
            .and_then(|value| value.parse::<SocketAddr>().ok());

        let endpoint = match snapshot
            .dacp_addr
            .as_deref()
            .and_then(|value| value.parse::<SocketAddr>().ok())
        {
            Some(endpoint) => endpoint,
            None => self.resolve_endpoint(&dacp_id, peer_addr).await?,
        };

        match self
            .send_to_endpoint(command, &active_remote, endpoint)
            .await
        {
            Ok(result) => Ok(result),
            Err(err) => {
                self.state.set_dacp_endpoint(None);
                if matches!(err, DacpError::Io(_) | DacpError::Timeout) {
                    let endpoint = self.resolve_endpoint(&dacp_id, peer_addr).await?;
                    self.send_to_endpoint(command, &active_remote, endpoint)
                        .await
                } else {
                    Err(err)
                }
            }
        }
    }

    async fn send_to_endpoint(
        &self,
        command: &'static str,
        active_remote: &str,
        endpoint: SocketAddr,
    ) -> Result<DacpSendResult, DacpError> {
        let request = dacp_http_request(command, &endpoint.to_string(), active_remote);
        let mut stream = timeout(DACP_IO_TIMEOUT, TcpStream::connect(endpoint))
            .await
            .map_err(|_| DacpError::Timeout)??;
        timeout(DACP_IO_TIMEOUT, stream.write_all(request.as_bytes()))
            .await
            .map_err(|_| DacpError::Timeout)??;
        let status_line = "request written".to_string();

        self.state
            .set_dacp_status(format!("{command} -> {status_line}"));
        Ok(DacpSendResult {
            command,
            endpoint,
            status_line,
        })
    }

    async fn resolve_endpoint(
        &self,
        dacp_id: &str,
        peer_addr: Option<SocketAddr>,
    ) -> Result<SocketAddr, DacpError> {
        let daemon = self
            .daemon
            .as_ref()
            .as_ref()
            .ok_or_else(|| DacpError::MdnsUnavailable("browser was not started".to_string()))?;
        if let Some(endpoint) = self.cached_endpoint_for_peer(peer_addr) {
            return Ok(endpoint);
        }
        let receiver = daemon
            .browse(DACP_SERVICE_TYPE)
            .map_err(|err| DacpError::MdnsUnavailable(err.to_string()))?;
        let deadline = Instant::now() + DACP_DISCOVERY_TIMEOUT;

        loop {
            let Some(remaining) = deadline.checked_duration_since(Instant::now()) else {
                let _ = daemon.stop_browse(DACP_SERVICE_TYPE);
                let err = DacpError::DiscoveryTimeout(dacp_id.to_string());
                self.state.set_dacp_error(err.to_string());
                return Err(err);
            };
            match timeout(remaining, receiver.recv_async()).await {
                Ok(Ok(ServiceEvent::ServiceResolved(service))) => {
                    if !dacp_service_matches(dacp_id, &service) {
                        continue;
                    }
                    if let Some(endpoint) =
                        choose_endpoint(&service, peer_addr.map(|addr| addr.ip()))
                    {
                        let _ = daemon.stop_browse(DACP_SERVICE_TYPE);
                        self.state.set_dacp_endpoint(Some(endpoint));
                        debug!(
                            dacp_id,
                            %endpoint,
                            service = service.get_fullname(),
                            "DACP service resolved"
                        );
                        return Ok(endpoint);
                    }
                }
                Ok(Ok(_)) => {}
                Ok(Err(err)) => {
                    let _ = daemon.stop_browse(DACP_SERVICE_TYPE);
                    let err = DacpError::MdnsUnavailable(err.to_string());
                    self.state.set_dacp_error(err.to_string());
                    return Err(err);
                }
                Err(_) => {
                    let _ = daemon.stop_browse(DACP_SERVICE_TYPE);
                    let err = DacpError::DiscoveryTimeout(dacp_id.to_string());
                    self.state.set_dacp_error(err.to_string());
                    return Err(err);
                }
            }
        }
    }

    fn prewarm_endpoint(&self) {
        let snapshot = self.state.snapshot().remote_control;
        if snapshot.dacp_addr.is_some()
            || snapshot.dacp_id.is_none()
            || snapshot.active_remote.is_none()
        {
            return;
        }
        let dacp_id = snapshot.dacp_id.unwrap();
        let peer_addr = snapshot
            .peer_addr
            .as_deref()
            .and_then(|value| value.parse::<SocketAddr>().ok());
        let controller = self.clone();
        let Ok(handle) = tokio::runtime::Handle::try_current() else {
            return;
        };
        handle.spawn(async move {
            if let Err(err) = controller.resolve_endpoint(&dacp_id, peer_addr).await {
                debug!(%err, "DACP endpoint prewarm failed");
            }
        });
    }

    fn cached_endpoint_for_peer(&self, peer_addr: Option<SocketAddr>) -> Option<SocketAddr> {
        let snapshot = self.state.snapshot().remote_control;
        let endpoint = snapshot.dacp_addr?.parse::<SocketAddr>().ok()?;
        if let Some(peer_addr) = peer_addr
            && endpoint.ip().is_ipv4() != peer_addr.ip().is_ipv4()
        {
            return None;
        }
        Some(endpoint)
    }
}

pub fn dacp_command_for_alias(alias: &str) -> Option<&'static str> {
    let normalized = alias.trim().to_ascii_lowercase();
    match normalized.as_str() {
        "next" | "nextitem" | "next-track" | "nexttrack" => Some("nextitem"),
        "previous" | "prev" | "previtem" | "previous-track" | "previoustrack" => Some("previtem"),
        "playpause" | "toggle" | "toggle-playback" => Some("playpause"),
        "play" | "resume" => Some("play"),
        "pause" => Some("pause"),
        "stop" => Some("stop"),
        _ if normalized.contains("next") => Some("nextitem"),
        _ if normalized.contains("previous") || normalized.contains("prev") => Some("previtem"),
        _ if normalized.contains("playpause") || normalized.contains("toggle") => Some("playpause"),
        _ => None,
    }
}

pub fn is_navigation_alias(alias: &str) -> bool {
    matches!(dacp_command_for_alias(alias), Some("nextitem" | "previtem"))
}

pub fn dacp_http_request(command: &str, host: &str, active_remote: &str) -> String {
    format!(
        "GET /ctrl-int/1/{command} HTTP/1.1\r\nHost: {host}\r\nActive-Remote: {active_remote}\r\n\r\n"
    )
}

fn dacp_service_matches(dacp_id: &str, service: &ResolvedService) -> bool {
    let needle = dacp_id.to_ascii_lowercase();
    service
        .get_fullname()
        .to_ascii_lowercase()
        .contains(&needle)
        || service.get_properties().iter().any(|property| {
            property.key().to_ascii_lowercase().contains("dacp")
                && property.val_str().to_ascii_lowercase().contains(&needle)
        })
}

fn choose_endpoint(service: &ResolvedService, peer_ip: Option<IpAddr>) -> Option<SocketAddr> {
    let addresses = service
        .get_addresses()
        .iter()
        .map(|addr| addr.to_ip_addr())
        .collect::<Vec<_>>();
    let chosen = if let Some(peer_ip) = peer_ip {
        addresses
            .iter()
            .copied()
            .find(|addr| *addr == peer_ip)
            .or_else(|| {
                addresses
                    .iter()
                    .copied()
                    .find(|addr| addr.is_ipv4() == peer_ip.is_ipv4() && !addr.is_loopback())
            })
            .or_else(|| addresses.iter().copied().find(|addr| !addr.is_loopback()))
    } else {
        addresses.iter().copied().find(|addr| !addr.is_loopback())
    }
    .or_else(|| addresses.first().copied())?;
    Some(SocketAddr::new(chosen, service.get_port()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maps_track_navigation_aliases_to_dacp_commands() {
        assert_eq!(dacp_command_for_alias("next"), Some("nextitem"));
        assert_eq!(dacp_command_for_alias("nextitem"), Some("nextitem"));
        assert_eq!(dacp_command_for_alias("previous"), Some("previtem"));
        assert_eq!(dacp_command_for_alias("prev"), Some("previtem"));
        assert!(is_navigation_alias("previous-track"));
    }

    #[test]
    fn maps_playback_aliases_to_dacp_commands() {
        assert_eq!(dacp_command_for_alias("playpause"), Some("playpause"));
        assert_eq!(dacp_command_for_alias("toggle"), Some("playpause"));
        assert_eq!(dacp_command_for_alias("play"), Some("play"));
        assert_eq!(dacp_command_for_alias("pause"), Some("pause"));
        assert_eq!(dacp_command_for_alias("stop"), Some("stop"));
    }

    #[test]
    fn formats_dacp_http_request() {
        let request = dacp_http_request("nextitem", "192.0.2.4:3689", "123456");
        assert!(request.starts_with("GET /ctrl-int/1/nextitem HTTP/1.1\r\n"));
        assert!(request.contains("Host: 192.0.2.4:3689\r\n"));
        assert!(request.contains("Active-Remote: 123456\r\n"));
        assert!(request.ends_with("\r\n\r\n"));
    }

    #[tokio::test]
    async fn missing_session_returns_unavailable() {
        let state = AppState::new(crate::config::Config::default());
        let controller = DacpController::disabled(state);
        let err = controller.send_alias("next").await.unwrap_err();
        assert!(matches!(err, DacpError::MissingSession));
    }

    #[tokio::test]
    async fn send_returns_after_request_write_without_waiting_for_response() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let endpoint = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 256];
            let read = tokio::io::AsyncReadExt::read(&mut stream, &mut buf)
                .await
                .unwrap();
            String::from_utf8_lossy(&buf[..read]).into_owned()
        });

        let state = AppState::new(crate::config::Config::default());
        state.set_remote_control_session(
            Some("abc".to_string()),
            Some("remote-token".to_string()),
            Some(endpoint),
        );
        state.set_dacp_endpoint(Some(endpoint));
        let controller = DacpController::disabled(state);

        let result = controller.send_alias("next").await.unwrap();

        assert_eq!(result.status_line, "request written");
        let request = server.await.unwrap();
        assert!(request.contains("GET /ctrl-int/1/nextitem HTTP/1.1\r\n"));
        assert!(request.contains("Active-Remote: remote-token\r\n"));
    }
}
