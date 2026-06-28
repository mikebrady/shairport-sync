use std::{
    env,
    path::Path,
    process::{Child, Command},
    sync::Arc,
};

use anyhow::{Context, anyhow, bail};
use mdns_sd::{IfKind, ServiceDaemon, ServiceInfo};
use parking_lot::Mutex;
use tracing::{debug, warn};

use crate::{
    airplay::txt_records::AirplayService,
    config::{MdnsBackendName, MdnsConfig},
};

#[derive(Clone, Debug)]
pub enum MdnsBackend {
    Auto,
    Builtin,
    Avahi,
    DnsSd,
    External,
    Off,
}

#[derive(Clone)]
pub struct MdnsAdvertiser {
    backend: MdnsBackend,
    config: MdnsConfig,
    builtin_daemons: Arc<Mutex<Vec<ServiceDaemon>>>,
    external_children: Arc<Mutex<Vec<Child>>>,
    published_services: Arc<Mutex<Vec<AirplayService>>>,
    active_backend: Arc<Mutex<Option<MdnsBackend>>>,
}

impl MdnsBackend {
    pub fn from_config(config: &MdnsConfig) -> Self {
        match config.backend {
            MdnsBackendName::Auto => Self::Auto,
            MdnsBackendName::Builtin => Self::Builtin,
            MdnsBackendName::Avahi => Self::Avahi,
            MdnsBackendName::DnsSd => Self::DnsSd,
            MdnsBackendName::External => Self::External,
            MdnsBackendName::Off => Self::Off,
        }
    }
}

impl MdnsAdvertiser {
    pub fn new(backend: MdnsBackend, config: MdnsConfig) -> Self {
        Self {
            backend,
            config,
            builtin_daemons: Arc::new(Mutex::new(Vec::new())),
            external_children: Arc::new(Mutex::new(Vec::new())),
            published_services: Arc::new(Mutex::new(Vec::new())),
            active_backend: Arc::new(Mutex::new(None)),
        }
    }

    pub async fn publish(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        // Store for later republish
        *self.published_services.lock() = services.clone();
        let backend = match self.backend {
            MdnsBackend::Auto => self.publish_auto(services).await?,
            MdnsBackend::Builtin => {
                self.publish_builtin(services).await?;
                MdnsBackend::Builtin
            }
            MdnsBackend::Avahi => {
                self.publish_external_program("avahi-publish-service", services)
                    .await?;
                MdnsBackend::Avahi
            }
            MdnsBackend::DnsSd => {
                self.publish_external_program("dns-sd", services).await?;
                MdnsBackend::DnsSd
            }
            MdnsBackend::External => {
                self.publish_configured_external(services).await?;
                MdnsBackend::External
            }
            MdnsBackend::Off => MdnsBackend::Off,
        };
        *self.active_backend.lock() = Some(backend);
        Ok(())
    }

    pub fn active_backend_name(&self) -> Option<String> {
        self.active_backend.lock().as_ref().map(MdnsBackend::name)
    }

    /// Republish with the same services (e.g. after TXT record changes).
    /// For the builtin backend this re-registers; for external backends it spawns new processes.
    pub async fn republish(&self) -> anyhow::Result<()> {
        let services = self.published_services.lock().clone();
        if services.is_empty() {
            return Ok(());
        }
        self.publish(services).await
    }

    async fn publish_builtin(&self, services: Vec<AirplayService>) -> anyhow::Result<()> {
        let daemon = ServiceDaemon::new().context("failed to create built-in mDNS daemon")?;
        if let Some(interface) = self.config.interface.as_deref() {
            daemon
                .enable_interface(interface)
                .with_context(|| format!("failed to enable mDNS interface {interface}"))?;
        } else {
            daemon
                .enable_interface(IfKind::IPv4)
                .context("failed to enable IPv4 mDNS interfaces")?;
        }
        for service in services {
            let txt: Vec<(&str, &str)> = service
                .txt
                .iter()
                .filter_map(|entry| entry.split_once('='))
                .collect();
            let info = ServiceInfo::new(
                &service.service_type,
                &service.instance_name,
                &format!("{}.local.", self.config.hostname),
                "",
                service.port,
                &txt[..],
            )
            .map(ServiceInfo::enable_addr_auto)
            .with_context(|| {
                format!("failed to build service info for {}", service.service_type)
            })?;
            daemon
                .register(info)
                .with_context(|| format!("failed to register {}", service.service_type))?;
        }
        self.builtin_daemons.lock().push(daemon);
        Ok(())
    }

    async fn publish_auto(&self, services: Vec<AirplayService>) -> anyhow::Result<MdnsBackend> {
        let mut errors = Vec::new();
        for backend in auto_backend_candidates() {
            if !backend.is_available(&self.config) {
                debug!(
                    backend = backend.name(),
                    "mDNS backend unavailable, trying fallback"
                );
                continue;
            }
            let result = match backend {
                MdnsBackend::Builtin => self.publish_builtin(services.clone()).await,
                MdnsBackend::Avahi => {
                    self.publish_external_program("avahi-publish-service", services.clone())
                        .await
                }
                MdnsBackend::DnsSd => {
                    self.publish_external_program("dns-sd", services.clone())
                        .await
                }
                MdnsBackend::External => self.publish_configured_external(services.clone()).await,
                MdnsBackend::Off | MdnsBackend::Auto => unreachable!(),
            };
            match result {
                Ok(()) => return Ok(backend),
                Err(err) => {
                    warn!(backend = backend.name(), %err, "mDNS backend failed, trying fallback");
                    errors.push(format!("{}: {err}", backend.name()));
                }
            }
        }
        Err(anyhow!(
            "no mDNS backend could publish services: {}",
            errors.join("; ")
        ))
    }

    async fn publish_configured_external(
        &self,
        services: Vec<AirplayService>,
    ) -> anyhow::Result<()> {
        let Some(command) = self.config.external_command.as_deref() else {
            bail!("mdns.backend=external requires mdns.external_command");
        };
        self.publish_external_program(command, services).await
    }

    async fn publish_external_program(
        &self,
        command: &str,
        services: Vec<AirplayService>,
    ) -> anyhow::Result<()> {
        self.stop_external_children();
        for service in services {
            let mut cmd = Command::new(command);
            if command.eq_ignore_ascii_case("dns-sd") || command.ends_with("dns-sd") {
                cmd.arg("-R")
                    .arg(&service.instance_name)
                    .arg(trim_local_domain(&service.service_type))
                    .arg("local")
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            } else if command.eq_ignore_ascii_case("avahi-publish-service")
                || command.ends_with("avahi-publish-service")
            {
                cmd.arg(&service.instance_name)
                    .arg(trim_local_domain(&service.service_type))
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            } else {
                cmd.arg(&service.instance_name)
                    .arg(&service.service_type)
                    .arg(service.port.to_string())
                    .args(service.txt.iter());
            }

            let child = cmd
                .spawn()
                .with_context(|| format!("failed to spawn {command}"))?;
            self.external_children.lock().push(child);
        }
        Ok(())
    }

    fn stop_external_children(&self) {
        let mut children = self.external_children.lock();
        for child in children.iter_mut() {
            match child.try_wait() {
                Ok(Some(_)) => {}
                Ok(None) => {
                    let _ = child.kill();
                    let _ = child.wait();
                }
                Err(_) => {
                    let _ = child.kill();
                    let _ = child.wait();
                }
            }
        }
        children.clear();
    }
}

impl MdnsBackend {
    fn is_available(&self, config: &MdnsConfig) -> bool {
        match self {
            Self::Auto => false,
            Self::Builtin => true,
            Self::Avahi => command_exists("avahi-publish-service"),
            Self::DnsSd => command_exists("dns-sd"),
            Self::External => config
                .external_command
                .as_deref()
                .is_some_and(command_exists),
            Self::Off => true,
        }
    }

    pub fn name(&self) -> String {
        match self {
            Self::Auto => "auto",
            Self::Builtin => "builtin",
            Self::Avahi => "avahi",
            Self::DnsSd => "dns-sd",
            Self::External => "external",
            Self::Off => "off",
        }
        .to_string()
    }
}

fn auto_backend_candidates() -> Vec<MdnsBackend> {
    if cfg!(target_os = "linux") {
        vec![MdnsBackend::Avahi, MdnsBackend::DnsSd, MdnsBackend::Builtin]
    } else if cfg!(target_os = "macos") || cfg!(target_os = "windows") {
        vec![MdnsBackend::DnsSd, MdnsBackend::Avahi, MdnsBackend::Builtin]
    } else {
        vec![MdnsBackend::DnsSd, MdnsBackend::Avahi, MdnsBackend::Builtin]
    }
}

fn command_exists(command: &str) -> bool {
    let command_path = Path::new(command);
    if command_path.components().count() > 1 {
        return command_path.is_file();
    }
    let Some(path_var) = env::var_os("PATH") else {
        return false;
    };
    let extensions = executable_extensions();
    env::split_paths(&path_var).any(|dir| {
        extensions
            .iter()
            .any(|ext| dir.join(format!("{command}{ext}")).is_file())
    })
}

#[cfg(windows)]
fn executable_extensions() -> Vec<String> {
    let mut extensions = vec![String::new()];
    if let Some(pathext) = env::var_os("PATHEXT") {
        extensions.extend(
            pathext
                .to_string_lossy()
                .split(';')
                .filter(|ext| !ext.is_empty())
                .map(|ext| ext.to_string()),
        );
    }
    extensions
}

#[cfg(not(windows))]
fn executable_extensions() -> Vec<String> {
    vec![String::new()]
}

impl Drop for MdnsAdvertiser {
    fn drop(&mut self) {
        if Arc::strong_count(&self.external_children) == 1 {
            self.stop_external_children();
        }
    }
}

fn trim_local_domain(service_type: &str) -> &str {
    service_type
        .strip_suffix(".local.")
        .or_else(|| service_type.strip_suffix(".local"))
        .unwrap_or(service_type)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trims_local_domain_for_external_publishers() {
        assert_eq!(trim_local_domain("_airplay._tcp.local."), "_airplay._tcp");
        assert_eq!(trim_local_domain("_raop._tcp.local"), "_raop._tcp");
        assert_eq!(trim_local_domain("_raop._tcp"), "_raop._tcp");
    }

    #[test]
    fn auto_backend_prefers_native_provider_before_builtin() {
        let candidates = auto_backend_candidates();
        assert_eq!(
            candidates.last().map(MdnsBackend::name).as_deref(),
            Some("builtin")
        );
        if cfg!(target_os = "linux") {
            assert_eq!(
                candidates.first().map(MdnsBackend::name).as_deref(),
                Some("avahi")
            );
        }
        if cfg!(target_os = "macos") || cfg!(target_os = "windows") {
            assert_eq!(
                candidates.first().map(MdnsBackend::name).as_deref(),
                Some("dns-sd")
            );
        }
    }
}
